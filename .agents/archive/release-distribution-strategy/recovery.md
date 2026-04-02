# Recovery Guide: Release and Distribution Strategy

**Last Updated**: 2025-11-22
**Current Phase**: Phase 1 - Planning & Research ✅

## Quick Context

This workspace contains the PRD and implementation plan for establishing a comprehensive release and distribution strategy for the DuckDB Oracle extension. The strategy covers three interconnected initiatives:

1. **Unsigned Release Process** - GitHub Release workflow for testing binaries
2. **Automated DuckDB Updates** - Monitoring and compatibility testing
3. **Community Extension Submission** - Official DuckDB repository listing

## Current Status

### Completed

- [x] Requirements analysis
- [x] Research on DuckDB community extension process
- [x] Research on GitHub Actions matrix builds
- [x] PRD creation (prd.md)
- [x] Task breakdown (tasks.md)
- [x] Recovery guide (this file)

### In Progress

- [ ] None (ready for Expert Agent to begin implementation)

### Next Steps

**Immediate**: Expert Agent should begin Phase 1, Task 1.1 (Design Release Workflow Structure)

## Files in This Workspace

```
specs/active/release-distribution-strategy/
├── prd.md          # Comprehensive Product Requirements Document
├── tasks.md        # Detailed task breakdown by phase
├── recovery.md     # This file - session resume guide
├── research/       # Research findings (to be populated)
└── tmp/            # Temporary files (to be populated)
```

## Key Findings from Research

### DuckDB Community Extensions

