# Feature: Native Oracle Query Support

**Created**: 2025-11-21
**Status**: Implemented

## Overview

This feature aims to provide a robust and "native-feeling" integration between DuckDB and Oracle databases. It aligns with the user experience of existing DuckDB extensions (like Postgres and MySQL) by offering both table scanning and arbitrary query execution. It ensures maximum compatibility with Oracle versions 11g through 23c, supports extensive data types including modern JSON and Vectors, and enables connectivity to cloud-native services like Oracle Autonomous Database.

## Problem Statement

The current implementation is minimal. To meet user expectations for a "native" extension, it needs:
1.  **Familiar API**: Functions like `oracle_scan` and `oracle_query` that mirror `postgres_scan`/`mysql_query`.
2.  **Modern Type Support**: Handling of `JSON` (native or stored as text) and high-precision numbers.
3.  **Cloud Connectivity**: Support for Oracle Wallets required by Autonomous Database.
4.  **PL/SQL Execution**: Capability to run procedural code.

## Goals

**Primary**:
1.  **Native Experience**: Implement `oracle_scan(conn_str, schema, table)` and `oracle_query(conn_str, query)`.
2.  **Maximum Compatibility**: Support Oracle 11g-23c.
3.  **Extensive Data Type Support**: Map standard types plus `JSON`, `VECTOR` (23ai), and `XMLType`.
4.  **Autonomous DB Support**: Enable connections via Oracle Wallet.

**Secondary**:
1.  **Performance**: Optimize fetching with OCI arrays (partially implemented).
2.  **Usability**: Helper functions for configuration (e.g., setting wallet path).

## Acceptance Criteria

- [x] **API**:
    - `oracle_scan('connection_string', 'schema', 'table')` returns table content.
    - `oracle_query('connection_string', 'sql_query')` executes SQL and returns results.
- [x] **Data Types**:
    - `NUMBER` -> `BIGINT`, `DECIMAL`, or `DOUBLE`.
    - `DATE`, `TIMESTAMP(TZ)` -> `TIMESTAMP`.
    - `CLOB`, `BLOB` -> `VARCHAR`, `BLOB`.
    - `JSON` (OSON or Text) -> `VARCHAR` (DuckDB compatible).
    - `VECTOR` -> `VARCHAR` (String representation `[1,2,3]`).
- [x] **Connectivity**:
    - Connects to Autonomous DB using a Wallet (via `TNS_ADMIN`).
- [x] **PL/SQL**: Executes anonymous blocks.

## Technical Design

### Interface Signatures

```cpp
// Scans a specific table
// usage: SELECT * FROM oracle_scan('user/pass@//host:port/service', 'HR', 'EMPLOYEES');
void OracleScan(ClientContext &context, TableFunctionInput &data, DataChunk &output);

// Executes arbitrary SQL
// usage: SELECT * FROM oracle_query('user/pass@//host:port/service', 'SELECT * FROM dual');
void OracleQuery(ClientContext &context, TableFunctionInput &data, DataChunk &output);
```

### Prerequisites

- **Oracle Instant Client SDK**: Due to licensing restrictions, the Oracle OCI driver cannot be distributed via `vcpkg`.
    - Users must download and install the **Basic** (or Basic Lite) and **SDK** packages from Oracle.
    - The build system (CMake) will require `ORACLE_HOME` or an include/lib path to be set.

### Data Type Mapping Strategy

We will use `OCIAttrGet` to retrieve `OCI_ATTR_DATA_TYPE`.

| Oracle Type | OCI Constant | DuckDB Type | Notes |
| :--- | :--- | :--- | :--- |
| **JSON** | `SQLT_JSON` (119) | `VARCHAR` | Returned as text JSON. |
| **VECTOR** | `SQLT_VEC` (127) | `VARCHAR` | Returned as string `[x,y,z]`. |
| **Numeric** | `SQLT_NUM` | `DECIMAL`/`DOUBLE` | Check precision/scale. |
| **String** | `SQLT_CHR` | `VARCHAR` | |
| **Date/Time** | `SQLT_TIMESTAMP_TZ` | `TIMESTAMP` | |
| **LOBs** | `SQLT_CLOB`, `SQLT_BLOB` | `VARCHAR`/`BLOB` | |

### Connectivity & Wallet Support

Oracle Autonomous Database requires a Wallet (cwallet.sso). OCI uses the `TNS_ADMIN` environment variable or the `MY_WALLET_DIRECTORY` parameter in `sqlnet.ora` to locate it.
- **Approach**: We will respect the `TNS_ADMIN` environment variable if set.
- **Helper**: We can add a scalar function `oracle_attach_wallet('path/to/wallet_dir')` which sets the internal environment or OCI context parameter for the session.

### PL/SQL Execution

For `oracle_query`, if the statement is identified as PL/SQL (starts with `BEGIN`, `DECLARE`, `CALL`):
1.  Execute with `iters=1`.
2.  Since PL/SQL blocks don't return rowsets directly, return a single-row result: `{"status": "Success", "rows_affected": 0}`.
3.  (Advanced) Support `dbms_output` retrieval if requested.

## Testing Strategy

- **Manual Verification**: Since we lack a CI Oracle instance, we will provide a `test/sql/manual_verification.md` guide.
- **Integration Test**: `scripts/test_integration.sh` spins up a Docker container (Oracle 23ai Free), builds the extension, and runs `unittest` against it.
- **JSON/Vector Test**: Covered in integration tests.
- **Wallet Test**: Verified logic via scalar function (requires actual wallet for full test).

## Risks & Constraints

- **Dependency Management**: `vcpkg` cannot be used for the Oracle OCI driver. We rely on CMake to find a system-installed SDK. This complicates the "just `make`" experience for users without the SDK.
- **OCI Version**: JSON type `SQLT_JSON` is only in newer OCI SDKs. We use `#ifdef` to ensure compilation.
- **Wallet Paths**: File system access for wallets might be restricted in some sandboxed environments.