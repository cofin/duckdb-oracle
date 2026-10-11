---
type: Gotcha
title: Oracle OCI Edge Cases, Compatibility & Pitfalls
description: Verified edge cases in Oracle Call Interface development, Instant Client quirks, data type precision traps, and timeout behaviors.
scope: gotcha
domain: oracle
tags: [oracle, oci, gotchas, ora-24315, libaio, data-loss]
status: stable
---

# Oracle OCI Edge Cases, Compatibility & Pitfalls

## 1. ORA-24315 on OCI 23.x Instant Client

When setting call timeouts on server pre-attach or statement handles:
```cpp
OCIAttrSet(stmt.get(), OCI_HTYPE_STMT, &call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, errhp);
```
On Oracle 23.x (23ai) Instant Client, calling `OCIAttrSet` with `OCI_ATTR_CALL_TIMEOUT` can return `ORA-24315: attribute type invalid` depending on the exact connection state.

**Resolution (`src/include/oracle_utils.hpp`)**:
```cpp
inline void CheckOCIOptionalAttributeORA24315(sword status, OCIError *errhp, const std::string &msg) {
    if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO) {
        return;
    }
    // OCI 23.x Instant Client can reject timeout attrs with ORA-24315.
    // Filter this specific compatibility case; every other error remains fatal.
    if (OCIErrorCodeMatches(errhp, 24315)) {
        return;
    }
    CheckOCIError(status, errhp, msg);
}
```

## 2. Linux OS Dependency: `libaio.so.1`

Oracle Instant Client relies on Linux kernel asynchronous I/O (`libaio`).
- On older distributions (Ubuntu 20.04/22.04, Debian 11/12), installing `libaio1` installs `/usr/lib/x86_64-linux-gnu/libaio.so.1`.
- On Ubuntu 24.04 (Noble Numbat) and modern Debian derivatives, package 64-bit time transition renamed the package to `libaio1t64`, providing `/usr/lib/x86_64-linux-gnu/libaio.so.1t64`. Instant Client will fail to load with `error while loading shared libraries: libaio.so.1: cannot open shared object file`.
- **Resolution**: `Makefile` includes `ensure-libaio` macro that detects the library and symlinks `libaio.so.1t64 -> libaio.so.1`.

## 3. 64 KB (`65535`-Byte) Fetch Buffers & Indicator Truncation vs `ORA-24347`

In `src/oracle_scan.cpp` and `src/storage/oracle_table_entry.cpp`:
- Regular `VARCHAR2` / `CHAR` columns size their `SQLT_STR` buffer from `OCI_ATTR_CHAR_SIZE` / `OCI_ATTR_DATA_SIZE` (`ub2` 2-byte attributes), while `CLOB`, `NCLOB`, `BLOB`, `JSON`, `XMLTYPE`, `SDO_UTIL.TO_WKTGEOMETRY(col)`, and `VECTOR_SERIALIZE(col RETURNING CLOB)` allocate the maximum `ub2` define capacity (`65535` bytes via `IsLargeOracleCategory`).
- During `OCIStmtFetch2`, `OracleQueryFunction` inspects `ind == -2 || ind > 0` across all fetched rows and columns *before* checking `status == OCI_SUCCESS_WITH_INFO`, and on `OCI_SUCCESS_WITH_INFO` only raises a generic truncation error if `OCIErrorCodeMatches(errhp, 1406)` (`ORA-01406`). This reports exact column/row truncation diagnostics while ignoring benign `ORA-24347` warnings (NULL column in an aggregate function).

## 4. Unconstrained NUMBER -> DOUBLE Precision Loss

Oracle tables frequently declare numeric columns without precision or scale constraints: `col NUMBER` (or `NUMBER(*, 0)` where `OCI_ATTR_PRECISION == 0` and `OCI_ATTR_SCALE == 0`).
- DuckDB cannot map unconstrained `NUMBER` to fixed-width integer types (`BIGINT`) because Oracle numbers support up to 38 decimal digits.
- Both `OracleTypeRegistry::ResolveMetadata` and `OracleTypeRegistry::ResolveOciDescribe` map unconstrained `NUMBER` and `NUMBER(*, 0)` to `LogicalType::DOUBLE`.
- **Pitfall**: Double-precision floating point (`IEEE 754`) provides 53 bits of mantissa (~15-17 significant decimal digits). Integers or IDs exceeding $2^{53} - 1$ (9,007,199,254,740,991) will suffer silent precision loss. For exact precision, declare columns with explicit scale/precision (e.g. `NUMBER(19, 0)`) or use `TO_CHAR()` on the Oracle side.

## 5. Timezone Handling on TSTZ / LTZ

When fetching `TIMESTAMP WITH TIME ZONE` or `TIMESTAMP WITH LOCAL TIME ZONE` columns as `SQLT_STR`:
- `OracleConnectionManager::CreateConnection` configures `NLS_TIMESTAMP_TZ_FORMAT = 'YYYY-MM-DD HH24:MI:SS.FF TZH:TZM'` and `NLS_NUMERIC_CHARACTERS = '.,'` in `ALTER SESSION SET`.
- `ParseTimestamp` (`src/oracle_type_conversion.cpp`) calls `Timestamp::TryConvertTimestamp(str_val.c_str(), str_val.size(), result, true)` (`use_offset = true`) so timezone offsets are converted into UTC microseconds for DuckDB's `LogicalType::TIMESTAMP_TZ`.

## 6. Configurable Call Timeout (`oracle_call_timeout_ms`)

`oracle_scan.cpp`, `oracle_functions.cpp`, and `oracle_connection_manager.cpp` configure `OCI_ATTR_CALL_TIMEOUT` from `oracle_call_timeout_ms` (default `30000` ms, validated between `0` and `3600000` ms):
- Long-running analytical scans that take more than 30 seconds per round-trip without returning rows will timeout with `ORA-01013: user requested cancel of current operation`.
- Increase `SET oracle_call_timeout_ms = 600000;` (or `0` to disable), tune `oracle_prefetch_rows`, or apply predicate pushdown for heavy queries.

## 7. Per-`ATTACH` Options vs `DBConfig::AddExtensionOption`

In DuckDB `v1.5.x`, `DBConfig::AddExtensionOption` inserts default values directly into `db_config.user_settings`, causing `ClientContext::TryGetCurrentSetting` to return a `SettingScope::GLOBAL` match even when the user never ran `SET oracle_*`.
- **Resolution (`src/oracle_settings.cpp`, `src/oracle_connection_resolver.cpp`)**: `ApplyAttachOptions` records every explicitly provided `ATTACH` option's global setting key in `OracleSettings::attach_overridden_keys`, and `GetOracleSettings` preserves `state->settings` for any key in `attach_overridden_keys` unless `lookup.GetScope() == SettingScope::LOCAL`.

## 8. Oracle Empty String (`''`) NULL Semantics in Filter Pushdown

Oracle treats empty `VARCHAR2` literals (`''`) as `NULL`, so `WHERE col = ''` and `WHERE col <> ''` always evaluate to `UNKNOWN` in Oracle (returning 0 rows), whereas DuckDB evaluates `''` as a non-null zero-length string.
- **Resolution (`src/oracle_pushdown.cpp`)**: `ConstantToSQL` and `TryExtractLike` / `TryExtractPrefix` / `TryExtractSuffix` / `TryExtractContains` refuse to push down empty string literals (`str_val.empty()`), leaving empty-string predicates for DuckDB's local filter executor.
