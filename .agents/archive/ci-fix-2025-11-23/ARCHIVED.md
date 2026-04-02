# Archive: CI Fix

**Date**: 2025-11-23
**Status**: SUCCESS

## Summary
Fixed CI hangs and optimized Oracle extension performance.

## Key Achievements
1.  **Connection Stability**: Implemented `OracleConnectionManager` with `OCI_THREADED` and connection pooling/timeouts. Eliminated hangs.
2.  **Performance**: Implemented **Array Fetch** (Streaming) using `OCIDefineArrayOfStruct`, reducing round-trips significantly.
3.  **Stability**: Fixed memory corruption ("invalid next size") and internal errors in DuckDB integration.
4.  **Correctness**: Fixed `ORA-01007` (column mismatch) by ensuring `GetScanFunction` relies on `BindInternal` for column names.
5.  **Testing**: Organized integration tests and ensured they pass reliably.

## Artifacts
- `src/oracle_connection_manager.cpp` (New)
- `src/oracle_extension.cpp` (Refactored)
- `test/integration/oracle/` (New test structure)
