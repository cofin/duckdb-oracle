# PRD: CI Fix & Connection Optimization

## Problem Statement
The current CI pipeline is unstable due to hanging integration tests. The Oracle extension exhibits hanging behavior during connection/logon, likely due to improper OCI threading handling and resource contention. Additionally, data fetching is fully buffered, leading to high memory usage and potential performance bottlenecks for large datasets.

## Goals
1.  **Fix Hanging Issues**: Ensure robust, non-blocking connections, especially under concurrent load.
2.  **Optimize Performance**: Implement streaming fetches (no full buffering) to minimize memory footprint.
3.  **Ensure Thread Safety**: Support multi-threaded read-only queries against DuckDB/Oracle.
4.  **Stabilize CI**: Ensure integration tests pass reliably in GitHub Actions.

## User Stories
- As a developer, I want `make integration` to run reliably without hanging so I can merge code with confidence.
- As a user, I want to query large Oracle tables without running out of memory.
- As a user, I want to run concurrent queries against the Oracle extension without deadlocks or crashes.

## Acceptance Criteria
1.  **No Hangs**: Integration tests complete successfully (pass or fail, but no hang) within a reasonable timeout.
2.  **Streaming Fetch**: `oracle_scan` utilizes OCI array fetching and DuckDB's streaming execution model (returning chunks as they are ready) rather than buffering all rows.
3.  **Thread Safety**: Multiple concurrent `SELECT` queries work correctly.
4.  **CI Green**: The GitHub Actions workflow passes.
5.  **Performance**: Settings for `prefetch_rows`, `prefetch_memory`, and `array_size` are respected and tuned.

## Patterns to Follow
- **OCI Threaded Mode**: Use `OCI_THREADED` for `OCIEnvCreate`.
- **Explicit Session Management**: Use `OCIServerAttach` / `OCISessionBegin` instead of `OCILogon`.
- **DuckDB Table Function**: Implement `bind`, `init_global`, `init_local`, `scan` correctly for parallel/streaming execution.
