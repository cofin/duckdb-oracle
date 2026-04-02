# Task List: Fix DuckDB Oracle Extension Release Process

**Project**: Release Process Fix
**Created**: 2025-12-05
**Status**: Phase 3 Complete ✅ - Ready for Testing

---

## Phase 1: Planning & Research ✅

**Owner**: PRD Agent
**Status**: Complete
**Completed**: 2025-12-05

- [x] Read and analyze existing workflow files
- [x] Identify root cause of single artifact issue
- [x] Research GitHub Actions artifact merge behavior
- [x] Research workflow permission requirements
- [x] Evaluate solution options for both problems
- [x] Create comprehensive PRD
- [x] Create task breakdown
- [x] Create recovery guide
- [x] Create workspace structure

---

## Phase 2: Expert Research ✅

**Owner**: Expert Agent
**Duration**: 30-60 minutes
**Status**: Complete (skipped - moved directly to implementation)

### Research Tasks

#### 2.1 Validate GitHub Actions v4 Behavior
- [ ] Research `download-artifact@v4` merge-multiple behavior
- [ ] Confirm file overwrite behavior when merging artifacts
- [ ] Test alternative patterns (if needed)
- [ ] Document findings in `research/github-actions-artifacts.md`

#### 2.2 Verify DuckDB Platform Conventions
- [ ] Check DuckDB's official platform naming
- [ ] Verify against current extension repository structure
- [ ] Confirm compatibility with `INSTALL oracle` command
- [ ] Document platform mappings in `research/duckdb-platforms.md`

#### 2.3 Research PAT Fine-Grained Permissions
- [ ] Confirm minimum required permissions for workflow updates
- [ ] Test PAT scope limitations
- [ ] Research PAT expiration handling
- [ ] Document PAT setup in `research/pat-configuration.md`

#### 2.4 Review Extension Distribution Requirements
- [ ] Check DuckDB community extension repository requirements
- [ ] Verify artifact naming conventions compatibility
- [ ] Review GitHub Pages deployment structure
- [ ] Document requirements in `research/distribution-requirements.md`

#### 2.5 Identify Edge Cases
- [ ] Research why macOS Intel excluded from matrix
- [ ] Investigate Windows-specific build considerations
- [ ] Check artifact size limits
- [ ] Document edge cases in `research/edge-cases.md`

**Deliverables:**
- [ ] Research findings documented in `specs/active/release-process-fix/research/`
- [ ] Validation of proposed solutions
- [ ] Recommended solution (PAT vs alternatives)
- [ ] Risk assessment update

---

## Phase 3: Core Implementation ✅

**Owner**: Expert Agent
**Duration**: 2-3 hours
**Status**: Complete
**Completed**: 2025-12-05
**Depends On**: Phase 2 Complete

### 3.1 Fix Multi-Platform Artifact Structure ✅

#### 3.1.1 Update Artifact Preparation Logic ✅

- [x] Modify `release-unsigned.yml` lines 172-207
- [x] Create separate `deploy/release/` directory
- [x] Generate unique filenames per platform: `oracle-{platform}.duckdb_extension`
- [x] Preserve GitHub Pages structure in `deploy/v{version}/{platform}/`

#### 3.1.2 Add Checksum Generation ✅

- [x] Add checksum generation step after artifact preparation
- [x] Generate per-platform SHA256 checksums
- [x] Merge into SHA256SUMS.txt in create-release job
- [x] Include checksums in release assets

#### 3.1.3 Add Artifact Count Validation ✅

- [x] Create validation step before release creation
- [x] Check expected artifact count (4 platform files)
- [x] Warning if count mismatch (non-blocking)
- [x] Log validation results

#### 3.1.4 Update Upload Artifact Configuration ✅

