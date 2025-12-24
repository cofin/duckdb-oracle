# Spec: DuckDB Version Update & Auto-Release Fix

## Overview
This track addresses two related maintenance tasks: updating the DuckDB extension to be compatible with the latest stable DuckDB version (v1.4.3 or newer) and fixing the logic in the auto-release GitHub Action workflow. The goal is to ensure the extension remains compatible with upstream DuckDB and that the automated process for detecting and proposing these updates is robust.

## Functional Requirements
1.  **DuckDB Version Update:**
    -   Update the DuckDB submodule/dependency to the latest stable version (e.g., v1.4.3).
    -   Update `extension_config.cmake` or relevant build files to reflect the new version.
    -   Ensure the extension compiles and passes all tests with the new DuckDB version.

2.  **Auto-Release Workflow Fix (`duckdb-update-check.yml`):**
    -   Modify the workflow to handle the case where the update branch (e.g., `feat/duckdb-vX.Y.Z`) already exists.
    -   **Force Update:** If the branch exists, the workflow must force-update it (reset/rebase) to ensure it's fresh from `main` before applying the version bump.
    -   **PR Creation:** Ensure a Pull Request is created or updated even if the branch pre-existed. It should not exit silently just because the branch is present.
    -   **Stale PR Handling:** (Existing logic) Continue to close stale PRs for older versions.

## Non-Functional Requirements
-   **Stability:** The version update must not introduce regressions.
-   **Automation:** The fix to the workflow should minimize manual intervention for future updates.

## Acceptance Criteria
-   [ ] The project builds and passes all tests (`make test`, `make integration`) against the new DuckDB version.
-   [ ] The `duckdb-update-check.yml` workflow successfully creates a PR for a new version, even if the target branch was manually created or left over from a failed run.
-   [ ] The CI pipeline for the new version PR passes.

## Out of Scope
-   Major refactoring of the extension architecture (unless required by DuckDB API changes, which is unlikely for minor updates).
