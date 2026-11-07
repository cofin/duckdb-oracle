# Catalog, Schema & TableFunction Scans

## 1. Per-`ATTACH` Catalog Isolation (`src/storage/oracle_catalog.cpp`, `src/storage/oracle_schema_entry.cpp`)

- **`OracleAttach`**: Allocates `make_shared_ptr<OracleCatalogState>(connection_string, wallet_path)` per attachment and registers it under the attached alias (`OracleCatalogState::Register(state, name)`).
- **`OracleCatalog` & `OracleSchemaEntry`**:
  - Inherit directly from `duckdb::Catalog` and `duckdb::SchemaCatalogEntry` (`info.internal = false`).
  - Synchronize cached schemas and tables against `state->GetCacheGeneration()`, moving invalidated entries into `retired_schemas` / `retired_tables` so in-flight queries retain valid pointers.
  - `OracleSchemaEntry::Scan` records tables with unsupported column types in `unresolvable_entries` and skips them so `duckdb_tables()` / `duckdb_columns()` succeed, while direct `LookupEntry` rethrows the error.
  - `OracleSchemaEntry::GetSimilarEntry` scores against `state->ListObjects(name)` without issuing per-table `ALL_TAB_COLUMNS` queries.

## 2. `OracleTableEntry` & `TableFunction` Optimizer Callbacks (`src/storage/oracle_table_entry.cpp`, `src/oracle_scan.cpp`)

- `OracleTableEntry::LoadColumns` queries `ALL_TAB_COLUMNS` with a `LEFT JOIN all_tables` in a single round-trip to populate column definitions, `NotNullConstraint` (`nullable == "N"`), and `estimated_row_count` (`ALL_TABLES.NUM_ROWS`).
- `oracle_scan` registers:
  - `projection_pushdown = true`
  - `filter_pushdown = true`
  - `filter_prune = true`
  - `pushdown_complex_filter = OraclePushdownComplexFilter`
  - `get_bind_info = OracleGetBindInfo`
  - `get_table_entry = OracleGetTableEntry`
  - `cardinality = OracleCardinality`
  - `to_string = OracleToString`
- `OracleQueryFunction` populates vectors column-major and calls `output.SetChildCardinality(rows_fetched)` (and `output.SetChildCardinality(0)` on completion).
