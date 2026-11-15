---
type: Gotcha
title: Type Fidelity Limitations and Verified Behavior
description: Known precision boundaries, pushdown asymmetries, temporal truncation limits, and verified Oracle behavior across NUMBER and zoned timestamps.
scope: gotcha
domain: oracle
status: stable
tags: [oracle, gotchas, limitations, numeric, timestamps]
generated:
  by: flow/0.30.1
  at: 2026-11-15T17:29:00Z
verified:
  - by: agent:coordinator
    at: 2026-11-15T17:29:00Z
    commit: ceeb56a
sources:
  - id: num-reg
    resource: src/oracle_type_registry.cpp::NumberLogicalType
    title: Pushdown flag and precision cutoff for NUMBER columns
  - id: write-sink
    resource: src/storage/oracle_write.cpp::OracleWriteGlobalState::Sink
    title: DOUBLE magnitude overflow and float formatting behavior
  - id: type-conv
    resource: src/oracle_type_conversion.cpp::ParseTimestamp
    title: Microsecond timestamp truncation
---

# Type Fidelity Limitations and Verified Behavior

<!-- truth: start -->
- Setting `oracle_number_as = 'VARCHAR'` disables Oracle filter pushdown (`pushdown_eligible = false`) so comparisons follow DuckDB string ordering locally rather than mismatched SQL predicates[^num-reg].
- Passing a single-precision DuckDB `FLOAT` into an Oracle `FLOAT` catalog column widens to DuckDB `DOUBLE` inside DuckDB's binder (`f_col` is mapped to `DOUBLE`) before `OracleWriteGlobalState::Sink` runs.
- Doubles exceeding Oracle `NUMBER` range (around $10^{126}$, such as `1e300`) fail inside Oracle with `ORA-01426` on batch execution and trigger statement-atomic rollback[^write-sink].
- Oracle `TIMESTAMP(7)`..`TIMESTAMP(9)` fractional seconds beyond 6 digits truncate to DuckDB microsecond precision (`LogicalType::TIMESTAMP`)[^type-conv].
<!-- truth: end -->

## Numeric Precision & Pushdown Caveats

- **Default `oracle_number_as = 'DOUBLE'`**: Unconstrained `NUMBER` defaults to `DOUBLE` (53-bit significand) for backward compatibility. Integers above $2^{53}$ or base-10 fractions requiring >15–17 digits lose precision on read unless `oracle_number_as` is set to `'DECIMAL(p,s)'` or `'VARCHAR'`[^num-reg].
- **`VARCHAR` Pushdown Asymmetry**: Mapping unconstrained `NUMBER` to `'VARCHAR'` disables filter pushdown on those columns (`pushdown_eligible = false`) so DuckDB applies predicates on the returned strings directly rather than pushing `'9' < '20'` against numeric Oracle columns[^num-reg].
- **Fixed `ATTACH` Setting Snapshot**: Attached `OracleTableEntry` column definitions capture `state->settings.number_as` when `ATTACH` executes; changing `oracle_number_as` later with `SET` affects `oracle_query`/`oracle_scan` and subsequent `ATTACH` catalogs, not previously attached catalogs.
- **DuckDB `FLOAT` Binder Widening**: Because Oracle `FLOAT` columns expose `LogicalType::DOUBLE` in DuckDB, inserting a `FLOAT` expression into an Oracle `FLOAT` catalog column causes DuckDB to cast `FLOAT -> DOUBLE` before entering `Sink`; pass `DOUBLE` or `DECIMAL` literals directly to avoid upstream 32-bit rounding noise[^write-sink].
- **Oracle `NUMBER` Magnitude Upper Bound**: `1e20` and `1e-7` bind cleanly via `SQLT_CHR` exponential notation in Oracle 23ai, whereas binary doubles exceeding Oracle `NUMBER`'s exponent limit (~$10^{126}$, such as `1e300`) raise `ORA-01426` during `OCIStmtExecute` and roll back the batch atomically[^write-sink].

## Temporal Caveats

- **`TIMESTAMP(7..9)` Microsecond Truncation**: DuckDB `TIMESTAMP` stores microseconds; Oracle fractional seconds past 6 digits truncate on read and bind as 6-digit fractional seconds on write[^type-conv].
- **UTC Session Normalization for `TIMESTAMP WITH [LOCAL] TIME ZONE`**: `OracleConnectionManager::CreateConnection` sets `TIME_ZONE = '+00:00'` and `NLS_TIMESTAMP_TZ_FORMAT = 'YYYY-MM-DD HH24:MI:SS.FFTZH:TZM'` (without a space before `TZH:TZM`) so non-UTC `TIMESTAMP WITH TIME ZONE` offsets convert cleanly into UTC `LogicalType::TIMESTAMP` without ICU and round-trips preserve exact UTC instants (`SYS_EXTRACT_UTC`) even under non-UTC client `TZ`. Because session `TIME_ZONE` is pinned to `'+00:00'`, Oracle SQL expressions using `CURRENT_DATE`, `CURRENT_TIMESTAMP`, or `LOCALTIMESTAMP` execute in UTC (`+00:00`), and source timezone offsets are normalized to UTC rather than retained separately.

[^num-reg]: `src/oracle_type_registry.cpp::NumberLogicalType` — `pushdown_eligible` and 38-digit `DECIMAL` boundaries.
[^write-sink]: `src/storage/oracle_write.cpp::OracleWriteGlobalState::Sink` — `SQLT_CHR` float formatting and batch error propagation.
[^type-conv]: `src/oracle_type_conversion.cpp::ParseTimestamp` — Microsecond timestamp parsing.
