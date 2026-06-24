# Manual Verification Guide

Since we cannot run live Oracle tests in the CI environment, please perform the following manual verification steps with a running Oracle Database.

## Prerequisites

1.  **Oracle Database**: A running instance (e.g., 19c or 21c).
2.  **Oracle Instant Client SDK**: Installed and `ORACLE_HOME` set.
3.  **DuckDB with Extension**: Built using `make`.

## Test 1: Basic Connectivity & Scan

```sql
LOAD 'build/release/extension/oracle/oracle.duckdb_extension';
CREATE SECRET local_oracle (
    TYPE oracle,
    USER 'system',
    PASSWORD 'oracle',
    HOST 'localhost',
    PORT 1521,
    SERVICE 'xfepdb1'
);
ATTACH '' AS ora (TYPE oracle, SECRET local_oracle);

SELECT * FROM ora.HR.EMPLOYEES;
```

## Test 2: Arbitrary Query

```sql
-- oracle_query executes trusted raw Oracle SQL.
SELECT * FROM oracle_query('ora',
    'SELECT EMPLOYEE_ID, FIRST_NAME, HIRE_DATE FROM HR.EMPLOYEES WHERE SALARY > 5000');
```

## Test 3: JSON Support

```sql
-- Create table in Oracle:
-- CREATE TABLE JSON_TEST (ID NUMBER, DATA JSON);
-- INSERT INTO JSON_TEST VALUES (1, '{"key": "value"}');

SELECT * FROM ora.SYSTEM.JSON_TEST;
-- Verify that the 'DATA' column is of type JSON (DuckDB) and content is correct.
```

## Test 4: Wallet Connectivity

```sql
CREATE SECRET adb_oracle (
    TYPE oracle,
    USER 'admin',
    PASSWORD 'password',
    SERVICE 'service_alias',
    WALLET_PATH '/path/to/wallet'
);
ATTACH '' AS adb (TYPE oracle, SECRET adb_oracle);
SELECT * FROM oracle_query('adb', 'SELECT 1 FROM DUAL');
```
