# Architecture Guide: duckdb-oracle

**Last Updated**: 2025-11-22

## Overview

This extension adds Oracle connectivity to DuckDB via the **Oracle Call Interface (OCI)**. It uses DuckDB's extension API to expose table functions and a helper scalar function.

## Project Structure

```
duckdb-oracle/
├── src/
│   ├── oracle_extension.cpp        # Main entry point and implementation
│   ├── include/                    # Header files
│   │   ├── oracle_extension.hpp
│   │   ├── oracle_storage_extension.hpp
│   │   └── ...
│   └── storage/                    # Storage extension implementation
│       ├── oracle_storage_extension.cpp
│       ├── oracle_catalog.cpp
│       └── ...
├── test/sql/                       # SQL-based test suite
├── vcpkg.json                      # Dependency management (includes OpenSSL)
├── CMakeLists.txt                  # Build configuration (DuckDB macros + OCI detection)
└── Makefile                        # Build/test automation
```

## Core Components

### Extension Entry Point (`src/oracle_extension.cpp`)

`OracleExtension::Load` registers four functions:

- `oracle_scan` (Table Function): runs `SELECT * FROM <schema>.<table>` against Oracle; supports filter/projection pushdown when `oracle_enable_pushdown` is true.
- `oracle_query` (Table Function): runs an arbitrary SQL query against Oracle with optional pushdown for simple filters/projection.
- `oracle_attach_wallet` (Scalar Function): sets `TNS_ADMIN` for wallet-based authentication.
- `oracle_clear_cache` (Scalar Function): clears cached Oracle metadata/connection state held by attached databases.

### Storage Extension (`src/storage/`)

The storage extension allows attaching Oracle databases directly using `ATTACH '...' AS oracle`.

- `OracleStorageExtension` (`src/storage/oracle_storage_extension.cpp`): Handles the `ATTACH` command and creates the catalog.
- `OracleCatalog` (`src/storage/oracle_catalog.cpp`): Implements the DuckDB catalog interface for Oracle.
- `OracleTransactionManager`: Manages transactions (currently read-only auto-commit).

### OCI Resource Management

**OracleConnectionManager** (`src/oracle_connection_manager.cpp`) is a singleton that manages:
- The global `OCIEnv` handle (created with `OCI_THREADED`).
- A pool of `OracleContext` objects, keyed by connection string.
- Explicit `OCIServerAttach` and `OCISessionBegin` for robust connection handling.
- Connection timeouts (default 30s) to prevent hangs.

`OracleBindData` holds a `std::shared_ptr<OracleConnectionHandle>`, which keeps a reference to an `OracleContext`. When the handle is destroyed, the context is returned to the pool (if caching is enabled).

### Bind & Execute Model

- **Bind (`OracleBindInternal`)**
  - Acquires a connection from the manager.
  - Allocates and prepares an `OCIStmt` handle.
  - Executes with `OCI_DESCRIBE_ONLY` to infer metadata.
- **Init (`OracleInitGlobal`)**
  - Re-prepares the statement if the query changed (e.g., due to pushdown).
  - Allocates persistent buffers for **Array Fetch** (`OCIDefineArrayOfStruct`).
  - Defines columns using `OCIDefineByPos`.
- **Execute (`OracleQueryFunction`)**
  - Executes the statement (`OCIStmtExecute`) once.
  - Fetches rows in batches (`OCIStmtFetch2` with `OCI_FETCH_NEXT`).
  - Reads data using stride-based access from the array buffers.
  - Converts OCI types -> DuckDB vectors.

### Error Handling

`CheckOCIError` wraps OCI status codes and raises `IOException` with the server error text when a call fails.

## Data Flow

1. `oracle_scan(conn, schema, table)` → binds connection and constructed `SELECT *` → fetches rows.
2. `oracle_query(conn, sql)` → binds connection and provided SQL → fetches rows (or executes PL/SQL).
3. `oracle_attach_wallet(path)` → sets `TNS_ADMIN=path` for subsequent OCI connections in the process.

## Type Mapping (current implementation)

| Oracle `SQLT`        | DuckDB Type | Notes |
|----------------------|-------------|-------|
| `SQLT_CHR`, `SQLT_AFC`, `SQLT_VCS`, `SQLT_AVC` | `VARCHAR` | |
| `SQLT_NUM`, `SQLT_VNU` | `BIGINT` if scale 0 and precision ≤18, else `DOUBLE` | Precision >18 coerces to `DOUBLE` for portability |
| `SQLT_INT`, `SQLT_UIN` | `BIGINT` | |
| `SQLT_FLT`, `SQLT_BFLOAT`, `SQLT_BDOUBLE`, `SQLT_IBFLOAT`, `SQLT_IBDOUBLE` | `DOUBLE` | |
| `SQLT_DAT`, `SQLT_ODT`, `SQLT_TIMESTAMP`, `SQLT_TIMESTAMP_TZ`, `SQLT_TIMESTAMP_LTZ` | `TIMESTAMP` (fetched as string today) | |
| `SQLT_CLOB`, `SQLT_BLOB`, `SQLT_BIN`, `SQLT_LBI`, `SQLT_LNG`, `SQLT_LVC` | `BLOB` | |
| `SQLT_JSON`, `SQLT_VEC` | `VARCHAR` | |
| default/unknown       | `VARCHAR` | fallback |

## Build & Linking Notes

- `CMakeLists.txt` requires CMake ≥ 3.10, links OpenSSL (from vcpkg) and OCI (from `ORACLE_HOME`).
- OCI library and include paths are discovered via `ORACLE_HOME` with a fallback to `/usr/share/oracle/instantclient_23_26`; builds fail fast if not found.
- Both static and loadable DuckDB extension targets are produced via `build_static_extension` and `build_loadable_extension`.