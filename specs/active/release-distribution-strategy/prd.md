# PRD: DuckDB Oracle Extension Release and Distribution Strategy

**Status**: Phase 1 - Planning & Research
**Created**: 2025-11-22
**Last Updated**: 2025-11-22
**Owner**: PRD Agent

## Overview

This PRD defines a comprehensive release and distribution strategy for the DuckDB Oracle extension, covering three distinct but interconnected initiatives:

1. **Unsigned Release Process for Testing** - GitHub Release workflow for building and distributing unsigned extension binaries for testing and development
2. **Automated DuckDB Version Updates** - Monitoring and automation to keep the extension compatible with new DuckDB releases
3. **Community Extension Submission** - Process for listing the extension in the official DuckDB community extensions repository with signed binaries

Together, these initiatives will enable rapid iteration during development (unsigned releases), ensure compatibility with DuckDB evolution (automated updates), and provide production-ready distribution (community extension).

## Problem Statement

Currently, the DuckDB Oracle extension faces several distribution challenges:

1. **No Release Mechanism**: Developers and users cannot easily download pre-built binaries for testing
2. **Manual Version Tracking**: Maintainers must manually monitor DuckDB releases and update compatibility
3. **No Production Distribution**: Extension is not available through DuckDB's official installation mechanisms (INSTALL/LOAD)
4. **Platform Coverage Gaps**: No automated multi-platform builds for Linux (x64, arm64), macOS (x64, arm64), Windows (x64)
5. **Testing Bottleneck**: Users must build from source to test, limiting adoption and feedback

## Goals

### Primary Goals

1. **Enable Testing Distribution**: Users can download unsigned binaries for development/testing within 1 hour of a commit to main
2. **Automate Compatibility Tracking**: System detects new DuckDB versions within 24 hours and triggers compatibility checks
3. **Achieve Community Extension Status**: Extension is listed in DuckDB community extensions repository and installable via `INSTALL oracle FROM community`
4. **Multi-Platform Coverage**: Automated builds for all supported platforms (Linux x64/arm64, macOS x64/arm64, Windows x64)

### Secondary Goals

1. **Version Matrix Documentation**: Maintain a compatibility matrix showing which extension versions work with which DuckDB versions
2. **Release Notes Automation**: Auto-generate release notes from git commits and PR descriptions
3. **Download Metrics**: Track extension download counts to measure adoption
4. **CI Performance**: Keep build times under 30 minutes per platform

## Target Users

1. **Data Engineers**: Testing Oracle connectivity in development environments
2. **Database Administrators**: Evaluating DuckDB as an analytics layer over Oracle
3. **Open Source Contributors**: Testing PRs and contributing features
4. **Production Users**: Installing stable, signed binaries via DuckDB's extension system
5. **Extension Maintainer**: Tracking compatibility and managing releases

## Technical Scope

### Phase 1: Unsigned Release Process

**Goal**: Create GitHub Release workflow for unsigned extension binaries

#### Build Matrix

| Platform | Architecture | OS Image | Oracle Instant Client |
|----------|--------------|----------|----------------------|
| Linux | x86_64 | ubuntu-24.04 | instantclient_23_6 |
| Linux | aarch64 | ubuntu-24.04 (ARM) | instantclient_23_6 |
| macOS | x86_64 | macos-13 | instantclient_19_8 |
| macOS | arm64 | macos-14 | instantclient_19_8 |
| Windows | x86_64 | windows-2022 | instantclient_21_15 |

**Excluded Platforms** (per README.md):
- wasm_mvp, wasm_eh, wasm_threads (Oracle Instant Client incompatible with browser sandbox)
- osx_amd64 (excluded in MainDistributionPipeline.yml)

#### Workflow Design

**File**: `.github/workflows/release-unsigned.yml`

**Triggers**:
- Manual workflow_dispatch with version input
- Git tag push matching `v*` (e.g., v0.1.0, v0.2.0-beta1)
- Optional: Commits to main with `[release]` tag

**Jobs**:

