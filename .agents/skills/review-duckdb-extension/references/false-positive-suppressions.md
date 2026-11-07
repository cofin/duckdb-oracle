# DuckDB Extension False-Positive Suppressions (`FP-DDB-01`..`FP-DDB-05`)

- **`FP-DDB-01` (DO NOT FLAG)**: Returning `true` with an empty SQL string for `TableFilterType::DYNAMIC_FILTER` and `TableFilterType::BLOOM_FILTER` in `FilterToSQL` (`src/oracle_pushdown.cpp`).
  - *Rationale*: DuckDB evaluates dynamic join filters and bloom filters locally in the scan operator; returning `true` with empty SQL allows static `TableFilterSet` entries on other columns to still be pushed down to Oracle.
- **`FP-DDB-02` (DO NOT FLAG)**: Skipping tables that throw `InvalidInputException` / `NotImplementedException` during `OracleSchemaEntry::Scan` (`unresolvable_entries`) while rethrowing in `OracleSchemaEntry::LookupEntry`.
  - *Rationale*: Allows `duckdb_tables()` and `information_schema.columns` to enumerate schemas containing legacy/unsupported Oracle tables without failing the entire catalog scan, while direct queries against an unsupported table still report the exact error.
- **`FP-DDB-03` (DO NOT FLAG)**: Retaining evicted `OracleSchemaEntry` and `OracleTableEntry` instances inside `retired_schemas` and `retired_tables` vectors on cache invalidation (`SyncGeneration`).
  - *Rationale*: Preserves pointer stability for in-flight DuckDB query plans holding raw `optional_ptr<CatalogEntry>` references across `oracle_clear_cache()`.
- **`FP-DDB-04` (DO NOT FLAG)**: Calling both `duckdb_extension_load(oracle)` and `duckdb_extension_statically_link(oracle)` in `extension_config.cmake`.
  - *Rationale*: Required by DuckDB `v2.0.0` CMake build scripts so static `unittest` and `duckdb` shell targets link `oracle_extension` and define `EXT_VERSION_ORACLE`.
- **`FP-DDB-05` (DO NOT FLAG)**: Setting `repo.ref: HEAD` inside the local repository's `description.yml`.
  - *Rationale*: The in-repo `description.yml` uses `ref: HEAD` for local `extension-ci-tools` workflows, while the upstream `duckdb/community-extensions` PR pins `repo.ref` and `repo.variegata` to exact 40-character release commit SHAs.
