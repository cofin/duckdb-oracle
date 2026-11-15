---
type: Spec
flow_id: unconstrained-number
title: "Exact round-trip for Oracle NUMBER and related type fidelity"
description: "Fix decimal corruption when writing FLOAT/DOUBLE to Oracle NUMBER/FLOAT, add oracle_number_as setting, tighten exact NUMBER mappings, verify zoned timestamps, and bump to v0.3.2."
status: stable
state: completed
has_chapters: true
chapters:
  - ch01-numeric-fidelity
  - ch02-timezone-roundtrip
current_chapter: null
current_task: null
planning_quality: standard
planning_degraded_reason: null
plan_revision: 1
plan_commit: 83efe19
state_revision: 2
last_reviewed_commit: 1309446
created_at: 2026-11-15T17:02:00Z
tags: [oracle, numbers, write-pipeline, settings, release]
sources:
  - id: research-dossier
    resource: .agents/bundles/research/number-and-type-fidelity_20261115.md
    title: "Primary research findings for Issue #44 and sibling type-fidelity defects"
  - id: type-registry
    resource: src/oracle_type_registry.cpp::OracleTypeRegistry::ResolveWrite
    title: "Oracle type registry mapping and write bind selection"
  - id: oracle-write
    resource: src/storage/oracle_write.cpp::OracleWriteGlobalState::Sink
    title: "Batched write sink and column binding"
---

# Exact round-trip for Oracle NUMBER and related type fidelity

<!-- truth: start -->
- Preserve `LogicalType::DOUBLE` as the default for unconstrained `NUMBER` and add the `oracle_number_as` setting (`'DOUBLE'`, `'DECIMAL(p,s)'`, `'VARCHAR'`) plus `ATTACH ... (number_as ...)` override support[^type-registry].
- Map exact `INTEGER`/`NUMBER(*, s)` (`DECIMAL(38, s)`), negative-scale `NUMBER(p, -s)` (`DECIMAL(p + s, 0)`), and overscaled `NUMBER(p, s > p)` (`DECIMAL(s, s)`) when width `<= 38`[^type-registry].
- Bind `FLOAT` and `DOUBLE` write values as shortest round-trip decimal text (`SQLT_CHR` via `duckdb_fmt::format`) unless the Oracle target is `BINARY_FLOAT` or `BINARY_DOUBLE`, rejecting `NaN`/`Infinity` with a column-named `InvalidInputException`[^oracle-write].
- Verify zoned timestamp round-trips empirically in `ch02-timezone-roundtrip`, then port to `feat/duckdb-v1.5.6` (`0.2.4`) and update `duckdb/community-extensions` (`0.3.2`) post-merge[^research-dossier].
<!-- truth: end -->

## 1. Specification, Context & Provenance

When reading plain `NUMBER` columns as `DOUBLE` and writing them back via `INSERT INTO`, `OracleTypeRegistry::ResolveWrite` delegates to `BindTypeForDuckDBType`, which unconditionally selects `SQLT_BDOUBLE` even when the Oracle target column is decimal `NUMBER` or `FLOAT`[^type-registry][^oracle-write]. Oracle converts binary doubles to `NUMBER` using 17 significant digits (`13.8` -> `13.800000000000001`). Meanwhile, `NumberLogicalType` maps several exact `NUMBER` variants (`INTEGER`, `NUMBER(*, s)`, `NUMBER(p, -s)`, `NUMBER(p, s > p)`) to `DOUBLE` even though exact DuckDB `DECIMAL` widths (`<= 38`) exist[^research-dossier].

## 2. Requirements, Concrete Contracts & Architecture

- **R1 (`NUMBER` mapping & `oracle_number_as`)**:
  - `OracleTypeMetadata::has_precision` distinguishes catalog `NUMBER(*, s)` (`has_precision = false`, `has_scale = true`, `0 <= scale <= 38` -> `DECIMAL(38, s)`) from unconstrained `NUMBER` (`settings.number_as`).
  - OCI describe sets `has_precision = (precision != 0)` and clears `has_scale = false` when `precision == 0` so `oracle_query` expression columns follow `settings.number_as`.
  - Constrained `NUMBER(p, s)` maps negative scale (`p + s <= 38` -> `DECIMAL(p + s, 0)`) and overscale (`p < s <= 38` -> `DECIMAL(s, s)`), falling back to `settings.number_as` above width `38`.
  - `oracle_number_as` (`VARCHAR`, default `'DOUBLE'`) and `ATTACH` option `number_as` accept `'DOUBLE'`, `'VARCHAR'`, or `'DECIMAL(p,s)'` (`1 <= p <= 38`, `0 <= s <= p`). When `NUMBER` resolves to `VARCHAR`, `pushdown_eligible` is set to `false`.
