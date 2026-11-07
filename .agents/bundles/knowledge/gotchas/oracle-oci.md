---
type: Gotcha
title: Oracle OCI Edge Cases, Compatibility & Pitfalls
description: Verified edge cases in Oracle Call Interface development, Instant Client quirks, data type precision traps, and timeout behaviors.
scope: gotcha
domain: oracle
status: stable
tags: [oracle, oci, gotchas, ora-24315, libaio, data-loss]
generated:
  by: flow/0.27.0
  at: 2026-11-07T17:36:00Z
verified:
  - by: agent:flow-setup
    at: 2026-11-07T17:36:00Z
    commit: 36539f3
sources:
  - id: oci-utils
    resource: src/include/oracle_utils.hpp::CheckOCIOptionalAttributeORA24315
    title: ORA-24315 timeout attribute compatibility filter on OCI 23.x
  - id: oracle-scan
    resource: src/oracle_scan.cpp::OracleQueryFunction
    title: 65535-byte ub2 fetch buffer cap, indicator truncation check, and SetChildCardinality
  - id: type-reg
    resource: src/oracle_type_registry.cpp::OracleTypeRegistry::ResolveMetadata
    title: Unconstrained NUMBER mapping to LogicalType::DOUBLE
  - id: type-conv
    resource: src/oracle_type_conversion.cpp::ParseTimestamp
    title: Timezone offset parsing for TIMESTAMP WITH [LOCAL] TIME ZONE
  - id: oracle-settings
    resource: src/oracle_settings.cpp::GetOracleSettings
    title: Per-ATTACH attach_overridden_keys preservation vs SettingScope::LOCAL
  - id: oracle-pushdown
    resource: src/oracle_pushdown.cpp::ConstantToSQL
    title: Empty string literal NULL guard in Oracle filter pushdown
---

# Oracle OCI Edge Cases, Compatibility & Pitfalls

<!-- truth: start -->
- Wrap `OCI_ATTR_CALL_TIMEOUT` attribute calls in `CheckOCIOptionalAttributeORA24315` (`src/include/oracle_utils.hpp`) because OCI 23.x Instant Client can reject timeout attributes with `ORA-24315`[^oci-utils].
- Check `ind == -2 || ind > 0` across all fetched rows and columns before checking `status == OCI_SUCCESS_WITH_INFO`, and on `OCI_SUCCESS_WITH_INFO` only fail when `OCIErrorCodeMatches(errhp, 1406)` (`ORA-01406`) so benign `ORA-24347` warnings are ignored[^oracle-scan].
- Preserve per-`ATTACH` option overrides in `OracleSettings::attach_overridden_keys` unless `lookup.GetScope() == SettingScope::LOCAL` (`src/oracle_settings.cpp`), because `DBConfig::AddExtensionOption` populates `SettingScope::GLOBAL` defaults[^oracle-settings].
- Resolve `state->GetVersionInfo()` in `OracleBindInternal` *before* acquiring `conn_handle` from `OracleConnectionManager` and preserve `version_detected` in `ClearCaches()` so `oracle_connection_limit = 1` never deadlocks on a second connection.
<!-- truth: end -->

## 1. ORA-24315 on OCI 23.x Instant Client

When setting call timeouts on server pre-attach or statement handles[^oci-utils]:
```cpp
OCIAttrSet(stmt.get(), OCI_HTYPE_STMT, &call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, errhp);
```
On Oracle 23.x (23ai) Instant Client, calling `OCIAttrSet` with `OCI_ATTR_CALL_TIMEOUT` can return `ORA-24315: attribute type invalid` depending on the connection state.

**Resolution (`src/include/oracle_utils.hpp`)**[^oci-utils]:
```cpp
inline void CheckOCIOptionalAttributeORA24315(sword status, OCIError *errhp, const std::string &msg) {
    if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO) {
        return;
    }
    if (OCIErrorCodeMatches(errhp, 24315)) {
        return;
    }
    CheckOCIError(status, errhp, msg);
}
```

## 2. Linux OS Dependency: `libaio.so.1`

