---
type: Pattern
title: "DuckDB Write Pipeline: Sink, Combine & Finalize"
description: Implementation of DuckDB's PhysicalOperator Sink pattern for high-speed writes into Oracle tables via PhysicalOracleInsert.
scope: pattern
domain: duckdb
tags: [duckdb, write-pipeline, sink-pattern, physical-operator, array-insert]
status: stable
---

# DuckDB Write Pipeline: Sink, Combine & Finalize

DuckDB executes `INSERT INTO` queries using a physical pipeline sink pattern. When an Oracle table is the target of an insert, `OracleCatalog::PlanInsert` (`src/storage/oracle_schema_entry.cpp`) returns an instance of `PhysicalOracleInsert` (`src/storage/oracle_insert.cpp`), which implements DuckDB's `PhysicalOperator` sink interface.

## Pipeline Architecture

```
PhysicalPlan: LogicalInsert -> PhysicalOracleInsert
                                       |
    +----------------------------------+----------------------------------+
    |                                                                     |
    v                                                                     v
1. GetGlobalSinkState(context)                               2. GetLocalSinkState(context)
   - OracleInsertGlobalState                                    - OracleInsertLocalState
   - Shared connection & statement                              - Lightweight local sink state
   - atomic insert_count counter
    |                                                                     |
    +----------------------------------+----------------------------------+
                                       |
                                       v
3. Sink(ExecutionContext, DataChunk, OperatorSinkInput)
   - Ingests DataChunk batch (up to 2048 tuples)
   - Transposes column vectors into OCI array bind buffers
   - Executes OCIStmtExecute with iters=chunk.size()
   - Returns SinkResultType::NEED_MORE_INPUT
                                       |
                                       v
4. Combine(ExecutionContext, OperatorSinkCombineInput)
   - Completes sink thread execution
   - Returns SinkCombineResultType::FINISHED
                                       |
                                       v
5. Finalize(Pipeline, Event, ClientContext, OperatorSinkFinalizeInput)
   - Calls OCITransCommit via OracleWriteGlobalState::Finalize
   - Flushes remaining pending batches and closes statement
   - Returns SinkFinalizeType::READY
```

## State Management

```cpp
class OracleInsertGlobalState : public GlobalSinkState {
public:
    explicit OracleInsertGlobalState(unique_ptr<OracleWriteGlobalState> write_state_p)
        : write_state(std::move(write_state_p)), insert_count(0), returned(false) {}

    unique_ptr<OracleWriteGlobalState> write_state;
    std::atomic<idx_t> insert_count {0};
    bool returned = false;
};

class OracleInsertLocalState : public LocalSinkState {};
```

## Vector-to-Buffer Transposition (`UnifiedVectorFormat`)

In `OracleWriteBatch` (`src/storage/oracle_write.cpp`), DuckDB's column vectors are normalized via `UnifiedVectorFormat` (`col_vec.ToUnifiedFormat(count, vdata)`) and transposed into OCI array bind buffers without per-row `Value` allocation:

1. **Numeric Vectors**: Fixed-width integers (`TINYINT`, `SMALLINT`, `INTEGER`, `BIGINT`, `UTINYINT`, `USMALLINT`, `UINTEGER`) and floating-point (`FLOAT`, `DOUBLE`) values read directly from `UnifiedVectorFormat::GetData<T>(vdata)[idx]` into contiguous `int64_t` (`SQLT_INT`) or `double` (`SQLT_BDOUBLE`) arrays.
2. **Date & Timestamp Vectors**: Oracle `DATE` columns convert `date_t` or `timestamp_t` values directly into 7-byte `OCIDate` structs bound with `SQLT_ODT`, while `TIMESTAMP` and `TIMESTAMP WITH TIME ZONE` columns format canonical strings wrapped in `TO_TIMESTAMP(:b, 'YYYY-MM-DD HH24:MI:SS.FF6')` / `TO_TIMESTAMP_TZ(:b, 'YYYY-MM-DD HH24:MI:SS.FF6 TZH:TZM')`.
3. **Zero-Copy String, Spatial & Vector Columns**: `VARCHAR` and `BLOB` vectors read `string_t` slices directly via `s.GetData()` and `s.GetSize()` without intermediate `std::string` allocation, validate against `NumericLimits<ub2>::Maximum()` (`65535` bytes), and copy into contiguous stride buffers.
4. **Validity Check**: For each row `r` in the batch (`idx = vdata.sel->get_index(offset + r)`):
   ```cpp
   if (!vdata.validity.RowIsValid(idx)) {
       ind_vec[r] = -1;
       len_vec[r] = 0;
   } else {
       ind_vec[r] = 0;
   }
   ```
5. **Execution & Source Cardinality**: `OCIStmtExecute` executes `iters = batch_rows` with `OCI_DEFAULT` inside a mutex-serialized write lock, and `PhysicalOracleInsert::GetDataInternal` sets `chunk.SetChildCardinality(1)` when returning the inserted row count.

## Finalization & Statement-Atomic Transaction Commit

`PhysicalOracleInsert::Finalize` commits the active transaction on the service context:
```cpp
SinkFinalizeType PhysicalOracleInsert::Finalize(Pipeline &pipeline, Event &event, ClientContext &context,
                                               OperatorSinkFinalizeInput &input) const {
    auto &gstate = input.global_state.Cast<OracleInsertGlobalState>();
    OracleWriteFinalize(*gstate.gstate); // Issues OCITransCommit
    return SinkFinalizeType::READY;
}
```
If any sink operation throws an exception before `Finalize` completes, `OCITransRollback` is explicitly invoked on the active service context so uncommitted partial batches never leak into pooled connections.
