---
type: Domain
title: DuckDB 2.0 Vectorized Engine, Type System & Extension APIs
description: Core DuckDB v2.0.0 vectorized execution primitives, VectorBuffer size synchronization, const/mutable Vector access, Identifier/Expression APIs, and TableFunction optimizer hooks.
scope: domain
domain: duckdb
tags: [duckdb, vectorized-execution, datachunk, logical-type, table-function]
status: stable
---

# DuckDB 2.0 Vectorized Engine, Type System & Extension APIs

DuckDB (`v2.0.0`) executes queries using a vectorized Morsel-driven pipeline where operators process batches of up to `STANDARD_VECTOR_SIZE` (`2048`) rows in `DataChunk` objects.

## DataChunk & VectorBuffer Memory Architecture (`v2.0.0`)

```
+-----------------------------------------------------------------------------------+
| DataChunk (Capacity: 2048 tuples, Count: N <= 2048)                               |
+-----------------------------------------------------------------------------------+
| Vector 0 (LogicalType::BIGINT, VectorBufferType::STANDARD_BUFFER, v_size = N)     |
| - DataBuffer: [int64_t, int64_t, ...] (FlatVector::GetDataMutable<int64_t>)       |
| - ValidityMask: Bitmask array (FlatVector::ValidityMutable / SetNull)             |
+-----------------------------------------------------------------------------------+
| Vector 1 (LogicalType::VARCHAR, VectorBufferType::STRING_BUFFER, v_size = N)      |
| - DataBuffer: [string_t, string_t, ...] (16 bytes: 4B len + 12B inline or prefix) |
| - AuxiliaryData: StringHeap (StringVector::AddString / AddStringOrBlob)           |
+-----------------------------------------------------------------------------------+
| Vector 2 (LogicalType::LIST(FLOAT), VectorBufferType::LIST_BUFFER, v_size = N)    |
| - DataBuffer: [list_entry_t, ...] (offset + length pairs)                         |
| - ChildVector: Vector of LogicalType::FLOAT (ListVector::GetEntryMutable)         |
+-----------------------------------------------------------------------------------+
```

### Key Vector Primitives & DuckDB 2.0 Contracts

1. **`DataChunk::SetChildCardinality(count)` vs `SetCardinalityUnsafe(count)`**:
   - Every `VectorBuffer` in DuckDB `v2.0.0` tracks its own element count (`v_size`, returned by `vector.size()`).
   - Core vectorized operators such as `VectorOperations::IsNull` and `VectorOperations::IsNotNull` (`duckdb/src/common/vector_operations/null_operations.cpp`) iterate over `0..input.size()`, not `DataChunk::size()`.
   - Table functions (`OracleQueryFunction` in `src/oracle_scan.cpp`) and physical source operators (`PhysicalOracleInsert::GetDataInternal` in `src/storage/oracle_insert.cpp`) that populate vectors via `FlatVector::GetDataMutable`, `FlatVector::SetNull`, or `StringVector::AddString` **must** call `output.SetChildCardinality(row_count)` (which calls `FlatVector::SetSize(v, row_count)` on every flat/constant child vector and sets `DataChunk::count`) rather than `SetCardinalityUnsafe(row_count)`.

2. **Strict Const vs Mutable Vector Access (`v2.0.0`)**:
   - Read-only access uses `FlatVector::GetData<T>(const Vector &)`, `FlatVector::Validity(const Vector &)`, and `ListVector::GetEntry(const Vector &)`.
   - Writable access requires `FlatVector::GetDataMutable<T>(Vector &)`, `FlatVector::ValidityMutable(Vector &)`, `FlatVector::SetNull(Vector &, idx_t, bool)`, and `ListVector::GetEntryMutable(Vector &)`.
   - Temporary vectors constructed for casting or intermediate evaluation use `VectorDataInitialization::UNINITIALIZED`:
     ```cpp
     Vector varchar_vec(LogicalType::VARCHAR, VectorDataInitialization::UNINITIALIZED);
     ```

3. **`UnifiedVectorFormat` for Arbitrary Input Vectors**:
   - Physical sink operators (`OracleWriteBatch` in `src/storage/oracle_write.cpp`) receive chunks whose columns may be `FLAT_VECTOR`, `CONSTANT_VECTOR`, or `DICTIONARY_VECTOR`.
   - Call `col_vec.ToUnifiedFormat(count, vdata)` once per column and index via `auto idx = vdata.sel->get_index(row_idx)` and `vdata.validity.RowIsValid(idx)` combined with `UnifiedVectorFormat::GetData<T>(vdata)` instead of calling `col_vec.GetValue(row_idx)` per row.

4. **`string_t` Inlining & Zero-Copy Slicing**:
   - Strings $\le 12$ bytes are stored inline inside the 16-byte `string_t` struct; strings $> 12$ bytes store a 4-byte length, 4-byte prefix, and 8-byte pointer into the vector's `StringHeap`.
   - Use `StringVector::AddString(vector, ptr, len)` for `VARCHAR` / `JSON` output and `StringVector::AddStringOrBlob(vector, ptr, len)` for `BLOB` output (`src/oracle_type_conversion.cpp`).

