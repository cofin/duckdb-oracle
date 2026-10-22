# Release Process

This document describes how releases are built and published for the DuckDB Oracle extension.

## Versioning

The project tracks two versions:

- **Extension version** (e.g., `v0.3.0`): Defined in `description.yml`. Used for Git tags, GitHub Releases, and the version reported by `duckdb_extensions()`.
- **DuckDB version** (e.g., `v2.0.0`): The DuckDB version the extension is compiled against. Used in the GitHub Pages layout expected by DuckDB's extension installer:

```text
https://cofin.github.io/duckdb-oracle/{duckdb_version}/{platform}/oracle.duckdb_extension.gz
```

## Automated Release Pipeline

1. **Version bump**: Update the version in `description.yml` on a branch and merge via pull request.
2. **Auto-tag** (`auto-tag.yml`): On push to `main`, reads `description.yml` and creates tag `v{version}` if it does not already exist.
3. **Build & deploy** (`release-unsigned.yml`): Triggered by the new tag. Builds binaries for all supported platforms, publishes compressed extensions to the `gh-pages` branch, and creates a GitHub Release with platform archives and checksums.

### Supported Platforms

| Platform | Architecture | DuckDB Target |
|----------|--------------|---------------|
| Linux    | x86_64       | `linux_amd64` |
| Linux    | ARM64        | `linux_arm64` |
| macOS    | Apple Silicon| `osx_arm64`   |
| Windows  | x86_64       | `windows_amd64` |

## Cutting a Release

Bump the version on a feature branch and open a pull request targeting `main`:

```bash
make bump-version bump=patch
git add .bumpversion.toml description.yml test/unit_tests/test_extension_metadata.test
git commit -S -m "chore(release): bump extension to v0.3.1"
```

Once merged to `main`, `auto-tag.yml` tags the commit and triggers `release-unsigned.yml`.

### Manual Trigger (Fallback)

If a tag build needs to be re-run manually:

```bash
gh workflow run release-unsigned.yml -f version=v0.3.0
```

## DuckDB Version Upgrades

The `duckdb-update-check.yml` workflow checks daily for new stable DuckDB releases. When a new release is detected, it runs `make bump-duckdb VERSION=v{version}` on a `feat/duckdb-v{version}` branch and opens a pull request for review. See [UPDATING.md](UPDATING.md) for manual upgrade commands and [SETUP.md](SETUP.md) for token configuration.
