---
type: Spec
flow_id: ci-build-release-hardening
title: "Chapter 4: GitHub Actions Workflows, Build System, OCI Setup Scripts & Licensing"
state: planned
plan_revision: 1
plan_commit: 4d4fd44274bda21b9de7aa3efad471dd23b1789d
state_revision: 1
current_task: null
last_operation: null
operation_targets: []
last_verified_checkpoint: null
created_at: 2026-10-11T17:15:00Z
updated_at: 2026-10-11T17:15:00Z
description: Remediate duckdb-update-check.yml closing its own open PR daily, release-unsigned.yml publishing partial releases and masking smoke-test crashes, integration-tests.yml stale instantclient_23_6 env and double make release, GitHub Actions script injection, $GITHUB_ENV LD_LIBRARY_PATH expansion, macOS OCI version/mount cleanup, extension_config.cmake TEST_DIR, Makefile ensure_libaio, and LICENSE vs description.yml alignment.
tags: [bugfix, ci-cd, github-actions, build, scripts, license]
parent_prd: codebase-remediation-prd
research: []
---

# Chapter 4: GitHub Actions Workflows, Build System, OCI Setup Scripts & Licensing

**Flow ID:** `ci-build-release-hardening`
**Parent PRD:** [`codebase-remediation-prd`](../codebase-remediation-prd/spec.md)

## Problem & Goal

