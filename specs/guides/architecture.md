# Architecture Guide: duckdb-oracle

**Last Updated**: 2025-11-22

## Overview

This project is a C++ extension for DuckDB. It follows the standard architectural pattern for DuckDB extensions, leveraging DuckDB's internal APIs to add custom functionality. It specifically uses the **Oracle Call Interface (OCI)** to interact with Oracle databases.

## Project Structure

```
/usr/local/google/home/codyfincher/code/utils/duckdb-oracle/
├───src/
│   ├───oracle_extension.cpp  # Main entry point and implementation
│   └───include/
│       └───oracle_extension.hpp
├───test/
│   └───sql/                  # SQL-based test suite
├───vcpkg.json                # Dependency management (includes OpenSSL)
├───CMakeLists.txt            # Build configuration
└───Makefile                  # Build automation
```

## Core Components

### 1. Extension Entry Point (`src/oracle_extension.cpp`)

The `OracleExtension` class implements the `Load` function, which is the hook DuckDB calls. It registers:

- **`oracle_scan`** (Table Function): Scans a specific Oracle table.
- **`oracle_query`** (Table Function): Executes a raw SQL query against Oracle.
- **`oracle_attach_wallet`** (Scalar Function): Configures the environment for Oracle Wallet authentication.

### 2. OCI Resource Management (`OracleBindData`)

The `OracleBindData` struct (in `oracle_extension.cpp`) encapsulates the state required for an Oracle connection using RAII principles. It manages:

- `OCIEnv*`: The OCI environment handle.
- `OCISvcCtx*`: The service context.
- `OCIStmt*`: The statement handle.
- `OCIError*`: The error handle.

The destructor of `OracleBindData` ensures these handles are properly freed (`OCIHandleFree`, `OCILogoff`) to prevent memory leaks.

## Design Patterns

- **DuckDB Extension API**: Utilizes `TableFunction` and `ScalarFunction` to expose C++ logic to SQL.
- **Bind & Execute Model**:
  - **Bind Phase (`OracleBindInternal`)**: Establishes the OCI connection, prepares the statement, and deduces return types (`SQLT_*` -> `LogicalType`).
  - **Execute Phase (`OracleQueryFunction`)**: Executes the statement and fetches results row-by-row using `OCIStmtFetch2`, populating DuckDB's `DataChunk`.
- **RAII**: Used for OCI handles to ensure exception safety.

## Data Flow

1.  **Scanning**: A user executes `SELECT * FROM oracle_scan('connection_string', 'SCHEMA', 'TABLE')`.
    *   DuckDB invokes `OracleScanBind`.
    *   Extension connects via OCI, retrieves table metadata (columns, types).
    *   Extension maps OCI types (including JSON, Vector) to DuckDB types.
    *   DuckDB invokes `OracleQueryFunction` to fetch data row-by-row (or batched) using OCI fetch).

2.  **Querying**: A user executes `SELECT * FROM oracle_query('connection_string', 'SELECT ...')`.
    *   DuckDB invokes `OracleQueryBind`.
    *   Extension prepares the statement.
    *   If it's a SELECT, it describes columns and proceeds like scan.
    *   If it's PL/SQL (`BEGIN...`), it executes immediately and returns a status row.

3.  **Configuration**: User calls `oracle_attach_wallet('/path')`.
    *   Extension sets `TNS_ADMIN` environment variable for the process.

### Data Type Mapping

| Oracle Type (`SQLT`) | DuckDB Type | Notes |
| :--- | :--- | :--- |
| `SQLT_CHR`, `SQLT_AFC`, `SQLT_VCS` | `VARCHAR` | Standard strings |
| `SQLT_NUM`, `SQLT_INT` | `BIGINT`, `DECIMAL`, `DOUBLE` | Depends on precision/scale |
| `SQLT_FLT`, `SQLT_BDOUBLE` | `DOUBLE` | Floating point |
| `SQLT_DAT`, `SQLT_TIMESTAMP` | `TIMESTAMP` | |
| `SQLT_CLOB`, `SQLT_BLOB` | `BLOB` | |
| `SQLT_JSON` | `VARCHAR` | Fetched as string |
