# PRD: Fix DuckDB Oracle Extension Release Process

**Status**: Phase 1 - Planning & Research ✅
**Created**: 2025-12-05
**Owner**: PRD Agent
**Epic**: Release Infrastructure

---

## Executive Summary

The DuckDB Oracle Extension's release automation has two critical defects preventing proper multi-platform distribution:

1. **Single Extension Artifact Problem**: Only one `oracle.duckdb_extension` file is uploaded to GitHub Releases instead of 5 platform-specific binaries
2. **Workflow Permission Error**: The automated DuckDB version update workflow cannot push changes to workflow files due to missing `workflows` permission

These issues block the ability to distribute the extension across all supported platforms and prevent automated dependency updates.

---

## Problem Statement

### Problem 1: Single Extension Artifact

**Current Behavior:**
- GitHub Release shows only one `oracle.duckdb_extension` file
- Expected: 5 platform-specific files:
  - `oracle-linux-x86_64.duckdb_extension`
  - `oracle-linux-aarch64.duckdb_extension`
  - `oracle-macos-arm64.duckdb_extension`
  - `oracle-windows-x86_64.duckdb_extension`
  - (Missing: `oracle-macos-x86_64.duckdb_extension` - excluded from build matrix)

**Root Cause Analysis:**

Looking at `release-unsigned.yml` line 296:
```yaml
files: artifacts/*.duckdb_extension
```

The issue is in the artifact structure created during the build:

**Prepare Artifacts step (lines 172-201):**
```bash
# Each build creates TWO files:
1. deploy/${DUCKDB_VERSION}/${DUCKDB_PLATFORM}/oracle.duckdb_extension.gz  # For GitHub Pages
2. deploy/oracle.duckdb_extension  # For release attachment (PROBLEM!)
```

**The Problem:**
- All 5 builds write to the SAME filename: `deploy/oracle.duckdb_extension`
- When artifacts are downloaded with `merge-multiple: true` (line 261), the last build overwrites all previous ones
- Only the last platform's build survives to be uploaded to GitHub Release

**Why This Happens:**
- GitHub Actions v4 `download-artifact` with `merge-multiple: true` merges artifacts into a flat directory
- Multiple artifacts containing `oracle.duckdb_extension` at the same path overwrite each other
- The workflow needs UNIQUE filenames per platform

### Problem 2: Workflow Permission Error

**Current Error:**
```
! [remote rejected] feat/duckdb-v1.4.2 -> feat/duckdb-v1.4.2
  (refusing to allow a GitHub App to create or update workflow
   `.github/workflows/main-distribution-pipeline.yml` without `workflows` permission)
```

**Root Cause:**
The `duckdb-update-check.yml` workflow attempts to:
1. Modify `.github/workflows/main-distribution-pipeline.yml` (line 146)
2. Commit and push changes (lines 151-153)
3. Uses default `GITHUB_TOKEN` which lacks `workflows` permission

**Why `GITHUB_TOKEN` is Insufficient:**

From GitHub's security model:
- `GITHUB_TOKEN` has `contents: write` permission (line 19)
- Modifying workflow files requires the `workflows` scope
- This is intentionally restricted to prevent:
  - Malicious workflow injection
  - Secret exfiltration via modified workflows
  - Infinite workflow trigger loops

**Security Context:**
This is a GitHub security feature, not a bug. Workflows are treated as privileged code that can access repository secrets.

---

## Goals

### Primary Goals
1. **Multi-Platform Release Artifacts**: Upload all 5 platform-specific extensions to GitHub Releases with unique filenames
2. **Fix Workflow Permissions**: Enable automated DuckDB version updates without workflow permission errors
3. **Maintain Backward Compatibility**: Preserve existing GitHub Pages deployment structure
4. **Preserve Security**: Don't weaken security posture while fixing permissions

### Secondary Goals
1. **Clear Artifact Naming**: Use DuckDB's platform naming conventions (e.g., `linux_amd64`, `osx_arm64`)
2. **Improve Release Notes**: Include platform-specific download instructions
3. **Validate Release Artifacts**: Add checksum generation for integrity verification
4. **Document Recovery Process**: Enable manual release creation if automation fails

---

## Target Users

1. **End Users**: Data engineers downloading extension binaries for their platform
2. **CI/CD Systems**: Automated tools consuming extension releases
3. **DuckDB Community**: Users browsing the extension repository
4. **Maintainers**: Developers managing releases and version updates