1. **validate-version**: Verify version tag format, check for duplicate releases
2. **build-matrix**: Matrix build across all platforms
   - Install Oracle Instant Client (reuse scripts/setup_oci_*.sh/ps1)
   - Build extension in release mode
   - Run smoke tests (require oracle, connection failure tests)
   - Upload artifacts (oracle.duckdb_extension)
3. **create-release**: Create GitHub Release with artifacts
   - Generate release notes from commits since last tag
   - Attach binaries with naming: `oracle-{version}-{platform}-{arch}.duckdb_extension`
   - Mark as pre-release for beta/rc tags
   - Include installation instructions in release body

#### Artifact Naming Convention

```
oracle-v0.1.0-linux-x86_64.duckdb_extension
oracle-v0.1.0-linux-aarch64.duckdb_extension
oracle-v0.1.0-macos-x86_64.duckdb_extension
oracle-v0.1.0-macos-arm64.duckdb_extension
oracle-v0.1.0-windows-x86_64.duckdb_extension
```

#### Installation Instructions (in Release Notes)

```sql
-- Download the appropriate binary for your platform from Assets below
-- Load in DuckDB with unsigned extension support:
duckdb -unsigned

-- Then load the extension:
LOAD '/path/to/oracle-v0.1.0-linux-x86_64.duckdb_extension';

-- Verify:
SELECT oracle_query('user/pass@host:1521/service', 'SELECT 1 FROM DUAL');
```

### Phase 2: Automated DuckDB Version Updates

**Goal**: Automatically detect new DuckDB releases and trigger compatibility testing

#### DuckDB Version Monitoring

**File**: `.github/workflows/duckdb-update-check.yml`

**Triggers**:
- Schedule: Daily cron at 6:00 UTC
- Manual workflow_dispatch

**Process**:

1. **Fetch DuckDB Releases**: Query GitHub API for duckdb/duckdb releases
2. **Compare Versions**: Check against current `duckdb_version` in MainDistributionPipeline.yml
3. **Filter Releases**:
   - Only stable releases (no pre-release unless opted in)
   - Only minor/patch updates (v1.4.x → v1.5.0, v1.5.0 → v1.5.1)
   - Skip major version updates (v1.x → v2.x) - requires manual review
4. **Trigger Compatibility Check**:
   - Create new branch: `feat/duckdb-v{new_version}`
   - Update MainDistributionPipeline.yml with new version
   - Update duckdb submodule to new tag
   - Trigger build workflow
5. **Notification**:
   - On success: Create issue titled "DuckDB v{version} compatibility verified - Ready for merge"
   - On failure: Create issue titled "DuckDB v{version} compatibility check failed" with build logs
   - Assign to maintainer(s) defined in CODEOWNERS

#### Compatibility Matrix

**File**: `docs/COMPATIBILITY.md` (auto-updated)

| DuckDB Version | Extension Version | Status | Tested Date |
|----------------|-------------------|--------|-------------|
| v1.4.1 | v0.1.0 | ✅ Compatible | 2025-11-22 |
| v1.5.0 | v0.2.0 | ✅ Compatible | 2025-12-15 |
| v1.5.1 | v0.2.0 | ✅ Compatible | 2025-12-20 |

#### Configuration

**File**: `.github/duckdb-update-config.yml`

```yaml
monitoring:
  enabled: true
  check_frequency: daily
  include_prerelease: false
  auto_merge_patch: false  # Require manual approval even for patches

version_policy:
  major_update: manual      # Always requires manual review
  minor_update: auto_test   # Auto-test, create issue for review
  patch_update: auto_test   # Auto-test, create issue for review

notifications:
  create_issues: true
  notify_maintainers: true
  slack_webhook: ""  # Optional

maintainers:
  - github_username_1
  - github_username_2
```

### Phase 3: Community Extension Submission

**Goal**: List extension in duckdb/community-extensions repository

#### Pre-Submission Checklist

**Documentation Requirements**:
- [x] README.md with installation and usage examples
- [x] LICENSE file (Apache-2.0)
- [ ] CONTRIBUTING.md with contribution guidelines
- [ ] Comprehensive test coverage (SQL + integration tests)
- [ ] Code quality checks (clang-tidy, formatting)

