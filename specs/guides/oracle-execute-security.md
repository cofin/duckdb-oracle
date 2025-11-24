# oracle_execute Security Best Practices

**Feature**: Arbitrary SQL Execution
**Version**: 1.0
**Last Updated**: 2025-11-23
**Security Level**: HIGH RISK - SQL Injection Possible

## Overview

The `oracle_execute()` function allows execution of arbitrary Oracle SQL statements (DDL, DML, PL/SQL blocks) without expecting a result set. This function **does not use prepared statements** and is vulnerable to SQL injection if used improperly.

## Function Signature

```sql
oracle_execute(connection_string VARCHAR, sql_statement VARCHAR) -> VARCHAR
```

**Returns**: Status message with row count for DML, or "Statement executed successfully" for DDL/PL/SQL.

## Security Risks

### SQL Injection Vulnerability

**CRITICAL**: `oracle_execute()` directly interpolates SQL strings without parameter binding.

**Vulnerable Code**:

```sql
-- NEVER DO THIS - Vulnerable to SQL injection
CREATE TABLE user_input_table (user_id VARCHAR);
INSERT INTO user_input_table VALUES ('abc; DROP TABLE employees; --');

SELECT oracle_execute(
    'user/pass@db',
    'DELETE FROM logs WHERE user_id = ''' ||
    (SELECT user_id FROM user_input_table LIMIT 1) || ''''
);
-- Executes: DELETE FROM logs WHERE user_id = 'abc'; DROP TABLE employees; --'
-- Result: employees table destroyed
```

**Attack Vectors**:
- String concatenation with user input
- Dynamic SQL generation from untrusted sources
- Embedded commands in application variables
- Second-order injection (stored user input)

### Credential Exposure

**RISK**: Connection strings contain credentials in plaintext.

**Vulnerable Code**:

```sql
-- Connection string in query logs
SELECT oracle_execute(
    'admin/SuperSecret123@prod-db:1521/PROD',
    'CREATE INDEX ...'
);
-- Credentials visible in DuckDB query logs, history files
```

### Unauthorized Actions

**RISK**: Function executes with permissions of connection user.

**Dangerous Operations**:
- `DROP TABLE` / `DROP INDEX` - Data loss
- `GRANT` / `REVOKE` - Privilege escalation
- `ALTER SYSTEM` - Database-wide changes
- `CREATE USER` - Account creation
- PL/SQL packages - Arbitrary code execution

## Safe Usage Patterns

### 1. Use DuckDB SecretManager (Recommended)

**Best Practice**: Store credentials in secrets, never in connection strings.

```sql
-- Create secret with credentials
CREATE SECRET oracle_prod (
    TYPE oracle,
    USER 'app_user',
    PASSWORD 'SecurePassword123'
);

-- Use secret for connection (credentials not in query text)
SELECT oracle_execute(
    (SELECT 'localhost:1521/PRODDB'),  -- Connection details only
    'CREATE INDEX idx_orders ON orders(order_date)'
);

-- Or use ATTACH with secret, then oracle_query
ATTACH 'localhost:1521/PRODDB' AS prod (TYPE oracle, SECRET oracle_prod);
-- (oracle_execute still requires connection string - use oracle_query instead)
```

### 2. Never Concatenate User Input

**Safe Pattern**: Validate and sanitize all inputs.

```sql
-- SAFE: Hardcoded SQL (no user input)
SELECT oracle_execute(
    'user/pass@db',
    'DELETE FROM temp_table WHERE created < SYSDATE - 7'
);

-- UNSAFE: Concatenated user input
SELECT oracle_execute(
    'user/pass@db',
    'DELETE FROM logs WHERE user_id = ''' || user_input || ''''
);

-- SAFER: Validation before use
CREATE TABLE validated_inputs (user_id VARCHAR);

-- Application validates: user_id matches regex [A-Za-z0-9]{1,20}
INSERT INTO validated_inputs
    SELECT user_id FROM user_input_table
    WHERE regexp_matches(user_id, '^[A-Za-z0-9]{1,20}$');

-- Use validated input (still risky, but limited attack surface)
SELECT oracle_execute(
    'user/pass@db',
    'DELETE FROM logs WHERE user_id = ''' ||
    (SELECT user_id FROM validated_inputs LIMIT 1) || ''''
);
```

### 3. Use Allowlists for Dynamic SQL

