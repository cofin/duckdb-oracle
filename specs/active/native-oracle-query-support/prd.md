# Feature: Native Oracle Query Support

**Created**: 2025-11-21
**Status**: Planning

## Overview

This feature aims to provide a robust and "native-feeling" integration between DuckDB and Oracle databases. It goes beyond simple query execution by ensuring maximum compatibility with various Oracle versions, supporting an extensive range of data types, and enabling the execution of PL/SQL blocks directly from DuckDB. This will allow users to seamlessly query Oracle data and leverage Oracle's procedural capabilities within their DuckDB workflows.

## Problem Statement

The current implementation of the Oracle extension is limited to basic `VARCHAR`, `DOUBLE`, and `TIMESTAMP` types and simple SQL queries. Users working with complex Oracle schemas often encounter unsupported data types (like `NUMBER` with specific precision, `CLOB`/`BLOB`, `RAW`, `INTERVAL`) or need to execute PL/SQL for data setup or manipulation, which is currently not possible. This limits the extension's utility for enterprise-grade applications.

## Goals

**Primary**:

1. **Maximum Compatibility**: Support Oracle Database versions 11g, 12c, 19c, 21c, and 23c.
2. **Extensive Data Type Support**: Map the vast majority of Oracle SQL types to their most appropriate DuckDB equivalents.
3. **PL/SQL Support**: Enable execution of anonymous PL/SQL blocks and stored procedures.

**Secondary**:

1. **Performance**: Optimize data fetching using OCI array interfaces (already partially in place but needs refinement for all types).
2. **Usability**: Provide clear error messages for OCI failures.

## Acceptance Criteria

- [ ] **Data Types**: The extension must correctly read and display:
  - `VARCHAR2`, `CHAR`, `NVARCHAR2`, `NCHAR` -> `VARCHAR`
  - `NUMBER` (integers) -> `BIGINT`
  - `NUMBER` (floating point/decimal) -> `DECIMAL` or `DOUBLE`
  - `FLOAT`, `BINARY_FLOAT`, `BINARY_DOUBLE` -> `DOUBLE`
  - `DATE`, `TIMESTAMP` -> `TIMESTAMP`
  - `TIMESTAMP WITH TIME ZONE` -> `TIMESTAMP WITH TIME ZONE` (or `TIMESTAMP` + Offset handling)
  - `CLOB` -> `VARCHAR`
  - `BLOB`, `RAW`, `LONG RAW` -> `BLOB`
- [ ] **PL/SQL**: Users can execute `BEGIN ... END;` blocks.
- [ ] **Connection**: Connection string handling remains robust.
- [ ] **Versioning**: Verified against at least one recent Oracle version (e.g., 19c or 21c via Docker/CI if possible, or manual verification).

## Technical Design

### Technology Stack

- **Language**: C++
- **Library**: Oracle Call Interface (OCI) - using standard `oci.h`.
- **Framework**: DuckDB Extension API.

### Data Type Mapping Strategy

We will use `OCIAttrGet` to retrieve `OCI_ATTR_DATA_TYPE` and `OCI_ATTR_PRECISION`/`OCI_ATTR_SCALE` to determine the best DuckDB type.

| Oracle Type (OCI Constant) | DuckDB Logical Type | Notes |
| :--- | :--- | :--- |
| `SQLT_CHR`, `SQLT_AFC`, `SQLT_VCS` | `VARCHAR` | Standard string handling. |
| `SQLT_NUM`, `SQLT_VNU` | `DECIMAL(p,s)` or `DOUBLE` | Check precision/scale. If scale=0, use `BIGINT` or `HUGEINT`. |
| `SQLT_INT` | `BIGINT` | Native integer. |
| `SQLT_FLT`, `SQLT_BFLOAT`, `SQLT_BDOUBLE` | `DOUBLE` | Native floating point. |
| `SQLT_DAT`, `SQLT_ODT` | `TIMESTAMP` | Oracle DATE includes time. |
| `SQLT_TIMESTAMP`, `SQLT_TIMESTAMP_IX` | `TIMESTAMP` | High precision timestamp. |
| `SQLT_TIMESTAMP_TZ` | `TIMESTAMP_TZ` | Timezone aware. |
| `SQLT_CLOB` | `VARCHAR` | Fetch as chunks or complete string if fits in memory. |
| `SQLT_BLOB`, `SQLT_BIN`, `SQLT_LBI` | `BLOB` | Binary data. |
| `SQLT_RNUM` | `VARCHAR` | RowID is best represented as a string. |

### PL/SQL Execution

For PL/SQL, the `OCIStmtPrepare` and `OCIStmtExecute` flow is similar, but:

1. We need to detect if the query is a PL/SQL block (starts with `BEGIN` or `DECLARE`) or a standard `SELECT`.
2. If it's PL/SQL, `OCIStmtExecute` is called with `iters=1`.
3. We might not get a result set. The `oracle_query` function is a table function, so it *must* return a schema.
    - **Strategy**: For PL/SQL execution that doesn't return a cursor, we can return a single row with a status message or "Success".
    - **Advanced**: Support `REF CURSOR` as an OUT parameter? For now, we focus on *execution* (side effects). A `oracle_execute` scalar function might be more appropriate for non-returning blocks, but `oracle_query` can handle it by returning `NULL` or a status.

### Architecture Changes

1. **Refined Bind Phase**: `OracleQueryBind` needs to be more sophisticated in type detection. It interacts with the OCI Describe phase (`OCIStmtExecute` with `OCI_DESCRIBE_ONLY` mode is often used, or `OCIStmtExecute` with iterators=0 then `OCIAttrGet`).
2. **Robust Fetch Loop**: `OracleQueryFunction` needs to handle different `OCIDefineByPos` calls based on the detected type.
    - For `CLOB`/`BLOB`, we might need `OCILobLocator`.

## Testing Strategy

### SQL Tests

- **Basic Queries**: `SELECT * FROM oracle_query(..., 'SELECT 1 FROM DUAL')`
- **Data Types**: Create a table in Oracle (setup script) with all types, then query it.
  - Since we don't have a live Oracle instance in the CI, we rely on unit tests mocking OCI or manual verification. *Constraint*: We might need to assume a mock or just robust code structure.
- **PL/SQL**: `SELECT * FROM oracle_query(..., 'BEGIN NULL; END;')`.

### Edge Cases

- Null values for every supported type.
- Empty CLOBs/BLOBs.
- Oracle errors (e.g., invalid table) -> Should throw meaningful DuckDB exception.

## Risks & Constraints

- **Testing**: Lack of a running Oracle instance in the environment limits automated testing. We must write code carefully and perhaps stub OCI if possible (complex) or rely on the user to test.
- **Complexity**: OCI is verbose and manual memory management is error-prone. We must use RAII wrappers (like `OracleBindData` destructor) rigorously.
