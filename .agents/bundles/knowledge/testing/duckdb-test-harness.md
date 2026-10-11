---
type: Testing
title: DuckDB Test Harness Architecture & Suites
description: DuckDB testing framework, sqllogictest syntax, C++ unit test runner, and containerized Oracle integration tests.
scope: testing
domain: duckdb
tags: [duckdb, testing, sqllogictest, integration-tests, test-harness]
status: stable
---

# DuckDB Test Harness Architecture & Suites

The `duckdb-oracle` repository maintains a two-tiered testing harness:
1. **Fast Unit / Smoke Tests (`make test-unit`)**: Runs in seconds without requiring an external Oracle Database container.
2. **Containerized Integration Tests (`make integration`)**: Spawns an Oracle Free 23ai container (`gvenzl/oracle-free:23-slim`) to validate full query execution, writes, transactions, spatial types, and pushdown against a live Oracle engine.

## 1. Test Harness Structure

```
test/
├── unit_tests/                    # Fast smoke tests (executed by make test-unit)
│   └── test_oracle_unit.test      # Parser and offline connection resolution tests
├── integration_tests/             # Full containerized integration tests (make integration)
│   ├── init_sql/                  # DDL and seed scripts mounted into the Oracle container
│   │   ├── 01_users.sql           # Test user creation (duckdb_test)
│   │   ├── 02_tables.sql          # Test tables (datatypes, spatial, vectors)
│   │   └── 03_data.sql            # Seed dataset
│   ├── test_oracle_attach.test    # ATTACH syntax and SecretManager validation
│   ├── test_oracle_query.test     # SELECT, aggregations, and pushdown
│   ├── test_oracle_insert.test    # INSERT, bulk array bind, and transaction commit/rollback
│   └── test_oracle_spatial.test   # SDO_GEOMETRY to DuckDB GEOMETRY mappings
└── unittest                       # DuckDB sqllogictest binary compiled from duckdb submodule
```

## 2. DuckDB sqllogictest Syntax (`.test` Files)

DuckDB tests are written in the SQLite sqllogictest dialect with DuckDB-specific extensions:

```sql
# Name and load extension
# statement ok: expects success without output
statement ok
LOAD 'build/release/extension/oracle/oracle.duckdb_extension';

# statement error: expects failure matching regex
statement error
ATTACH 'bad_conn' AS ora (TYPE oracle);
----
IO Error: Failed to connect to Oracle

# query <expected_type_string> [sort_order]
# I = Integer, T = Text, R = Floating point / Real, B = Boolean
query IT
SELECT employee_id, first_name FROM ora.employees WHERE employee_id = 100;
----
100	Steven

# Validate NULL handling
query I
SELECT commission_pct FROM ora.employees WHERE employee_id = 100;
----
NULL
```

## 3. Integration Test Automation (`scripts/test_integration.sh`)

Integration testing is orchestrated by `scripts/test_integration.sh`:

- **Container Management**: Checks for Docker / Podman (`${RUNTIME:-docker}`). Cleans up stale containers (`duckdb-oracle-test-db`).
- **Healthcheck & Startup**: Polls the container until Oracle signals database readiness (`FREEPDB1` open for read/write).
- **Initialization**: Automatically mounts and runs scripts in `test/integration_tests/init_sql/`.
- **Execution**: Runs `test/unittest` against `test/integration_tests/*.test` with environment variables:
  ```bash
  ORACLE_USER=duckdb_test \
  ORACLE_PASSWORD=duckdb_test \
  ORACLE_SERVICE=FREEPDB1 \
  ORACLE_HOST=localhost \
  ORACLE_PORT=1521 \
  ./build/release/test/unittest "test/integration_tests/*.test"
  ```
- **Cleanup**: Stops and removes the test container unless `--keep-container` or `--no-cleanup` is passed.

## 4. Canonical Test Commands

| Command | Environment Required | Scope |
| :--- | :--- | :--- |
| `make test-unit` | Local OCI SDK only | Fast smoke tests; validates extension loading and syntax without a database. |
| `make integration` | Docker / Podman + OCI SDK | Compiles release build, launches containerized Oracle Free 23ai, and runs all integration tests. |
| `SKIP_BUILD=1 make integration` | Docker / Podman | Runs integration tests reusing an existing release binary. |