**Safe Pattern**: Limit dynamic components to predefined allowlists.

```sql
-- Table name allowlist
CREATE TABLE allowed_tables (table_name VARCHAR);
INSERT INTO allowed_tables VALUES ('temp_data'), ('staging_data'), ('cache_data');

-- Validate table name before use
CREATE OR REPLACE MACRO safe_truncate(table_name_input VARCHAR) AS (
    SELECT CASE
        WHEN EXISTS (SELECT 1 FROM allowed_tables WHERE table_name = table_name_input)
        THEN oracle_execute('user/pass@db', 'TRUNCATE TABLE ' || table_name_input)
        ELSE 'Error: Table not in allowlist'
    END
);

-- Usage
SELECT safe_truncate('temp_data');  -- Allowed
SELECT safe_truncate('employees');  -- Rejected
```

### 4. Principle of Least Privilege

**Best Practice**: Use Oracle accounts with minimal permissions.

```sql
-- Create dedicated DuckDB user in Oracle (read-only + specific procedures)
-- In Oracle (as DBA):
CREATE USER duckdb_app IDENTIFIED BY "SecurePass123";
GRANT CONNECT TO duckdb_app;
GRANT SELECT ON hr.employees TO duckdb_app;
GRANT EXECUTE ON hr_pkg.update_salaries TO duckdb_app;
-- DO NOT GRANT: CREATE, DROP, ALTER, DBA role

-- Use limited account from DuckDB
CREATE SECRET oracle_limited (
    TYPE oracle,
    USER 'duckdb_app',
    PASSWORD 'SecurePass123'
);

-- Even if SQL injection occurs, damage limited
SELECT oracle_execute(
    'duckdb_app/SecurePass123@db',
    'DROP TABLE employees'  -- Fails: insufficient privileges
);
```

### 5. Audit and Monitor

**Best Practice**: Log all `oracle_execute()` calls and review regularly.

```sql
-- Create audit log
CREATE TABLE oracle_execute_audit (
    timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    connection_info VARCHAR,
    sql_statement VARCHAR,
    result VARCHAR
);

-- Wrap oracle_execute with logging
CREATE OR REPLACE MACRO logged_oracle_execute(conn VARCHAR, sql VARCHAR) AS (
    SELECT (
        INSERT INTO oracle_execute_audit (connection_info, sql_statement, result)
        VALUES (
            regexp_replace(conn, ':[^@]+@', ':***@'),  -- Redact password
            sql,
            oracle_execute(conn, sql)
        ),
        'Executed and logged'
    )
);

-- Review audit log
SELECT timestamp, sql_statement FROM oracle_execute_audit
ORDER BY timestamp DESC LIMIT 100;
```

## Secure Code Examples

### Example 1: DDL with Hardcoded SQL

```sql
-- SAFE: No user input, credentials in secret
CREATE SECRET ora_admin (TYPE oracle, USER 'admin', PASSWORD 'Pass123');

SELECT oracle_execute(
    'prod-db:1521/PROD',
    'CREATE INDEX idx_emp_dept ON employees(department_id) TABLESPACE idx_ts'
);
```

### Example 2: PL/SQL Procedure Call with Parameters

```sql
-- SAFE: Use Oracle procedure parameters, not string concatenation

-- In Oracle, create parameterized procedure:
-- CREATE OR REPLACE PROCEDURE update_salary(p_emp_id NUMBER, p_pct NUMBER) AS
-- BEGIN
--   UPDATE employees SET salary = salary * p_pct WHERE employee_id = p_emp_id;
--   COMMIT;
-- END;

-- From DuckDB (parameters passed as Oracle bind variables - SAFE)
SELECT oracle_execute(
    'hr_user/pass@db',
    'BEGIN update_salary(101, 1.05); END;'
);
-- No string concatenation, Oracle handles parameterization
```

### Example 3: Batch DDL Operations

```sql
-- SAFE: Hardcoded list of operations
WITH ddl_statements AS (
    SELECT unnest([
        'CREATE INDEX idx1 ON table1(col1)',
        'CREATE INDEX idx2 ON table2(col2)',
        'ANALYZE TABLE table1 COMPUTE STATISTICS'
    ]) AS stmt
)
SELECT stmt, oracle_execute('user/pass@db', stmt) AS result
FROM ddl_statements;
```

## Insecure Code Examples (DO NOT USE)

