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

## Vector-to-Buffer Transposition

In `OracleWriteGlobalState::Sink`, DuckDB's column-major vectors are converted to OCI bind buffers:

1. **Numeric & Decimal Vectors**: Fixed-width integers (`int64_t`) and `double` values copy directly into contiguous array buffers via `FlatVector::GetData<T>`; `DECIMAL(p, s)` and `HUGEINT` values are formatted with exact scale into string buffers.
2. **Date & Timestamp Vectors**: Oracle `DATE` columns are populated as 7-byte `OCIDate` structs bound with `SQLT_ODT`, while `TIMESTAMP` and `TIMESTAMP WITH TIME ZONE` columns format canonical strings wrapped in `TO_TIMESTAMP(:b, 'YYYY-MM-DD HH24:MI:SS.FF6')` / `TO_TIMESTAMP_TZ(:b, 'YYYY-MM-DD HH24:MI:SS.FF6 TZH:TZM')`.
3. **String, Spatial & Vector Columns**: Strings (`string_t`), WKT geometries (`SDO_GEOMETRY(:b, srid)`), and serialized vectors (`TO_VECTOR(:b)`) are validated against `NumericLimits<ub2>::Maximum()` (`65535` bytes) and copied into contiguous stride buffers.
4. **Validity Check**: For each row `i` in the chunk:
   ```cpp
   if (!validity.RowIsValid(src_idx)) {
       col_buf.indicators[row_idx] = -1;
   } else {
       col_buf.indicators[row_idx] = 0;
   }
   ```
5. **Execution**: `OCIStmtExecute` executes `iters = current_batch_rows` with `OCI_DEFAULT` inside a mutex-serialized write lock.

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
