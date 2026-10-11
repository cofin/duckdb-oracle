---
type: Domain
title: Oracle OCI Domain Concepts & Execution Lifecycle
description: Core OCI concepts, statement execution lifecycle, parameter binding, indicator variables, SQLT external data types, and error handling.
scope: domain
domain: oracle
tags: [oracle, oci, sqlt-types, indicators, statement-lifecycle]
status: stable
---

# Oracle OCI Domain Concepts & Execution Lifecycle

The Oracle Call Interface is a procedural C interface for executing SQL, PL/SQL, and object operations against Oracle databases.

## Statement Execution Lifecycle

Statement processing in OCI follows five distinct phases:

```
+-------------------------------------------------------------------------------+
| 1. Statement Allocation: OCIHandleAlloc(envhp, &stmt, OCI_HTYPE_STMT, 0, 0)   |
+-------------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------------+
| 2. Preparation: OCIStmtPrepare(stmt, errhp, query, len, OCI_NTV_SYNTAX, 0)    |
+-------------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------------+
| 3. Parameter Binding (DML/Queries with parameters):                           |
|    OCIBindByPos / OCIBindByName / OCIBindArrayOfStruct                        |
+-------------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------------+
| 4. Execution & Describe:                                                      |
|    - Queries: OCIStmtExecute(..., iters=0, mode=OCI_DEFAULT)                  |
|    - Schema discovery: iters=0, mode=OCI_DESCRIBE_ONLY                        |
|    - DML batch writes: iters=N, mode=OCI_DEFAULT                              |
+-------------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------------+
| 5. Output Defining & Fetching:                                                |
|    - Column define: OCIDefineByPos(stmt, &def, errhp, pos, buf, len, SQLT_...) |
|    - Iterative fetch: OCIStmtFetch2(stmt, errhp, nrows, OCI_FETCH_NEXT, 0)   |
+-------------------------------------------------------------------------------+
```

### Execution Modes

| Mode Flag | Purpose | Behavior |
| :--- | :--- | :--- |
| `OCI_DEFAULT` | Standard execution | Compiles SQL on the server and prepares cursor for fetching (`iters=0` for `SELECT`, `iters=N` for array DML within an explicit transaction). |
| `OCI_DESCRIBE_ONLY` | Metadata inspection | Compiles query and fetches column metadata without executing the query or allocating server cursor state. |
| `OCI_COMMIT_ON_SUCCESS` | Auto-commit | Automatically commits the active transaction when statement execution succeeds without errors (used by `oracle_execute`). |

## Column Defining & Indicator Variables

When fetching rows from Oracle, C client buffers must be bound to each projected column using `OCIDefineByPos`.

### Indicator Variables (`sb2`)
Because C types cannot natively represent database `NULL` states, OCI pairs every data buffer with a signed 2-byte indicator variable (`sb2`):

| Indicator Value | Symbol | Meaning | Action in DuckDB |
| :--- | :--- | :--- | :--- |
| `0` | `OCI_IND_NOTNULL` | Valid, non-null value | Value copied into DuckDB vector; validity bit set to true. |
| `-1` | `OCI_IND_NULL` | Value is SQL `NULL` | Value buffer ignored; DuckDB `ValidityMask::SetInvalid` invoked. |
| `-2` | `OCI_IND_BADALLOC` | Data truncated (length exceeds `sb2`) | Raises `IOException` reporting column truncation before `OCI_SUCCESS_WITH_INFO` checks. |
| `> 0` | N/A | Actual byte length prior to truncation | Raises `IOException` reporting original column length vs buffer cap. |

## External Data Types (`SQLT_*`)

Oracle maps its internal database storage types to client-side external types specified during `OCIDefineByPos` and `OCIBindByPos`:

