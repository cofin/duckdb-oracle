# OCI Type Conversion, Indicators & Array Batching

## 1. Fetch Buffer Sizing & Truncation Checks (`src/oracle_scan.cpp`, `src/oracle_type_conversion.cpp`)

- **Buffer Capacity**: Regular `VARCHAR2` / `CHAR` columns allocate `std::max<ub4>(col_size * 4, 64)` capped at `65535` bytes (`NumericLimits<ub2>::Maximum()`). Large or rewritten categories (`CLOB`, `NCLOB`, `BLOB`, `JSON`, `XMLTYPE`, `SDO_GEOMETRY`, `VECTOR`) allocate the full `65535`-byte `ub2` buffer (`IsLargeOracleCategory`).
- **Indicator Inspection Order**:
  1. After `OCIStmtFetch2`, scan `indicators[col][row]` for `ind == -2 || ind > 0` across all fetched rows and columns and throw a column-specific `IOException` immediately.
  2. If `status == OCI_SUCCESS_WITH_INFO`, only throw when `OCIErrorCodeMatches(errhp, 1406)` (`ORA-01406`) so benign `ORA-24347` warnings are ignored.
- **Zero-Copy `OracleSlice` Conversion**: Pass `OracleSlice {ptr, actual_len}` into `SetOracleOutputValue` (`src/oracle_type_conversion.cpp`) to parse numerics, dates, timestamps (`Timestamp::TryConvertTimestamp(..., true)`), WKT geometries, and `VECTOR` arrays directly into `FlatVector::GetDataMutable<T>`.

## 2. Bulk Array Write Bindings (`src/storage/oracle_write.cpp`)

- Normalize input vectors once per column via `col_vec.ToUnifiedFormat(count, vdata)`.
- Bind integers as `int64_t` (`SQLT_INT`), floating-point values as `double` (`SQLT_BDOUBLE`), Oracle `DATE` values as 7-byte `OCIDate` structs (`SQLT_ODT`), binary values as `SQLT_BIN`, and strings/timestamps/spatial/vectors as `SQLT_STR` with SQL wrapper expressions (`TO_TIMESTAMP`, `TO_TIMESTAMP_TZ`, `SDO_GEOMETRY(:b, srid)`, `TO_VECTOR(:b)`).
- Validate every string/binary element length against `NumericLimits<ub2>::Maximum()` (`65535` bytes) before copying into the contiguous stride buffer.
