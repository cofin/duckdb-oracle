# DuckDB Extension Review Checklist (`DDB-01`..`DDB-08`)

| Rule ID | Severity | Check |
| :--- | :--- | :--- |
| `DDB-01` | `P0` | **`DataChunk::SetChildCardinality` Synchronization**: Any code populating flat vectors via `FlatVector::GetDataMutable`, `FlatVector::SetNull`, or `StringVector::AddString` (`OracleQueryFunction`, `PhysicalOracleInsert::GetDataInternal`) must call `output.SetChildCardinality(count)`, never `SetCardinalityUnsafe(count)`. |
| `DDB-02` | `P1` | **Direct `Catalog` & `SchemaCatalogEntry` Inheritance**: `OracleCatalog` and `OracleSchemaEntry` must inherit directly from `Catalog` and `SchemaCatalogEntry` (`info.internal = false`, never `DuckCatalog`) and synchronize cached entries via `OracleCatalogState::cache_generation`. |
| `DDB-03` | `P1` | **Oracle Empty-String (`''`) Pushdown Guard**: `src/oracle_pushdown.cpp` must never push empty string literals (`''`) into Oracle `WHERE` clauses (`=`, `!=`, `<`, `<=`, `>`, `>=`, `IN`, `LIKE`, `prefix`, `suffix`, `contains`). |
| `DDB-04` | `P1` | **Filter Column Pruning (`filter_prune`)**: `OracleInitGlobal` must map `input.projection_ids` into `input.column_ids` via `BuildOutputColumnMapping` so filter-only columns are omitted from the Oracle `SELECT` list and `OCIDefineByPos` buffers. |
| `DDB-05` | `P1` | **`UnifiedVectorFormat` in Physical Sinks**: `OracleWriteBatch` must normalize input vectors with `col_vec.ToUnifiedFormat(count, vdata)` rather than assuming `FLAT_VECTOR` or calling `GetValue(row)` per row. |
| `DDB-06` | `P1` | **Offline Unit Test Portability**: `test/unit_tests/*.test` must pass on clean runners without Oracle Instant Client installed; tests reaching a connection attempt must match both missing Instant Client and connection error messages via `<REGEX>:`. |
| `DDB-07` | `P1` | **Community Extension Descriptor Integrity**: `description.yml` and `scripts/bump_duckdb_version.sh` must keep exact 40-char commit SHAs for `repo.ref` / `repo.variegata` and never overwrite `variegata:` with `vcpkg_commit`. |
| `DDB-08` | `P2` | **Canonical Test File Placement**: New tests must be added to existing canonical `test/unit_tests/` or `test/integration_tests/` files rather than standalone `test_*remediation*.test` or `test_*audit*.test` files. |
