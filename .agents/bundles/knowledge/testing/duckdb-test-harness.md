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
├── README.md                                  # Test suite and manual verification guide
├── unit_tests/                                # Fast offline unit tests (executed by make test-unit)
│   ├── test_extension_metadata.test           # Extension version and load verification
│   ├── test_oracle_wallet.test                # Offline wallet validation and secret resolution
│   └── oracle/
│       ├── connection_milestone.test          # Connection string and secret parsing unit tests
│       ├── execution_milestone.test           # Execution and setting validation unit tests
│       ├── pushdown_milestone.test            # Filter and projection pushdown unit tests
│       ├── storage_milestone.test             # Attached catalog and storage unit tests
│       ├── test_metadata.test                 # Lazy schema loading and metadata object type settings
│       ├── test_schema_resolution.test        # CURRENT_SCHEMA resolution settings
│       └── test_tuning_settings.test          # Tuning settings defaults and range validation
└── integration_tests/                         # Full containerized integration tests (make integration)
    ├── init_sql/
    │   └── 01_setup.sql                       # Test user creation and grants (duckdb_test on FREEPDB1)
    ├── test_connection.test                   # Live connection and secret authentication tests
    ├── test_datatypes.test                    # Core Oracle data type mappings
    ├── test_edge_cases.test                   # NULLs, empty tables, and multi-kilobyte CLOBs
    ├── test_metadata_queries.test             # Metadata enumeration and lazy loading against Oracle
    ├── test_pipelined.test                    # Pipelined table functions via oracle_query
    ├── test_pushdown.test                     # Predicate and projection pushdown integration tests
    ├── test_security.test                     # Credential redaction and privilege boundary tests
    ├── test_types_comprehensive.test          # Comprehensive numeric, temporal, LOB, and vector types
    ├── test_views_matviews.test               # Views, materialized views, and synonyms
    └── oracle/
        ├── connection.test                    # Connection pooling and wallet/secret integration
        ├── execution.test                     # oracle_query, oracle_scan, and flashback queries
        ├── partition_pruning.test             # Partition metadata and partition-key filter pushdown
        ├── pushdown.test                      # Complex filter pushdown verification
        ├── spatial.test                       # SDO_GEOMETRY WKT point/polygon read/write round-trips
        ├── write.test                         # Bulk array INSERT INTO attached Oracle tables
        ├── write_transaction.test             # Statement-atomic commit, rollback, and transaction guards
        └── write_vector.test                  # Oracle 23ai VECTOR read/write round-trips
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