---

## Technical Scope

### Fix 1: Multi-Platform Release Artifacts

**Changes Required:**

1. **Modify Artifact Preparation** (`release-unsigned.yml` lines 172-201):
   ```yaml
   - name: Prepare Artifacts
     shell: bash
     run: |
       DUCKDB_VERSION=${{ needs.validate-version.outputs.duckdb_version }}
       PLATFORM=${{ matrix.platform }}
       ARCH=${{ matrix.arch }}

       # Map to DuckDB platform strings
       if [ "$PLATFORM" == "linux" ] && [ "$ARCH" == "x86_64" ]; then
         DUCKDB_PLATFORM="linux_amd64"
       elif [ "$PLATFORM" == "linux" ] && [ "$ARCH" == "aarch64" ]; then
         DUCKDB_PLATFORM="linux_arm64"
       elif [ "$PLATFORM" == "macos" ] && [ "$ARCH" == "arm64" ]; then
         DUCKDB_PLATFORM="osx_arm64"
       elif [ "$PLATFORM" == "windows" ] && [ "$ARCH" == "x86_64" ]; then
         DUCKDB_PLATFORM="windows_amd64"
       else
         echo "Unknown platform/arch combination: $PLATFORM / $ARCH"
         exit 1
       fi

       # Create directory for GitHub Pages deployment
       mkdir -p deploy/${DUCKDB_VERSION}/${DUCKDB_PLATFORM}

       # Compress for GitHub Pages
       gzip -c build/release/extension/oracle/oracle.duckdb_extension > \
         deploy/${DUCKDB_VERSION}/${DUCKDB_PLATFORM}/oracle.duckdb_extension.gz

       # FIXED: Use unique filename for release attachment
       mkdir -p deploy/release
       cp build/release/extension/oracle/oracle.duckdb_extension \
         deploy/release/oracle-${DUCKDB_PLATFORM}.duckdb_extension
   ```

2. **Update Upload Artifact Path**:
   ```yaml
   - name: Upload Artifact
     uses: actions/upload-artifact@v4
     with:
       name: oracle-${{ matrix.platform }}-${{ matrix.arch }}
       path: |
         deploy/v*
         deploy/release
   ```

3. **Update Release Files Pattern** (line 296):
   ```yaml
   files: artifacts/release/*.duckdb_extension
   ```

**Expected Result:**
- GitHub Release contains 5 files:
  - `oracle-linux_amd64.duckdb_extension`
  - `oracle-linux_arm64.duckdb_extension`
  - `oracle-osx_arm64.duckdb_extension`
  - `oracle-windows_amd64.duckdb_extension`
  - (Optional: `oracle-osx_amd64.duckdb_extension` if macOS Intel added to matrix)

### Fix 2: Workflow Permission Error

**Solution Options:**

#### Option A: Use Personal Access Token (PAT) - RECOMMENDED

**Pros:**
- Most secure approach
- Explicit permission granting
- Auditable via PAT management
- Can be revoked/rotated independently

**Cons:**
- Requires manual PAT creation and repository secret setup
- PAT expiration management needed

**Implementation:**

1. **Create PAT** (Manual step for repository owner):
   - Go to GitHub Settings → Developer settings → Personal access tokens → Fine-grained tokens
   - Create token with permissions:
     - Repository: `contents: write`
     - Repository: `workflows: write`
     - Repository: `pull_requests: write`
   - Set expiration (recommend: 1 year with calendar reminder)
   - Store as repository secret: `WORKFLOW_UPDATE_TOKEN`

2. **Update Workflow** (`duckdb-update-check.yml`):
   ```yaml
   - name: Checkout
     uses: actions/checkout@v4
     with:
       fetch-depth: 0
       submodules: true
       token: ${{ secrets.WORKFLOW_UPDATE_TOKEN }}  # ADD THIS

   # ... rest of steps use this token for git operations

   - name: Create Branch and Update
     env:
       NEW_VERSION: ${{ steps.check.outputs.new_version }}
       GITHUB_TOKEN: ${{ secrets.WORKFLOW_UPDATE_TOKEN }}  # CHANGE THIS
   ```

#### Option B: Remove Workflow File Modifications - ALTERNATIVE