**Build Requirements**:
- [x] Based on DuckDB extension-template structure
- [x] Uses extension-ci-tools for builds
- [x] CMake build system
- [x] vcpkg for dependencies (OpenSSL)
- [ ] Oracle Instant Client setup documented and automated

**Repository Requirements**:
- [x] Public GitHub repository
- [x] Open source license (Apache-2.0)
- [x] Active maintenance (responsive to issues/PRs)
- [ ] CODEOWNERS file
- [ ] Security policy (SECURITY.md)

#### DuckDB Community Extensions description.yml

**File**: To be created in PR to duckdb/community-extensions

**Path**: `extensions/oracle/description.yml`

```yaml
extension:
  name: oracle
  description: Native Oracle database extension for DuckDB enabling read-only access to Oracle tables
  version: 1.0.0
  language: C++
  build: cmake
  license: Apache-2.0
  excluded_platforms: "wasm_mvp;wasm_eh;wasm_threads;osx_amd64"

  maintainers:
    - github_username

repo:
  github: duckdb-oracle-org/duckdb-oracle
  ref: v1.0.0

docs:
  hello_world: |
    -- Attach Oracle database
    ATTACH 'user/password@//host:1521/service' AS ora (TYPE oracle);

    -- Query Oracle table
    SELECT * FROM ora.HR.EMPLOYEES WHERE employee_id = 101;

    -- Use table function
    SELECT * FROM oracle_scan('user/pass@host:1521/service', 'HR', 'EMPLOYEES');

  extended_description: |
    This extension provides native Oracle connectivity through Oracle Call Interface (OCI).

    Features:
    - Read-only access to Oracle tables via ATTACH syntax
    - Table and query functions (oracle_scan, oracle_query)
    - Oracle Wallet support for Autonomous Database
    - Secret Manager integration for credential management
    - Filter pushdown optimization
    - Configurable fetch parameters for performance tuning

    Platform Support:
    Requires Oracle Instant Client runtime. Supports Linux (x64, arm64), macOS (x64, arm64),
    and Windows (x64). WebAssembly platforms not supported due to native library requirements.

    Use Cases:
    - Analytics on Oracle data without ETL
    - Data lake queries combining Oracle with Parquet/CSV
    - Cross-database joins (Oracle + PostgreSQL + S3)
    - Oracle database migration and testing

  # Optional: Specify function signatures for auto-documentation
  functions:
    - name: oracle_query
      parameters:
        - name: connection_string
          type: VARCHAR
        - name: query
          type: VARCHAR
      description: Execute arbitrary SQL query against Oracle database

    - name: oracle_scan
      parameters:
        - name: connection_string
          type: VARCHAR
        - name: schema_name
          type: VARCHAR
        - name: table_name
          type: VARCHAR
      description: Scan an Oracle table (equivalent to SELECT * FROM schema.table)

    - name: oracle_attach_wallet
      parameters:
        - name: wallet_path
          type: VARCHAR
      description: Set TNS_ADMIN for Oracle Wallet (Autonomous Database support)

    - name: oracle_clear_cache
      description: Clear cached Oracle metadata and connections

  # Extension settings for documentation
  settings:
    - name: oracle_enable_pushdown
      type: BOOLEAN
      default: false
      description: Push filters to Oracle WHERE clause

    - name: oracle_prefetch_rows
      type: INTEGER
      default: 200
      description: OCI prefetch rows per round-trip

    - name: oracle_array_size
      type: INTEGER
      default: 256
      description: Rows fetched per OCI iteration

# Build configuration for community CI
build:
  # Pre-build step to install Oracle Instant Client
  before_build: |
    if [ "$(uname)" = "Linux" ]; then
      ./scripts/setup_oci_linux.sh
    elif [ "$(uname)" = "Darwin" ]; then
      ./scripts/setup_oci_macos.sh
    elif [ "$OS" = "Windows_NT" ]; then
      pwsh -ExecutionPolicy Bypass -File ./scripts/setup_oci_windows.ps1
    fi
    export ORACLE_HOME=$(find $(pwd)/oracle_sdk -maxdepth 1 -name "instantclient_*" | head -n1)

  # Build flags
  cmake_flags: "-DCMAKE_BUILD_TYPE=Release"

  # Test configuration
  test_command: "make test"
```