Oracle Instant Client relies on Linux kernel asynchronous I/O (`libaio`).
- On Ubuntu 20.04/22.04 and Debian 11/12, installing `libaio1` provides `/usr/lib/x86_64-linux-gnu/libaio.so.1`.
- On Ubuntu 24.04 (Noble Numbat) and newer Debian derivatives, the 64-bit time transition renamed the package to `libaio1t64`, providing `/usr/lib/x86_64-linux-gnu/libaio.so.1t64`.
- **Resolution**: `Makefile`'s `ensure_libaio` macro detects `libaio.so.1t64` and symlinks it to `libaio.so.1`.

## 3. 64 KB (`65535`-Byte) Fetch Buffers & Indicator Truncation vs `ORA-24347`

In `src/oracle_scan.cpp` and `src/storage/oracle_table_entry.cpp`[^oracle-scan]:
- Regular `VARCHAR2` / `CHAR` columns size their `SQLT_STR` buffer from `OCI_ATTR_CHAR_SIZE` / `OCI_ATTR_DATA_SIZE` (`ub2` 2-byte attributes), while `CLOB`, `NCLOB`, `BLOB`, `JSON`, `XMLTYPE`, `SDO_UTIL.TO_WKTGEOMETRY(col)`, and `VECTOR_SERIALIZE(col RETURNING CLOB)` allocate the maximum `ub2` define capacity (`65535` bytes via `IsLargeOracleCategory`).
- During `OCIStmtFetch2`, `OracleQueryFunction` inspects `ind == -2 || ind > 0` across all fetched rows and columns *before* checking `status == OCI_SUCCESS_WITH_INFO`, and on `OCI_SUCCESS_WITH_INFO` only raises a truncation error if `OCIErrorCodeMatches(errhp, 1406)` (`ORA-01406`), ignoring benign `ORA-24347` warnings (NULL column in an aggregate function)[^oracle-scan].

## 4. Unconstrained NUMBER -> DOUBLE Precision Loss

Oracle tables frequently declare numeric columns without precision or scale constraints (`col NUMBER` or `NUMBER(*, 0)` where `OCI_ATTR_PRECISION == 0` and `OCI_ATTR_SCALE == 0`)[^type-reg].
- Both `OracleTypeRegistry::ResolveMetadata` and `OracleTypeRegistry::ResolveOciDescribe` map unconstrained `NUMBER` and `NUMBER(*, 0)` to `LogicalType::DOUBLE`[^type-reg].
- **Pitfall**: IEEE-754 double-precision floating point provides 53 bits of mantissa (~15–17 significant decimal digits). Integers exceeding $2^{53} - 1$ (`9,007,199,254,740,991`) lose exact precision unless projected via `TO_CHAR(col, 'TM')` or declared with explicit precision (`NUMBER(19, 0)`).

## 5. Timezone Handling on TSTZ / LTZ

When fetching `TIMESTAMP WITH TIME ZONE` or `TIMESTAMP WITH LOCAL TIME ZONE` columns as `SQLT_STR`[^type-conv]:
- `OracleConnectionManager::CreateConnection` configures `NLS_TIMESTAMP_TZ_FORMAT = 'YYYY-MM-DD HH24:MI:SS.FF TZH:TZM'` and `NLS_NUMERIC_CHARACTERS = '.,'` in `ALTER SESSION SET`.
- `ParseTimestamp` (`src/oracle_type_conversion.cpp`) calls `Timestamp::TryConvertTimestamp(str_val.c_str(), str_val.size(), result, true)` (`use_offset = true`) so timezone offsets convert into UTC microseconds for DuckDB's `LogicalType::TIMESTAMP_TZ`[^type-conv].

## 6. Configurable Call Timeout (`oracle_call_timeout_ms`)

`oracle_scan.cpp`, `oracle_functions.cpp`, and `oracle_connection_manager.cpp` configure `OCI_ATTR_CALL_TIMEOUT` from `oracle_call_timeout_ms` (default `30000` ms, validated between `0` and `3600000` ms)[^oracle-scan]:
- Long-running analytical scans taking more than 30 seconds per round-trip without returning rows time out with `ORA-01013: user requested cancel of current operation`.
- Increase `SET oracle_call_timeout_ms = 600000;` (or `0` to disable), tune `oracle_prefetch_rows`, or apply predicate pushdown for heavy queries.

## 7. Per-`ATTACH` Options vs `DBConfig::AddExtensionOption`

