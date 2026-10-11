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
| 2. Preparation: OCIStmtPrepare2(svchp, &stmt, errhp, query, len, ..., DEFAULT)|
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
|    - DML batch writes: iters=N, mode=OCI_BATCH_ERRORS                         |
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
| `OCI_DEFAULT` | Standard query execution | Compiles SQL on the server and prepares cursor for fetching. `iters` must be 0 for SELECT. |
| `OCI_DESCRIBE_ONLY` | Metadata inspection | Compiles query and fetches column metadata without executing the query or allocating server cursor state. |
| `OCI_COMMIT_ON_SUCCESS` | Auto-commit | Automatically commits the active transaction when statement execution succeeds without errors. |
| `OCI_BATCH_ERRORS` | Bulk DML error tolerance | In array inserts, continues processing subsequent rows when a row fails with a constraint violation, collecting row-level errors in `OCI_ATTR_DML_ROW_OFFSET`. |

## Column Defining & Indicator Variables

When fetching rows from Oracle, C client buffers must be bound to each projected column using `OCIDefineByPos`.

### Indicator Variables (`sb2`)
Because C types cannot natively represent database `NULL` states, OCI pairs every data buffer with a signed 2-byte indicator variable (`sb2`):

| Indicator Value | Symbol | Meaning | Action in DuckDB |
| :--- | :--- | :--- | :--- |
| `0` | `OCI_IND_NOTNULL` | Valid, non-null value | Value copied into DuckDB vector; validity bit set to true. |
| `-1` | `OCI_IND_NULL` | Value is SQL `NULL` | Value buffer ignored; DuckDB `ValidityMask::SetInvalid` invoked. |
| `-2` | `OCI_IND_BADALLOC` | Data truncated due to memory | Buffer size insufficient; logged or raised as conversion error. |
| `> 0` | N/A | Actual byte length prior to truncation | String/binary exceeded allocated buffer cap; indicates buffer overflow. |

## External Data Types (`SQLT_*`)

Oracle maps its internal database storage types to client-side external types specified during `OCIDefineByPos` and `OCIBindByPos`:

| Oracle Internal Type | External OCI Type | C Representation | Target DuckDB `LogicalType` |
| :--- | :--- | :--- | :--- |
| `NUMBER(p, 0)` (p <= 4) | `SQLT_INT` | `int16_t` | `LogicalType::SMALLINT` |
| `NUMBER(p, 0)` (p <= 9) | `SQLT_INT` | `int32_t` | `LogicalType::INTEGER` |
| `NUMBER(p, 0)` (p <= 18)| `SQLT_INT` | `int64_t` | `LogicalType::BIGINT` |
| `NUMBER(p, s)` (s > 0)  | `SQLT_STR` / `SQLT_NUM` | `char[]` / `OCINumber` | `LogicalType::DECIMAL(p, s)` |
| `NUMBER` (unconstrained)| `SQLT_FLT` / `SQLT_STR` | `double` | `LogicalType::DOUBLE` |
| `FLOAT`, `BINARY_DOUBLE`| `SQLT_BDOUBLE` | `double` | `LogicalType::DOUBLE` |
| `BINARY_FLOAT` | `SQLT_BFLOAT` | `float` | `LogicalType::FLOAT` |
| `VARCHAR2`, `CHAR`, `CLOB` | `SQLT_STR` | Null-terminated string | `LogicalType::VARCHAR` |
| `RAW`, `BLOB` | `SQLT_BIN` | Byte array | `LogicalType::BLOB` |
| `DATE` | `SQLT_DAT` (7 bytes) | Century, year, month, day, hour, min, sec | `LogicalType::DATE` / `TIMESTAMP` |
| `TIMESTAMP` | `SQLT_TIMESTAMP` | `OCIDateTime*` descriptor | `LogicalType::TIMESTAMP` |
| `TIMESTAMP WITH TIME ZONE` | `SQLT_TIMESTAMP_TZ` | `OCIDateTime*` descriptor | `LogicalType::TIMESTAMP_TZ` |
| `SDO_GEOMETRY` | `SQLT_STR` / `SQLT_NTY` | WKT via `SDO_UTIL.TO_WKTGEOMETRY` or ADT | `LogicalType::GEOMETRY` (Spatial) |
| `VECTOR` (Oracle 23ai) | `SQLT_STR` / Binary Array | Float vector array | `LogicalType::LIST(LogicalType::FLOAT)` |

## Error Diagnostics (`OCIErrorGet`)

Every OCI API returns a signed word (`sword`):
- `OCI_SUCCESS` (`0`): Operation succeeded.
- `OCI_SUCCESS_WITH_INFO` (`1`): Succeeded with warning (e.g., column truncation or server message).
- `OCI_NO_DATA` (`100`): End of cursor reached during fetch.
- `OCI_ERROR` (`-1`): Operation failed; error text and code available in `OCIError`.
- `OCI_INVALID_HANDLE` (`-2`): Handle pointer is null or corrupted.

Diagnostic retrieval pattern implemented in `CheckOCIError` (`src/include/oracle_utils.hpp`):
```cpp
inline void CheckOCIError(sword status, OCIError *errhp, const std::string &msg) {
    if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO) {
        return;
    }
    text errbuf[512];
    sb4 errcode = 0;
    if (errhp) {
        OCIErrorGet(reinterpret_cast<dvoid *>(errhp), 1, nullptr, &errcode,
                    errbuf, sizeof(errbuf), OCI_HTYPE_ERROR);
        throw IOException(msg + ": " + std::string(reinterpret_cast<char *>(errbuf)));
    }
    throw IOException(msg + ": (No Error Handle)");
}
```
