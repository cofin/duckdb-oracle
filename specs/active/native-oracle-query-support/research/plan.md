# Research Plan: Native OracleDB Query Support

## Objective
To perform a comprehensive analysis of the `ocilib` C library to create a detailed and actionable implementation plan for the Oracle DuckDB extension, removing all major research ambiguities.

## Research Summary
The research process involved cloning the `ocilib` GitHub repository and performing a detailed analysis of its public API headers (`ocilib.h`, `ocilibc/api.h`) and source code.

## Key Findings & Answers to Research Questions

### 1. Connection Management
-   **Connection Function**: `OCI_ConnectionCreate(db, user, pwd, mode)` is the primary function. The `db` parameter is a string that can be a full connection string or a TNS alias.
-   **TNS & Autonomous DB**: The library does **not** have specific APIs for TNS or wallet handling. It relies entirely on the underlying Oracle Client configuration. For TNS connections, the `db` parameter should be the TNS alias, and the `TNS_ADMIN` environment variable must point to the directory containing `tnsnames.ora` and `sqlnet.ora`. For Autonomous Database, the same mechanism applies, with the wallet files being located via the configuration in `tnsnames.ora`.
-   **Error Handling**: A robust error API is available (`OCI_GetLastError`, `OCI_ErrorGetString`, `OCI_ErrorGetOCICode`), allowing for detailed error reporting.
-   **Connection Pooling**: `ocilib` supports connection pooling via `OCI_PoolCreate()` and `OCI_PoolGetConnection()`, which should be used to manage connections efficiently.

### 2. Query Execution and Data Fetching
-   **Statement Lifecycle**: `OCI_StatementCreate()`, `OCI_Prepare()` (or `OCI_ExecuteStmt()` to combine), `OCI_Execute()`, `OCI_GetResultset()`, `OCI_FetchNext()`, `OCI_StatementFree()`.
-   **Metadata**: `OCI_GetColumnCount()` and `OCI_GetColumn(rs, index)` provide access to `OCI_Column*` handles, which can be queried for name, type, size, precision, and scale.
-   **Streaming**: The fetch-based interface (`OCI_FetchNext`) is designed for streaming results and avoids buffering the entire result set in memory. For LOBs, `OCI_LobRead2()` allows for chunked reading.

### 3. Data Type Mapping
The `ocilib` API provides a clear, one-to-one mapping for most data types. The implementation will involve a large `switch` statement on the column type returned by `OCI_ColumnGetType()` and `OCI_ColumnGetSubType()`.

| Oracle Type(s) | `ocilib` Get Function | DuckDB `LogicalType` | Notes |
| :--- | :--- | :--- | :--- |
| `CHAR`, `VARCHAR2`, `NCHAR`, `NVARCHAR2` | `OCI_GetString()` | `VARCHAR` | |
| `CLOB`, `NCLOB`, `LONG` | `OCI_GetLob()` -> `OCI_LobRead2()` | `VARCHAR` | Stream the LOB content. |
| `NUMBER(p,s)`, `INTEGER`, `DECIMAL` | `OCI_GetDouble()` / `OCI_GetInt()` / `OCI_GetBigInt()` | `DOUBLE` / `INTEGER` / `BIGINT` / `DECIMAL` | Choose based on precision/scale. `OCI_Number` API can be used for arbitrary precision. |
| `BINARY_FLOAT`, `BINARY_DOUBLE` | `OCI_GetFloat()` / `OCI_GetDouble()` | `FLOAT` / `DOUBLE` | |
| `DATE` | `OCI_GetDate()` | `DATE` | |
| `TIMESTAMP` | `OCI_GetTimestamp()` | `TIMESTAMP` | |
| `TIMESTAMP WITH TIME ZONE` | `OCI_GetTimestamp()` | `TIMESTAMP_TZ` | `ocilib` handles TZ info. |
| `TIMESTAMP WITH LOCAL TIME ZONE` | `OCI_GetTimestamp()` | `TIMESTAMP_TZ` | `ocilib` handles TZ info. |
| `INTERVAL YEAR TO MONTH` | `OCI_GetInterval()` | `INTERVAL` | |
| `INTERVAL DAY TO SECOND` | `OCI_GetInterval()` | `INTERVAL` | |
| `BLOB`, `LONG RAW`, `RAW` | `OCI_GetLob()` -> `OCI_LobRead2()` / `OCI_GetRaw()` | `BLOB` | |
| `ROWID`, `UROWID` | `OCI_GetString()` | `VARCHAR` | Treated as opaque strings. |

### 4. Build and Integration
-   **Headers**: The primary header to include is `ocilib.h`.
-   **Linking**: The `CMakeLists.txt` will need to link against the `ocilib` library found by `vcpkg`.

## Final Decision
The research is complete. `ocilib` is a suitable and well-featured library for this task. The implementation plan is validated, and the previous risks related to research have been fully mitigated. The project can proceed to the implementation phase with a high degree of confidence.