**Approach:**
- Stop modifying `.github/workflows/main-distribution-pipeline.yml` in automation
- Use GitHub UI/manual PRs for version updates
- Automation only creates an issue with update instructions

**Pros:**
- No PAT required
- Simpler security model
- Humans review version changes

**Cons:**
- Loses automation benefits
- Manual intervention required
- Slower response to DuckDB updates

**Implementation:**

Replace lines 145-156 with:
```yaml
# Create issue instead of PR
- name: Create Update Issue
  env:
    NEW_VERSION: ${{ steps.check.outputs.new_version }}
    GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
  run: |
    gh issue create \
      --title "Update DuckDB to v${NEW_VERSION}" \
      --body "A new DuckDB version is available...\n\n**Manual Steps:**\n1. Update submodule: \`git -C duckdb checkout v${NEW_VERSION}\`\n2. Update \`.github/workflows/main-distribution-pipeline.yml\`\n3. Test build\n4. Create PR" \
      --label "duckdb-update" \
      --label "dependencies"
```

#### Option C: Split Workflow Files - HYBRID

**Approach:**
- Move version configuration to a separate file (e.g., `.github/duckdb-version.txt`)
- Workflow reads version from this file
- Automation only modifies the version file (not workflow)

**Pros:**
- No workflow permission needed
- Still mostly automated
- Clearer separation of config vs code

**Cons:**
- Requires refactoring existing workflow
- More complex workflow logic

**Recommendation:** **Use Option A (PAT)** for full automation with explicit security model.

### Additional Improvements

1. **Add Checksum Generation**:
   ```yaml
   - name: Generate Checksums
     run: |
       cd deploy/release
       sha256sum *.duckdb_extension > SHA256SUMS.txt
   ```

2. **Improve Release Notes**:
   ```yaml
   - name: Generate Release Notes
     run: |
       echo "## Platform-Specific Downloads" >> notes.md
       echo "" >> notes.md
       echo "| Platform | Architecture | Download |" >> notes.md
       echo "|----------|--------------|----------|" >> notes.md
       echo "| Linux | x86_64 | [oracle-linux_amd64.duckdb_extension](...) |" >> notes.md
       # ... etc
   ```

3. **Add Validation Step**:
   ```yaml
   - name: Validate Artifacts
     run: |
       EXPECTED_COUNT=5
       ACTUAL_COUNT=$(ls artifacts/release/*.duckdb_extension | wc -l)
       if [ "$ACTUAL_COUNT" != "$EXPECTED_COUNT" ]; then
         echo "Error: Expected $EXPECTED_COUNT artifacts, found $ACTUAL_COUNT"
         exit 1
       fi
   ```

---

## Acceptance Criteria

### Fix 1: Multi-Platform Release Artifacts
- [ ] GitHub Release contains 5 platform-specific extension files
- [ ] Each file named with DuckDB platform convention: `oracle-{platform}.duckdb_extension`
- [ ] GitHub Pages deployment unchanged (still uses compressed `.gz` format)
- [ ] Validation step prevents release if artifact count is incorrect
- [ ] SHA256 checksums included in release assets

### Fix 2: Workflow Permissions
- [ ] PAT created with minimal required permissions
- [ ] PAT stored as repository secret `WORKFLOW_UPDATE_TOKEN`
- [ ] Workflow successfully pushes branch with modified workflow file
- [ ] PR auto-creation works without permission errors
- [ ] Documentation includes PAT rotation procedure

### General Quality
- [ ] Release notes include platform-specific download table
- [ ] Manual release creation documentation exists
- [ ] Rollback procedure documented
- [ ] Smoke test validates all platform artifacts load correctly

---

## Implementation Phases

### Phase 1: Planning & Research ✅
**Owner**: PRD Agent
**Status**: Complete

- [x] Analyze release workflow structure
- [x] Identify root cause of single artifact issue
- [x] Research workflow permission requirements
- [x] Evaluate solution options for both problems
- [x] Create comprehensive PRD

### Phase 2: Expert Research
**Owner**: Expert Agent
**Duration**: 30-60 minutes

**Research Tasks:**
1. Validate artifact merge behavior in GitHub Actions v4
2. Confirm DuckDB platform naming conventions
3. Research PAT fine-grained permissions scope
4. Review DuckDB community extension repository requirements
5. Identify any edge cases (e.g., macOS Intel exclusion)

