# DuckDB Update Automation

This repository uses an automated workflow to detect new DuckDB releases and initiate compatibility testing.

## Workflow Overview

The `.github/workflows/duckdb-update-check.yml` workflow runs daily at 6:00 UTC. It performs the following steps:

1.  **Check**: Queries the GitHub API for the latest stable DuckDB release.
2.  **Compare**: Compares the latest version with the current version used in `main-distribution-pipeline.yml`.
3.  **Branch**: If a new version is found, it creates a new branch `feat/duckdb-v{version}`.
4.  **Update**: Updates the `duckdb` submodule and the version in `main-distribution-pipeline.yml`.
5.  **PR**: Creates a Pull Request to `main`.

## Handling Updates

When a new Pull Request is created by the automation:

1.  **Review CI**: Check the checks on the Pull Request. The `main-distribution-pipeline.yml` will run automatically.
2.  **If CI Passes**:
    *   Review the changes (usually just submodule and workflow file).
    *   Merge the PR.
    *   Tag a new release (e.g., `v0.1.1`) to trigger the `release-unsigned.yml` workflow.
3.  **If CI Fails**:
    *   Check out the branch locally.
    *   Investigate build or test failures.
    *   Apply fixes.
    *   Push fixes to the branch.
    *   Once CI passes, merge and release.

## Manual Trigger

You can manually trigger the update check workflow from the GitHub Actions tab:

1.  Go to **Actions** > **DuckDB Update Check**.
2.  Click **Run workflow**.
3.  Optionally check "Check for pre-release versions".

## Configuration

The workflow behavior is defined in `.github/workflows/duckdb-update-check.yml`. Currently, it defaults to checking only stable releases.