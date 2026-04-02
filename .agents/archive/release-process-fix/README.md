# Release Process Fix - Project Overview

**Status**: Phase 1 Complete ✅ | Ready for Phase 2 (Expert Research)
**Created**: 2025-12-05
**Priority**: Critical
**Type**: Bug Fix

---

## Quick Summary

Two critical defects in release automation:

1. **Only 1 artifact uploads instead of 5** - Root cause: filename collision during merge
2. **Workflow update fails** - Root cause: missing workflows permission

**Impact**: Cannot distribute multi-platform extension, cannot automate dependency updates

---

## Project Structure

```
specs/active/release-process-fix/
├── README.md           # This file - project overview
├── prd.md              # Complete PRD (15 pages, detailed analysis)
├── tasks.md            # Phase-by-phase task breakdown
├── recovery.md         # Session recovery guide for any agent
├── research/           # Research findings (Phase 2)
│   └── .gitkeep
└── tmp/                # Temporary files
    └── .gitkeep
```

---

## Files to Modify

### Primary Changes
- `.github/workflows/release-unsigned.yml` - Fix artifact structure
- `.github/workflows/duckdb-update-check.yml` - Add PAT usage

### Documentation (Phase 6)
- `docs/release/pat-setup.md` (new)
- `docs/release/manual-release.md` (new)
- `docs/troubleshooting.md` (update)

---

## Root Causes Explained

### Problem 1: Single Artifact
```yaml
# All 5 builds write to SAME path:
deploy/oracle.duckdb_extension

# When merged with merge-multiple: true:
artifacts/oracle.duckdb_extension  # Last build overwrites all others
```

**Fix**: Use unique filenames per platform
```yaml
deploy/release/oracle-linux_amd64.duckdb_extension
deploy/release/oracle-linux_arm64.duckdb_extension
deploy/release/oracle-osx_arm64.duckdb_extension
deploy/release/oracle-windows_amd64.duckdb_extension
```

### Problem 2: Workflow Permission
```
Error: refusing to allow a GitHub App to create or update workflow
       `.github/workflows/main-distribution-pipeline.yml`
       without `workflows` permission
```

**Fix**: Use Personal Access Token (PAT) with workflows permission instead of GITHUB_TOKEN

---

## Implementation Phases

| Phase | Owner | Duration | Status |
|-------|-------|----------|--------|
| 1. Planning & Research | PRD Agent | 1 hour | ✅ Complete |
| 2. Expert Research | Expert Agent | 30-60 min | 🔜 Next |
| 3. Core Implementation | Expert Agent | 2-3 hours | Not Started |
| 4. Integration & Testing | Expert Agent | 1-2 hours | Not Started |
| 5. Testing | Testing Agent | 1 hour | Not Started |
| 6. Documentation | Docs & Vision | 1 hour | Not Started |
| 7. Quality Gate & Archive | Docs & Vision | 30 min | Not Started |

**Total Estimated Time**: 4-6 hours

---

## Key Decisions

1. **Use unique filenames** - Prevents artifact overwrite during merge
2. **Use PAT with workflows permission** - Enables automated workflow updates
3. **Preserve GitHub Pages structure** - Maintains backward compatibility
4. **Add checksums** - Improves security and integrity verification
5. **Add validation** - Prevents incomplete releases

---

## Prerequisites

### For Phase 3 Implementation
- [ ] **PAT Required**: Repository owner must create Personal Access Token
  - Permissions: `contents: write`, `workflows: write`, `pull_requests: write`
  - Store as repository secret: `WORKFLOW_UPDATE_TOKEN`
  - See PRD Appendix B for detailed steps

---

## Next Steps

### For Expert Agent
1. Read `prd.md` (comprehensive technical details)
2. Read `tasks.md` (detailed task list)
3. Start Phase 2: Research
   - Validate GitHub Actions v4 behavior
   - Confirm DuckDB platform naming
   - Research PAT permissions
   - Document findings in `research/`
4. Proceed to Phase 3: Implementation

### For Repository Owner
1. Review `prd.md` Section "Fix 2: Workflow Permission Error"
2. Create PAT following PRD Appendix B
3. Store as repository secret `WORKFLOW_UPDATE_TOKEN`
4. Notify Expert Agent when ready

---

## Success Criteria

Project complete when:
- ✅ 5 platform-specific files in GitHub Releases
- ✅ Automated DuckDB updates work without errors
- ✅ Documentation complete
- ✅ Manual fallback procedures documented

---

## Quick Links

- **Full PRD**: [prd.md](./prd.md) - Complete analysis and solutions
- **Task List**: [tasks.md](./tasks.md) - Phase-by-phase breakdown
- **Recovery Guide**: [recovery.md](./recovery.md) - How to resume work
- **AGENTS.md**: [/home/cody/code/other/duckdb-oracle/AGENTS.md](../../AGENTS.md) - Workflow system

---

## References

### Source Analysis
- [GitHub Actions upload-artifact v4 Migration](https://github.com/actions/upload-artifact/blob/main/docs/MIGRATION.md)
- [GitHub Actions Workflow Permissions](https://github.com/orgs/community/discussions/35410)
- [Refusing to allow GitHub App to update workflow](https://github.com/orgs/community/discussions/27072)

### DuckDB
- [Extension Distribution Guide](https://duckdb.org/docs/extensions/overview)
- [Platform Naming Conventions](https://github.com/duckdb/duckdb/blob/main/scripts/extension_distribution.py)

---

**Created by**: PRD Agent
**Phase 1 Complete**: 2025-12-05
**Ready for**: Expert Agent (Phase 2)
