# CI Fix - Task List

**Workspace**: `specs/active/ci-fix/`
**Created**: 2025-11-23
**Status**: In Progress

## Overview
Fix GitHub Actions CI failures by properly organizing tests and enhancing integration test script to work seamlessly in both local and CI environments.

## Root Cause
- Integration tests requiring Oracle database running in CI without container
- Tests not properly separated (unit vs integration)
- Integration script needs enhancement for CI use

## Tasks

### Phase 1: Test Organization ✅
- [x] Create `test/integration/oracle/` directory
- [x] Move `advanced_edge_cases.test` to integration
- [x] Move `advanced_execute_integration.test` to integration
- [x] Move `advanced_metadata_integration.test` to integration
- [x] Move `advanced_schema_resolution_integration.test` to integration
- [x] Update `Makefile` to separate unit and integration test targets

### Phase 2: Enhance Integration Script ✅
- [x] Analyze current `scripts/test_integration.sh` implementation
- [x] Add `--cleanup` flag (default: true locally, false in CI)
- [x] Add `--keep-container` flag for debugging
- [x] Add trap handlers for proper cleanup on exit
- [x] Update script to run tests from `test/integration/`
- [x] Update `Makefile` integration target

### Phase 3: GitHub Actions Integration ✅
- [x] Review current `.github/workflows/OracleCI.yml`
- [x] Update workflow to run both unit and integration tests
- [x] Configure proper step names and comments
- [x] Leverage existing CI environment variable

### Phase 4: Documentation & Validation ✅
- [x] Update `README.md` with test categories
- [x] Run `make test` locally (unit tests) - 13 tests passed
- [x] Verify integration test discovery - 4 tests found
- [x] Update task tracking file
- [ ] Commit changes
- [ ] Verify CI passes
- [ ] Archive workspace

## Progress Log

### 2025-11-23 - Initial Setup
- Created workspace directory
- Created task tracking file

### 2025-11-23 - Implementation Complete
- Moved 4 integration tests to `test/integration/oracle/`
- Updated Makefile to exclude integration tests from `make test`
- Enhanced `scripts/test_integration.sh` with:
  - `--keep-container` and `--no-cleanup` flags
  - Better cleanup handling with exit code detection
  - Support for running tests from `test/integration/` directory
- Updated `.github/workflows/OracleCI.yml` to run both unit and integration tests
- Added comprehensive Testing section to README.md
- Validated unit tests: 13 tests, 116 assertions passed
- Validated integration test discovery: 4 tests found correctly
