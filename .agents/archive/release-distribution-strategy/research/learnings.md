# Release and Distribution Strategy - Learnings and Progress

**Status**: Complete (Phase 3) ✅
**Date**: 2025-11-24

## Progress Summary

We have successfully implemented a robust release and distribution strategy for the DuckDB Oracle extension.

### 1. Unsigned Release Workflow (Phase 1)

We created a GitHub Actions workflow (`.github/workflows/release-unsigned.yml`) that:
- Triggers on `v*` tags.
- Builds unsigned binaries for Linux (x86_64, ARM64), macOS (ARM64), and Windows (x86_64).
- **Learning**: Explicitly including the DuckDB version in the artifact name (e.g., `oracle-v0.1.0-duckdb-v1.4.1-...`) is crucial for user clarity, as extensions are ABI-specific.
- **Refinement**: macOS x86_64 was dropped from the matrix as Oracle Instant Client 19c+ support is limited/legacy on that platform and ARM64 is the priority.

### 2. Automated DuckDB Version Updates (Phase 2)

We implemented an automated check (`.github/workflows/duckdb-update-check.yml`) that:
- Runs daily.
- Compares the current project version against the latest DuckDB release.
- **Learning**: Creating a Pull Request is superior to just creating an issue. It allows CI to run immediately on the new version, verifying compatibility before a human even reviews it.
- We established a `docs/COMPATIBILITY.md` matrix to track tested versions.

### 3. Community Extension Preparation (Phase 3)

We prepared the repository for submission to the DuckDB Community Extensions registry:
- **Documentation**: Created `CONTRIBUTING.md` and `SECURITY.md`. Added `CODEOWNERS`.
- **Metadata**: Created `description.yml` with full extension details.
- **Test Restructuring**:
    - Renamed `test/sql` to `test/unit_tests` for clarity.
    - Renamed `test/integration` to `test/integration_tests`.
    - Normalized test filenames to `test_*.test`.
    - **Critical Fix**: Updated `scripts/test_integration.sh` to dynamically replace `${ORACLE_PORT}` and `${ORACLE_CONNECTION_STRING}` in test files, allowing standard `.test` files to run against the ephemeral container.
    - **Coverage**: Added robust tests for:
        - Partitioning (Range/List)
        - Vectors (Oracle 23ai)
        - JSON types
        - Views & Materialized Views
        - Pipelined Functions
        - XML & BLOBs
- **Code Quality**: Verified `make format` compliance.

### 4. OCI Array Fetch & Data Corruption (Critical Fixes)

We encountered and resolved severe data corruption issues during OCI Array Fetch (`OCIStmtFetch2`):

- **Symptoms**:
    - Data from one column appearing in the buffer of the next column ("column shift").
    - Garbage values (e.g., `1.0` double appearing in a binary buffer) when mixing `NUMBER` (bound as `SQLT_FLT`) and `RAW`/`BLOB` columns.
    - `ORA-00932` (incompatible data type) when fetching `BLOB` as `SQLT_STR`.

- **Root Cause**:
    - Appears to be an internal OCI driver issue regarding buffer alignment/stride calculation in `OCIDefineArrayOfStruct` when mixing specific types (Float/Double) with variable-length types, or when buffer sizes vary significantly.

- **Solution Implemented**:
    - **Universal String Binding for Numbers**: We switched `BIGINT` and `DOUBLE` binding from `SQLT_INT`/`SQLT_FLT` to `SQLT_STR`. We parse the string results in the query function. This eliminated the binary layout corruption.
    - **Explicit LOB Types**: `BLOB` columns are now bound as `SQLT_LBI` (Long Binary) and `CLOB` as `SQLT_LNG` (Long) to support inline fetching.
    - **RAW as String**: `RAW` columns are bound as `SQLT_STR` (Hex String) for maximum stability. DuckDB handles the hex-to-blob cast automatically.
    - **Minimum Buffer Alignment**: Enforced a minimum buffer size of 4000 bytes for all columns to ensure consistent memory layout.

### 5. Schema Resolution Robustness

- **Issue**: The `oracle_use_current_schema` setting was not reliably mapping DuckDB's default `main` schema to the Oracle user's schema in all paths (e.g., `ATTACH` vs explicit usage).
- **Fix**: Enhanced `OracleSchemaGenerator` to explicitly intercept `main` (and `DEFAULT_SCHEMA`) requests and redirect them to the detected current Oracle schema.

## Key Technical Insights

### Oracle Catalog & DDL

- **Caching Issue**: The DuckDB catalog caches table existence. When using `oracle_execute` to perform DDL (create tables/views), the catalog is not automatically aware of the changes.
- **Solution**: We must explicitly call `oracle_clear_cache()` in tests after DDL operations to force a metadata refresh. This ensures subsequent `SELECT` statements see the new objects.

### Oracle Connection Strings in Tests

- **Challenge**: Integration tests need to connect to a dynamic port exposed by the Docker container.
- **Solution**: We used a placeholder `${ORACLE_CONNECTION_STRING}` in `.test` files. The runner script (`test_integration.sh`) uses `sed` to replace this with the actual connection string into a temporary test file before execution.
- **Refinement**: We aligned `oracle_execute` to resolve attached database aliases (e.g., `oracle_execute('ora', ...)`) to simplify tests, but the dynamic connection string approach remains vital for the initial `ATTACH`.

## Next Steps

1.  **Tag Release**: Tag `v0.1.0` to trigger the first unsigned release.
2.  **Community PR**: Submit the `description.yml` to `duckdb/community-extensions`.
3.  **Maintenance**: Monitor the automated update checks.

## Artifacts

- `.github/workflows/release-unsigned.yml`
- `.github/workflows/duckdb-update-check.yml`
- `docs/RELEASE.md`
- `docs/UPDATING.md`
- `docs/COMPATIBILITY.md`
- `CONTRIBUTING.md`
- `SECURITY.md`
- `specs/active/release-distribution-strategy/research/description.yml`
- `specs/guides/oci-array-fetch-quirks.md`