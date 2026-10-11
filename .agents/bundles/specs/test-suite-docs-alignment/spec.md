---
type: Spec
flow_id: test-suite-docs-alignment
title: "Chapter 5: Test Suite De-duplication, Spatial/Integration Coverage & Documentation/OKF Alignment"
state: completed
plan_revision: 1
plan_commit: 4d4fd44274bda21b9de7aa3efad471dd23b1789d
state_revision: 2
current_task: null
last_operation: finish
operation_targets: []
last_verified_checkpoint: 2026-10-11T18:35:00Z
created_at: 2026-10-11T17:17:00Z
updated_at: 2026-10-11T18:35:00Z
description: Remediate duplicate unit and integration test files, stale advanced_*.test comments, manual_verification.md inside test/unit_tests/* glob, hardcoded credentials and missing cleanup in integration tests, disabled SDO_GEOMETRY integration test, and documentation/OKF drift.
tags: [testing, documentation, sqllogictest, okf]
parent_prd: codebase-remediation-prd
research: []
---

# Chapter 5: Test Suite De-duplication, Spatial/Integration Coverage & Documentation/OKF Alignment

**Flow ID:** `test-suite-docs-alignment`
**Parent PRD:** [`codebase-remediation-prd`](../codebase-remediation-prd/spec.md)

## Problem & Goal

The unit and integration test suites, repository documentation, and OKF knowledge bundles have 2 Medium and 1 Low findings (`M-27`, `M-28`, `L-11`):
- `test/unit_tests/` contains duplicate/redundant `.test` files (`test/unit_tests/test_metadata.test` duplicating `test/unit_tests/oracle/test_metadata.test` with a stale `v0.1.3` assertion, and `test/unit_tests/test_schema_resolution.test` duplicating `test/unit_tests/oracle/test_schema_resolution.test`), stale comments referencing 6 non-existent `advanced_*.test` files, and `test/unit_tests/manual_verification.md` sitting inside the `test/unit_tests/*` unittest glob.
- `test/integration_tests/` duplicates 3 top-level `.test` files (`test_write_basic.test`, `test_write_vector.test`, `test_partitioning.test`) with `test/integration_tests/oracle/`, leaves `test_write_spatial.test.disabled` disabled as an 8-line stub, hardcodes `test_user/test_password@localhost:1521/FREEPDB1` in `test_edge_cases.test`, `test_pipelined.test`, and `test_views_matviews.test`, and defines an unused `test_types` table in `01_setup.sql`.
- `CHANGELOG.md` contains a fabricated `[1.0.0] - 2024-03-24` release and removed `oracle_attach_wallet()`, duplicates `[0.1.2]`, and omits `0.2.0`–`0.2.3`; `README.md:135` falsely states views are not visible in `SHOW TABLES`; `CONTRIBUTING.md` points to `your-org/duckdb-oracle` and `instantclient_23_6`; `docs/RELEASE.md` references `v1.5.5`/`0.2.2`; `SECURITY.md` lacks a reporting URL; and `.agents/bundles/` contains stale `v1.5.5` and architectural references.

## Scope & Constraints

- **In Scope**:
  - `test/unit_tests/**`, `test/README.md`
  - `test/integration_tests/**`
  - `README.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, `SECURITY.md`, `docs/RELEASE.md`
  - `.agents/bundles/knowledge/**`, `.agents/bundles/product/**`
- **Out of Scope**:
  - C++ runtime source files and CI/build scripts (Chapters 1–4).

## Implementation Plan

### Phase 1: Test Suite Consolidation, Spatial/Integration Coverage & Documentation Alignment

- [x] **Task 1.1: Consolidate unit test suite, relocate manual_verification.md, and expand unit regression tests** -> [tasks/1.1.md](tasks/1.1.md)
  - **Objective**: Remediate `M-27` (unit tests part) and `L-11` (`test/README.md` part) across `test/unit_tests/**` and `test/README.md`.
  - **Verification Strategy**: `tdd`
  - **Files**: `test/unit_tests/test_metadata.test`, `test/unit_tests/test_schema_resolution.test`, `test/unit_tests/oracle/test_metadata.test`, `test/unit_tests/oracle/test_schema_resolution.test`, `test/unit_tests/oracle/test_tuning_settings.test`, `test/unit_tests/manual_verification.md`, `test/README.md`
- [x] **Task 1.2: Deduplicate integration tests, enable SDO_GEOMETRY spatial integration test, and parameterize credentials** -> [tasks/1.2.md](tasks/1.2.md)
  - **Objective**: Remediate `M-27` (integration tests part) across `test/integration_tests/**`.
  - **Verification Strategy**: `Verify sqllogictest syntax and placeholder parameterization`
  - **Depends On**: `test-suite-docs-alignment:1.1`
  - **Files**: `test/integration_tests/init_sql/01_setup.sql`, `test/integration_tests/test_write_basic.test`, `test/integration_tests/test_write_vector.test`, `test/integration_tests/test_partitioning.test`, `test/integration_tests/test_write_spatial.test.disabled`, `test/integration_tests/oracle/spatial.test`, `test/integration_tests/oracle/partition_pruning.test`, `test/integration_tests/test_edge_cases.test`, `test/integration_tests/test_pipelined.test`, `test/integration_tests/test_views_matviews.test`
- [x] **Task 1.3: Align CHANGELOG.md, README.md, CONTRIBUTING.md, SECURITY.md, and docs/RELEASE.md with v0.2.3 / DuckDB v1.5.6** -> [tasks/1.3.md](tasks/1.3.md)
  - **Objective**: Remediate `M-28` and `L-11` (`SECURITY.md` part) across `CHANGELOG.md`, `README.md`, `CONTRIBUTING.md`, `SECURITY.md`, and `docs/RELEASE.md`.
  - **Verification Strategy**: `Verify documentation links and version consistency`
  - **Depends On**: `test-suite-docs-alignment:1.2`
  - **Files**: `README.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, `SECURITY.md`, `docs/RELEASE.md`
- [x] **Task 1.4: Synchronize OKF knowledge and product bundles with DuckDB v1.5.6 and remediated codebase architecture** -> [tasks/1.4.md](tasks/1.4.md)
  - **Objective**: Remediate `L-11` (OKF knowledge/product part) across `.agents/bundles/knowledge/**` and `.agents/bundles/product/**`.
  - **Verification Strategy**: `Verify OKF frontmatter and internal links`
  - **Depends On**: `test-suite-docs-alignment:1.3`
  - **Files**: `.agents/bundles/knowledge/architecture/oracle-oci-subsystem.md`, `.agents/bundles/knowledge/architecture/duckdb-extension-architecture.md`, `.agents/bundles/knowledge/domains/oracle-oci.md`, `.agents/bundles/knowledge/gotchas/oracle-oci.md`, `.agents/bundles/knowledge/patterns/oci-raii-handles.md`, `.agents/bundles/knowledge/patterns/oci-batch-operations.md`, `.agents/bundles/knowledge/patterns/duckdb-pushdown.md`, `.agents/bundles/knowledge/patterns/duckdb-write-pipeline.md`, `.agents/bundles/knowledge/testing/duckdb-test-harness.md`, `.agents/bundles/product/product-guidelines.md`, `.agents/bundles/product/tech-stack.md`

## Continuity Snapshot

- **Active Flow / Lifecycle**: `test-suite-docs-alignment` / `planned`
- **Current Task & Claimant**: `null`
- **Last Verified Checkpoint**: `null`
- **Decisions**:
  - Consolidate manual verification instructions into `test/README.md` and remove `test/unit_tests/manual_verification.md` so `./build/release/test/unittest "test/unit_tests/*"` only matches `.test` files.
  - Replace `test/integration_tests/test_write_spatial.test.disabled` with an active `test/integration_tests/oracle/spatial.test` that tests `SDO_GEOMETRY` point/polygon WKT read and write round-trips.
- **Recent Discoveries**:
  - `test/unit_tests/test_metadata.test` was an exact duplicate of `test/unit_tests/oracle/test_metadata.test`, and `test/unit_tests/test_schema_resolution.test` was an exact duplicate of `test/unit_tests/oracle/test_schema_resolution.test`.
- **Blockers & Unblock Conditions**: `none`
- **Next Exact Step**: Execute `test-suite-docs-alignment:1.1`.
- **Plan Identity**: `plan_revision: 1`, `plan_commit: null`
- **State Identity**: `state_revision: 0`, `last_operation: null`, `operation_targets: []`
- **Relevant Knowledge Paths**:
  - `.agents/bundles/knowledge/testing/duckdb-test-harness.md`
  - `.agents/bundles/knowledge/standards/project-management.md`

## Verification Evidence

None yet (`state: planned`).
