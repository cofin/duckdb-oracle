# Manual Verification Guide

Since we cannot run live Oracle tests in the CI environment, please perform the following manual verification steps with a running Oracle Database.

## Prerequisites

1.  **Oracle Database**: A running instance (e.g., 19c or 21c).
2.  **Oracle Instant Client SDK**: Installed and `ORACLE_HOME` set.
3.  **DuckDB with Extension**: Built using `make`.

## Test 1: Basic Connectivity & Scan

```sql
LOAD 'build/release/extension/oracle/oracle.duckdb_extension';
-- Replace with your actual connection string
SELECT * FROM oracle_scan('system/oracle@//localhost:1521/xfepdb1', 'HR', 'EMPLOYEES');
```

## Test 2: Arbitrary Query

```sql
SELECT * FROM oracle_query('system/oracle@//localhost:1521/xfepdb1', 
    'SELECT EMPLOYEE_ID, FIRST_NAME, HIRE_DATE FROM HR.EMPLOYEES WHERE SALARY > 5000');
```

## Test 3: JSON Support

```sql
-- Create table in Oracle:
-- CREATE TABLE JSON_TEST (ID NUMBER, DATA JSON);
-- INSERT INTO JSON_TEST VALUES (1, '{"key": "value"}');

SELECT * FROM oracle_scan('system/oracle@//localhost:1521/xfepdb1', 'SYSTEM', 'JSON_TEST');
-- Verify that the 'DATA' column is of type JSON (DuckDB) and content is correct.
```

## Test 4: Wallet Connectivity

```sql
-- Ensure wallet is configured in /path/to/wallet
SELECT oracle_attach_wallet('/path/to/wallet');
SELECT * FROM oracle_query('admin/password@service_alias', 'SELECT 1 FROM DUAL');
```