## DuckDB 2.0 Catalog, Identifier & Expression APIs

1. **`duckdb::Identifier` in Bind & Schema Signatures**:
   - `table_function_bind_t` receives `vector<Identifier> &names` (`names.emplace_back(col_name)`), whereas `StringUtil::Join` requires `vector<string>`.
   - `Catalog::GetDefaultSchema()` returns `optional<Identifier>` (`src/include/oracle_catalog.hpp`, `src/storage/oracle_schema_entry.cpp`).
   - `SchemaCatalogEntry::Scan(ClientContext &context, CatalogType type, const std::function<void(CatalogEntry &)> &callback)` requires the `ClientContext &` overload when enumerating entries for `duckdb_tables()` and `duckdb_views()`.

2. **Encapsulated `Expression` & `ParsedExpression` Accessors**:
   - Direct member fields (`expr.expression_class`, `expr.type`, `expr.return_type`, `expr.alias`) are private in DuckDB `v2.0.0`.
   - Use `expr.GetExpressionClass()`, `expr.GetExpressionType()`, `expr.GetReturnType()`, and `expr.GetAlias()` (`src/oracle_pushdown.cpp`).

## DuckDB LogicalType Hierarchy (`src/oracle_type_registry.cpp`)

- **Primitives**: `BOOLEAN`, `TINYINT`, `SMALLINT`, `INTEGER`, `BIGINT`, `HUGEINT`, `FLOAT`, `DOUBLE`.
- **Fixed-Point Decimals**: `LogicalType::DECIMAL(width, scale)` ($1 \le \text{width} \le 38$).
- **Temporal Types**: `DATE`, `TIMESTAMP`, `TIMESTAMP_TZ`.
- **JSON & Nested Types**:
  - `LogicalType::JSON()`: Alias over `VARCHAR` used for Oracle 21c/23ai native `JSON` (`SQLT_JSON`).
  - `LogicalType::LIST(child_type)`: Variable-length list used for Oracle 23ai `VECTOR` columns (`FLOAT` or `DOUBLE` elements).
  - `LogicalType::GEOMETRY` / `LogicalType::GEOMETRY("EPSG:<srid>")`: Native DuckDB geometry type populated via WKT from `SDO_UTIL.TO_WKTGEOMETRY`.

## TableFunction Registration & Optimizer Hooks (`src/oracle_scan.cpp`)

```cpp
TableFunction oracle_scan("oracle_scan", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
                          OracleQueryFunction, OracleScanBind, OracleInitGlobal);
oracle_scan.projection_pushdown = true;
oracle_scan.filter_pushdown = true;
oracle_scan.filter_prune = true;
oracle_scan.pushdown_complex_filter = OraclePushdownComplexFilter;
oracle_scan.to_string = OracleToString;
oracle_scan.cardinality = OracleCardinality;
oracle_scan.get_bind_info = OracleGetBindInfo;
oracle_scan.get_table_entry = OracleGetTableEntry;
```

### Optimizer & Execution Callbacks

| Callback / Flag | Implementation (`src/oracle_scan.cpp`) | Purpose |
| :--- | :--- | :--- |
| `projection_pushdown = true` | `OracleInitGlobal` | Projects only referenced columns in the generated Oracle `SELECT` list. |
| `filter_pushdown = true` | `OraclePushdownFilterSet` | Pushes `TableFilterSet` constraints (`=`, `!=`, `<`, `<=`, `>`, `>=`, `IN`, `IS NULL`, `IS NOT NULL`) into Oracle SQL. |
| `filter_prune = true` | `BuildOutputColumnMapping` | Maps `input.projection_ids` into `input.column_ids` so filter-only columns are omitted from the `SELECT` list and `OCIDefineByPos` buffers. |
| `pushdown_complex_filter` | `OraclePushdownComplexFilter` | Extracts `LIKE`, `prefix`, `suffix`, `contains`, `IN`, and conjunction expressions into Oracle `WHERE` clauses. |
| `get_bind_info` | `OracleGetBindInfo` | Returns `BindInfo(*bind_data.table_entry)` for catalog table scans or `BindInfo(ScanType::EXTERNAL)` for raw queries. |
| `get_table_entry` | `OracleGetTableEntry` | Exposes `optional_ptr<CatalogEntry>(bind_data.table_entry)` to DuckDB optimizer rules (`LogicalGet::GetTable()`). |
| `cardinality` | `OracleCardinality` | Returns `NodeStatistics(estimated_row_count)` populated from `ALL_TABLES.NUM_ROWS` during `OracleTableEntry::LoadColumns`. |
| `to_string` | `OracleToString` | Surfaces `Table` / `Query` and pushed-down `Filters` in `EXPLAIN` plan output. |
