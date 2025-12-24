# Plan: DuckDB Version Update & Auto-Release Fix

## Phase 1: DuckDB Version Update (v1.4.3)
- [x] Task: Update DuckDB submodule to the target version tag. 40359d2
    - [ ] Sub-task: Execute `git submodule update --remote --merge` or specifically checkout `v1.4.3` in the `duckdb` directory.
- [ ] Task: Update version strings in project configuration files.
    - [ ] Sub-task: Update `duckdb_version` in `.github/workflows/main-distribution-pipeline.yml`.
    - [ ] Sub-task: Check and update `extension_config.cmake` if version-specific flags are needed.
- [ ] Task: Verify the build with the new DuckDB version.
    - [ ] Sub-task: Execute `make clean && make release`.
- [ ] Task: Run the existing test suite to ensure no regressions.
    - [ ] Sub-task: Execute `make test`.
    - [ ] Sub-task: Execute `make integration` (requires Oracle container).
- [ ] Task: Conductor - User Manual Verification 'Phase 1: DuckDB Version Update (v1.4.3)' (Protocol in workflow.md)

## Phase 2: Auto-Release Workflow Fix
- [ ] Task: Modify `.github/workflows/duckdb-update-check.yml` to support force-updating existing branches.
    - [ ] Sub-task: Update the git logic to check if the branch exists, and if so, reset it to `origin/main` before applying changes.
- [ ] Task: Ensure the workflow creates or updates a Pull Request even if the branch already existed.
    - [ ] Sub-task: Refine the `gh pr create` logic to handle existing PRs or create a new one if it's missing but the branch is present.
- [ ] Task: (Optional) Test the workflow fix using a dry-run or manual trigger if possible.
- [ ] Task: Conductor - User Manual Verification 'Phase 2: Auto-Release Workflow Fix' (Protocol in workflow.md)

## Phase 3: Final Validation and Quality Gate
- [ ] Task: Ensure all CI checks pass on the version update PR.
- [ ] Task: Verify that all public APIs are documented (Doxygen style).
- [ ] Task: Run `make tidy-check` to ensure code quality standards are met.
- [ ] Task: Conductor - User Manual Verification 'Phase 3: Final Validation and Quality Gate' (Protocol in workflow.md)
