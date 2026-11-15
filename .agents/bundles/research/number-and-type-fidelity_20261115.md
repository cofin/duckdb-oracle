---
type: Research
title: Oracle NUMBER Round-Trip Precision and Related Type-Fidelity Findings
description: Root-cause trace and sibling defect audit for Issue #44 across type registry, scan describe, batch writes, filter pushdown, session timezone handling, and DuckDB v1.5.6 divergence.
status: stable
tags: [oracle, oci, numbers, precision, timestamps, research]
generated:
  by: bb-research
  at: 2026-11-15T17:02:00Z
verified:
  - by: agent:research
    at: 2026-11-15T17:02:00Z
    commit: 83efe19
sources:
  - id: type-registry
    resource: src/oracle_type_registry.cpp::OracleTypeRegistry::ResolveWrite
    title: NumberLogicalType, BindTypeForDuckDBType, and ResolveWrite
  - id: oracle-write
    resource: src/storage/oracle_write.cpp::OracleWriteGlobalState::Sink
    title: UnifiedVectorFormat sink and SQLT_BDOUBLE column binding
  - id: oracle-scan
    resource: src/oracle_scan.cpp::OracleBindInternal
    title: OCI describe precision/scale capture and SQLT_STR fetch
  - id: table-entry
    resource: src/storage/oracle_table_entry.cpp::LoadColumns
    title: ALL_TAB_COLUMNS precision and scale parsing
  - id: conn-mgr
    resource: src/oracle_connection_manager.cpp::OracleConnectionManager::CreateConnection
    title: Session ALTER SESSION NLS setup
  - id: pushdown
    resource: src/oracle_pushdown.cpp::OraclePushdownComplexFilter
    title: Pushed-down filter extraction and residual removal
---

# Oracle NUMBER Round-Trip Precision and Related Type-Fidelity Findings

<!-- truth: start -->
- Issue #44 corrupts decimal fractions on write (`13.8 -> 13.800000000000001`) because `BindTypeForDuckDBType` binds `FLOAT` and `DOUBLE` as `SQLT_BDOUBLE` regardless of Oracle target type (`NUMBER` or `FLOAT`), while fetches already arrive as exact `SQLT_STR` text[^type-registry][^oracle-scan].
- Session initialization pins `NLS_NUMERIC_CHARACTERS = '.,'` and DuckDB's bundled `duckdb_fmt::format("{}", val)` emits shortest round-trip decimal text (`13.8`, `0.1`, `1e+20`, `1e-07`), enabling lossless `SQLT_CHR` decimal text binding on both `main` (`v2.0.0`) and `feat/duckdb-v1.5.6`[^conn-mgr].
- `NumberLogicalType` currently maps `INTEGER`/`NUMBER(*, s)` (`has_precision = false`, `has_scale = true`), `NUMBER(p, -s)`, and `NUMBER(p, s > p)` to `DOUBLE` even when exact `DECIMAL(w, s)` (`w <= 38`) fits[^type-registry][^table-entry].
<!-- truth: end -->

## Root Cause Trace

1. **Read mapping (`src/oracle_type_registry.cpp`)**: `NumberLogicalType` returns `LogicalType::DOUBLE` whenever `!has_scale`, `precision == 0`, `precision > 38`, `scale < 0`, or `scale > precision`[^type-registry].
2. **Fetch (`src/oracle_scan.cpp`)**: `OracleInitGlobal` defines all non-`BLOB` columns as `SQLT_STR`, so Oracle transmits exact decimal text; precision loss on read occurs only when `SetOracleOutputValue` parses text to `double`[^oracle-scan].
3. **Write bind (`src/oracle_type_registry.cpp`, `src/storage/oracle_write.cpp`)**: `BindTypeForDuckDBType` always selects `SQLT_BDOUBLE` for `FLOAT` and `DOUBLE`, and `BindColumn` copies the IEEE-754 8-byte representation; Oracle converts binary double to `NUMBER` using 17 significant digits[^type-registry][^oracle-write].

## Sibling Defects & Verification Status

- **Oracle `FLOAT` target writes**: `FLOAT` is a decimal `NUMBER` subtype and suffers the same `SQLT_BDOUBLE` rounding as `NUMBER`[^type-registry].
- **Large integer keys (`> 2^53`) in plain `NUMBER`**: Altered on read via `DOUBLE`, breaking exact equality joins and filters[^type-registry].
- **`INTEGER` / `NUMBER(*, s)` / `NUMBER(p, -s)` / `NUMBER(p, s > p)`**: All map needlessly to `DOUBLE`; `DECIMAL(38, s)`, `DECIMAL(p - s, 0)`, and `DECIMAL(s, s)` are exact whenever width `<= 38`[^type-registry][^table-entry].
- **`oracle_query` expressions**: Aggregate/computed expressions (`SUM`, `AVG`, `COUNT(*)`) describe with `OCI_ATTR_PRECISION = 0`; honor `oracle_number_as` on describe too[^oracle-scan].
- **Unresolved target metadata & non-finite values**: `PrepareOracleWriteBindData` falls back to `SQLT_BDOUBLE` on unknown targets and `BindColumn` passes `NaN`/`Infinity` through to Oracle without a column-named error[^oracle-write].
- **Filter pushdown asymmetry**: Pushed filters are stripped from DuckDB's residual plan, evaluating on exact Oracle `NUMBER` values while unpushed filters run on `DOUBLE`[^pushdown].
- **Zoned timestamps (code-only; requires live Oracle reproduction)**: `TIMESTAMP WITH TIME ZONE` converts to UTC `TIMESTAMP` on read (`use_offset = true`), then writes via `TO_TIMESTAMP(..., 'YYYY-MM-DD HH24:MI:SS.FF')` without a zone offset while `ALTER SESSION` does not set `TIME_ZONE`[^conn-mgr].
- **DuckDB `v1.5.6` divergence**: `oracle_type_registry.*`, `oracle_settings.*`, and `oracle_connection_manager.cpp` are identical between `main` and `origin/feat/duckdb-v1.5.6`; `oracle_write.cpp` differs by using `Flatten(count)` and `FlatVector::GetData` rather than `ToUnifiedFormat`[^oracle-write].

## Dependency & CI Audit

- `duckdb` (`v2.0.0` on `main`, `v1.5.6` on `feat/duckdb-v1.5.6`) and Oracle Instant Client setup (`23.26.2.0.0`) require no upgrades.
- `duckdb_fmt::format` is available in DuckDB's bundled `third_party/fmt/include` on both `v2.0.0` and `v1.5.6`, requiring zero external library additions.

[^type-registry]: `src/oracle_type_registry.cpp::OracleTypeRegistry::ResolveWrite` — Number mapping and write bind type selection.
[^oracle-write]: `src/storage/oracle_write.cpp::OracleWriteGlobalState::Sink` — Batch write binding and transposition.
[^oracle-scan]: `src/oracle_scan.cpp::OracleBindInternal` — OCI describe metadata extraction and `SQLT_STR` fetch.
[^table-entry]: `src/storage/oracle_table_entry.cpp::LoadColumns` — `ALL_TAB_COLUMNS` precision and scale parsing.
[^conn-mgr]: `src/oracle_connection_manager.cpp::OracleConnectionManager::CreateConnection` — Session NLS configuration.
[^pushdown]: `src/oracle_pushdown.cpp::OraclePushdownComplexFilter` — Filter extraction and residual pruning.