| Oracle Internal Type | External OCI Type (Fetch / Bind) | C Representation | Target DuckDB `LogicalType` |
| :--- | :--- | :--- | :--- |
| `NUMBER(p, 0)` (p <= 4) | `SQLT_STR` (fetch) / `SQLT_INT` (bind) | `char[]` / `int64_t` | `LogicalType::SMALLINT` |
| `NUMBER(p, 0)` (p <= 9) | `SQLT_STR` (fetch) / `SQLT_INT` (bind) | `char[]` / `int64_t` | `LogicalType::INTEGER` |
| `NUMBER(p, 0)` (p <= 18)| `SQLT_STR` (fetch) / `SQLT_INT` (bind) | `char[]` / `int64_t` | `LogicalType::BIGINT` |
| `NUMBER(p, s)` (s > 0)  | `SQLT_STR` | `char[]` | `LogicalType::DECIMAL(p, s)` |
| `NUMBER` (unconstrained)| `SQLT_STR` (fetch) / `SQLT_BDOUBLE` (bind) | `char[]` / `double` | `LogicalType::DOUBLE` |
| `FLOAT`, `BINARY_DOUBLE`| `SQLT_STR` (fetch) / `SQLT_BDOUBLE` (bind) | `char[]` / `double` | `LogicalType::DOUBLE` |
| `BINARY_FLOAT` | `SQLT_STR` (fetch) / `SQLT_BDOUBLE` (bind) | `char[]` / `double` | `LogicalType::FLOAT` |
| `VARCHAR2`, `CHAR`, `CLOB`, `NCLOB` | `SQLT_STR` (up to `65535` bytes) | String buffer + `rlenp` | `LogicalType::VARCHAR` |
| `RAW`, `LONG RAW`, `BLOB` | `SQLT_BIN` (fetch, up to `65535` bytes) | Raw byte array + `rlenp` | `LogicalType::BLOB` |
| `DATE` | `SQLT_STR` (fetch) / `SQLT_ODT` (bind) | Formatted string / `OCIDate` struct | `LogicalType::TIMESTAMP` |
| `TIMESTAMP` | `SQLT_STR` | Formatted string (`NLS_TIMESTAMP_FORMAT`) | `LogicalType::TIMESTAMP` |
| `TIMESTAMP WITH [LOCAL] TIME ZONE` | `SQLT_STR` | Formatted string (`NLS_TIMESTAMP_TZ_FORMAT`) | `LogicalType::TIMESTAMP_TZ` |
| `SDO_GEOMETRY` | `SQLT_STR` (`65535` bytes) | WKT via `SDO_UTIL.TO_WKTGEOMETRY` (`CLOB`) | `LogicalType::GEOMETRY` (or `VARCHAR`) |
| `VECTOR` (Oracle 23ai) | `SQLT_STR` (`65535` bytes) | `VECTOR_SERIALIZE(... RETURNING CLOB)` | `LogicalType::ARRAY` / `LogicalType::LIST` |

## Error Diagnostics (`OCIErrorGet`)

Every OCI API returns a signed word (`sword`):
- `OCI_SUCCESS` (`0`): Operation succeeded.
- `OCI_SUCCESS_WITH_INFO` (`1`): Succeeded with warning (e.g., `ORA-24347` NULL warning in aggregate or `ORA-01406` truncation).
- `OCI_NO_DATA` (`100`): End of cursor reached during fetch.
- `OCI_ERROR` (`-1`): Operation failed; error text and code available in `OCIError`.
- `OCI_INVALID_HANDLE` (`-2`): Handle pointer is null or corrupted.

Diagnostic retrieval pattern implemented in `CheckOCIError` (`src/include/oracle_utils.hpp`):
```cpp
inline void CheckOCIError(sword status, OCIError *errhp, const std::string &msg) {
    if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO) {
        return;
    }
    if (status == OCI_INVALID_HANDLE || !errhp) {
        throw IOException("%s: Invalid OCI handle (status %d)", msg, static_cast<int>(status));
    }
    text errbuf[512] = {0};
    sb4 errcode = 0;
    sword get_rc = OCIErrorGet(reinterpret_cast<dvoid *>(errhp), 1, nullptr, &errcode,
                               errbuf, sizeof(errbuf), OCI_HTYPE_ERROR);
    if (get_rc != OCI_SUCCESS) {
        throw IOException("%s: OCI failure (status %d, OCIErrorGet returned %d)", msg,
                          static_cast<int>(status), static_cast<int>(get_rc));
    }
    throw IOException("%s: %s (Error code: %d)", msg, std::string(reinterpret_cast<char *>(errbuf)), errcode);
}
```