**Deliverables:**
- Research findings in `specs/active/release-process-fix/research/`
- Validation of proposed solutions
- Alternative approaches (if discovered)

### Phase 3: Core Implementation
**Owner**: Expert Agent
**Duration**: 2-3 hours

**Tasks:**
1. Modify `release-unsigned.yml`:
   - Update artifact preparation logic (unique filenames)
   - Add checksum generation
   - Add artifact count validation
   - Update release files pattern
2. Create PAT setup documentation
3. Update `duckdb-update-check.yml`:
   - Add PAT usage in checkout step
   - Update GITHUB_TOKEN references
4. Improve release notes generation

### Phase 4: Integration & Testing
**Owner**: Expert Agent
**Duration**: 1-2 hours

**Tasks:**
1. Test workflow with workflow_dispatch trigger
2. Validate artifact structure after build matrix completes
3. Verify GitHub Pages deployment unchanged
4. Test PAT-based workflow update (if PAT available)
5. Create test release to verify 5 artifacts appear

### Phase 5: Testing (Automatic)
**Owner**: Testing Agent
**Duration**: 1 hour

**Tasks:**
1. Create workflow validation tests
2. Add artifact structure tests
3. Document manual testing procedure
4. Create rollback test scenario

### Phase 6: Documentation (Automatic)
**Owner**: Docs & Vision Agent
**Duration**: 1 hour

**Tasks:**
1. Update release documentation with new artifact structure
2. Document PAT creation and rotation procedure
3. Create manual release creation guide
4. Update troubleshooting guide

### Phase 7: Quality Gate & Archive (Automatic)
**Owner**: Docs & Vision Agent
**Duration**: 30 minutes

**Checklist:**
- All acceptance criteria met
- Documentation complete
- Rollback procedure tested
- Knowledge captured in `specs/guides/`
- Workspace archived to `specs/archive/release-process-fix/`

---

## Dependencies

### Internal
- Access to GitHub repository settings (for PAT secret storage)
- Ability to trigger workflow_dispatch for testing
- Historical release data for comparison

### External
- GitHub Actions v4 artifact behavior
- DuckDB platform naming conventions
- GitHub PAT fine-grained permissions API

### Blocking
- **Critical**: Repository maintainer must create PAT for Option A
- **Optional**: macOS Intel runner for 6th platform (currently excluded)

---

## Risks & Mitigations

### Risk 1: PAT Expiration
**Impact**: High - Breaks automated updates when PAT expires
**Probability**: Certain (PATs have expiration dates)
**Mitigation**:
- Set PAT expiration to 1 year
- Add calendar reminder 2 weeks before expiration
- Document PAT rotation procedure
- Consider GitHub App alternative (future enhancement)

### Risk 2: Artifact Download Timing
**Impact**: Medium - Race condition if artifacts aren't fully uploaded before download
**Probability**: Low (needs dependency specified)
**Mitigation**:
- `needs: build-matrix` already ensures all builds complete
- Add explicit artifact count validation

### Risk 3: Workflow Testing Complexity
**Impact**: Medium - Hard to test without creating real releases
**Probability**: Medium
**Mitigation**:
- Use workflow_dispatch for testing
- Create test tags (e.g., `v0.0.0-test`)
- Document rollback procedure

### Risk 4: Breaking GitHub Pages Deployment
**Impact**: High - Users can't install via custom repository
**Probability**: Low (separate artifact path)
**Mitigation**:
- Keep GitHub Pages artifact structure unchanged
- Add validation step for Pages deployment
- Test Pages deployment after changes

### Risk 5: DuckDB Platform Naming Changes
**Impact**: Low - Incompatibility with DuckDB tooling
**Probability**: Low (conventions stable)
**Mitigation**:
- Document platform mapping clearly
- Add link to DuckDB's platform conventions
- Monitor DuckDB CI changes

---

## Success Metrics

### Quantitative
- **Artifact Count**: 5 platform-specific files in each release (currently: 1)
- **Workflow Success Rate**: 100% for DuckDB version updates (currently: 0%)
- **Release Creation Time**: <30 minutes end-to-end (unchanged)
- **Manual Intervention**: 0 releases requiring manual upload (currently: 100%)

### Qualitative
- Users report successful downloads for their specific platform
- No confusion about which file to download
- Automated dependency updates work seamlessly
- Clear documentation reduces support burden

---

## References

