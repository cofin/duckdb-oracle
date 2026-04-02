# DuckDB Version Update - Executive Summary

**Created**: 2025-12-14
**Status**: Planning Complete
**Priority**: P0 (Critical)
**Estimated Time**: 13-18 hours

---

## The Problem

The DuckDB Oracle extension is currently pinned to **DuckDB v1.4.1**, missing two important patch releases:
- **v1.4.2** (November 12, 2025) - Bugfixes and performance optimizations
- **v1.4.3** (December 9, 2025) - Latest LTS with Windows Arm64 support

**Why the auto-update didn't work:**
The automated version monitoring system requires a GitHub Personal Access Token (`WORKFLOW_UPDATE_TOKEN`) to modify workflow files, which is missing from the repository secrets. Without it, the daily check silently fails.

---

## The Solution

### High-Level Plan

1. **Update DuckDB** from v1.4.1 → v1.4.3 LTS
2. **Fix Workflows** to use modern GitHub Actions syntax
3. **Remove Redundancy** by consolidating duplicate CI workflows
4. **Document Setup** so future auto-updates work correctly

### What Gets Changed

**Git Submodules:**
- `duckdb` → checkout v1.4.3 tag
- `extension-ci-tools` → update to v1.4.3 or main

**Workflow Files:**
- `.github/workflows/main-distribution-pipeline.yml` - Update version pins (4 lines)
- `.github/workflows/duckdb-update-check.yml` - Fix deprecated Python syntax
- `.github/workflows/oracle-ci.yml` - Remove (redundant with main pipeline)

**Documentation:**
- `README.md` - Update version references
- `CONTRIBUTING.md` - Add PAT token setup guide
- `docs/COMPATIBILITY.md` - Add v1.4.3 entry
- `docs/SETUP.md` - New file for GitHub Actions secrets
- `docs/RELEASE.md` - Update automated process docs

---

## Timeline & Phases

### Phase 1: Planning ✅ (Complete)
**Duration**: 2 hours
**Output**: PRD, task list, recovery guide

### Phase 2: Research (Expert Agent)
**Duration**: 4-5 hours
**Tasks**:
- Review DuckDB v1.4.2/v1.4.3 release notes
- Test local build with v1.4.3
- Run integration tests
- Document findings

### Phase 3: Implementation (Expert Agent)
**Duration**: 3-4 hours
**Tasks**:
- Update submodules
- Update workflow files
- Fix Python syntax
- Remove redundant workflow
- Create PR

### Phase 4: Testing (Testing Agent - Auto)
**Duration**: 3-4 hours
**Tasks**:
- Monitor CI builds (4 platforms)
- Test auto-update workflow (simulated)
- Dry-run release workflow
- Verify integration tests

### Phase 5: Documentation (Docs Agent - Auto)
**Duration**: 2-3 hours
**Tasks**:
- Update all documentation
- Create PAT setup guide
- Update compatibility matrix

### Phase 6: Merge & Archive (Docs Agent - Auto)
**Duration**: 1-2 hours
**Tasks**:
- Final quality gate
- Merge PR
- Tag release
- Archive workspace

**Total**: 13-18 hours

---

## Key Findings from Investigation

### 5 GitHub Actions Workflows Analyzed

1. **main-distribution-pipeline.yml** ✅ Keep
   - Primary build for all platforms
   - Calls DuckDB extension-ci-tools
   - **Action**: Update version pins to v1.4.3

2. **release-unsigned.yml** ✅ Keep
   - Triggered on git tags (v*)
   - Builds releases for all platforms
   - Deploys to GitHub Pages
   - **Action**: No changes needed (reads from submodule)

3. **duckdb-update-check.yml** ⚠️ Fix
   - Auto-detects new DuckDB versions daily
   - **Broken**: Missing `WORKFLOW_UPDATE_TOKEN` secret
   - **Broken**: Uses deprecated `::set-output` syntax
   - **Action**: Fix Python syntax, document PAT requirement

4. **auto-tag.yml** ✅ Keep
   - Auto-creates tags on merge to main
   - **Action**: No changes needed

5. **oracle-ci.yml** ❌ Remove
   - Redundant with main-distribution-pipeline
   - Wastes CI minutes
   - **Action**: Delete file

### Critical Missing Secret

**`WORKFLOW_UPDATE_TOKEN`** (GitHub Personal Access Token)
- **Required By**: `duckdb-update-check.yml`, `auto-tag.yml`
- **Purpose**: Allow workflows to modify `.github/workflows/` files
- **Scopes**: `repo`, `workflow`
- **Setup**: User must create and add to repository secrets
- **Impact**: Auto-updates won't work until this is configured

---

## Success Criteria

**Build & Test:**
- [ ] All 4 platforms build successfully (Linux x64/ARM64, macOS ARM64, Windows x64)
- [ ] All unit tests pass
- [ ] All integration tests pass
- [ ] No performance regressions

**Workflows:**
- [ ] Auto-update workflow can detect and create PR for new versions
- [ ] Auto-tag workflow creates tags on merge
- [ ] Release workflow generates artifacts and publishes release

**Documentation:**
- [ ] All version references updated to v1.4.3
- [ ] PAT setup guide complete with screenshots
- [ ] Compatibility matrix current

---

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| **Breaking API changes in v1.4.3** | Review release notes thoroughly, test locally first |
| **CI tools v1.4.3 branch doesn't exist** | Fall back to `main` branch or stay on v1.4.1 tools |
| **Integration tests fail** | Fix compatibility issues, may need OCI client update |
| **User doesn't set PAT token** | Document clearly, provide manual update process |

---

## Quick Start for Expert Agent

```bash
# Read the full PRD
cat /home/cody/code/other/duckdb-oracle/specs/active/duckdb-version-update/prd.md

# Read task breakdown
cat /home/cody/code/other/duckdb-oracle/specs/active/duckdb-version-update/tasks.md

# Start Phase 2: Research
# Task 2.1: Review DuckDB v1.4.2 and v1.4.3 release notes
```

**First Research Question**: Are there any breaking changes in DuckDB v1.4.2 or v1.4.3 that affect extensions?

---

## Files in This Workspace

```
specs/active/duckdb-version-update/
├── prd.md                    # Full Product Requirements Document (12 sections)
├── tasks.md                  # Detailed task breakdown (25 tasks, 6 phases)
├── recovery.md               # Session resume guide for any agent
├── SUMMARY.md                # This file - executive summary
├── research/                 # Research findings (to be populated)
│   └── .gitkeep
└── tmp/                      # Temporary files
    └── .gitkeep
```

---

## External References

**DuckDB Releases:**
- [v1.4.3 Announcement](https://duckdb.org/2025/12/09/announcing-duckdb-143)
- [v1.4.2 Announcement](https://duckdb.org/2025/11/12/announcing-duckdb-142)
- [Release Calendar](https://duckdb.org/release_calendar)

**Extension Resources:**
- [Extension CI Tools](https://github.com/duckdb/extension-ci-tools)
- [Extension Template](https://github.com/duckdb/extension-template)

**GitHub Actions:**
- [Environment Files](https://docs.github.com/en/actions/using-workflows/workflow-commands-for-github-actions#environment-files)
- [Personal Access Tokens](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/creating-a-personal-access-token)

---

**Status**: Ready for Expert Agent to begin Phase 2
**Next Task**: Task 2.1 - Review DuckDB v1.4.2 and v1.4.3 release notes