#### Submission Process

**Steps**:

1. **Prepare Repository**:
   - Ensure all checklist items complete
   - Tag stable release (v1.0.0)
   - Verify builds pass on all platforms via MainDistributionPipeline.yml
   - Run local integration tests with containerized Oracle

2. **Create description.yml**:
   - Write comprehensive metadata following schema above
   - Include hello_world, extended_description, functions, settings
   - Test locally that format is valid YAML

3. **Submit PR to duckdb/community-extensions**:
   - Fork repository
   - Create `extensions/oracle/description.yml`
   - Open PR with title: "Add Oracle extension"
   - PR description should include:
     - Extension purpose and key features
     - Link to repository and documentation
     - Confirmation of license and maintenance commitment
     - Known limitations (read-only, Oracle Instant Client requirement)

4. **CI Review**:
   - DuckDB community CI will automatically build extension
   - Address any build failures or test issues
   - Respond to review feedback from DuckDB maintainers

5. **Approval and Merge**:
   - Once approved, DuckDB team merges PR
   - Extension is automatically built, signed, and published
   - Available via `INSTALL oracle FROM community`

#### Post-Submission Maintenance

**Ongoing Responsibilities**:

1. **Bug Fixes**: Respond to issues within 7 days
2. **Security Updates**: Apply Oracle Instant Client security patches promptly
3. **Version Updates**: Submit new description.yml when releasing major versions
4. **Compatibility**: Ensure extension stays compatible with new DuckDB versions
5. **Documentation**: Keep examples and guides up-to-date

**Version Updates**:

When releasing a new version (e.g., v1.1.0):
- Update `version` and `repo.ref` in description.yml
- Submit PR to duckdb/community-extensions
- Community CI rebuilds and re-signs binaries

## Acceptance Criteria

### Phase 1: Unsigned Release Process

- [ ] GitHub Release workflow creates releases on version tag push
- [ ] Workflow builds binaries for all 5 supported platforms
- [ ] Artifacts are named consistently and attached to releases
- [ ] Release notes include installation instructions
- [ ] Smoke tests pass before release creation
- [ ] Manual test: Download binary, load in DuckDB -unsigned, connect to Oracle

### Phase 2: Automated DuckDB Version Updates

- [ ] Daily workflow checks for new DuckDB versions
- [ ] Workflow creates compatibility test branch for new versions
- [ ] Issue is created on successful/failed compatibility test
- [ ] COMPATIBILITY.md is auto-updated with test results
- [ ] Manual test: Trigger workflow, verify issue creation

### Phase 3: Community Extension Submission

- [ ] All pre-submission checklist items complete
- [ ] description.yml created with complete metadata
- [ ] PR submitted to duckdb/community-extensions
- [ ] Community CI builds pass on all platforms
- [ ] PR approved and merged by DuckDB maintainers
- [ ] Extension installable via `INSTALL oracle FROM community`
- [ ] Manual test: Fresh DuckDB instance, run `INSTALL oracle FROM community; LOAD oracle;`

## Implementation Phases

### Phase 1: Unsigned Release Workflow (Est. 3-5 days)

**Tasks**:
1. Design release-unsigned.yml workflow
2. Implement matrix builds for 5 platforms
3. Integrate Oracle Instant Client setup scripts
4. Create artifact upload and release creation logic
5. Test workflow with beta release (v0.1.0-beta1)
6. Document usage in docs/RELEASE.md

**Deliverable**: Working GitHub Release workflow producing unsigned binaries

### Phase 2: DuckDB Update Automation (Est. 2-3 days)

**Tasks**:
1. Design duckdb-update-check.yml workflow
2. Implement GitHub API integration for version checking
3. Create auto-branch and compatibility test logic
4. Implement issue creation and notification system
5. Create COMPATIBILITY.md template and update logic
6. Test with simulated DuckDB version update

**Deliverable**: Automated compatibility monitoring system

### Phase 3: Community Extension Preparation (Est. 1 week)

