# Feature: DuckDB Version Update (v1.4.3) & Auto-Build Fix

**Status**: Planning
**Created**: 2025-12-14
**Last Updated**: 2025-12-24
**Priority**: P0 (Critical)
**Owner**: PRD Agent

## Intelligence Context (NEW)

**Complexity**: Medium
**Similar Features**:
- `.github/workflows/main-distribution-pipeline.yml` (Existing workflow pattern)
- `duckdb/` (Submodule management pattern)

**Patterns to Follow**:
- **Split-Pipeline Pattern**: Separate packaging (`main-distribution-pipeline`) from integration testing (`oracle-ci.yml`).
- **Triple-Point Update**: Synchronized updates of submodules, workflow configs, and CMake files.
- **Secret-Dependent Automation**: Using PATs for workflow modifications.

**Tool Selection**:
- Reasoning: `crash` (Used for redundancy analysis)
- Research: Web search (DuckDB release notes) + Codebase analysis

---

## 1. Overview

Update the DuckDB Oracle extension to the **latest DuckDB 1.4.3 LTS release** (released December 9, 2025) and repair the broken auto-build/release process. This update addresses critical stability fixes in the core engine and modernizes the CI infrastructure by fixing deprecated GitHub Actions syntax.

**Key Goals**:
1.  **Core Update**: Move DuckDB submodule from v1.4.1 -> v1.4.3 LTS.
2.  **Infrastructure Repair**: Fix `duckdb-update-check.yml` (deprecated syntax) and rename `oracle-ci.yml` to `integration-tests.yml` to reflect its true purpose.
3.  **Automation Enablement**: Document and require `WORKFLOW_UPDATE_TOKEN` to enable self-updating capabilities.

## 2. Problem Statement

### Current State Analysis
-   **Version Lag**: Pinned to v1.4.1, missing v1.4.2 and v1.4.3 fixes.
-   **Broken Automation**: The `duckdb-update-check.yml` workflow fails silently due to a missing Personal Access Token (PAT) and crashes on deprecated `::set-output` syntax.
-   **Architectural Confusion**: `oracle-ci.yml` appears redundant with `main-distribution-pipeline.yml` but actually performs unique integration testing (Split-Pipeline Pattern).

### Identified Issues
1.  **Substrait API Removal**: v1.4.3 removes Substrait APIs; need to verify no usage.
2.  **Deprecated Syntax**: `::set-output` will stop working soon.
3.  **Missing Secret**: `WORKFLOW_UPDATE_TOKEN` is required for the bot to commit workflow updates.

## 3. Acceptance Criteria

**Version Compliance**:
- [ ] DuckDB submodule points to `v1.4.3` tag.
- [ ] `extension-ci-tools` submodule points to `v1.4.3` tag (or main).
- [ ] `main-distribution-pipeline.yml` inputs set to `v1.4.3`.

**Workflow Integrity**:
- [ ] `duckdb-update-check.yml` uses `$GITHUB_OUTPUT` syntax.
- [ ] `oracle-ci.yml` is RENAMED to `integration-tests.yml` (not deleted).
- [ ] All 4 build platforms (Linux/Mac/Win) pass in `main-distribution-pipeline`.
- [ ] Integration tests (Oracle container) pass in `integration-tests.yml`.

**Pattern Compliance**:
- [ ] Follows "Split-Pipeline" pattern (Packaging vs Integration).
- [ ] Follows "Triple-Point" update pattern (Submodule + Workflow + Config).
- [ ] Documentation includes PAT setup for "Secret-Dependent Automation".

**Documentation**:
- [ ] `docs/COMPATIBILITY.md` updated.
- [ ] `docs/SETUP.md` created with PAT instructions.

## 4. Technical Design

### Affected Components
-   **Submodules**: `duckdb/`, `extension-ci-tools/`
-   **Workflows**:
    -   `.github/workflows/main-distribution-pipeline.yml`
    -   `.github/workflows/duckdb-update-check.yml`
    -   `.github/workflows/oracle-ci.yml` (Renamed)
-   **Documentation**: `README.md`, `docs/*`

### Implementation Approach

**Phase 1: Workflow Infrastructure (The Setup)**
1.  Rename `oracle-ci.yml` -> `integration-tests.yml` to clarify intent.
2.  Fix Python syntax in `duckdb-update-check.yml`.

**Phase 2: The Upgrade (The Core)**
1.  Update git submodules.
2.  Update versions in `main-distribution-pipeline.yml`.
3.  Verify no usage of Substrait API in `src/`.

**Phase 3: Validation (The Check)**
1.  Local build with `make release`.
2.  Local integration test with `make integration`.
3.  CI validation via PR.

### Code Samples (Pattern Alignment)

**GitHub Output Fix**:
```python
# Old (Anti-pattern)
print(f"::set-output name=new_version::{latest}")

# New (Correct Pattern)
with open(os.environ['GITHUB_OUTPUT'], 'a') as fh:
    print(f"new_version={latest}", file=fh)
```

**Workflow Version Pinning**:
```yaml
jobs:
  duckdb-stable-build:
    uses: duckdb/extension-ci-tools/.github/workflows/_extension_distribution.yml@v1.4.3
    with:
      duckdb_version: v1.4.3
      ci_tools_version: v1.4.3
```

## 5. Testing Strategy

### Unit Tests
-   Standard `make test` execution (sqllogic tests).
-   Must pass 100%.

### Integration Tests
-   `make integration` (runs `scripts/test_integration.sh`).
-   Spins up `gvenzl/oracle-free:23-slim`.
-   Verifies OCI connectivity and data type mapping with new DuckDB core.

### Edge Cases
-   **Substrait**: Ensure compilation doesn't fail due to missing symbols.
-   **Secret Absence**: Workflow should fail gracefully or warn if `WORKFLOW_UPDATE_TOKEN` is missing (documentation mitigation).

### Pattern Test Coverage
-   [ ] Verify `integration-tests.yml` still runs `make integration`.
-   [ ] Verify `main-distribution-pipeline.yml` uses the correct `extension-ci-tools` version.

## 6. Risks & Mitigations

-   **Risk**: `extension-ci-tools` v1.4.3 tag missing.
    -   **Mitigation**: Fallback to `main` branch or `v1.4.2` if compatible.
-   **Risk**: API Breakage (Substrait).
    -   **Mitigation**: Pre-implementation grep check.
-   **Risk**: Windows Arm64 incompatibility.
    -   **Mitigation**: Keep existing architecture exclusions for now.

## 7. Dependencies
-   DuckDB v1.4.3
-   Oracle Instant Client (handled by setup scripts)
-   GitHub Actions Runners

## 8. References
-   [DuckDB v1.4.3 Release Notes](https://duckdb.org/2025/12/09/announcing-duckdb-143)
-   [Research Plan](./research/plan.md)
-   [Pattern Analysis](./patterns/analysis.md)