- **R2 (Decimal text binding for `FLOAT`/`DOUBLE` writes)**:
  - `OracleTypeRegistry::ResolveWrite` selects `SQLT_CHR` for `FLOAT`/`DOUBLE` inputs unless `normalized_type` is `BINARY_FLOAT` or `BINARY_DOUBLE`.
  - `OracleWriteGlobalState::Sink` formats `float` and `double` inputs directly from `UnifiedVectorFormat` via `duckdb_fmt::format("{}", val)` into `converted_strings`, rejecting `NaN`/`Infinity` with a column-named `InvalidInputException`.
- **R3 (Documentation, knowledge alignment & `v0.3.2` bump)**:
  - Add a `Type Mapping` table and `oracle_number_as` configuration row to `README.md`, prepare `[0.3.2]` in `CHANGELOG.md` and `docs/COMPATIBILITY.md`, run `make bump-version bump=patch` (`0.3.1` -> `0.3.2`), and reconcile OKF knowledge chapters, `ADR-002`, `type-fidelity-limitations.md`, and review skills/suppressions.
- **R4 (Zoned timestamp round-trip verification)**:
  - Reproduce `TIMESTAMP WITH TIME ZONE` / `TIMESTAMP WITH LOCAL TIME ZONE` round-trips against Oracle; if UTC-less writes or session-local reads shift instants, pin `TIME_ZONE = '+00:00'` in `ALTER SESSION` and document the `CURRENT_DATE`/`CURRENT_TIMESTAMP` caveat.

## 3. Documentation & OKF Knowledge Plan

- Update `README.md`, `CHANGELOG.md`, `docs/COMPATIBILITY.md`, `.bumpversion.toml`, `description.yml`, and `test/unit_tests/test_extension_metadata.test`.
- Update `.agents/bundles/knowledge/domains/oracle-oci.md`, `gotchas/oracle-oci.md`, add `decisions/adr-002-oracle-number-mapping.md`, add `gotchas/type-fidelity-limitations.md`, and update `.agents/skills/oracle-oci/references/types-and-batches.md`, `review-oracle-oci/references/checklist.md`, and `review-oracle-oci/references/false-positive-suppressions.md`.

## 4. Requirement-to-Task Traceability Matrix

| Requirement | Chapter | Task | Verification |
| --- | --- | --- | --- |
| R1 | ch01-numeric-fidelity | t01 | `test_tuning_settings.test`, `type_registry.test`, `type_conversion.test`, `pushdown.test` |
| R2 | ch01-numeric-fidelity | t02 | `write.test` (`SELECT TO_CHAR(n, 'TM9')` assertions) |
| R3 | ch01-numeric-fidelity | t03 | `test_extension_metadata.test`, `make lint`, `bb validate --workspace . --json` |
| R4 | ch02-timezone-roundtrip | t01 | `type_conversion.test` (`SYS_EXTRACT_UTC` round-trip check) |

## 5. Implementation Plan

- [x] [1309446] ch01-numeric-fidelity — NUMBER mapping, oracle_number_as, decimal write binding, and v0.3.2 docs (chapters/ch01-numeric-fidelity/spec.md)
- [x] [1309446] ch02-timezone-roundtrip — Reproduce and fix zoned timestamp round-trip (chapters/ch02-timezone-roundtrip/spec.md)

## 6. Current State & Remaining Plan Snapshot

- All `ch01-numeric-fidelity` (`997a5d3`, `ceeb56a`, `2e98073`) and `ch02-timezone-roundtrip` (`e4eca72`) tasks are complete and reviewed at `1309446`. Follow-up work targets `fix/v1.5.6-unconstrained-number` (`0.2.4` on `feat/duckdb-v1.5.6`) and post-merge `duckdb/community-extensions` (`0.3.2`).

[^research-dossier]: `.agents/bundles/research/number-and-type-fidelity_20261115.md` — Primary research findings for Issue #44 and sibling type-fidelity defects.
[^type-registry]: `src/oracle_type_registry.cpp::OracleTypeRegistry::ResolveWrite` — Oracle type registry mapping and write bind selection.
[^oracle-write]: `src/storage/oracle_write.cpp::OracleWriteGlobalState::Sink` — Batched write sink and column binding.
