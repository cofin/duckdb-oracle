# Testing

This directory contains SQLLogicTests organized into two suites:

- `test/unit_tests/` — Offline unit tests (no Oracle database or Instant Client required).
- `test/integration_tests/` — End-to-end tests executed against an Oracle Free 23ai container (`gvenzl/oracle-free:23-slim`).

## Running Tests

### Unit Tests

```bash
make test-unit
```

### Containerized Integration Tests

Requires Docker or Podman. Starts an Oracle container, seeds the test schema, and runs `test/integration_tests/`:

```bash
make integration

# Reuse an existing release build without recompiling:
SKIP_BUILD=1 make integration
```

## Manual Verification

When testing against a live Oracle instance with Oracle Instant Client installed:

```sql
LOAD 'build/release/extension/oracle/oracle.duckdb_extension';

-- 1. Attach with a secret
CREATE SECRET local_oracle (
    TYPE oracle,
    HOST 'localhost',
    PORT 1521,
    SERVICE 'FREEPDB1',
    USER 'system',
    PASSWORD 'oracle'
);
ATTACH '' AS ora (TYPE oracle, SECRET local_oracle);

-- 2. Query attached tables
SELECT * FROM ora.hr.employees;

-- 3. Run direct Oracle SQL
SELECT * FROM oracle_query('ora',
    'SELECT employee_id, first_name, hire_date FROM hr.employees WHERE salary > 5000');

-- 4. Connect with an Oracle Wallet
CREATE SECRET wallet_oracle (
    TYPE oracle,
    SERVICE 'service_alias',
    USER 'admin',
    PASSWORD 'password',
    WALLET_PATH '/path/to/wallet'
);
ATTACH '' AS ora_wallet (TYPE oracle, SECRET wallet_oracle);

SELECT * FROM oracle_query('ora_wallet', 'SELECT 1 FROM dual');
```
