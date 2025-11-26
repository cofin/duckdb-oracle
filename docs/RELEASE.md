# DuckDB Oracle Extension Release Process

This document outlines the process for creating new releases of the DuckDB Oracle extension.

## Prerequisites

- Push access to the repository
- `gh` CLI tool installed (optional, for manual triggering)

## Creating a Release

The release process is automated via GitHub Actions. To create a new release, you simply need to push a version tag.

### 1. Release Versioning

We follow [Semantic Versioning](https://semver.org/):

- `vX.Y.Z` for stable releases (e.g., `v0.1.0`)
- `vX.Y.Z-beta.N` for beta releases (e.g., `v0.1.0-beta.1`)

### 2. Triggering the Release

#### Method A: Git Tag (Recommended)

```bash
# 1. Ensure you are on the main branch and up to date
git checkout main
git pull

# 2. Create the tag
git tag v0.1.0

# 3. Push the tag
git push origin v0.1.0
```

This will automatically trigger the `release-unsigned.yml` workflow, which will:

1. Build the extension for all supported platforms:
   - Linux x86_64
   - Linux aarch64 (ARM64)
   - macOS arm64 (Apple Silicon)
   - Windows x86_64
2. Run smoke tests.
3. Create a GitHub Release.
4. Upload the unsigned binaries.

#### Method B: Manual Trigger (For testing)

You can manually trigger a release build for any branch using the GitHub Actions UI or CLI.

**GitHub UI:**

1. Go to "Actions" tab.
2. Select "Release Unsigned Extension".
3. Click "Run workflow".
4. Enter the version tag (e.g., `v0.1.0-test`).

**GitHub CLI:**

```bash
gh workflow run release-unsigned.yml -f version=v0.1.0-test
```

## Verifying the Release

1. Go to the [Releases](https://github.com/duckdb/duckdb-oracle/releases) page.
2. Verify the new release exists.
3. Download the binary for your platform.
4. Test loading it in DuckDB:

```bash
# Start DuckDB with unsigned flag
./duckdb -unsigned

# In DuckDB:
LOAD 'path/to/oracle-v0.1.0-duckdb-v1.1.3-linux-x86_64.duckdb_extension';
SELECT oracle_query('user/pass@host:1521/service', 'SELECT 1 FROM DUAL');
```

## Artifacts

The release will contain the following artifacts, each including the supported DuckDB version in the filename:

- `oracle-vX.Y.Z-duckdb-vX.Y.Z-linux-x86_64.duckdb_extension`
- `oracle-vX.Y.Z-duckdb-vX.Y.Z-linux-aarch64.duckdb_extension`
- `oracle-vX.Y.Z-duckdb-vX.Y.Z-macos-arm64.duckdb_extension`
- `oracle-vX.Y.Z-duckdb-vX.Y.Z-windows-x86_64.duckdb_extension`

## Community Extension Submission

For the extension to be available via `INSTALL oracle FROM community`, a pull request must be submitted to the [duckdb/community-extensions](https://github.com/duckdb/community-extensions) repository.

See [COMPATIBILITY.md](COMPATIBILITY.md) for the current compatibility matrix.
