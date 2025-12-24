# Spec: Enhance stability and robustness of Oracle Write support (INSERT/COPY)

## Overview
The goal of this track is to move the Oracle write support (INSERT and COPY) from a functional prototype to a production-ready, highly stable component. This includes robust error handling for batch OCI operations, handling of edge cases in data types, and ensuring performance and stability during large-scale data migrations.

## Goals
- **Robust Error Handling:** Capture and report specific OCI errors during batch binds and execution, ensuring no silent failures or crashes.
- **Type Completeness:** Ensure all supported Oracle types are correctly handled during writes, including NULL values and large strings/blobs.
- **Scale Stability:** Verify stability and memory management during large `INSERT INTO ... SELECT ...` operations.
- **High Test Coverage:** Achieve >80% coverage for the `oracle_write.cpp` and related storage classes.

## Technical Details
- **OCI Batch Binds:** Refine the usage of `OCIBindArrayOfStruct` and `OCIStmtExecute` with `iters > 1`.
- **Error Diagnostics:** Use `OCIErrorGet` to extract granular error information when a batch operation fails.
- **Memory Management:** Ensure RAII for all allocated buffers used during the write process.
- **Pushdown Integration:** Ensure that write operations interact correctly with DuckDB's internal data representation (DataChunks).

## Acceptance Criteria
- [ ] No segfaults or crashes during large writes.
- [ ] All OCI errors are reported as actionable DuckDB exceptions.
- [ ] Integration tests pass for standard types, NULLs, and edge cases.
- [ ] Performance is optimized for batch writes (verified via benchmarking).
