# Recovery Guide: Release Process Fix

**Project**: Fix DuckDB Oracle Extension Release Process
**Created**: 2025-12-05
**Last Updated**: 2025-12-05
**Current Phase**: Phase 1 Complete ✅

---

## Quick Context

This project fixes two critical release process issues:

1. **Single Artifact Problem**: Only one extension file uploads to GitHub Releases instead of 5 platform-specific binaries
2. **Workflow Permission Error**: Automated DuckDB version updates fail with workflow permission denied

**Root Causes:**
- All platform builds write to same filename, last build overwrites others when merged
- Default GITHUB_TOKEN lacks `workflows` permission for modifying workflow files

**Solutions:**
- Use unique filenames per platform: `oracle-{platform}.duckdb_extension`
- Use Personal Access Token (PAT) with workflows permission

---

## Current Status

### Phase 1: Planning & Research ✅ COMPLETE
- **Completed**: 2025-12-05
- **Owner**: PRD Agent
- **Deliverables**:
  - ✅ Comprehensive PRD created: `specs/active/release-process-fix/prd.md`
  - ✅ Task breakdown created: `specs/active/release-process-fix/tasks.md`
  - ✅ Recovery guide created: `specs/active/release-process-fix/recovery.md`
  - ✅ Workspace structure initialized

### Phase 2: Expert Research 🔜 NEXT
- **Status**: Not Started
- **Owner**: Expert Agent
- **Estimated Duration**: 30-60 minutes
- **Blocking**: None (ready to start)

---

## Project Files

### Active Workspace
```
specs/active/release-process-fix/
├── prd.md                          # Complete PRD with technical details
├── tasks.md                        # Phased task breakdown
├── recovery.md                     # This file
├── research/                       # Research findings (Phase 2)
└── tmp/                            # Temporary files
```

### Files to Modify (Phase 3)
```
.github/workflows/
├── release-unsigned.yml            # Main fix - artifact structure
└── duckdb-update-check.yml         # Secondary fix - PAT usage

docs/release/                       # New documentation (Phase 6)
├── pat-setup.md                    # PAT creation guide
└── manual-release.md               # Fallback procedures
```

---

## How to Resume Work

### For Expert Agent (Phase 2: Research)

**Start Here:**
1. Read the PRD: `specs/active/release-process-fix/prd.md`
2. Read the task list: `specs/active/release-process-fix/tasks.md`
3. Begin Phase 2 research tasks

**Research Focus:**
1. **GitHub Actions v4 Artifacts**:
   - Confirm `merge-multiple: true` file overwrite behavior
   - Research: "GitHub Actions upload-artifact v4 merge behavior"
   - Document findings in `research/github-actions-artifacts.md`

2. **DuckDB Platform Naming**:
   - Check: `linux_amd64`, `linux_arm64`, `osx_arm64`, `windows_amd64`
   - Verify against DuckDB's extension distribution code
   - Document in `research/duckdb-platforms.md`

3. **PAT Permissions**:
   - Minimum required: `contents: write`, `workflows: write`, `pull_requests: write`
   - Research fine-grained token scoping
   - Document in `research/pat-configuration.md`

4. **Edge Cases**:
   - Why macOS Intel excluded from build matrix?
   - Any artifact size limits?
   - Document in `research/edge-cases.md`

**Research Deliverables:**
- Create directory: `specs/active/release-process-fix/research/`
- Document findings in markdown files
- Update PRD if new information discovered
- Confirm or adjust implementation approach

**Next Steps After Research:**
- Review findings
- Validate proposed solutions
- Proceed to Phase 3 implementation if approved

### For Expert Agent (Phase 3: Implementation)

**Prerequisites:**
- Phase 2 research complete
- PAT created and stored (if not available, document as blocking)

**Implementation Order:**

1. **Start with Fix 1 (Multi-Platform Artifacts)**:
   - Can implement and test independently
   - No external dependencies
   - File: `.github/workflows/release-unsigned.yml`

2. **Then Fix 2 (Workflow Permissions)**:
   - Requires PAT to be created first
   - File: `.github/workflows/duckdb-update-check.yml`

**Key Files to Modify:**

```yaml
# .github/workflows/release-unsigned.yml
Lines to change:
- 172-201: Artifact preparation (add unique filenames)
- 203-207: Upload artifact paths
- 263-286: Release notes generation (add platform table)
- 296: Release files pattern (point to release/ subdirectory)

# .github/workflows/duckdb-update-check.yml
Lines to change:
- 24: Add token parameter to checkout
- 123: Replace GITHUB_TOKEN with WORKFLOW_UPDATE_TOKEN
- 164: Verify PAT used for PR creation
```

**Testing Strategy:**
1. Use `workflow_dispatch` for safe testing
2. Create test tag: `v0.0.0-test-release-fix`
3. Monitor workflow execution
4. Download artifacts and verify structure
5. Invoke Testing Agent after successful test