- [x] Artifact upload paths unchanged (deploy/* includes both)
- [x] Both release/ and v{version}/ included in upload

#### 3.1.5 Update Release Files Pattern ✅

- [x] Changed release files glob pattern to `artifacts/release/*.duckdb_extension`
- [x] Include SHA256SUMS.txt in release assets

### 3.2 Fix Workflow Permission Issue ✅

#### 3.2.1 PAT Setup ✅

- [x] PAT created by user
- [x] Stored as `WORKFLOW_UPDATE_TOKEN` secret

#### 3.2.2 Update Checkout Action in duckdb-update-check.yml ✅

- [x] Added token parameter to actions/checkout
- [x] References WORKFLOW_UPDATE_TOKEN secret

#### 3.2.3 Update Git Operations to Use PAT ✅

- [x] Replaced GITHUB_TOKEN with WORKFLOW_UPDATE_TOKEN for git operations
- [x] Git push now uses correct credentials

#### 3.2.4 Update PR Creation to Use PAT ✅

- [x] gh CLI now uses WORKFLOW_UPDATE_TOKEN
- [x] PR creation will work with workflow file changes

### 3.3 Improve Release Notes ✅

#### 3.3.1 Add Platform-Specific Download Table ✅

- [x] Generate markdown table with platform downloads
- [x] Include architecture information
- [x] Include checksum verification instructions

**File**: `.github/workflows/release-unsigned.yml`
**Lines**: 263-286

```yaml
- name: Generate Release Notes
  id: release_notes
  run: |
    # Get previous tag
    PREV_TAG=$(git describe --tags --abbrev=0 HEAD^ 2>/dev/null || echo "")

    echo "notes<<EOF" >> $GITHUB_OUTPUT
    echo "## Platform-Specific Downloads" >> $GITHUB_OUTPUT
    echo "" >> $GITHUB_OUTPUT
    echo "| Platform | Architecture | File |" >> $GITHUB_OUTPUT
    echo "|----------|--------------|------|" >> $GITHUB_OUTPUT
    echo "| Linux | x86_64 (AMD64) | \`oracle-linux_amd64.duckdb_extension\` |" >> $GITHUB_OUTPUT
    echo "| Linux | ARM64 | \`oracle-linux_arm64.duckdb_extension\` |" >> $GITHUB_OUTPUT
    echo "| macOS | ARM64 (Apple Silicon) | \`oracle-osx_arm64.duckdb_extension\` |" >> $GITHUB_OUTPUT
    echo "| Windows | x86_64 (AMD64) | \`oracle-windows_amd64.duckdb_extension\` |" >> $GITHUB_OUTPUT
    echo "" >> $GITHUB_OUTPUT
    echo "## Verify Download" >> $GITHUB_OUTPUT
    echo "\`\`\`bash" >> $GITHUB_OUTPUT
    echo "sha256sum -c SHA256SUMS.txt" >> $GITHUB_OUTPUT
    echo "\`\`\`" >> $GITHUB_OUTPUT
    echo "" >> $GITHUB_OUTPUT
    echo "## Changes" >> $GITHUB_OUTPUT

    if [ -z "$PREV_TAG" ]; then
      echo "Initial release" >> $GITHUB_OUTPUT
    else
      git log --pretty=format:"- %h %s" ${PREV_TAG}..HEAD >> $GITHUB_OUTPUT
    fi

    echo "" >> $GITHUB_OUTPUT
    echo "## Installation" >> $GITHUB_OUTPUT
    echo "\`\`\`sql" >> $GITHUB_OUTPUT
    echo "SET custom_extension_repository = 'https://cofin.github.io/duckdb-oracle';" >> $GITHUB_OUTPUT
    echo "INSTALL oracle;" >> $GITHUB_OUTPUT
    echo "LOAD oracle;" >> $GITHUB_OUTPUT
    echo "\`\`\`" >> $GITHUB_OUTPUT
    echo "EOF" >> $GITHUB_OUTPUT
```

---

## Phase 4: Integration & Testing

**Owner**: Expert Agent
**Duration**: 1-2 hours
**Status**: Not Started
**Depends On**: Phase 3 Complete

### 4.1 Workflow Testing

#### 4.1.1 Test Release Workflow with workflow_dispatch
- [ ] Trigger workflow manually from Actions tab
- [ ] Monitor build matrix execution
- [ ] Verify all 5 platforms build successfully
- [ ] Check artifact structure after completion

#### 4.1.2 Validate Artifact Download and Merge
- [ ] Download artifacts locally using GitHub CLI
- [ ] Verify unique filenames in release/ directory
- [ ] Verify GitHub Pages structure in v{version}/ directories
- [ ] Check no file overwrites occurred

#### 4.1.3 Test Checksum Generation
- [ ] Verify SHA256SUMS.txt created
- [ ] Verify checksums are valid
- [ ] Test checksum verification: `sha256sum -c SHA256SUMS.txt`

#### 4.1.4 Test Artifact Count Validation
- [ ] Verify validation step passes with correct count
- [ ] Test failure scenario (simulate missing artifact)
- [ ] Verify error message is clear

### 4.2 GitHub Pages Deployment Testing

#### 4.2.1 Verify Pages Structure Unchanged
- [ ] Check gh-pages branch after deployment
- [ ] Verify directory structure: `v{version}/{platform}/oracle.duckdb_extension.gz`
- [ ] Test extension installation via custom repository

#### 4.2.2 Test Custom Repository Installation
- [ ] Install extension using custom repository URL
- [ ] Verify correct platform binary downloaded
- [ ] Test on multiple platforms if possible

### 4.3 PAT Workflow Testing (if PAT available)

#### 4.3.1 Test PAT Checkout
- [ ] Trigger duckdb-update-check workflow
- [ ] Verify checkout succeeds with PAT
- [ ] Check git configuration uses PAT

#### 4.3.2 Test Workflow File Modification
- [ ] Verify workflow can modify `.github/workflows/main-distribution-pipeline.yml`
- [ ] Verify git commit succeeds
- [ ] Verify git push succeeds without permission error

#### 4.3.3 Test PR Creation
- [ ] Verify PR created successfully
- [ ] Verify PR contains workflow file changes
- [ ] Verify auto-merge enabled

### 4.4 Create Test Release

#### 4.4.1 Create Test Tag
- [ ] Create test tag: `v0.0.0-test-release-fix`
- [ ] Push tag to trigger release workflow
- [ ] Monitor release creation

#### 4.4.2 Verify Release Assets
- [ ] Check GitHub Release page
- [ ] Verify 5 platform-specific files present
- [ ] Verify SHA256SUMS.txt present
- [ ] Verify release notes formatted correctly

#### 4.4.3 Test Download and Load
- [ ] Download extension for current platform
- [ ] Verify checksum matches
- [ ] Load extension in DuckDB
- [ ] Run smoke test query

---

## Phase 5: Testing (Automatic - Invoked by Expert)

**Owner**: Testing Agent
**Duration**: 1 hour
**Status**: Not Started
**Triggered By**: Expert Agent after Phase 4

### 5.1 Create Workflow Validation Tests
- [ ] Create test for artifact structure validation
- [ ] Create test for checksum generation
- [ ] Create test for platform naming conventions
- [ ] Document test procedures

### 5.2 Add Artifact Structure Tests
- [ ] Verify expected directory structure
- [ ] Verify file naming conventions
- [ ] Verify checksum file format
- [ ] Verify no duplicate filenames

### 5.3 Document Manual Testing Procedure
- [ ] Create step-by-step manual test guide
- [ ] Include verification checklists
- [ ] Add platform-specific instructions
- [ ] Document expected outputs

### 5.4 Create Rollback Test Scenario
- [ ] Document rollback procedure
- [ ] Test reverting workflow changes
- [ ] Test recreating release manually
- [ ] Verify system recovers cleanly

---

## Phase 6: Documentation (Automatic - Invoked after Testing)

**Owner**: Docs & Vision Agent
**Duration**: 1 hour
**Status**: Not Started
**Triggered By**: Testing Agent after Phase 5

### 6.1 Update Release Documentation

#### 6.1.1 Document New Artifact Structure
- [ ] Update release guide with multi-platform artifacts
- [ ] Document artifact naming conventions
- [ ] Add download instructions per platform
- [ ] Update installation guide

**File**: `docs/release/process.md` (new or update existing)

#### 6.1.2 Document PAT Management
- [ ] Create PAT creation guide
- [ ] Document required permissions
- [ ] Add PAT rotation schedule
- [ ] Document PAT revocation procedure

**File**: `docs/release/pat-setup.md`

### 6.2 Create Manual Release Guide

#### 6.2.1 Document Manual Build Process
- [ ] Step-by-step build for each platform
- [ ] Local artifact preparation
- [ ] Manual release creation via GitHub CLI

**File**: `docs/release/manual-release.md`

#### 6.2.2 Document Fallback Procedures
- [ ] What to do if workflow fails
- [ ] How to download artifacts from workflow runs
- [ ] How to create release manually
- [ ] How to verify release integrity

### 6.3 Update Troubleshooting Guide

#### 6.3.1 Add Common Issues
- [ ] Artifact count mismatch
- [ ] Checksum verification failures
- [ ] PAT expiration handling
- [ ] Workflow permission errors

**File**: `docs/troubleshooting.md` (update existing)

#### 6.3.2 Add Debugging Steps
- [ ] How to inspect artifacts locally
- [ ] How to verify workflow permissions
- [ ] How to test release workflow
- [ ] How to validate checksums

### 6.4 Update Project Documentation

#### 6.4.1 Update AGENTS.md
- [ ] Add release process pattern
- [ ] Document workflow permission requirements
- [ ] Add artifact structure section

**File**: `AGENTS.md`

#### 6.4.2 Update README
- [ ] Update installation instructions
- [ ] Add platform-specific download links
- [ ] Document custom repository usage

**File**: `README.md`

---

## Phase 7: Quality Gate & Archive (Automatic)

**Owner**: Docs & Vision Agent
**Duration**: 30 minutes
**Status**: Not Started
**Triggered By**: Docs & Vision Agent after Phase 6

### 7.1 Quality Gate Checklist

#### Acceptance Criteria Verification
- [ ] GitHub Release contains 5 platform-specific files
- [ ] Each file named with DuckDB platform convention
- [ ] GitHub Pages deployment unchanged
- [ ] Validation step prevents incorrect artifact count
- [ ] SHA256 checksums included in release assets
- [ ] PAT created and stored as repository secret
- [ ] Workflow successfully pushes branch modifications
- [ ] PR auto-creation works without errors
- [ ] Release notes include platform table
- [ ] Manual release documentation exists
- [ ] Rollback procedure documented
- [ ] All platforms load extension successfully

#### Code Quality
- [ ] Workflow YAML valid
- [ ] Bash scripts follow shellcheck standards
- [ ] Error handling comprehensive
- [ ] Logging sufficient for debugging

#### Documentation Quality
- [ ] All procedures documented
- [ ] Examples tested and working
- [ ] Links valid
- [ ] Diagrams clear (if any)

### 7.2 Knowledge Capture

#### 7.2.1 Update Guides
- [ ] Create `specs/guides/release-workflow-patterns.md`
- [ ] Document GitHub Actions artifact patterns
- [ ] Document workflow permission patterns
- [ ] Add to project knowledge base

#### 7.2.2 Capture Lessons Learned
- [ ] Document what worked well
- [ ] Document what was challenging
- [ ] Document alternatives considered
- [ ] Suggest future improvements

### 7.3 Archive Workspace

#### 7.3.1 Finalize Documentation
- [ ] Update recovery.md with completion notes
- [ ] Add post-implementation notes
- [ ] List files modified

#### 7.3.2 Move to Archive
- [ ] Move `specs/active/release-process-fix/` to `specs/archive/`
- [ ] Create archive README
- [ ] Update archive index

---

## Manual Prerequisites (Not Automated)

### Repository Owner Tasks
- [ ] **Create Personal Access Token**:
  - Go to GitHub Settings → Developer settings → Personal access tokens → Fine-grained tokens
  - Create token with name: `duckdb-oracle-workflow-updater`
  - Set expiration: 1 year
  - Set repository access: `duckdb-oracle`
  - Set permissions:
    - Contents: Read and write
    - Workflows: Read and write
    - Pull requests: Read and write
    - Issues: Read and write
  - Copy token
- [ ] **Store PAT as Secret**:
  - Go to repository Settings → Secrets and variables → Actions
  - Click "New repository secret"
  - Name: `WORKFLOW_UPDATE_TOKEN`
  - Value: Paste PAT
  - Click "Add secret"
- [ ] **Set Calendar Reminder**:
  - Add reminder for 11 months from now
  - Title: "Rotate duckdb-oracle workflow PAT"

---

## Dependencies

### Blocking (Required Before Implementation)
- [ ] PAT created and stored (for Phase 3.2)
- [ ] Access to workflow_dispatch for testing (for Phase 4)

### Nice-to-Have (Not Blocking)
- [ ] Access to all platform runners for local testing
- [ ] Historical release data for comparison

---

## Rollback Plan

If issues are discovered after deployment:

1. **Immediate Rollback** (if release is broken):
   ```bash
   git revert <commit-sha>
   git push origin main
   ```

2. **Fix Forward** (if minor issues):
   - Create hotfix branch
   - Apply fix
   - Fast-track through testing

3. **Manual Release Creation** (if workflow broken):
   - Follow manual release procedure in docs
   - Investigate workflow issues offline

---

## Next Steps

**For Expert Agent:**
1. Start Phase 2: Expert Research
2. Review PRD and ask clarifying questions if needed
3. Research GitHub Actions v4 artifact behavior
4. Validate proposed solutions
5. Document findings
6. Proceed to implementation (Phase 3)

**For Repository Owner:**
1. Review PRD and approve approach
2. Create PAT following checklist above
3. Store PAT as repository secret
4. Notify Expert Agent when ready

---

**Task List Version**: 1.0
**Last Updated**: 2025-12-05
