# Changelog

All notable changes to the DuckDB Oracle extension are documented in this file.

## [0.3.1] - 2026-10-22

### Changed
- **Documentation & Community Extension Descriptor**: Simplified and refined `README.md`, `description.yml`, and reference documentation under `docs/`, and fixed `scripts/bump_duckdb_version.sh` compatibility table appending.

## [0.3.0] - 2026-10-21

### Added
- **DuckDB v2.0.0 Support**: Upgraded target DuckDB version and release matrix to `v2.0.0`.
- **On-Demand Oracle Client Loading**: Replaced compile-time client linking with runtime loading of Oracle Instant Client (19c, 21c, or 23ai) on first connection. Installing and loading the extension (`LOAD oracle;`) requires no local Oracle libraries.
- **Optimizer Statistics & Bind Integration**: Wired DuckDB table function callbacks (`get_bind_info`, `get_table_entry`, `cardinality`, `to_string`, and `filter_prune`) and populated table row-count estimates from `ALL_TABLES.NUM_ROWS`.
- **DuckDB Community Extensions Support**: Added community extension descriptor (`description.yml`) and platform build configuration.

### Changed
- **Scan & Write Throughput**: Reduced intermediate allocations in numeric parsing and string/blob array binding, and aligned vector child cardinality updates with DuckDB 2.0 vector buffers.

## [0.2.3] - 2026-04-19

### Added
- **DuckDB v1.5.6 Support**: Upgraded target DuckDB version and release matrix to `v1.5.6`.
- **Call Timeouts & Statement Cache**: Added `oracle_call_timeout_ms` and `oracle_stmt_cache_size` settings.
- **Flashback Queries**: Added `as_of_scn` and `as_of_timestamp` parameters on `oracle_scan`.
- **Spatial Integration Tests**: Added `SDO_GEOMETRY` point and polygon read/write round-trip integration tests.

### Fixed
- **Environment Lifetime**: Co-owned the Oracle environment handle across pooled connections and statements so cached resources never outlive the environment.
- **Large Object & Vector Buffers**: Sized `CLOB`, `BLOB`, and serialized `VECTOR` fetch buffers to `65535` bytes with truncation detection.
- **Projection Pushdown**: Deferred SQL generation and statement preparation until optimizer projection and filter pushdown complete.
- **Catalog State Isolation**: Scoped schema and table metadata caches per attached catalog rather than sharing process-wide state.
- **Write Pipeline**: Fixed `DATE` binding in `INSERT`, string overflow checks, `NUMBER(p, s)` precision/scale mapping, and statement-atomic rollback on error.

## [0.2.2] - 2026-04-12

### Changed
- **DuckDB v1.5.5 Support**: Upgraded DuckDB submodule and release targets to `v1.5.5`.
- **Oracle Instant Client 23.26**: Updated setup scripts for Linux, macOS, and Windows to Instant Client `23.26`.

## [0.2.1] - 2026-03-28

### Added
- **Partition Metadata & Pruning**: Added partition discovery (`ALL_PART_TABLES`, `ALL_TAB_PARTITIONS`) and partition-aware filter pushdown.
- **Vector & Spatial Writes**: Enabled `INSERT INTO` for Oracle 23ai `VECTOR` and `MDSYS.SDO_GEOMETRY` columns.

## [0.2.0] - 2026-03-10

### Added
- **Table Writes (`INSERT INTO`)**: Added batched array binding for inserting into attached Oracle tables.
- **Secret Manager & Wallets**: Added `CREATE SECRET ... (TYPE oracle)` with `WALLET_PATH` support for Oracle Wallet and Autonomous Database connections.

## [0.1.3] - 2026-02-15

### Added
- **Lazy Schema Discovery**: Added `oracle_lazy_schema_loading`, `oracle_use_current_schema`, `oracle_metadata_object_types`, and `oracle_metadata_result_limit` settings.
- **Views & Synonyms**: Exposed views, materialized views, and synonyms in attached catalogs.

## [0.1.2] - 2026-01-25

### Added
- **Connection Pooling**: Added session pooling (`oracle_connection_cache`, `oracle_connection_limit`) and `oracle_clear_cache()`.
- **Cross-Platform Builds**: Added CI and release builds for Linux (`linux_amd64`, `linux_arm64`), macOS (`osx_arm64`), and Windows (`windows_amd64`).

## [0.1.1] - 2026-01-10

### Added
- **Batched Reads**: Added array fetching tunable via `oracle_array_size`, `oracle_prefetch_rows`, and `oracle_prefetch_memory`.
- **Filter & Projection Pushdown**: Added `WHERE` filter and column projection pushdown.

## [0.1.0] - 2025-12-20

### Added
- Initial release with `ATTACH ... (TYPE oracle)`, `oracle_scan`, `oracle_query`, and `oracle_execute`.
