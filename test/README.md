# Testing this extension

This directory contains SQLLogicTests for the DuckDB Oracle extension, organized into two suites:

- `test/unit_tests/` — Fast offline smoke and unit tests (no live Oracle database required).
- `test/integration_tests/` — End-to-end tests executed against a containerized Oracle Free 23ai instance (`gvenzl/oracle-free:23-slim`).

## Automated Test Suites

### Unit Tests

Run the offline unit test suite:

```bash
make test-unit
```

Or run the full default test target (which also builds `release` and runs unit tests, or `make test_debug` for debug builds):

```bash
make test
```

### Containerized Integration Tests

Requires Docker or Podman. Spins up an Oracle Free 23ai container, seeds the test schema, and runs all tests under `test/integration_tests/`:

```bash
make integration
```

To reuse an existing release build without recompiling:

```bash
SKIP_BUILD=1 make integration
```

## Manual Verification Guide

When testing manually against a live Oracle Database (19c, 21c, or 23ai) with Oracle Instant Client SDK (`ORACLE_HOME`) installed and the extension built via `make`:

### 1. Basic Connectivity & Scan

```sql
LOAD 'build/release/extension/oracle/oracle.duckdb_extension';

CREATE SECRET local_oracle (
    TYPE oracle,
    USER 'system',
    PASSWORD 'oracle',
    HOST 'localhost',
    PORT 1521,
    SERVICE 'FREEPDB1'
);
ATTACH '' AS ora (TYPE oracle, SECRET local_oracle);

SELECT * FROM ora.HR.EMPLOYEES;
```

### 2. Arbitrary Raw Query (`oracle_query`)

```sql
-- oracle_query executes trusted raw Oracle SQL.
SELECT * FROM oracle_query('ora',
    'SELECT EMPLOYEE_ID, FIRST_NAME, HIRE_DATE FROM HR.EMPLOYEES WHERE SALARY > 5000');
```

### 3. JSON & Spatial Support

```sql
-- In Oracle:
-- CREATE TABLE JSON_TEST (ID NUMBER, DATA JSON);
-- INSERT INTO JSON_TEST VALUES (1, '{"key": "value"}');

SELECT * FROM ora.SYSTEM.JSON_TEST;
```

### 4. Wallet Connectivity (Autonomous Database / mTLS)

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
