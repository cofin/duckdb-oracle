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

## 3. String Buffer Cap (64 KB) vs Dynamic LOB Locators

In `src/oracle_scan.cpp`:
- Regular `VARCHAR2` / `CHAR` columns are fetched using `SQLT_STR` into 64 KB (`65535`-byte) pre-allocated row slots, with explicit truncation checks if `indicator > 0` or `-2`.
- `CLOB`, `NCLOB`, `BLOB`, `SDO_UTIL.TO_WKTGEOMETRY(col)`, and `VECTOR_SERIALIZE(col RETURNING CLOB)` are fetched using `OCILobLocator*` descriptors (`SQLT_CLOB` / `SQLT_BLOB`) and dynamically streamed via `OCILobGetLength2` and `OCILobRead2` so large LOBs, complex WKT polygons, and high-dimensional vectors are never truncated at 64 KB.

## 4. Unconstrained NUMBER -> DOUBLE Precision Loss

Oracle tables frequently declare numeric columns without precision or scale constraints: `col NUMBER`.
- DuckDB cannot map unconstrained `NUMBER` to fixed-width integer types (`BIGINT`) because Oracle numbers support up to 38 decimal digits.
- DuckDB Oracle maps unconstrained `NUMBER` to `LogicalType::DOUBLE`.
- **Pitfall**: Double-precision floating point (`IEEE 754`) provides 53 bits of mantissa (~15-17 significant decimal digits). Integers or IDs exceeding $2^{53} - 1$ (9,007,199,254,740,991) will suffer silent precision loss. For exact precision, declare columns with explicit scale/precision (e.g. `NUMBER(19, 0)`) or use `TO_CHAR()` on the Oracle side.

## 5. Timezone Handling on TSTZ / LTZ

When fetching `TIMESTAMP WITH TIME ZONE` or `TIMESTAMP WITH LOCAL TIME ZONE` columns:
- `SQLT_TIMESTAMP_TZ` / `SQLT_TIMESTAMP_LTZ` descriptors preserve timezone offset calculation via `OCIDateTimeGetTimeZoneOffset`, converting timestamps to UTC microseconds for DuckDB's `LogicalType::TIMESTAMP_TZ`.
- Verify that the session `TIME_ZONE` is synchronized when comparing local session timestamps.

## 6. Configurable Call Timeout (`oracle_call_timeout_ms`)

`oracle_scan.cpp` and `oracle_connection_manager.cpp` configure `OCI_ATTR_CALL_TIMEOUT` from `oracle_call_timeout_ms` (default `30000` ms, validated between `0` and `3600000` ms):
- Long-running analytical scans that take more than 30 seconds per round-trip without returning rows will timeout with `ORA-01013: user requested cancel of current operation`.
- Increase `SET oracle_call_timeout_ms = 600000;` (or `0` to disable), tune `oracle_prefetch_rows`, or apply predicate pushdown for heavy queries.
