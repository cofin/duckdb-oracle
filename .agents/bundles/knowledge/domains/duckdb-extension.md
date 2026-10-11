---
type: Domain
title: DuckDB Vectorized Engine & Type System
description: Core concepts of DuckDB's vectorized execution engine, DataChunk and Vector representations, LogicalType hierarchy, and TableFunction lifecycle.
scope: domain
domain: duckdb
tags: [duckdb, vectorized-execution, datachunk, logical-type, table-function]
status: stable
---

# DuckDB Vectorized Engine & Type System

DuckDB uses a vectorized relational execution model based on Morsel-driven parallelism. Instead of the Volcano tuple-at-a-time iterator model, DuckDB operators process batches of tuples represented as `DataChunk` objects.

## DataChunk & Vector Memory Architecture

```
+-----------------------------------------------------------------------------------+
| DataChunk (Capacity: 2048 tuples, Count: N <= 2048)                               |
+-----------------------------------------------------------------------------------+
| Vector 0 (LogicalType::INTEGER)                                                   |
| - DataBuffer: [int32_t, int32_t, int32_t, ...] (Contiguous 4-byte values)         |
| - ValidityMask: Bitmask array (1 bit per tuple: 1=Valid, 0=Null)                  |
| - VectorType: FLAT | CONSTANT | DICTIONARY | SEQUENCE                             |
+-----------------------------------------------------------------------------------+
| Vector 1 (LogicalType::VARCHAR)                                                   |
| - DataBuffer: [string_t, string_t, ...] (16 bytes each: length, prefix, ptr/data)  |
| - AuxiliaryBuffer: StringHeap (holds out-of-line strings > 12 bytes)              |
| - ValidityMask: Bitmask array                                                     |
+-----------------------------------------------------------------------------------+
| Vector 2 (LogicalType::LIST(LogicalType::FLOAT))                                  |
| - DataBuffer: [list_entry_t, ...] (offset + length pairs)                         |
| - ChildVector: Vector of LogicalType::FLOAT (flattened child elements)            |
+-----------------------------------------------------------------------------------+
```

### Key Vector Primitives

1. **`STANDARD_VECTOR_SIZE`**:
   - Compiles by default to `2048` rows.
   - All batch operators, table functions, and sinks must handle chunks with $0 \le \text{size} \le \text{STANDARD\_VECTOR\_SIZE}$.

2. **`ValidityMask`**:
   - High-performance bitmap tracking NULL values ($1 = \text{valid}, 0 = \text{null}$).
   - `FlatVector::SetNull(vector, row_idx, is_null)` or `vector.GetValidity().SetInvalid(row_idx)`.
   - Bypasses branching during vectorized arithmetic if `validity.AllValid()` returns true.

3. **`string_t` Inlining**:
   - Strings $\le 12$ bytes are completely inlined into the 16-byte `string_t` struct (4-byte length + 12-byte payload).
   - Strings $> 12$ bytes store a 4-byte length, a 4-byte prefix (enabling fast string equality/sorting without heap access), and an 8-byte pointer to the string heap managed by `StringVector::AddString`.

## DuckDB LogicalType Hierarchy

DuckDB enforces a strongly-typed catalog with extensible types:

- **Primitives**: `BOOLEAN`, `TINYINT`, `SMALLINT`, `INTEGER`, `BIGINT`, `HUGEINT` (128-bit), `FLOAT`, `DOUBLE`.
- **Fixed-Point Decimals**: `LogicalType::DECIMAL(width, scale)`. Stored internally as `int16_t` ($\le 4$ digits), `int32_t` ($\le 9$ digits), `int64_t` ($\le 18$ digits), or `hugeint_t` ($\le 38$ digits).
- **Temporal Types**: `DATE` (days since 1970-01-01), `TIME`, `TIMESTAMP` (microseconds since epoch), `TIMESTAMP_TZ`, `INTERVAL`.
- **Complex & Nested Types**:
  - `LogicalType::LIST(child_type)`: Offset/length pointing into a child vector (used for Oracle 23ai `VECTOR` embeddings).
  - `LogicalType::STRUCT(...)`: Ordered key-value composite types.
  - `LogicalType::GEOMETRY`: Provided by the `spatial` extension; compatible with WKB/WKT conversions.

## TableFunction Registration & Execution Lifecycle

Table functions produce `DataChunk` batches into the query pipeline (e.g., `oracle_scan` and `oracle_query`):

```cpp
TableFunction oracle_scan("oracle_scan", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
                          OracleScanFunction, OracleScanBind, OracleScanInitGlobal, OracleScanInitLocal);
oracle_scan.filter_pushdown = true;
oracle_scan.projection_pushdown = true;
oracle_scan.pushdown_complex_filter = OraclePushdownComplexFilter;
```

### Execution Lifecycle

1. **`Bind` (`OracleScanBind`)**:
   - Inspects input parameters (connection string, schema name, table name).
   - Resolves credentials via `SecretManager`.
   - Describes columns via OCI and populates `return_types` and `names`.
   - Returns a heap-allocated `FunctionData` (`OracleBindData`).

2. **`InitGlobal` (`OracleScanInitGlobal`)**:
   - Initializes shared execution state (`GlobalTableFunctionState`) across all worker threads.
   - Calculates partitions or establishes the primary cursor.

3. **`InitLocal` (`OracleScanInitLocal`)**:
   - Initializes thread-local execution state (`LocalTableFunctionState`).
   - Allocates thread-local OCI column buffers and define arrays.

4. **`Function` (`OracleScanFunction`)**:
   - Called repeatedly by DuckDB worker threads.
   - Populates the target `DataChunk` with up to `STANDARD_VECTOR_SIZE` tuples.
   - When no more rows exist, sets `chunk.SetCardinality(0)`.
