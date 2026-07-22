# DuckDB Update Automation

This repository uses an automated workflow to detect new DuckDB releases and initiate compatibility testing.

## Workflow Overview

The `.github/workflows/duckdb-update-check.yml` workflow runs daily at 6:00 UTC. It performs the following steps:

1.  **Check**: Queries the GitHub API for the latest stable DuckDB release.
2.  **Compare**: Compares the latest version with the current version used in `main-distribution-pipeline.yml`.
3.  **Branch**: If a new version is found, it creates a new branch `feat/duckdb-v{version}`.
4.  **Update**: Runs the repository's canonical bump script to align both submodules, CI references, extension metadata, the README support banner, and the compatibility matrix.
5.  **PR**: Creates a Pull Request to `main` for human review. The workflow never auto-merges an engine upgrade.

## Handling Updates

When a new Pull Request is created by the automation:

1.  **Review CI**: Check the distribution, code-quality, unit-test, and Oracle integration checks on the Pull Request.
2.  **If CI Passes**:
    *   Confirm the version surfaces and both submodules agree.
    *   Merge the PR.
    *   The change to `description.yml` makes `auto-tag.yml` create the extension tag. That tag triggers `release-unsigned.yml`; do not create a second tag manually.
3.  **If CI Fails**:
    *   Check out the branch locally.
    *   Investigate build or test failures.
    *   Apply fixes.
    *   Push fixes to the branch.
    *   Once CI passes, merge. The automatic tag and release workflows handle publication.

## Manual Updates

Preview a DuckDB upgrade without changing files or submodules:

```bash
make bump-duckdb VERSION=vX.Y.Z ARGS=--dry-run
```

Apply the upgrade locally:

```bash
make bump-duckdb VERSION=vX.Y.Z
```

The command updates the DuckDB and `extension-ci-tools` submodules, all six distribution-workflow references, the README banner, the compatibility matrix, and the extension patch version. It does not commit or push; review the diff and open a PR.

For an extension-only release, use the version targets directly:

```bash
make bump-version bump=patch
make bump-prerelease version=0.3.0-alpha.1
```

These targets keep `description.yml`, `.bumpversion.toml`, and the extension metadata test aligned.

## Manual Trigger

You can manually trigger the update check workflow from the GitHub Actions tab:

1.  Go to **Actions** > **DuckDB Update Check**.
2.  Click **Run workflow**.
3.  Optionally check "Check for pre-release versions".

## Configuration

The workflow behavior is defined in `.github/workflows/duckdb-update-check.yml`. Currently, it defaults to checking only stable releases.
