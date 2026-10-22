# Updating DuckDB

## Automated Checks

The `.github/workflows/duckdb-update-check.yml` workflow runs daily at 06:00 UTC:

1. Checks the latest stable DuckDB release tag on GitHub.
2. Compares it with `duckdb_version` in `.github/workflows/main-distribution-pipeline.yml`.
3. When a newer release exists, creates branch `feat/duckdb-v{version}`, runs `make bump-duckdb VERSION=v{version}`, and opens a pull request for review.

Once CI passes and the pull request is merged to `main`, `auto-tag.yml` tags the new extension version and triggers the release build.

## Manual Upgrades

Preview an upgrade without modifying files:

```bash
make bump-duckdb VERSION=vX.Y.Z ARGS=--dry-run
```

Apply the upgrade locally on a branch:

```bash
make bump-duckdb VERSION=vX.Y.Z
```

This updates the `duckdb` and `extension-ci-tools` submodules, workflow version references, the README version line, `docs/COMPATIBILITY.md`, and the extension patch version.

For an extension-only version bump (without changing the DuckDB version):

```bash
make bump-version bump=patch
make bump-prerelease version=0.3.1-alpha.1
```