**Source**: [DuckDB Community Extensions Documentation](https://duckdb.org/community_extensions/)

- Extensions submitted via PR to [duckdb/community-extensions](https://github.com/duckdb/community-extensions)
- Single file required: `extensions/{name}/description.yml`
- Community CI automatically builds, signs, and distributes extensions
- Extensions are signed by DuckDB CI, not by maintainer
- No manual key management required

**description.yml Format**:
- `extension` section: name, description, version, language, build, license, maintainers
- `repo` section: github, ref (git tag/commit)
- `docs` section: hello_world, extended_description, functions, settings
- `build` section: before_build script, cmake_flags, test_command

### GitHub Actions Matrix Builds

**Source**: [GitHub Actions Matrix Strategy](https://runs-on.com/github-actions/the-matrix-strategy/)

- Matrix builds run jobs in parallel across different configurations
- Common matrix dimensions: OS, architecture, language version
- Each matrix combination runs as separate job
- Artifacts can be uploaded from each matrix job
- Release job can download all artifacts and create unified release

### DuckDB Extension CI Tools

**Source**: [duckdb/extension-ci-tools](https://github.com/duckdb/extension-ci-tools)

- Reusable workflows for building DuckDB extensions
- Handles multi-platform builds automatically
- Current project already uses v1.4.1 in main-distribution-pipeline.yml
- Provides extension distribution pipeline

## Architecture Decisions

### Decision 1: Unsigned vs Community Releases

**Decision**: Implement both
- Unsigned releases for rapid testing and development
- Community extension for production use
- Unsigned releases allow testing before community submission
- Users get choice of convenience (community) vs latest features (unsigned)

**Rationale**: Enables fast iteration while working toward production-ready distribution

### Decision 2: DuckDB Version Monitoring

**Decision**: Automated daily checks with manual review
- Auto-detect new DuckDB versions daily
- Auto-create test branch and trigger builds
- Create issue for maintainer review (not auto-merge)
- Maintainer approves compatibility before merging

**Rationale**: Balance automation with safety - DuckDB API changes can be breaking

### Decision 3: Platform Coverage

**Decision**: Support 5 platforms initially
- Linux x86_64
- Linux aarch64
- macOS x86_64
- macOS arm64
- Windows x86_64

**Excluded**: WebAssembly (Oracle Instant Client incompatible), macOS amd64 (already excluded in main-distribution-pipeline.yml)

**Rationale**: Cover most common platforms while respecting technical constraints

### Decision 4: Version Strategy

**Decision**: Semantic versioning (semver)
- MAJOR.MINOR.PATCH (e.g., v1.2.3)
- Major: Breaking changes or DuckDB major version updates
- Minor: New features, backward compatible
- Patch: Bug fixes, backward compatible
- Pre-release tags: v1.0.0-beta.1, v1.0.0-rc.1

**Rationale**: Industry standard, clear semantics for users

## Agent-Specific Instructions

### For Expert Agent

**If resuming Phase 1 (Unsigned Release Workflow)**:

1. Read prd.md sections:
   - "Phase 1: Unsigned Release Process"
   - "Acceptance Criteria - Phase 1"
2. Read tasks.md "Phase 1" section
3. Start with Task 1.1: Create `.github/workflows/release-unsigned.yml`
4. Reference existing `.github/workflows/main-distribution-pipeline.yml` for patterns
5. Reference existing `scripts/setup_oci_*.sh` for Oracle Instant Client setup

**Key Files to Reference**:
- `.github/workflows/main-distribution-pipeline.yml` - Existing CI workflow
- `.github/workflows/oracle-ci.yml` - Linux build example
- `scripts/setup_oci_linux.sh` - Oracle Instant Client setup for Linux
- `scripts/setup_oci_macos.sh` - Oracle Instant Client setup for macOS
- `scripts/setup_oci_windows.ps1` - Oracle Instant Client setup for Windows
- `CMakeLists.txt` - Build configuration
- `Makefile` - Build commands

**Testing Approach**:
- Use workflow_dispatch for manual testing
- Create test tag (v0.1.0-test) for validation
- Test locally with DuckDB -unsigned before creating real release
- Delete test releases after validation

**If resuming Phase 2 (DuckDB Update Automation)**:

1. Read prd.md section "Phase 2: Automated DuckDB Version Updates"
2. Read tasks.md "Phase 2" section
3. Start with Task 2.1: Create `.github/workflows/duckdb-update-check.yml`
4. Use GitHub API to query duckdb/duckdb releases
5. Create `.github/duckdb-update-config.yml` for configuration

**Key Considerations**:
- Use GitHub CLI (gh) or REST API for version checking
- Semver comparison for version analysis
- Branch naming: `feat/duckdb-v{version}`
- Issue templates in tasks.md

### For Testing Agent

**If resuming Phase 3 (Community Extension Preparation)**:

Your role: Enhance test coverage before community submission

1. Read prd.md section "Phase 3: Community Extension Submission - Pre-Submission Checklist"
2. Read tasks.md Task 3.3 "Enhance Test Coverage"
3. Review existing tests in `test/sql/*.test`
4. Add edge case tests:
   - NULL values in all Oracle column types
   - Empty result sets
   - Large result sets (>1M rows)
   - Connection timeout scenarios
   - Invalid credentials
5. Ensure integration tests cover all features

**Testing Framework**:
- DuckDB SQL test runner (`.test` files)
- Integration tests with containerized Oracle (`scripts/test_integration.sh`)
- Run tests with `make test` and `make integration`

### For Docs & Vision Agent

**If resuming Phase 3 (Community Extension Preparation)**:

Your role: Complete documentation requirements and create description.yml

1. Read prd.md section "Phase 3: Community Extension Submission"
2. Read tasks.md Tasks 3.1 and 3.6
3. Create missing documentation:
   - `CONTRIBUTING.md` - Contribution guidelines
   - `SECURITY.md` - Security policy
4. Create `description.yml` following schema in prd.md
5. Review against example: [httpserver/description.yml](https://github.com/duckdb/community-extensions/blob/main/extensions/httpserver/description.yml)

**Documentation Structure**:
- CONTRIBUTING.md: Setup, testing, code style, PR process
- SECURITY.md: Vulnerability reporting, supported versions, update policy
- description.yml: See PRD for complete schema and example

**If resuming Phase 4 (Community Extension Submission)**:

Your role: Create PR description and respond to feedback

1. Read prd.md section "Phase 3: Community Extension Submission - Submission Process"
2. Read tasks.md Task 4.4 "Create Pull Request"
3. Use PR description template in tasks.md
4. Respond to maintainer feedback on documentation/description

**If resuming Phase 5 (Post-Submission)**:

Your role: Create maintenance runbook and documentation

1. Read prd.md section "Long-Term Maintenance"
2. Read tasks.md Task 5.1 "Create Maintenance Runbook"
3. Create `docs/MAINTENANCE.md` with all processes
4. Document release, update, triage, security, deprecation processes

## Implementation Sequence

**Critical Path**:

```
Phase 1 (3-5 days)
  └─> Phase 4 (1-2 weeks, includes review time)

Phase 2 (2-3 days, can run parallel to Phase 1)

Phase 3 (1 week, can run parallel to Phase 1)
  └─> Phase 4

Phase 5 (ongoing after Phase 4)
```

**Recommended Approach**:
1. Start Phase 1 and Phase 2 in parallel (Expert Agent)
2. Start Phase 3 once Phase 1 is 50% complete (Testing + Docs & Vision)
3. Begin Phase 4 when Phase 1 is complete and Phase 3 is 80% complete
4. Phase 5 begins after Phase 4 is complete

## Known Issues and Workarounds

### Issue 1: Oracle Instant Client Download URLs Change

**Impact**: Build may fail if Oracle changes download URLs

**Workaround**:
- Scripts have fallback URLs (versioned and latest)
- Consider caching OCI libraries in GitHub Actions cache
- Document manual download process in CONTRIBUTING.md

### Issue 2: GitHub-Hosted ARM Runners Availability

**Impact**: May not have ubuntu-24.04-arm64 runners available

**Workaround**:
- Use ubuntu-latest with QEMU emulation
- Or use self-hosted ARM runner
- Or exclude Linux ARM from initial release

### Issue 3: DuckDB API Breaking Changes

**Impact**: Extension may break with new DuckDB versions

**Workaround**:
- Automated testing catches issues early
- Version pinning provides stability
- Compatibility matrix documents working versions
- Manual review before merging updates

## Testing Checklist

Before considering each phase complete:

**Phase 1 Testing**:
- [ ] Trigger workflow manually with test version
- [ ] Verify all 5 platforms build successfully
- [ ] Verify smoke tests pass on all platforms
- [ ] Verify artifacts upload correctly
- [ ] Verify release is created with all binaries
- [ ] Download Linux x86_64 binary
- [ ] Test locally: `duckdb -unsigned`, `LOAD '/path/to/oracle.duckdb_extension'`
- [ ] Verify extension loads and functions available
- [ ] Delete test release

**Phase 2 Testing**:
- [ ] Trigger workflow manually
- [ ] Verify new DuckDB version detected (simulate if needed)
- [ ] Verify test branch created
- [ ] Verify build triggered
- [ ] Verify issue created with correct template
- [ ] Verify COMPATIBILITY.md updated
- [ ] Clean up test branch and issue

**Phase 3 Testing**:
- [ ] Run `make test` - all tests pass
- [ ] Run `make integration` - all tests pass
- [ ] Run `make tidy-check` - no warnings
- [ ] Trigger main-distribution-pipeline.yml - all platforms pass
- [ ] Review description.yml syntax
- [ ] Test before_build script on Linux/macOS/Windows

**Phase 4 Testing**:
- [ ] Create v1.0.0 tag
- [ ] Verify unsigned release created
- [ ] Test unsigned binaries
- [ ] Submit PR to community-extensions
- [ ] Address all CI feedback
- [ ] After merge: Install via `INSTALL oracle FROM community`
- [ ] Test installed extension

## Useful Commands

**Trigger Unsigned Release** (after Phase 1):
```bash
git tag v0.1.0-test
git push origin v0.1.0-test
# Monitor: https://github.com/{org}/{repo}/actions
```

**Trigger DuckDB Update Check** (after Phase 2):
```bash
gh workflow run duckdb-update-check.yml
# Or via GitHub UI: Actions -> DuckDB Update Check -> Run workflow
```

**Test Description YAML Syntax**:
```bash
# Install yamllint
pip install yamllint

# Validate syntax
yamllint description.yml
```

**Local Build Test**:
```bash
# Install Oracle Instant Client
./scripts/setup_oci_linux.sh
export ORACLE_HOME=$(find $(pwd)/oracle_sdk -maxdepth 1 -name "instantclient_*" | head -n1)

# Build
make release

# Test
make test
```

**Test Unsigned Extension**:
```bash
# Download binary from release
wget https://github.com/{org}/{repo}/releases/download/v0.1.0-test/oracle-v0.1.0-test-linux-x86_64.duckdb_extension

# Load in DuckDB
duckdb -unsigned

# In DuckDB:
D LOAD './oracle-v0.1.0-test-linux-x86_64.duckdb_extension';
D SELECT oracle_query('user/pass@host:1521/service', 'SELECT 1 FROM DUAL');
```

## External Resources

**DuckDB Documentation**:
- [Community Extensions](https://duckdb.org/community_extensions/)
- [Extension Distribution](https://duckdb.org/docs/stable/extensions/extension_distribution)
- [Securing Extensions](https://duckdb.org/docs/stable/operations_manual/securing_duckdb/securing_extensions)

**GitHub Repositories**:
- [duckdb/extension-template](https://github.com/duckdb/extension-template)
- [duckdb/extension-ci-tools](https://github.com/duckdb/extension-ci-tools)
- [duckdb/community-extensions](https://github.com/duckdb/community-extensions)

**Example description.yml Files**:
- [httpserver](https://github.com/duckdb/community-extensions/blob/main/extensions/httpserver/description.yml)
- [duckpgq](https://github.com/duckdb/community-extensions/blob/main/extensions/duckpgq/description.yml)

**GitHub Actions Resources**:
- [Matrix Strategy Guide](https://runs-on.com/github-actions/the-matrix-strategy/)
- [GitHub Releases API](https://docs.github.com/en/rest/releases/releases)
- [GitHub Issues API](https://docs.github.com/en/rest/issues/issues)

## Contact and Escalation

**For Technical Questions**:
- Review existing workflows: `.github/workflows/*.yml`
- Check DuckDB Discord/Slack community
- Review extension-template repository issues

**For Community Extension Questions**:
- Check [community-extensions discussions](https://github.com/duckdb/community-extensions/discussions)
- Review existing extension PRs for examples
- Reach out to DuckDB maintainers on Discord

**For Blocking Issues**:
- Create issue in this repository with `[BLOCKED]` prefix
- Tag relevant agent in issue (Expert, Testing, Docs & Vision)
- Document issue in this recovery guide

## Session Resume Checklist

When resuming work on this initiative:

1. [ ] Read this recovery guide completely
2. [ ] Check current status in "Current Status" section above
3. [ ] Review completed tasks in tasks.md
4. [ ] Identify next task in sequence
5. [ ] Read relevant PRD section for context
6. [ ] Read agent-specific instructions for your role
7. [ ] Review any research findings in research/ directory
8. [ ] Check for any blocking issues
9. [ ] Begin implementation

**Time estimate to resume**: 15-20 minutes for context review

---

**Summary**: This workspace is ready for Expert Agent to begin Phase 1 implementation. All planning, research, and documentation is complete. The PRD provides comprehensive requirements, the task list provides detailed implementation steps, and this recovery guide enables quick context switching.