### GitHub Actions Documentation
- [actions/upload-artifact v4 Migration](https://github.com/actions/upload-artifact/blob/main/docs/MIGRATION.md)
- [actions/upload-artifact merge behavior](https://github.com/actions/upload-artifact/blob/main/merge/README.md)
- [Workflow permissions and GitHub Apps](https://github.com/orgs/community/discussions/35410)

### DuckDB Documentation
- [Extension Distribution Guide](https://duckdb.org/docs/extensions/overview)
- [Platform Naming Conventions](https://github.com/duckdb/duckdb/blob/main/scripts/extension_distribution.py)

### Related Issues
- [GitHub Actions: merge artifacts after matrix steps](https://stackoverflow.com/questions/73498168/github-actions-merge-artifacts-after-matrix-steps)
- [Refusing to allow GitHub App to update workflow](https://github.com/orgs/community/discussions/27072)

### Similar Implementations
- DuckDB's own extension CI: `.github/workflows/_extension_distribution.yml@v1.4.1`
- Other community extensions using multi-platform releases

---

## Appendix A: Artifact Structure Comparison

### Current (Broken)
```
artifacts/
└── oracle.duckdb_extension  # Only last build survives
```

### Proposed (Fixed)
```
artifacts/
├── v1.1.3/
│   ├── linux_amd64/
│   │   └── oracle.duckdb_extension.gz
│   ├── linux_arm64/
│   │   └── oracle.duckdb_extension.gz
│   ├── osx_arm64/
│   │   └── oracle.duckdb_extension.gz
│   └── windows_amd64/
│       └── oracle.duckdb_extension.gz
└── release/
    ├── oracle-linux_amd64.duckdb_extension
    ├── oracle-linux_arm64.duckdb_extension
    ├── oracle-osx_arm64.duckdb_extension
    ├── oracle-windows_amd64.duckdb_extension
    └── SHA256SUMS.txt
```

### GitHub Release Assets
- `oracle-linux_amd64.duckdb_extension`
- `oracle-linux_arm64.duckdb_extension`
- `oracle-osx_arm64.duckdb_extension`
- `oracle-windows_amd64.duckdb_extension`
- `SHA256SUMS.txt`

---

## Appendix B: PAT Creation Checklist

**Manual Steps for Repository Owner:**

1. Navigate to: Settings → Developer settings → Personal access tokens → Fine-grained tokens
2. Click "Generate new token"
3. Token Configuration:
   - **Name**: `duckdb-oracle-workflow-updater`
   - **Expiration**: 1 year (365 days)
   - **Repository access**: Only select repositories → `duckdb-oracle`
   - **Permissions**:
     - Repository permissions:
       - Contents: Read and write
       - Workflows: Read and write
       - Pull requests: Read and write
       - Issues: Read and write
4. Click "Generate token"
5. Copy token immediately (shown only once)
6. Go to repository: Settings → Secrets and variables → Actions
7. Click "New repository secret"
8. Name: `WORKFLOW_UPDATE_TOKEN`
9. Value: Paste the PAT
10. Click "Add secret"
11. Add calendar reminder for token renewal in 11 months

**Security Notes:**
- Token is scoped to single repository
- Limited to specific permissions
- Can be revoked at any time
- Rotate on schedule or if compromised
- Do not commit token to version control

---

## Appendix C: Manual Release Creation Procedure

**If automation fails, follow these steps:**

1. **Build All Platforms Locally** (requires access to each OS):
   ```bash
   # On Linux x86_64
   make release
   cp build/release/extension/oracle/oracle.duckdb_extension oracle-linux_amd64.duckdb_extension

   # Repeat for each platform...
   ```

2. **Or Use GitHub Actions Manually**:
   - Go to Actions → Release Extension
   - Click "Run workflow"
   - Download artifacts from workflow run
   - Extract platform-specific files

3. **Create Release**:
   ```bash
   gh release create v0.x.x \
     oracle-linux_amd64.duckdb_extension \
     oracle-linux_arm64.duckdb_extension \
     oracle-osx_arm64.duckdb_extension \
     oracle-windows_amd64.duckdb_extension \
     SHA256SUMS.txt \
     --title "Release v0.x.x" \
     --notes-file RELEASE_NOTES.md
   ```

4. **Verify**:
   - Check release page shows all 5 files
   - Verify checksums match
   - Test download and load on at least 2 platforms

---

**End of PRD**
