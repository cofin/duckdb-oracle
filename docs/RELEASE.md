# DuckDB Oracle Extension Release Process

This document outlines how releases are created and deployed for the DuckDB Oracle extension.

## Versioning

This project uses two independent version tracks:

- **Extension version** (e.g., `v0.2.1`): Defined in `description.yml`. Used for git tags, GitHub Releases, and the runtime version reported by `duckdb_extensions()`.
- **DuckDB version** (e.g., `v1.5.4`): The DuckDB version the extension is built against. Used for the GitHub Pages directory structure that DuckDB's extension loader expects.

When a user runs `INSTALL oracle`, DuckDB fetches from:

```text
https://cofin.github.io/duckdb-oracle/{duckdb_version}/{platform}/oracle.duckdb_extension.gz
```

## Automated Release Flow

The release pipeline is fully automated:

1. **Version bump** (`description.yml`): When the extension version changes on `main`, the pipeline detects it.
2. **Auto-tag** (`auto-tag.yml`): On every push to `main`, reads `description.yml` and creates a git tag `v{version}` if it doesn't exist.
3. **Build & Deploy** (`release-unsigned.yml`): Triggered by the new tag. Builds for all platforms, deploys to GitHub Pages, and creates a GitHub Release.

### Supported Platforms

| Platform | Architecture | DuckDB Platform |
|----------|--------------|-----------------|
| Linux    | x86_64       | `linux_amd64`   |
| Linux    | ARM64        | `linux_arm64`   |
| macOS    | ARM64        | `osx_arm64`     |
| Windows  | x86_64       | `windows_amd64` |

## Creating a Release

### Automatic (Recommended)

Bump the `version:` field in `description.yml`, commit to `main`, and push. The pipeline handles the rest.

```bash
# Edit description.yml to bump version
git add description.yml
git commit -m "chore: bump extension version to 0.2.2"
git push origin main
```

### Manual Tag

```bash
git checkout main && git pull
git tag v0.2.2
git push origin v0.2.2
```

### Manual Workflow Dispatch (Fallback)

If the automated flow fails, trigger a release manually:

**GitHub CLI:**

```bash
gh workflow run release-unsigned.yml -f version=v0.2.2
```

**GitHub UI:**

1. Go to Actions > Release Extension.
2. Click "Run workflow".
3. Enter the version tag (e.g., `v0.2.2`).

## DuckDB Version Updates

The `duckdb-update-check.yml` workflow runs daily at 06:00 UTC to check for new DuckDB releases. When a new version is found, it:

1. Creates a branch `feat/duckdb-v{version}`.
2. Updates the DuckDB submodule and CI workflow.
3. Bumps the extension patch version in `description.yml`.
4. Creates a Pull Request with auto-merge enabled.

After the PR merges, the auto-tag and release workflows handle the rest.

### Prerequisites

The update check workflow requires a Personal Access Token (PAT) stored as `WORKFLOW_UPDATE_TOKEN`. See [SETUP.md](SETUP.md) for configuration details.

## Verifying a Release

1. Check the [Releases](https://github.com/cofin/duckdb-oracle/releases) page.
2. Verify the GitHub Pages deployment has the correct DuckDB version directory.
3. Test installation:

```sql
-- Start DuckDB with unsigned flag
-- ./duckdb -unsigned

SET custom_extension_repository = 'https://cofin.github.io/duckdb-oracle';
INSTALL oracle;
LOAD oracle;
```

See [COMPATIBILITY.md](COMPATIBILITY.md) for the current compatibility matrix.