**Tasks**:
1. Complete pre-submission checklist
2. Create CONTRIBUTING.md
3. Create SECURITY.md
4. Add CODEOWNERS file
5. Write comprehensive description.yml
6. Test description.yml format locally
7. Verify builds on all platforms via MainDistributionPipeline.yml

**Deliverable**: Repository ready for community submission

### Phase 4: Community Extension Submission (Est. 1-2 weeks)

**Tasks**:
1. Tag v1.0.0 release
2. Submit PR to duckdb/community-extensions
3. Address CI feedback
4. Respond to reviewer comments
5. Await approval and merge
6. Verify extension installation works

**Deliverable**: Extension listed in DuckDB community extensions

### Phase 5: Testing & Documentation (Ongoing)

**Tasks**:
1. Test unsigned release workflow with real users
2. Monitor compatibility check automation
3. Document lessons learned
4. Create maintenance runbook
5. Update CLAUDE.md with release procedures

**Deliverable**: Production-ready release and distribution system

## Dependencies

### Internal Dependencies

1. **Oracle Instant Client Setup Scripts**: scripts/setup_oci_{linux,macos,windows}
2. **Build System**: CMake, Makefile, vcpkg.json
3. **CI Infrastructure**: MainDistributionPipeline.yml, OracleCI.yml
4. **Test Suite**: test/sql/*.test, scripts/test_integration.sh
5. **Documentation**: README.md, docs/

### External Dependencies

1. **GitHub Actions**: Workflow execution environment
2. **Oracle Instant Client**: Available for download (version 19c, 21c, 23c)
3. **DuckDB Extension CI Tools**: duckdb/extension-ci-tools v1.4.1+
4. **DuckDB Community Extensions Repo**: duckdb/community-extensions
5. **Build Tools**: CMake 3.10+, C++17 compiler, vcpkg

### API Dependencies

1. **GitHub REST API**: For DuckDB release monitoring
2. **GitHub Releases API**: For creating and managing releases
3. **GitHub Issues API**: For creating compatibility notifications

## Risks & Mitigations

### Risk 1: Oracle Instant Client Availability in CI

**Impact**: High - Cannot build without OCI libraries
**Probability**: Medium - Oracle occasionally changes download URLs

**Mitigation**:
- Scripts have fallback URLs (latest vs versioned)
- Cache OCI libraries in GitHub Actions cache
- Document manual download process
- Consider self-hosting OCI libraries if legal/license permits

### Risk 2: DuckDB API Breaking Changes

**Impact**: High - Extension may become incompatible
**Probability**: Medium - DuckDB evolves rapidly

**Mitigation**:
- Automated compatibility testing catches issues early
- Maintain compatibility matrix
- Version pinning in MainDistributionPipeline.yml
- Active monitoring of DuckDB release notes and changelogs

### Risk 3: Community Extension Rejection

**Impact**: Medium - Delays production distribution
**Probability**: Low - Extension follows template and standards

**Mitigation**:
- Pre-submission checklist ensures readiness
- Engage with DuckDB community early for feedback
- Unsigned releases provide alternative distribution
- Maintain high code quality and test coverage

### Risk 4: Platform-Specific Build Failures

**Impact**: Medium - Reduces platform coverage
**Probability**: Medium - Oracle Instant Client platform quirks

**Mitigation**:
- Platform-specific setup scripts already tested
- Matrix builds fail fast, isolate platform issues
- Test on actual platform runners before release
- Document platform-specific requirements

### Risk 5: Extension Signing Complexity

**Impact**: Low - Only affects community extension
**Probability**: Low - DuckDB CI handles signing

**Mitigation**:
- Unsigned releases available for testing
- Community CI manages signing automatically
- No manual key management required

## Research Questions for Expert Agent

1. **Oracle Instant Client Caching**: Can we cache OCI libraries in GitHub Actions to speed up builds? What's the cache eviction policy?

2. **DuckDB Extension ABI Compatibility**: Does DuckDB maintain ABI compatibility within minor versions (e.g., v1.4.0 → v1.4.1)? Can we test with v1.4.1 and support v1.4.x?

3. **Build Optimization**: Are there compiler flags or build settings that reduce binary size without sacrificing performance? Current binaries ~5MB uncompressed.

4. **Community Extension Build Time**: What's the typical build time for C++ extensions in DuckDB community CI? Do we need to optimize for faster builds?

5. **Version Tagging Strategy**: Should we use semantic versioning with pre-release tags (v1.0.0-beta.1) or simple incrementing (v0.1.0, v0.2.0)?

6. **GitHub Release Retention**: How long should we keep old releases available? Disk space considerations?

7. **Artifact Compression**: Should we compress binaries (gzip) before upload to reduce download size? DuckDB extension loader compatibility?

8. **Multi-DuckDB Version Support**: Can a single extension binary support multiple DuckDB versions, or must we build per DuckDB version?

9. **Integration Test in CI**: Should we run full integration tests (with Oracle container) in release workflow, or just smoke tests?

10. **Windows Code Signing**: Does Windows require Authenticode signing for extension DLLs? Security warnings for unsigned binaries?

## Success Metrics

### Phase 1: Unsigned Release

- **Build Success Rate**: >95% of release builds succeed
- **Build Time**: <30 minutes per platform
- **Artifact Download Count**: Track downloads per release
- **User Feedback**: At least 5 GitHub issues/discussions from users testing unsigned releases

### Phase 2: Update Automation

- **Detection Latency**: New DuckDB versions detected within 24 hours
- **Compatibility Test Time**: <2 hours from detection to test result
- **False Positive Rate**: <10% of compatibility tests incorrectly report failures
- **Notification Accuracy**: 100% of test results generate appropriate issues

### Phase 3: Community Extension

- **Submission Timeline**: PR submitted within 2 weeks of Phase 1 completion
- **Approval Timeline**: PR approved within 4 weeks of submission
- **Installation Success Rate**: 100% of platforms install successfully via `INSTALL oracle FROM community`
- **User Adoption**: 100+ extension installs within first month after community listing

## Long-Term Maintenance

### Release Cadence

- **Patch Releases** (bug fixes): As needed, typically 1-2 per month
- **Minor Releases** (new features): Every 2-3 months
- **Major Releases** (breaking changes): Annually or as required by DuckDB major versions

### Versioning Policy

Follow semantic versioning (semver):
- **MAJOR**: Incompatible API changes or DuckDB major version updates
- **MINOR**: Backward-compatible functionality additions
- **PATCH**: Backward-compatible bug fixes

Example: v1.2.3 where 1=major, 2=minor, 3=patch

### Deprecation Policy

When deprecating features:
1. Announce deprecation in release notes
2. Maintain deprecated feature for at least 2 minor versions
3. Emit warnings when deprecated features are used
4. Remove in next major version

### Support Windows

- **Current Release**: Full support and updates
- **Previous Release**: Security fixes only for 3 months
- **Older Releases**: Best-effort support, encourage upgrade

## References

### DuckDB Documentation

- [Community Extensions Overview](https://duckdb.org/community_extensions/)
- [Community Extension Development](https://duckdb.org/community_extensions/development)
- [Extension Distribution](https://duckdb.org/docs/stable/extensions/extension_distribution)
- [Securing Extensions](https://duckdb.org/docs/stable/operations_manual/securing_duckdb/securing_extensions)

### GitHub Repositories

- [duckdb/extension-template](https://github.com/duckdb/extension-template) - Template structure
- [duckdb/extension-ci-tools](https://github.com/duckdb/extension-ci-tools) - Build workflows
- [duckdb/community-extensions](https://github.com/duckdb/community-extensions) - Submission target

### Examples

- [httpserver/description.yml](https://github.com/duckdb/community-extensions/blob/main/extensions/httpserver/description.yml)
- [duckpgq/description.yml](https://github.com/duckdb/community-extensions/blob/main/extensions/duckpgq/description.yml)

### Related Documents

- `CLAUDE.md` - Agent system overview
- `README.md` - Current installation instructions
- `MainDistributionPipeline.yml` - Existing CI workflow
- `scripts/setup_oci_*.sh` - Oracle Instant Client setup

---

**Next Steps**: Expert agent to implement Phase 1 (Unsigned Release Workflow) based on this PRD.
