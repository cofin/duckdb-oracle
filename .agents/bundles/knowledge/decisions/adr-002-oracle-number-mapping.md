---
type: Decision
title: "ADR-002: Configurable Unconstrained NUMBER Mapping and Decimal Text Write Binding"
description: Preserve DOUBLE default for unconstrained NUMBER reads via oracle_number_as while mapping bounded variants to DECIMAL and binding FLOAT/DOUBLE writes via shortest round-trip decimal text.
scope: decision
domain: oracle
status: stable
tags: [adr, oracle, numeric, decimal, sqlt-chr, write-pipeline]
generated:
  by: flow/0.30.1
  at: 2026-11-15T17:29:00Z
verified:
  - by: agent:coordinator
    at: 2026-11-15T17:29:00Z
    commit: ceeb56a
sources:
  - id: num-logical
    resource: src/oracle_type_registry.cpp::NumberLogicalType
    title: Exact NUMBER precision and scale mapping with oracle_number_as fallback
  - id: resolve-write
    resource: src/oracle_type_registry.cpp::OracleTypeRegistry::ResolveWrite
    title: Write bind type selection (SQLT_CHR vs SQLT_BDOUBLE)
  - id: write-sink
    resource: src/storage/oracle_write.cpp::OracleWriteGlobalState::Sink
    title: Shortest round-trip decimal text formatting via duckdb_fmt::format
  - id: num-setting
    resource: src/oracle_settings.cpp::ParseOracleNumberAs
    title: Validation and canonical formatting for oracle_number_as
  - id: attach-order
    resource: src/storage/oracle_storage_extension.cpp::OracleAttach
    title: Session setting resolution before explicit ATTACH option application
---

# ADR-002: Configurable Unconstrained NUMBER Mapping and Decimal Text Write Binding

<!-- truth: start -->
- Unconstrained `NUMBER` and out-of-range `NUMBER` columns default to `LogicalType::DOUBLE` (`oracle_number_as = 'DOUBLE'`) and support opt-in `'VARCHAR'` or `'DECIMAL(p, s)'` (`1 <= p <= 38`, `0 <= s <= p`) via `SET oracle_number_as` or `ATTACH ... (number_as ...)`[^num-logical][^num-setting].
- Exact `INTEGER`, `NUMBER(*, s)` (`s <= 38`), `NUMBER(p, -s)` (`p - s <= 38`), and `NUMBER(p, s > p)` (`s <= 38`) columns map deterministically to DuckDB `DECIMAL`[^num-logical].
- Writing `FLOAT` or `DOUBLE` values to non-binary Oracle targets (`NUMBER`, `FLOAT`) binds via `SQLT_CHR` using `duckdb_fmt::format("{}", val)` with explicit non-finite (`NaN`/`Infinity`) rejection, while `BINARY_FLOAT` and `BINARY_DOUBLE` retain `SQLT_BDOUBLE`[^resolve-write][^write-sink].
<!-- truth: end -->

## Context

Oracle allows `NUMBER` without declared precision or scale to store integers and base-10 decimals spanning up to 38 significant digits and scales outside a fixed DuckDB `DECIMAL(38, s)`. Meanwhile, `INSERT INTO` binds for `FLOAT` and `DOUBLE` inputs previously used `SQLT_BDOUBLE`, causing OCI's IEEE-754 binary-to-decimal conversion on Oracle `NUMBER` columns to store binary approximation noise such as `13.8 -> 13.800000000000001`.

In addition, catalog metadata queries (`ALL_TAB_COLUMNS`) and OCI describe attributes expose distinct representations for `INTEGER`, `NUMBER(*, s)`, negative-scale `NUMBER(p, -s)`, overscaled `NUMBER(p, s > p)`, Oracle `FLOAT(p)` (`scale == -127`), and `oracle_query` expressions (`precision == 0`).

## Decision

1. **Preserve `'DOUBLE'` as Default with Explicit `oracle_number_as`**: Register `oracle_number_as` (session/global setting and `ATTACH` `number_as` option) defaulting to `'DOUBLE'`, accepting `'VARCHAR'` or `'DECIMAL(p,s)'`[^num-setting]. Initialize `OracleCatalogState::settings` from `GetOracleSettings(context, nullptr)` before `ApplyOptions(attach_options)` so explicit `ATTACH` options take precedence over prior session `SET` statements[^attach-order].
2. **Tighten Deterministic `NUMBER` Variant Mapping**: Require `has_precision` tracking alongside `has_scale`. Map `NUMBER(*, s)` to `DECIMAL(38, s)`, `NUMBER(p, -s)` to `DECIMAL(p - s, 0)` when `p - s <= 38`, `NUMBER(p, s > p)` to `DECIMAL(s, s)` when `s <= 38`, keep `SQLT_NUM`/`SQLT_VNU` with `scale == -127` as `DOUBLE` (`OCI FLOAT`), and clear `has_scale = false` when `OCI_ATTR_PRECISION == 0` so computed `NUMBER` expressions respect `oracle_number_as`[^num-logical]. Disable filter pushdown (`pushdown_eligible = false`) when `NUMBER` resolves to `VARCHAR`[^num-logical].
3. **Decimal Text Binding on Write (`SQLT_CHR`)**: In `OracleTypeRegistry::ResolveWrite`, bind `FLOAT`/`DOUBLE` columns via `SQLT_CHR` unless `normalized_type` is `BINARY_FLOAT` or `BINARY_DOUBLE`[^resolve-write]. In `OracleWriteGlobalState::Sink`, reject `NaN`/`Infinity` with a column-named `InvalidInputException` and format finite `float`/`double` elements directly with `duckdb_fmt::format("{}", val)`[^write-sink].

## Rejected Alternatives

| Alternative | Reason Rejected |
| :--- | :--- |
| Change default unconstrained `NUMBER` mapping from `DOUBLE` to `VARCHAR` or `DECIMAL(38,10)` | Silently breaks existing schemas expecting numeric arithmetic or losing >10 fractional digits or integers > $10^{28}$ unless explicitly opted into. |
| Wrap Oracle SQL selects in `TO_CHAR(...)` by default | Forces server-side string conversion and breaks predicate pushdown on existing workloads. |
| Fixed precision float formatting (`printf("%.15g")` / `std::fixed`) | Truncates significant digits or alters shortest round-trip decimal representation compared to Dragonbox (`duckdb_fmt::format`). |

## Consequences

- `INSERT INTO oracle_table` with `FLOAT`/`DOUBLE` literals preserves base-10 decimals (`13.8`, `0.1`, `0.15`, `1e20`, `1e-7`) cleanly on Oracle `NUMBER` and `FLOAT` columns.
- Existing `ATTACH`ed catalogs snapshot `oracle_number_as` into `OracleCatalogState::settings` at `ATTACH` time, consistent with other catalog options.

[^num-logical]: `src/oracle_type_registry.cpp::NumberLogicalType` — Exact `NUMBER` precision/scale mapping and `oracle_number_as` fallback.
[^resolve-write]: `src/oracle_type_registry.cpp::OracleTypeRegistry::ResolveWrite` — Write bind type selection (`SQLT_CHR` vs `SQLT_BDOUBLE`).
[^write-sink]: `src/storage/oracle_write.cpp::OracleWriteGlobalState::Sink` — Shortest round-trip decimal text formatting and non-finite checks.
[^num-setting]: `src/oracle_settings.cpp::ParseOracleNumberAs` — Validation and formatting for `oracle_number_as`.
[^attach-order]: `src/storage/oracle_storage_extension.cpp::OracleAttach` — Session setting resolution before `ApplyOptions`.
