---
type: Pattern
title: "DuckDB Write Pipeline: Sink, Combine & Finalize"
description: Implementation of DuckDB's PhysicalOperator Sink pattern for high-speed writes into Oracle tables via PhysicalOracleInsert.
scope: pattern
domain: duckdb
status: stable
tags: [duckdb, write-pipeline, sink-pattern, physical-operator, array-insert]
generated:
  by: flow/0.27.0
  at: 2026-11-07T17:36:00Z
verified:
  - by: agent:bb-learn
    at: 2026-11-15T18:18:00Z
    commit: 499ea2c
sources:
  - id: plan-insert
    resource: src/storage/oracle_schema_entry.cpp::OracleCatalog::PlanInsert
    title: PhysicalOracleInsert planning with resolved schema and table names
  - id: phys-insert
    resource: src/storage/oracle_insert.cpp::PhysicalOracleInsert
    title: Sink, Combine, Finalize, and GetDataInternal physical operator methods
  - id: oracle-write
    resource: src/storage/oracle_write.cpp::OracleWriteGlobalState::Sink
    title: UnifiedVectorFormat transposition, decimal text float binding, OCIDate binding, and rollback safety
---

# DuckDB Write Pipeline: Sink, Combine & Finalize

<!-- truth: start -->
- Route `INSERT INTO` through `OracleCatalog::PlanInsert` (`src/storage/oracle_schema_entry.cpp`) into `PhysicalOracleInsert` (`src/storage/oracle_insert.cpp`), passing the resolved `oracle_table.GetSchemaName()` and `oracle_table.GetTableName()` so cross-schema and synonym targets resolve accurately[^plan-insert][^phys-insert].
- Normalize input columns in `OracleWriteGlobalState::Sink` (`src/storage/oracle_write.cpp`) via `col_vec.ToUnifiedFormat(vdata)`, bind `FLOAT`/`DOUBLE` writes to `NUMBER`/`FLOAT` as `SQLT_CHR` shortest decimal text via `duckdb_fmt::format` (`SQLT_BDOUBLE` for `BINARY_FLOAT`/`BINARY_DOUBLE`), bind `DATE` via 7-byte `OCIDate` (`SQLT_ODT`), and call `chunk.SetChildCardinality(1)` in `PhysicalOracleInsert::GetDataInternal`[^phys-insert][^oracle-write].
- Roll back uncommitted batches via `OCITransRollback` if any `Sink` batch fails prior to `OracleWriteFinalize` (`OCITransCommit`) so partial writes never leak into pooled connections[^oracle-write].
<!-- truth: end -->

DuckDB executes `INSERT INTO` queries using a physical pipeline sink pattern. When an Oracle table is the target of an insert, `OracleCatalog::PlanInsert` (`src/storage/oracle_schema_entry.cpp`) returns an instance of `PhysicalOracleInsert` (`src/storage/oracle_insert.cpp`), which implements DuckDB's `PhysicalOperator` sink interface[^plan-insert][^phys-insert].

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

In `OracleWriteGlobalState::Sink` (`src/storage/oracle_write.cpp`), DuckDB's column vectors are normalized via `UnifiedVectorFormat` (`chunk.data[col_idx].ToUnifiedFormat(unified_formats[col_idx])`) and transposed into OCI array bind buffers without per-row `Value` allocation on hot paths[^oracle-write]:

1. **Numeric Vectors**: Fixed-width integers targeting `BIGINT` (`TINYINT`, `SMALLINT`, `INTEGER`, `BIGINT`, `UTINYINT`, `USMALLINT`, `UINTEGER`) read via `UnifiedVectorFormat::GetData<T>(vdata)[idx]` into `int64_t` (`SQLT_INT`). Floating-point (`FLOAT`, `DOUBLE`) values bind via `SQLT_CHR` shortest round-trip decimal text (`duckdb_fmt::format`, rejecting `NaN`/`Infinity`) when targeting Oracle `NUMBER` or `FLOAT`, and retain binary `double` (`SQLT_BDOUBLE`) binding for `BINARY_FLOAT`/`BINARY_DOUBLE`.
2. **Date & Timestamp Vectors**: Oracle `DATE` columns convert `date_t` or `timestamp_t` values directly into 7-byte `OCIDate` structs bound with `SQLT_ODT`, while `TIMESTAMP`, `TIMESTAMP WITH TIME ZONE`, and `TIMESTAMP WITH LOCAL TIME ZONE` columns format canonical strings wrapped in `TO_TIMESTAMP(:b, 'YYYY-MM-DD HH24:MI:SS.FF')` under session `TIME_ZONE = '+00:00'`[^oracle-write].
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
5. **Execution & Source Cardinality**: `OCIStmtExecute` executes `iters = batch_rows` with `OCI_DEFAULT` inside a mutex-serialized write lock, and `PhysicalOracleInsert::GetDataInternal` sets `chunk.SetChildCardinality(1)` when returning the inserted row count[^phys-insert][^oracle-write].

## Finalization & Statement-Atomic Transaction Commit

`PhysicalOracleInsert::Finalize` commits the active transaction on the service context[^phys-insert]:
```cpp
SinkFinalizeType PhysicalOracleInsert::Finalize(Pipeline &pipeline, Event &event, ClientContext &context,
                                               OperatorSinkFinalizeInput &input) const {
    auto &gstate = input.global_state.Cast<OracleInsertGlobalState>();
    OracleWriteFinalize(*gstate.write_state); // Issues OCITransCommit
    return SinkFinalizeType::READY;
}
```
If any sink operation throws an exception before `Finalize` completes, `OCITransRollback` is explicitly invoked on the active service context so uncommitted partial batches never leak into pooled connections[^oracle-write].

[^plan-insert]: `src/storage/oracle_schema_entry.cpp::OracleCatalog::PlanInsert` — `PhysicalOracleInsert` planning with resolved schema and table names.
[^phys-insert]: `src/storage/oracle_insert.cpp::PhysicalOracleInsert` — `Sink`, `Combine`, `Finalize`, and `GetDataInternal` physical operator methods.
[^oracle-write]: `src/storage/oracle_write.cpp::OracleWriteBatch` — `UnifiedVectorFormat` transposition, `OCIDate` binding, and rollback safety.