**Invoke Testing Agent:**
After Phase 4 testing complete, invoke Testing Agent to create automated tests.

### For Testing Agent (Phase 5: Testing)

**When to Start:**
Expert Agent will invoke you after Phase 4 integration testing completes successfully.

**Context to Review:**
1. Read PRD: `specs/active/release-process-fix/prd.md`
2. Review implementation changes in `.github/workflows/`
3. Check test results from Phase 4

**Testing Focus:**
1. **Workflow Validation Tests**:
   - Artifact structure validation
   - Checksum generation validation
   - Platform naming validation

2. **Documentation**:
   - Manual testing procedures
   - Verification checklists
   - Rollback scenarios

**Automatically Invoke Docs & Vision Agent:**
After completing test creation and documentation, invoke Docs & Vision Agent for Phase 6.

### For Docs & Vision Agent (Phase 6-7: Documentation & Quality Gate)

**When to Start:**
Testing Agent will invoke you after Phase 5 completes.

**Documentation Tasks:**
1. **Release Documentation** (`docs/release/`):
   - Multi-platform artifact structure
   - Platform-specific download instructions
   - Custom repository installation

2. **PAT Management** (`docs/release/pat-setup.md`):
   - Step-by-step PAT creation
   - Permission requirements
   - Rotation schedule and procedure

3. **Manual Release Guide** (`docs/release/manual-release.md`):
   - Manual build process
   - Fallback procedures
   - Recovery steps

4. **Troubleshooting Updates** (`docs/troubleshooting.md`):
   - Artifact count mismatch
   - Checksum failures
   - PAT expiration
   - Permission errors

**Quality Gate Checklist:**
Review all acceptance criteria from PRD Section "Acceptance Criteria"

**Knowledge Capture:**
1. Create `specs/guides/release-workflow-patterns.md`
2. Document artifact handling patterns
3. Document workflow permission patterns

**Archive Workspace:**
After quality gate passes:
1. Update this recovery.md with completion notes
2. Move workspace to `specs/archive/release-process-fix/`
3. Update archive index

---

## Key Decisions Made

### Decision 1: Use Unique Filenames Per Platform
**Context**: GitHub Actions v4 merge-multiple overwrites files with same path
**Options Considered**:
- A: Separate artifacts per platform (no merge)
- B: Unique filenames in single merged artifact ✅ CHOSEN
- C: Use artifact matrix naming

**Chosen**: Option B
**Rationale**:
- Simpler release job (single download step)
- Compatible with GitHub Pages structure
- Matches DuckDB community extension patterns

### Decision 2: Use PAT for Workflow Updates
**Context**: GITHUB_TOKEN lacks workflows permission
**Options Considered**:
- A: Personal Access Token (PAT) with workflows scope ✅ CHOSEN
- B: Remove workflow file modifications (manual updates)
- C: Split config into separate file

**Chosen**: Option A
**Rationale**:
- Maintains full automation
- Explicit permission model
- Auditable and revocable
- Industry standard approach

### Decision 3: Preserve GitHub Pages Structure
**Context**: Don't break existing custom repository installation
**Decision**: Keep separate paths for Pages vs Release
- GitHub Pages: `v{version}/{platform}/oracle.duckdb_extension.gz`
- GitHub Release: `release/oracle-{platform}.duckdb_extension`

---

## Critical Information

### The Artifact Overwrite Bug

**Why Only One File Appears:**
```yaml
# In build-matrix job (runs 5 times in parallel):
deploy/oracle.duckdb_extension  # SAME PATH for all builds!

# When downloaded with merge-multiple: true:
artifacts/oracle.duckdb_extension  # Last build wins, others lost
```

**The Fix:**
```yaml
# In build-matrix job:
deploy/release/oracle-linux_amd64.duckdb_extension     # Unique!
deploy/release/oracle-linux_arm64.duckdb_extension     # Unique!
deploy/release/oracle-osx_arm64.duckdb_extension       # Unique!
deploy/release/oracle-windows_amd64.duckdb_extension   # Unique!

# When downloaded with merge-multiple: true:
artifacts/release/
├── oracle-linux_amd64.duckdb_extension
├── oracle-linux_arm64.duckdb_extension
├── oracle-osx_arm64.duckdb_extension
└── oracle-windows_amd64.duckdb_extension  # All present!
```

### The Workflow Permission Issue

**Why It Fails:**
- Workflow file: `.github/workflows/main-distribution-pipeline.yml`
- Modifying workflows requires `workflows` permission
- `GITHUB_TOKEN` only has `contents: write`
- Security feature to prevent workflow injection attacks

**The Fix:**
```yaml
# Create PAT with workflows permission
# Store as: WORKFLOW_UPDATE_TOKEN

# Use in checkout:
- uses: actions/checkout@v4
  with:
    token: ${{ secrets.WORKFLOW_UPDATE_TOKEN }}  # Not GITHUB_TOKEN

# Use in git operations:
env:
  GITHUB_TOKEN: ${{ secrets.WORKFLOW_UPDATE_TOKEN }}
```

