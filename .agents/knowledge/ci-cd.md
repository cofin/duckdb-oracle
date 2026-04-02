# CI/CD — Build, Test & Release

## Build System

### Local Build
```bash
make release              # CMake + Ninja (or Make), links OCI + OpenSSL
make debug                # Debug symbols, assertions enabled
```

**Prerequisites:**
- Oracle Instant Client SDK in `ORACLE_HOME` (or auto-detected paths)
- vcpkg for OpenSSL 3.0.8
- CMake 3.10+, Ninja (preferred) or Make

### OCI SDK Detection Order (CMakeLists.txt)
1. `$ORACLE_HOME/sdk/include` + `$ORACLE_HOME` libs
2. `oracle_sdk/instantclient_*/sdk/include` (local CI install)
3. `/usr/share/oracle/instantclient_23_26` (system fallback)

## Test Infrastructure

### Unit Tests (`make test`)
- No Oracle container needed
- Tests in `test/unit_tests/*.test` (sqllogictest format)
- Runner: `build/release/duckdb_unittest`
- Tests: attach/scan, secrets, pushdown, settings, error handling

### Integration Tests (`make integration`)
- Requires Oracle container: `gvenzl/oracle-free:23-slim`
- Tests in `test/integration_tests/*.test`
- Setup: `init_sql/01_setup.sql` creates test user, tables, data
- Connection via `${ORACLE_CONNECTION_STRING}` placeholder
- Coverage: write (basic, vector, spatial, LOBs), read (JSON, XML, partitioning, pipelined, views)

## GitHub Actions Workflows

### `integration-tests.yml` (PR/Push)
- Ubuntu latest
- Installs libaio + Oracle Instant Client 23.6
- Builds extension, runs unit + integration tests
- Primary quality gate for PRs

### `main-distribution-pipeline.yml` (Auto)
- Uses DuckDB's official `extension-ci-tools` reusable workflow
- **DuckDB version and CI tools version must match exactly** (currently v1.4.4)
- Builds for all supported platforms automatically

### `release-unsigned.yml` (Manual)
- Triggered with semver tag input (e.g., `v1.0.0`)
- Build matrix: Linux x86_64/aarch64, macOS arm64, Windows x86_64
- Platform-specific OCI setup scripts in `scripts/`
- Creates GitHub release with binary artifacts

### `auto-tag.yml`
- Monitors version changes, creates git tags automatically

### `duckdb-update-check.yml`
- Scheduled check for upstream DuckDB releases
- Alerts when new DuckDB version available

## Known CI Issues

- **GITHUB_TOKEN limitation**: Cannot modify `.github/workflows/` files. Any workflow changes require a PAT with `workflows` permission scope.
- **macOS Intel excluded**: No Oracle Instant Client available for x86_64 macOS
- **WebAssembly excluded**: OCI is a native C library, cannot compile to WASM

## Release Process

1. Ensure all tests green on `main`
2. Tag with semver: `git tag v1.x.x && git push --tags`
3. `release-unsigned.yml` triggers automatically
4. Verify artifacts for all 4 platforms
5. Update `description.yml` if extension metadata changed
6. Community extension submission via DuckDB registry PR

## DuckDB Version Upgrades

When DuckDB releases a new version:
1. Update `duckdb_version` in `main-distribution-pipeline.yml`
2. Update `extension-ci-tools` submodule to matching version
3. Update `release-unsigned.yml` if it pins versions
4. Run full test suite: `make test && make integration`
5. Check for API breaking changes in DuckDB changelog
6. Tag new release matching DuckDB version scheme
