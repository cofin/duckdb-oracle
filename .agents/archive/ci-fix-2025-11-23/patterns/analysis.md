# Pattern Analysis

## Relevant Patterns
- **OCI Array Fetch**: Using `OCIStmtFetch2` with a buffer size > 1 to reduce round-trips.
- **DuckDB Streaming**: Implementing `scan` function to fill `DataChunk` and return, maintaining state in `GlobalTableFunctionState`.
- **RAII for OCI Handles**: Wrapping OCI handles in C++ classes to ensure `OCIHandleFree` is called.
- **Connection Pooling**: Managing a pool of `OCISvcCtx` to avoid expensive logon/logoff operations.

## References
- [DuckDB Extension Guide](https://duckdb.org/docs/extensions/overview)
- Oracle OCI Documentation (via Context7)