---

## Validation Checklist

Before marking phase complete, verify:

### Phase 2 Complete When:
- [ ] GitHub Actions v4 behavior documented
- [ ] DuckDB platform naming confirmed
- [ ] PAT permissions validated
- [ ] Edge cases identified
- [ ] Research findings reviewed

### Phase 3 Complete When:
- [ ] `.github/workflows/release-unsigned.yml` modified
- [ ] `.github/workflows/duckdb-update-check.yml` modified
- [ ] Checksum generation added
- [ ] Artifact validation added
- [ ] Release notes improved
- [ ] All code changes tested locally

### Phase 4 Complete When:
- [ ] workflow_dispatch test successful
- [ ] All 5 platform artifacts built
- [ ] Artifact structure validated
- [ ] GitHub Pages deployment verified
- [ ] Test release created and verified
- [ ] Extensions load on test platforms

### Phase 5 Complete When:
- [ ] Workflow validation tests created
- [ ] Manual testing procedures documented
- [ ] Rollback scenarios tested
- [ ] Docs & Vision Agent invoked

### Phase 6 Complete When:
- [ ] Release documentation updated
- [ ] PAT setup guide created
- [ ] Manual release guide created
- [ ] Troubleshooting guide updated
- [ ] Quality gate checklist run

### Phase 7 Complete When:
- [ ] All acceptance criteria verified
- [ ] Knowledge captured in guides
- [ ] Workspace archived
- [ ] Project marked complete

---

## Common Issues & Solutions

### Issue: Build Matrix Fails
**Symptoms**: Not all platforms build successfully
**Check**:
- Oracle Instant Client setup scripts for each platform
- Platform-specific dependencies installed
- Runner availability (especially ARM runners)

**Solution**:
- Review workflow logs for specific platform
- Check setup_oci_{platform}.sh scripts
- Verify dependencies in workflow

### Issue: Artifacts Still Overwrite
**Symptoms**: Only one file in artifacts/release/ after download
**Check**:
- Unique filenames used in prepare step
- Correct path in upload-artifact
- merge-multiple: true in download-artifact

**Solution**:
- Verify each build creates unique filename
- Check deploy/release/ directory in logs
- Validate artifact upload includes release/ subdirectory

### Issue: PAT Authentication Fails
**Symptoms**: Push fails with authentication error
**Check**:
- PAT stored as correct secret name
- PAT has workflows permission
- Checkout uses token parameter
- PAT not expired

**Solution**:
- Verify secret name: WORKFLOW_UPDATE_TOKEN
- Recreate PAT with correct permissions
- Update checkout action configuration
- Check PAT expiration date

### Issue: Checksum Mismatch
**Symptoms**: sha256sum verification fails
**Check**:
- Checksums generated after file creation
- No file modifications after checksum generation
- Correct directory used for checksum command

**Solution**:
- Regenerate checksums
- Verify file integrity
- Check for compression issues

---

## Reference Links

### Documentation
- [PRD](./prd.md) - Complete Product Requirements Document
- [Tasks](./tasks.md) - Detailed task breakdown
- [AGENTS.md](/home/cody/code/other/duckdb-oracle/AGENTS.md) - Agent workflow system

### Workflows to Modify
- [release-unsigned.yml](../../.github/workflows/release-unsigned.yml)
- [duckdb-update-check.yml](../../.github/workflows/duckdb-update-check.yml)

### External References
- [GitHub Actions upload-artifact v4 Migration](https://github.com/actions/upload-artifact/blob/main/docs/MIGRATION.md)
- [GitHub Actions Workflow Permissions](https://github.com/orgs/community/discussions/35410)
- [DuckDB Extension Distribution](https://duckdb.org/docs/extensions/overview)

---

## Contact & Escalation

### Need Help?
1. **Read PRD First**: Most questions answered in `prd.md`
2. **Check Tasks**: Detailed steps in `tasks.md`
3. **Review AGENTS.md**: Workflow and patterns documented

### Blockers?
- **Missing PAT**: Repository owner must create (see PRD Appendix B)
- **Workflow Permission**: Check GitHub repository settings
- **Runner Access**: Verify GitHub Actions enabled for all platforms

---

## Success Criteria

This project is complete when:
1. ✅ GitHub Release shows 5 platform-specific extension files
2. ✅ Automated DuckDB version updates work without permission errors
3. ✅ Documentation complete and accurate
4. ✅ Manual fallback procedures documented
5. ✅ Knowledge captured in project guides
6. ✅ Workspace archived

---

## Version History

- **v1.0** (2025-12-05): Initial creation after Phase 1 complete
  - PRD created with root cause analysis
  - Tasks defined with 7-phase approach
  - Recovery guide established

---

**Next Agent**: Expert Agent (Phase 2: Research)
**Ready to Start**: Yes (no blockers)
**Estimated Time to Complete**: 4-6 hours total (all phases)
