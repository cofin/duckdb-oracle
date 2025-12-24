# Implementation Tasks: DuckDB Version Update (v1.4.3)

**Complexity**: Medium
**Estimated Checkpoints**: 8

## Phase 1: Planning & Research ✓

- [x] PRD created/updated
- [x] Research documented (Pattern Library Insights)
- [x] Patterns identified (Split-Pipeline, Triple-Point)
- [x] Workspace setup (added patterns/ dir)
- [x] Deep analysis completed (Redundancy check)

## Phase 2: Core Implementation

**Pattern Compliance**:
- [ ] Verify no Substrait API usage in `src/`
- [ ] Follow "Triple-Point" update (Submodule, Workflow, Config)

**Infrastructure Repair**:
- [ ] Rename `.github/workflows/oracle-ci.yml` to `.github/workflows/integration-tests.yml`
- [ ] Fix Python syntax in `.github/workflows/duckdb-update-check.yml` (Use `$GITHUB_OUTPUT`)

**Version Upgrade**:
- [ ] Update `duckdb` submodule to `v1.4.3`
- [ ] Update `extension-ci-tools` submodule to `v1.4.3` (or compatible)
- [ ] Update `main-distribution-pipeline.yml` inputs to `v1.4.3`

## Phase 3: Testing (Auto via /test command)

- [ ] Local build (`make release`)
- [ ] Local integration test (`make integration`)
- [ ] Verify GitHub Actions workflows pass (on PR)
- [ ] Verify `integration-tests.yml` triggers correctly

## Phase 4: Documentation (Auto via /review command)

- [ ] Update `docs/COMPATIBILITY.md`
- [ ] Create `docs/SETUP.md` (PAT instructions)
- [ ] Update `README.md` versions
- [ ] Quality gate passed

## Phase 5: Archival

- [ ] Workspace moved to specs/archive/
- [ ] Pattern library updated (if new patterns)