# Filter/Projection Pushdown & Physical Sink Writes

## 1. Projection Pushdown & Filter Column Pruning (`src/oracle_scan.cpp`, `src/oracle_pushdown.cpp`)

- **Projection Mapping (`BuildOutputColumnMapping`)**: In `OracleInitGlobal`, when `input.projection_ids` is non-empty (`filter_prune = true`), map each output column `i` to `input.column_ids[input.projection_ids[i]]` so filter-only columns are excluded from `state->active_bind_cols`, the generated Oracle `SELECT` list, and `OCIDefineByPos` buffers.
- **Filter Translation (`OraclePushdownFilterSet` & `OraclePushdownComplexFilter`)**:
  - Push `CONSTANT_COMPARISON`, `IN_FILTER`, `IS_NULL`, `IS_NOT_NULL`, `CONJUNCTION_AND`, `CONJUNCTION_OR`, `OPTIONAL_FILTER`, `EXPRESSION_FILTER`, and `LIKE`/`prefix`/`suffix`/`contains` on pushdown-eligible columns.
  - Refuse to push empty string literals (`''`) because Oracle treats `''` as `NULL`.
  - Skip `DYNAMIC_FILTER` and `BLOOM_FILTER` (`return true` with empty SQL) so local join filters do not disable static pushdown.

## 2. Physical Sink Write Pipeline (`src/storage/oracle_insert.cpp`, `src/storage/oracle_write.cpp`)

- `OracleCatalog::PlanInsert` passes resolved `oracle_table.GetSchemaName()` and `oracle_table.GetTableName()` to `PhysicalOracleInsert` so cross-schema and synonym inserts target the underlying table.
- `OracleWriteBatch` normalizes each input column via `col_vec.ToUnifiedFormat(count, vdata)`, transposes values into contiguous array bind buffers, executes `OCIStmtExecute(..., iters = batch_rows, ..., OCI_DEFAULT)`, and rolls back (`OCITransRollback`) on any error before `OracleWriteFinalize` (`OCITransCommit`).
- `PhysicalOracleInsert::GetDataInternal` calls `chunk.SetChildCardinality(1)` after writing `insert_count`.
