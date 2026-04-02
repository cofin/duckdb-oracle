# Implementation Tasks: Oracle Write Support

**Complexity**: Complex
**Estimated Checkpoints**: 10+

## Phase 1: Planning & Research ✓

- [x] PRD created
- [x] Research documented (OCI Array Binding & Transaction patterns)
- [x] Patterns identified (Adapter, RAII, Push-based Sink)
- [x] Workspace setup
- [x] Deep analysis completed

## Phase 2: Transaction & Connection Infrastructure ✓

- [x] **OracleTransactionManager**: Implement `CommitTransaction` (call `OCITransCommit`).
- [x] **OracleTransactionManager**: Implement `RollbackTransaction` (call `OCITransRollback`).
- [x] **OracleConnection**: Add helper methods for `Prepare`, `Bind`, and `Execute` (with iterations).
- [x] **Unit Test**: Verify transaction control (Commit/Rollback) with simple queries (e.g., `oracle_execute` with manual commit?).

## Phase 3: Core Insert Implementation (Adapted to CopyFunction) ✓

- [x] **OracleWriteState**: Create classes for `GlobalSinkState` and `LocalSinkState` (bind buffers).
- [x] **Bind Buffer Logic**: Implement `BindColumn` logic for C-arrays.
- [x] **OracleCopyFunction**: Implement `OracleWriteBind` to handle `COPY ... TO`.
    - [x] Generate `INSERT /*+ APPEND_VALUES */ INTO ...` SQL for Direct Path Load optimization.
    - [x] Prepare OCI statement.
    - [x] Define bind positions.
- [x] **OracleWrite**: Implement `Sink` function.
    - [x] Map `DataChunk` to bind buffers.
    - [x] Execute `OCIStmtExecute` for the batch.
    - [x] Handle errors and partial failures.
- [x] **OracleWrite**: Implement `Finalize`.

## Phase 4: Type Support & Data Conversion (Partial)

- [x] **Strings**: Support `VARCHAR` -> `SQLT_CHR` (handle null termination/length).
- [x] **Generic Fallback**: Currently mapping most types to string for safety/simplicity in initial version.
- [ ] **Integers**: Support `TINYINT`, `SMALLINT`, `INTEGER`, `BIGINT` -> `SQLT_INT` (Optimization).
- [ ] **Floats**: Support `FLOAT`, `DOUBLE` -> `SQLT_BDOUBLE` (Optimization).
- [ ] **Dates/Timestamps**: Support `DATE`, `TIMESTAMP` -> `SQLT_ODT` or `SQLT_TIMESTAMP` (Optimization).
- [x] **NULL Handling**: Ensure Indicator arrays are correctly populated.

## Phase 5: Advanced Features & Refinement

- [x] **COPY Support**: Verify `COPY ... FROM ...` uses the Insert path efficiently (via `COPY TO FORMAT ORACLE`).
- [ ] **LOB Support**: Handle BLOBs (inline `SQLT_BIN` if small, or error if too large).
- [x] **Performance Tuning**: Using `/*+ APPEND_VALUES */` for Direct Path Load performance.

## Phase 6: Testing & Documentation

- [x] **Unit Tests**: Create `test/sql/test_oracle_write.test`.
- [ ] **Integration Tests**: Run against real Oracle container.
- [ ] **Benchmarks**: Measure rows/sec vs row-by-row (sanity check).
- [ ] **Documentation**: Update `specs/guides/` with write-support patterns.
- [ ] **Review**: Pass quality gate.