In DuckDB (`v1.5.x` and `v2.0.0`), `DBConfig::AddExtensionOption` inserts default values directly into `db_config.user_settings`, causing `ClientContext::TryGetCurrentSetting` to return a `SettingScope::GLOBAL` match even when the user never ran `SET oracle_*`[^oracle-settings].
- **Resolution (`src/oracle_settings.cpp`, `src/oracle_connection_resolver.cpp`)**: `ApplyAttachOptions` records every explicitly provided `ATTACH` option's global setting key in `OracleSettings::attach_overridden_keys`, and `GetOracleSettings` preserves `state->settings` for any key in `attach_overridden_keys` unless `lookup.GetScope() == SettingScope::LOCAL`[^oracle-settings].

## 8. Oracle Empty String (`''`) NULL Semantics in Filter Pushdown

Oracle treats empty `VARCHAR2` literals (`''`) as `NULL`, so `WHERE col = ''` and `WHERE col <> ''` always evaluate to `UNKNOWN` in Oracle (returning 0 rows), whereas DuckDB evaluates `''` as a non-null zero-length string[^oracle-pushdown].
- **Resolution (`src/oracle_pushdown.cpp`)**: `ConstantToSQL` and `TryExtractLike` / `TryExtractPrefix` / `TryExtractSuffix` / `TryExtractContains` refuse to push down empty string literals (`str_val.empty()`), leaving empty-string predicates for DuckDB's local filter executor[^oracle-pushdown].

## 9. DuckDB 2.0 Vector Size Synchronization (`SetChildCardinality` vs `SetCardinalityUnsafe`)

In DuckDB `v2.0.0`, each `VectorBuffer` tracks its own element count (`vector.size()`). Vectorized operators such as `VectorOperations::IsNull` and `VectorOperations::IsNotNull` read `auto count = input.size();` rather than `DataChunk::size()`[^oracle-scan].
- **Resolution (`src/oracle_scan.cpp`, `src/storage/oracle_insert.cpp`)**: Always call `output.SetChildCardinality(rows_fetched)` (and `output.SetChildCardinality(0)` on empty/finished chunks) so `FlatVector::SetSize(v, rows_fetched)` is synchronized across all output vectors[^oracle-scan].

## 10. Single-Connection Pool (`oracle_connection_limit = 1`) & `GetVersionInfo()` Ordering

In `OracleBindInternal` (`src/oracle_scan.cpp`), resolving `state->GetVersionInfo()` opens a short-lived metadata connection when `version_detected` is false.
- **Pitfall**: Calling `state->GetVersionInfo()` *after* checking out `conn_handle` from `OracleConnectionManager` deadlocks when `oracle_connection_limit = 1`, and resetting `version_detected = false` inside `OracleCatalogState::ClearCaches()` re-triggers that deadlock after `oracle_clear_cache()`.
- **Resolution (`src/oracle_scan.cpp`, `src/storage/oracle_catalog.cpp`)**: Resolve `state->GetVersionInfo()` before acquiring `conn_handle` in `OracleBindInternal`, and preserve `version_detected` across `ClearCaches()`.

[^oci-utils]: `src/include/oracle_utils.hpp::CheckOCIOptionalAttributeORA24315` — `ORA-24315` timeout attribute compatibility filter on OCI 23.x.
[^oracle-scan]: `src/oracle_scan.cpp::OracleQueryFunction` — `65535`-byte `ub2` fetch buffer cap, indicator truncation check, and `SetChildCardinality`.
[^type-reg]: `src/oracle_type_registry.cpp::OracleTypeRegistry::ResolveMetadata` — Unconstrained `NUMBER` mapping to `LogicalType::DOUBLE`.
[^type-conv]: `src/oracle_type_conversion.cpp::ParseTimestamp` — Timezone offset parsing for `TIMESTAMP WITH [LOCAL] TIME ZONE`.
[^oracle-settings]: `src/oracle_settings.cpp::GetOracleSettings` — Per-`ATTACH` `attach_overridden_keys` preservation vs `SettingScope::LOCAL`.
[^oracle-pushdown]: `src/oracle_pushdown.cpp::ConstantToSQL` — Empty string literal `NULL` guard in Oracle filter pushdown.