The CI/CD workflows, OCI setup scripts, CMake/Makefile build configuration, and extension metadata have 1 High (4 sub-items), 4 Medium, and 1 Low findings (`H-24` CI/license parts, `M-23`..`M-26`, `L-10`):
- `.github/workflows/duckdb-update-check.yml` unconditionally closes *all* open PRs with the `duckdb-update` label—including an open PR already targeting `feat/duckdb-v${NEW_VERSION}`—and force-pushes a fresh PR every 24 hours (causing PRs #31, #32, #33, #34, and #35).
- `.github/workflows/release-unsigned.yml` only logs a warning when `ACTUAL_COUNT -lt EXPECTED_COUNT` (allowing partial releases), masks extension load/crash failures in its smoke test (`grep -q "ORA-" || echo "Extension loaded..."`), omits `ref:` on `actions/checkout` in build/release jobs, interpolates `${{ ... }}` directly inside `run:` blocks, and uses deprecated Node 16 actions (`peaceiris/actions-gh-pages@v3`, `softprops/action-gh-release@v1`).
- `.github/workflows/integration-tests.yml` hardcodes `ORACLE_HOME: /opt/oracle/instantclient_23_6` (while `setup_oci_linux.sh` installs `instantclient_23_26`) and runs `make release` twice (`Build Extension` + `make integration`).
- `scripts/setup_oci_linux.sh` and `scripts/setup_oci_macos.sh` write literal unexpanded `\$LD_LIBRARY_PATH` / `\$DYLD_LIBRARY_PATH` into `$GITHUB_ENV` and fail under `set -u` when `LD_LIBRARY_PATH` is unset; `setup_oci_macos.sh` is pinned to `23.26.1.0.0` and lacks an `EXIT` trap to detach mounted DMGs.
- `extension_config.cmake` omits `TEST_DIR`, `Makefile`'s `ensure_libaio` lacks `sudo` elevation for non-root runners and is missing from `test_debug_internal`/`test_reldebug_internal`, and `description.yml` declares `license: Apache-2.0` while `LICENSE` is `MIT`.

## Scope & Constraints

- **In Scope**:
  - `.github/workflows/duckdb-update-check.yml`, `.github/workflows/auto-tag.yml`
  - `.github/workflows/release-unsigned.yml`, `.github/workflows/integration-tests.yml`, `.github/workflows/main-distribution-pipeline.yml`
  - `scripts/setup_oci_linux.sh`, `scripts/setup_oci_macos.sh`, `scripts/setup_oci_windows.ps1`, `scripts/test_integration.sh`, `scripts/bump_duckdb_version.sh`
  - `extension_config.cmake`, `CMakeLists.txt`, `Makefile`, `description.yml`, `LICENSE`
- **Out of Scope**:
  - C++ runtime source files (Chapters 1–3).
  - Test `.test` files and documentation/OKF files (Chapter 5: `test-suite-docs-alignment`).

## Implementation Plan

### Phase 1: GitHub Actions Workflows, OCI Setup/Test Scripts, Build Config & License Metadata

- [x] **Task 1.1: Fix duckdb-update-check.yml daily PR churn, script injection, and auto-tag.yml exact tag check** -> [tasks/1.1.md](tasks/1.1.md)
  - **Objective**: Remediate `H-24` (`duckdb-update-check.yml` part) and `M-23` (`duckdb-update-check.yml` and `auto-tag.yml` parts) in `.github/workflows/duckdb-update-check.yml` and `.github/workflows/auto-tag.yml`.
  - **Verification Strategy**: `Verify workflow YAML syntax and shell logic via python3/actionlint`
  - **Files**: `.github/workflows/duckdb-update-check.yml`, `.github/workflows/auto-tag.yml`
- [x] **Task 1.2: Harden release-unsigned.yml artifact count & smoke test gates, fix integration-tests.yml OCI paths, and upgrade deprecated actions** -> [tasks/1.2.md](tasks/1.2.md)
  - **Objective**: Remediate `H-24` (`release-unsigned.yml` and `integration-tests.yml` parts), `M-23` (`release-unsigned.yml` part), and `M-24` across `.github/workflows/release-unsigned.yml`, `.github/workflows/integration-tests.yml`, and `.github/workflows/main-distribution-pipeline.yml`.
  - **Verification Strategy**: `Verify workflow YAML syntax via python3`
  - **Depends On**: `ci-build-release-hardening:1.1`
  - **Files**: `.github/workflows/release-unsigned.yml`, `.github/workflows/integration-tests.yml`, `.github/workflows/main-distribution-pipeline.yml`
- [x] **Task 1.3: Fix GITHUB_ENV library path expansion, macOS OCI version/DMG cleanup, and portable sed/sort in shell scripts** -> [tasks/1.3.md](tasks/1.3.md)
  - **Objective**: Remediate `M-25` and `M-26` (scripts part) across `scripts/setup_oci_linux.sh`, `scripts/setup_oci_macos.sh`, `scripts/setup_oci_windows.ps1`, `scripts/test_integration.sh`, and `scripts/bump_duckdb_version.sh`.
  - **Verification Strategy**: `Run bash -n on all shell scripts`
  - **Depends On**: `ci-build-release-hardening:1.2`
  - **Files**: `scripts/setup_oci_linux.sh`, `scripts/setup_oci_macos.sh`, `scripts/setup_oci_windows.ps1`, `scripts/test_integration.sh`, `scripts/bump_duckdb_version.sh`
- [x] **Task 1.4: Align description.yml license with LICENSE, configure extension_config.cmake TEST_DIR, and harden CMakeLists.txt & Makefile** -> [tasks/1.4.md](tasks/1.4.md)
  - **Objective**: Remediate `H-24` (license part), `M-26` (CMake/Makefile part), and `L-10` in `extension_config.cmake`, `CMakeLists.txt`, `Makefile`, `description.yml`, and `LICENSE`.
  - **Verification Strategy**: `tdd`
  - **Depends On**: `ci-build-release-hardening:1.3`
  - **Files**: `extension_config.cmake`, `CMakeLists.txt`, `Makefile`, `description.yml`, `LICENSE`

## Continuity Snapshot

- **Active Flow / Lifecycle**: `ci-build-release-hardening` / `planned`
- **Current Task & Claimant**: `null`
- **Last Verified Checkpoint**: `null`
- **Decisions**:
  - Align `description.yml` `license: MIT` with `LICENSE` (MIT License, Copyright 2025 Cody Fincher).
  - Skip closing/recreating `duckdb-update` PRs when an open PR for `feat/duckdb-v${NEW_VERSION}` already exists.
- **Recent Discoveries**:
  - `setup_oci_linux.sh` and `setup_oci_macos.sh` wrote literal `\$LD_LIBRARY_PATH` to `$GITHUB_ENV`, which GitHub Actions does not expand when parsing `GITHUB_ENV`.
- **Blockers & Unblock Conditions**: `none`
- **Next Exact Step**: Execute `ci-build-release-hardening:1.1`.
- **Plan Identity**: `plan_revision: 1`, `plan_commit: null`
- **State Identity**: `state_revision: 0`, `last_operation: null`, `operation_targets: []`
- **Relevant Knowledge Paths**:
  - `.agents/bundles/knowledge/standards/project-management.md`
  - `.agents/bundles/knowledge/standards/development-discipline.md`

## Verification Evidence

None yet (`state: planned`).
