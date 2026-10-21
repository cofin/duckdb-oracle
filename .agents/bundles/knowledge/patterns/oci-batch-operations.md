---
type: Pattern
title: OCI Batch Operations: Array Fetch & Array Bind
description: High-throughput batch data transfer patterns between DuckDB vectors and Oracle OCI using array fetching and bulk array binding.
scope: pattern
domain: oracle
tags: [oracle, oci, batch-processing, array-fetch, array-bind]
status: stable
---

# OCI Batch Operations: Array Fetch & Array Bind

Sequential, single-row round-trips over SQL*Net introduce massive network latency. High-throughput data movement requires batching at the OCI transport layer, aligned with DuckDB's vectorized execution model.

## 1. High-Throughput Array Fetching

In `src/oracle_scan.cpp`, DuckDB fetches rows in blocks rather than row-by-row:

```cpp
// 1. Configure prefetch parameters on statement handle before execute
ub4 prefetch_rows = static_cast<ub4>(OracleEffectivePrefetchRows(settings)); // Default: 256
CheckOCIError(OCIAttrSet(stmt.get(), OCI_HTYPE_STMT, &prefetch_rows, 0,
                         OCI_ATTR_PREFETCH_ROWS, errhp),
              errhp, "Failed to set OCI prefetch rows");

// 2. Bound call timeout via oracle_call_timeout_ms (default: 30000 ms)
ub4 call_timeout_ms = static_cast<ub4>(settings.call_timeout_ms);
CheckOCIOptionalAttributeORA24315(
    OCIAttrSet(stmt.get(), OCI_HTYPE_STMT, &call_timeout_ms, 0,
               OCI_ATTR_CALL_TIMEOUT, errhp),
    errhp, "Failed to set OCI statement call timeout");

// 3. Define output column buffers as contiguous arrays of capacity 'array_size'
// array_size is tuned between 1 and STANDARD_VECTOR_SIZE (2048)
for (idx_t col = 0; col < column_count; col++) {
    OCIDefineByPos(stmt.get(), &defnp, errhp, col + 1,
                   column_buffers[col].data(), buffer_size,
                   sqlt_type, indicator_array.data(),
                   actual_lengths.data(), nullptr, OCI_DEFAULT);
}

// 4. Fetch an entire batch into client buffers in one syscall
sword status = OCIStmtFetch2(stmt.get(), errhp, array_size, OCI_FETCH_NEXT, 0, OCI_DEFAULT);
```

### Tuning Prefetch, Array Sizing & Timeouts

- **`oracle_prefetch_rows`** (default: `256`): Controls how many rows the Oracle client library requests ahead from the server cache into the local SQL*Net receive buffer, matching `oracle_array_size` so each `OCIStmtFetch2` batch completes in one round-trip without excess client buffer memory.
- **`oracle_prefetch_memory`** (default: `0` = automatic): Bounded memory budget for prefetch buffers in bytes (up to 1 GiB).
- **`oracle_array_size`** (default: `256`): Number of rows fetched per `OCIStmtFetch2` invocation directly into the C++ application arrays. Must not exceed `STANDARD_VECTOR_SIZE` (`2048`).
- **`oracle_call_timeout_ms`** (default: `30000`): Per-round-trip OCI call timeout in milliseconds (`0` disables timeout, max `3,600,000`).
- **`oracle_stmt_cache_size`** (default: `20`): Per-session OCI prepared statement cache capacity (`0` disables caching, max `1024`).
- **Column-Major Scan Population & `OracleSlice` Parsing**: `OracleQueryFunction` (`src/oracle_scan.cpp`) iterates column-major (`for col_idx` outer, `for row_idx` inner) and passes non-owning `OracleSlice {const char *ptr, size_t len}` views to `SetOracleOutputValue` (`src/oracle_type_conversion.cpp`), writing directly into `FlatVector::GetDataMutable<T>` and synchronizing vector sizes via `output.SetChildCardinality(rows_fetched)`.

## 2. Bulk Array Binding for Writes (Sink Pattern)

During `INSERT INTO ora.table SELECT ...`, DuckDB pushes data through `PhysicalOracleInsert` into `src/storage/oracle_insert.cpp` and `src/storage/oracle_write.cpp`:

```
DuckDB DataChunk (Vector of Rows, up to 2048)
       |
       v  (Transposed Column-Wise into Contiguous Buffers)
+--------------------------------------------------------------+
| Col 0: int64_t values[2048]        indicators: sb2 ind[2048]  |
| Col 1: char str_values[2048][65536] indicators: sb2 ind[2048] |
| Col 2: OCIDate date_values[2048]   indicators: sb2 ind[2048]  |
+--------------------------------------------------------------+
       |
       v  (Bound with OCIBindByPos: SQLT_INT, SQLT_STR, SQLT_ODT)
OCIBindByPos(stmt, &bindhp, errhp, 1, values, sizeof(int64_t),
             SQLT_INT, ind, nullptr, nullptr, 0, nullptr, OCI_DEFAULT);
       |
       v  (Executed with iters = current_batch_rows)
OCIStmtExecute(svchp, stmt, errhp, iters /* current_batch_rows */, 0,
               nullptr, nullptr, OCI_DEFAULT);
```

### Column-Wise Array Binding Invariants

1. **Contiguous Storage & Stride**: Column buffers must be contiguous in memory across all rows in the batch, with string buffers sized to the maximum value length in the batch (validated `<= 65535` bytes, `NumericLimits<ub2>::Maximum()`) and `DATE` columns bound via 7-byte `OCIDate` (`SQLT_ODT`) structs.
2. **Indicator Parallelism**: Each column buffer requires an `sb2` indicator array of equal length. For every row `i`:
   - If DuckDB `ValidityMask::RowIsValid(i)` is true: `indicators[i] = 0`.
   - If row is null: `indicators[i] = -1`.
3. **Execution Iterations (`iters`)**: In `OCIStmtExecute`, pass `iters = current_batch_rows` with `OCI_DEFAULT`. Oracle processes all `N` rows in a single batch round-trip, and `OracleWriteGlobalState` rolls back the active transaction via `OCITransRollback` if any batch execution fails before `Finalize` commits.
