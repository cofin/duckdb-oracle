# Changelog

All notable changes to the DuckDB Oracle extension are documented in this file.

## [0.2.3] - 2026-04-19

### Added
- **DuckDB v1.5.6 Support**: Upgraded target DuckDB version and CI release matrix to `v1.5.6`.
- **Configurable OCI Timeouts & Statement Cache**: Added `oracle_call_timeout_ms` (default `30000`, `0`–`3600000`) and `oracle_stmt_cache_size` (default `20`, `0`–`1024`) settings.
- **Flashback Query Support**: Added `as_of_scn` (`UBIGINT`) and `as_of_timestamp` (`VARCHAR`) named parameters on `oracle_scan` and `oracle_query`.
- **Spatial Integration Coverage**: Added `SDO_GEOMETRY` WKT point/polygon read/write round-trip integration tests (`test/integration_tests/oracle/spatial.test`).

### Fixed
- **OCI Handle & Environment Lifetime**: Held `std::shared_ptr<OCIEnv>` inside `OracleConnectionContext` so cached connections and statements never outlive `OCIEnv`.
- **CLOB & Vector Truncation**: Bound `VECTOR_SERIALIZE(... RETURNING CLOB)` and dynamically read multi-chunk `CLOB`/`BLOB` descriptors via `OCILobGetLength2`/`OCILobRead2` without 64 KB silent truncation.
- **Projection Pushdown & Scan Re-preparation**: Deferred SQL generation and `OCIStmtPrepare2` to `OracleInitGlobal` after optimizer column projection and filter pushdown complete.
- **Per-ATTACH Catalog State Isolation**: Moved schema/table metadata caches and connection keys from process-global singletons into per-`OracleCatalog` state.
- **Write Pipeline Correctness**: Fixed `SQLT_ODT` `DATE` binding in `INSERT`, string buffer stride accounting, `NUMBER(p, s)` precision/scale mapping, and statement-atomic rollback on error.

## [0.2.2] - 2026-04-12

### Changed
- **DuckDB v1.5.5 Support**: Upgraded DuckDB submodule and release targets to `v1.5.5`.
- **Oracle Instant Client 23.26**: Updated Linux, macOS, and Windows setup scripts to Oracle Instant Client `23.26`.

## [0.2.1] - 2026-03-28

### Added
- **Partition Metadata & Pruning Pushdown**: Added partition discovery (`ALL_PART_TABLES`, `ALL_TAB_PARTITIONS`) and partition-key-aware filter pushdown.
- **Vector & Spatial Write Support**: Enabled `INSERT INTO` for Oracle 23ai `VECTOR` and `MDSYS.SDO_GEOMETRY` columns.

## [0.2.0] - 2026-03-10

### Added
- **Attached Table Writes (`INSERT INTO`)**: Implemented `PhysicalOracleInsert` sink operator with OCI bulk array binding (`OCI_BATCH_ERRORS`).
- **DuckDB SecretManager Integration**: Added `CREATE SECRET ... (TYPE oracle, ...)` with `WALLET_PATH` support for Oracle Wallet / mTLS connections.

## [0.1.3] - 2026-02-15

### Added
- **Lazy Schema & Object Discovery**: Added `oracle_lazy_schema_loading`, `oracle_use_current_schema`, `oracle_metadata_object_types`, and `oracle_metadata_result_limit` settings for large enterprise schemas.
- **Views, Materialized Views & Synonyms**: Exposed `VIEW`, `MATERIALIZED VIEW`, and `SYNONYM` objects in attached catalogs via `ALL_OBJECTS`.

## [0.1.2] - 2026-01-25

### Added
- **Connection Pooling**: Added `OracleConnectionManager` with `OCI_THREADED | OCI_OBJECT` environment management, session pooling (`oracle_connection_cache`, `oracle_connection_limit`), and cache clearing (`oracle_clear_cache()`).
- **Cross-Platform Builds**: Added build and CI support for Linux (`linux_amd64`, `linux_arm64`), macOS (`osx_arm64`), and Windows (`windows_amd64`).

## [0.1.1] - 2026-01-10

### Added
- **Array Fetch**: Implemented `OCIDefineByPos` batch fetching tunable via `oracle_array_size`, `oracle_prefetch_rows`, and `oracle_prefetch_memory`.
- **Filter & Projection Pushdown**: Added `WHERE` predicate and column projection pushdown for `oracle_scan` and attached tables.

## [0.1.0] - 2025-12-20

### Added
- Initial release of the DuckDB Oracle extension with `ATTACH ... (TYPE oracle)`, `oracle_scan`, `oracle_query`, and `oracle_execute`.