### Example 1: SQL Injection via Concatenation

```sql
-- INSECURE: User input concatenated directly
CREATE TABLE user_requests (table_name VARCHAR);
INSERT INTO user_requests VALUES ('logs; DROP TABLE employees; --');

SELECT oracle_execute(
    'user/pass@db',
    'TRUNCATE TABLE ' || (SELECT table_name FROM user_requests LIMIT 1)
);
-- Executes: TRUNCATE TABLE logs; DROP TABLE employees; --
-- Result: employees table deleted
```

### Example 2: Credential Exposure in Logs

```sql
-- INSECURE: Credentials in query text
SELECT oracle_execute(
    'admin/TopSecret@prod-db:1521/PROD',
    'ALTER SYSTEM SET ...'
);
-- DuckDB query log contains: admin/TopSecret@prod-db
-- Accessible via: SELECT * FROM duckdb_queries();
```

### Example 3: Dynamic Privilege Escalation

```sql
-- INSECURE: Malicious input escalates privileges
CREATE TABLE user_input (username VARCHAR);
INSERT INTO user_input VALUES ('attacker; GRANT DBA TO attacker; --');

SELECT oracle_execute(
    'dba_user/dba_pass@db',
    'CREATE USER ' || (SELECT username FROM user_input LIMIT 1) || ' IDENTIFIED BY "temp"'
);
-- Executes: CREATE USER attacker; GRANT DBA TO attacker; -- ...
-- Result: Attacker has full DBA privileges
```

## Alternatives to oracle_execute

### When to Use oracle_query Instead

If your SQL returns a result set, use `oracle_query()` instead (same security risks, but returns data):

```sql
-- Use oracle_query for SELECT statements
SELECT * FROM oracle_query('user/pass@db', 'SELECT * FROM employees');

-- Use oracle_execute for non-returning statements
SELECT oracle_execute('user/pass@db', 'DELETE FROM temp_table');
```

### When to Use ATTACH Instead

For read-only queries, use `ATTACH` with catalog integration (safer):

```sql
-- Safer approach: ATTACH with secret
CREATE SECRET oracle_readonly (
    TYPE oracle,
    USER 'read_user',
    PASSWORD 'ReadPass123'
);

ATTACH 'prod-db:1521/PROD' AS ora (TYPE oracle, SECRET oracle_readonly);

-- Query through catalog (no SQL injection risk)
SELECT * FROM ora.HR.EMPLOYEES WHERE department_id = 10;

-- No oracle_execute needed for queries
```

## Future Improvements

### Planned: Parameterized oracle_execute (v2)

```sql
-- FUTURE: Prepared statement support (not yet implemented)
SELECT oracle_execute_prepared(
    'user/pass@db',
    'DELETE FROM logs WHERE user_id = ?',
    ['abc123']  -- Parameters safely bound
);
```

Until parameterized version available, **avoid oracle_execute with user input**.

## Security Checklist

Before using `oracle_execute()`, verify:

- [ ] SQL statement is hardcoded or from trusted source
- [ ] No user input concatenated into SQL string
- [ ] Connection credentials stored in DuckDB secrets (not in query text)
- [ ] Oracle account has minimal permissions (principle of least privilege)
- [ ] Audit logging enabled for all oracle_execute calls
- [ ] Input validation with allowlists if dynamic SQL required
- [ ] Code reviewed by security team
- [ ] Considered alternatives (ATTACH, oracle_query, Oracle procedures)

## Incident Response

If SQL injection suspected:

1. **Immediate**: Revoke Oracle account credentials used in oracle_execute
2. **Review**: Check audit logs for unauthorized operations
3. **Assess**: Identify data loss, privilege escalation, or exfiltration
4. **Remediate**: Restore from backup if data modified
5. **Fix**: Remove vulnerable code, implement safe patterns
6. **Monitor**: Enhanced logging for future oracle_execute usage

## Related Documentation

- `/home/cody/code/other/duckdb-oracle/README.md` - oracle_execute usage examples
- `test/sql/oracle/advanced_execute.test` - Smoke tests
- `test/sql/oracle/advanced_execute_integration.test` - Integration tests with Oracle

## Changelog

### Version 1.0 (2025-11-23)

- Initial security guide
- Documented SQL injection risks
- Provided safe usage patterns
- Added secure code examples
- Security checklist for developers
