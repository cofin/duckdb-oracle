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

## 3. String & LOB Buffer Cap (65,535 Bytes)

In `src/oracle_scan.cpp`, string and text columns are fetched using `SQLT_STR` into pre-allocated memory buffers capped at 65,535 bytes (`uint16_t` max):
- For columns declared as `VARCHAR2(4000)`, `CHAR(2000)`, or short `CLOB` values, this operates reliably.
- For `CLOB`, `JSON`, or multi-byte UTF-8 data exceeding 65,535 bytes, single-shot fetch will either truncate data (indicator `sb2 > 0`) or fail. Piecewise LOB retrieval via `OCILobRead2` or chunked streaming is required for unbounded payloads.

## 4. Unconstrained NUMBER -> DOUBLE Precision Loss

Oracle tables frequently declare numeric columns without precision or scale constraints: `col NUMBER`.
- DuckDB cannot map unconstrained `NUMBER` to fixed-width integer types (`BIGINT`) because Oracle numbers support up to 38 decimal digits.
- DuckDB Oracle maps unconstrained `NUMBER` to `LogicalType::DOUBLE`.
- **Pitfall**: Double-precision floating point (`IEEE 754`) provides 53 bits of mantissa (~15-17 significant decimal digits). Integers or IDs exceeding $2^{53} - 1$ (9,007,199,254,740,991) will suffer silent precision loss. For exact precision, declare columns with explicit scale/precision (e.g. `NUMBER(19, 0)`) or use `TO_CHAR()` on the Oracle side.

## 5. Timezone Truncation on TSTZ / LTZ

When fetching `TIMESTAMP WITH TIME ZONE` or `TIMESTAMP WITH LOCAL TIME ZONE` columns via string formatting:
- If formatted naively or fetched as `SQLT_TIMESTAMP`, the client drops the timezone offset or normalizes it without persisting the offset into DuckDB's `TIMESTAMP_TZ` vector.
- Always verify that the session `TIME_ZONE` is synchronized between DuckDB and the Oracle connection session (`ALTER SESSION SET TIME_ZONE = ...`).

## 6. Hard-Coded 30s Call Timeout

`oracle_scan.cpp` and `oracle_connection_manager.cpp` configure an upper-bound call timeout of 30,000 milliseconds (`OCI_ATTR_CALL_TIMEOUT`).
- Long-running analytical scans on billion-row tables without partition splitting will timeout with `ORA-01013: user requested cancel of current operation` after 30 seconds of server processing if no rows are returned within the timeout window.
- Tune `oracle_prefetch_rows` or run targeted queries with predicate pushdown.
