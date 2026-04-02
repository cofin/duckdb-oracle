# PRD: Cleanup and Refactor

**Slug**: `cleanup-refactor`
**Date**: 2025-11-23
**Status**: Active

## Problem Statement
Following the successful implementation of CI fixes and the Array Fetch feature, the codebase requires cleanup. Integration test logs are too verbose, there may be unused code from previous iterations, and test filenames use inconsistent "advanced" prefixes.

## Goals
1.  **Reduce CI Noise**: Suppress Oracle container logs in integration tests by default.
2.  **Code Hygiene**: Remove dead code, unused variables, and legacy workarounds.
3.  **Test Organization**: Rename integration tests to be descriptive and consistent.

## Acceptance Criteria

### 1. Integration Script Improvements
- [ ] `scripts/test_integration.sh` defaults to suppressing container logs.
- [ ] New flag `--show-logs` enables container logging.
- [ ] CI workflow (`.github/workflows/OracleCI.yml`) updated to use `--show-logs` only on failure (if possible) or keep it quiet unless debugging is needed. (Actually, user said "makes a lot of noise that isn't helpful", so default quiet in CI is good).

### 2. Code Cleanup
- [ ] Review `src/oracle_extension.cpp` and related files for unused functions/variables.
- [ ] Check for any commented-out code or "TODO" comments that are no longer relevant.
- [ ] Verify `OracleBindInternal` and `OracleInitGlobal` signatures and usage.

### 3. Test Renaming
- [ ] Rename `test/integration/oracle/advanced_edge_cases.test` -> `test/integration/oracle/edge_cases.test` (or similar).
- [ ] Rename `test/integration/oracle/advanced_execute_integration.test` -> `test/integration/oracle/execute.test`.
- [ ] Rename `test/integration/oracle/advanced_metadata_integration.test` -> `test/integration/oracle/metadata.test`.
- [ ] Rename `test/integration/oracle/advanced_schema_resolution_integration.test` -> `test/integration/oracle/schema_resolution.test`.
- [ ] Update `Makefile` or test runners if they rely on hardcoded names (DuckDB test runner usually uses globs, but checking `Makefile` is required).

## Out of Scope
- New features.
- Changes to core logic (unless dead code).

## Dependencies
- `scripts/test_integration.sh`
- `src/`
- `test/integration/oracle/`
