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
1. **Fast Unit / Smoke Tests (`make test-unit`)**: Runs in seconds with zero external Oracle Instant Client or database container dependencies.
2. **Containerized Integration Tests (`make integration`)**: Installs Oracle Instant Client Basic via `scripts/setup_oci_linux.sh` and spawns an Oracle Free 23ai container (`gvenzl/oracle-free:23-slim`) to validate full query execution, writes, transactions, spatial types, and pushdown against a live Oracle engine.

## 1. Test Harness Structure

```
test/
├── README.md                                  # Test suite and manual verification guide
├── unit_tests/                                # Fast offline unit tests (executed by make test-unit)
│   ├── test_attach_and_scan.test              # ATTACH, DETACH, and oracle_scan offline validation
│   ├── test_connection_resolver.test          # Connection string and secret resolution validation
│   ├── test_execute_failures.test             # oracle_execute failure and transaction guard checks
│   ├── test_extension_metadata.test           # Extension version and load verification
│   ├── test_filter_pushdown.test              # Offline filter pushdown and parameter validation
│   ├── test_oracle_write_removed.test         # Verification that legacy oracle_write is not exposed
│   ├── test_public_surface.test               # Registered functions, settings, and secret types
│   ├── test_query_function.test               # oracle_query input validation
│   ├── test_secret_basic.test                 # Secret creation, wallet/SEPS, and TNS alias shapes
│   ├── test_secret_errors.test                # Secret parameter validation and error handling
│   ├── test_wallet_and_settings.test          # Wallet path and setting interaction checks
│   ├── spatial/
│   │   └── test_spatial_basic.test            # Spatial setting and WKT type surface checks
│   └── oracle/
│       ├── test_execute.test                  # oracle_execute argument and error checks
│       ├── test_metadata.test                 # Lazy schema loading and metadata object type settings
│       ├── test_schema_resolution.test        # CURRENT_SCHEMA resolution settings
│       ├── test_settings.test                 # Combined Oracle extension settings smoke checks
│       └── test_tuning_settings.test          # Tuning settings defaults, call_timeout_ms, stmt_cache_size, and overflow bounds
└── integration_tests/                         # Full containerized integration tests (make integration)
    ├── init_sql/
    │   └── 01_setup.sql                       # Test user creation and grants (duckdb_test on FREEPDB1)
    ├── test_connection_pool_lifecycle.test    # Connection pool reuse, health checks, and limits
    ├── test_edge_cases.test                   # NULLs, empty tables, and multi-kilobyte CLOBs
    ├── test_pipelined.test                    # Pipelined table functions via oracle_query
    ├── test_views_matviews.test               # Views, materialized views, and synonyms
    ├── test_write_lobs.test                   # CLOB/BLOB read and write round-trips
    └── oracle/
        ├── edge_cases.test                    # Boundary conditions and large payloads
        ├── execute.test                       # Live DDL/DML execution via oracle_execute
        ├── json.test                          # Oracle JSON read/write integration
        ├── metadata.test                      # Metadata enumeration and lazy loading against Oracle
        ├── partition_pruning.test             # Partition metadata and partition-key filter pushdown
        ├── pushdown.test                      # Filter and projection pushdown verification
        ├── schema_resolution.test             # Live CURRENT_SCHEMA and cross-schema resolution
        ├── streaming.test                     # Multi-chunk cursor fetch and streaming verification
        ├── type_conversion.test               # Numeric, temporal, and binary type conversions
        ├── type_registry.test                 # Metadata and OCI describe type registry resolution
        ├── vector.test                        # Oracle 23ai VECTOR read queries
        ├── write.test                         # Bulk array INSERT INTO attached Oracle tables
        ├── write_identifiers.test             # Quoted and case-sensitive identifier writes
        ├── write_json.test                    # JSON column writes
        ├── write_rollback.test                # Error rollback and recovery verification
        ├── write_transactions.test            # Statement-atomic commit and explicit transaction guards
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
| `make test-unit` | None (zero OCI SDK required) | Fast smoke tests; validates extension loading, settings, secrets, and syntax without Instant Client or a database. |
| `make integration` | Docker / Podman + OCI Basic runtime | Compiles release build, installs Instant Client Basic on Linux, launches containerized Oracle Free 23ai, and runs all 22 integration test files. |
| `SKIP_BUILD=1 make integration` | Docker / Podman + OCI Basic runtime | Runs integration tests reusing an existing release binary. |
