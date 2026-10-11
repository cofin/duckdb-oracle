---
type: Pattern
title: DuckDB Write Pipeline: Sink, Combine & Finalize
description: Implementation of DuckDB's PhysicalOperator Sink pattern for high-speed writes into Oracle tables via PhysicalOracleInsert.
scope: pattern
domain: duckdb
tags: [duckdb, write-pipeline, sink-pattern, physical-operator, array-insert]
status: stable
---

# DuckDB Write Pipeline: Sink, Combine & Finalize

DuckDB executes `INSERT INTO` queries using a physical pipeline sink pattern. When an Oracle table is the target of an insert, `OracleTableEntry::BindInsert` returns an instance of `PhysicalOracleInsert` (`src/storage/oracle_insert.cpp`), which implements DuckDB's `PhysicalOperator` sink interface.

## Pipeline Architecture

```
PhysicalPlan: LogicalInsert -> PhysicalOracleInsert
                                       |
    +----------------------------------+----------------------------------+
    |                                                                     |
    v                                                                     v
1. GetGlobalSinkState(context)                               2. GetLocalSinkState(context)
   - OracleInsertGlobalState                                    - OracleInsertLocalState
   - Shared connection & statement                              - Thread-local write state
   - insert_count counter                                       - Thread-local buffers
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
   - Merges thread-local state into global state
   - Returns SinkCombineResultType::FINISHED
                                       |
                                       v
5. Finalize(Pipeline, Event, ClientContext, OperatorSinkFinalizeInput)
   - Calls OCITransCommit via OracleWriteFinalize
   - Flushes remaining pending batches and closes statement
   - Returns SinkFinalizeType::READY
```

## State Management

```cpp
struct OracleInsertGlobalState : public GlobalSinkState {
    OracleInsertGlobalState(unique_ptr<OracleWriteBindData> bind_data_p,
                            unique_ptr<OracleWriteGlobalState> gstate_p)
        : bind_data(std::move(bind_data_p)), gstate(std::move(gstate_p)), insert_count(0) {}

    unique_ptr<OracleWriteBindData> bind_data;
    unique_ptr<OracleWriteGlobalState> gstate;
    idx_t insert_count;
};

struct OracleInsertLocalState : public LocalSinkState {
    OracleInsertLocalState() : lstate(make_uniq<OracleWriteLocalState>()) {}
    unique_ptr<OracleWriteLocalState> lstate;
};
```

## Vector-to-Buffer Transposition

In `OracleWriteSink`, DuckDB's column-major vectors are converted to OCI bind buffers:

1. **Numeric & Decimal Vectors**: Fixed-width integers (`int16_t`, `int32_t`, `int64_t`) and floats copy directly into contiguous array buffers; `DECIMAL(p, s)` values are formatted with exact scale into string buffers.
2. **Date & Timestamp Vectors**: Oracle `DATE` columns are populated as 7-byte `OCIDate` structs bound with `SQLT_ODT`, while `TIMESTAMP` and `TIMESTAMP WITH TIME ZONE` columns use `OCIDateTime*` descriptors.
3. **String, Spatial & Vector Columns**: Strings (`string_t`), WKT geometries (`SDO_GEOMETRY(:b)`), and serialized vectors (`TO_VECTOR(:b)`) are copied into 64 KB (`65536`-byte) row slots with explicit overflow checking.
4. **Validity Check**: For each row `i` in the chunk:
   ```cpp
   if (FlatVector::IsNull(vector, i)) {
       indicator_buffer[i] = OCI_IND_NULL;
   } else {
       indicator_buffer[i] = OCI_IND_NOTNULL;
   }
   ```
5. **Execution & Batch Errors**: `OCIStmtExecute` executes `iters = chunk.size()` with `OCI_BATCH_ERRORS`.

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
