# New Patterns Discovered

## Split-Pipeline CI Pattern

**Context**: Used in `integration-tests.yml` vs `main-distribution-pipeline.yml`.

**Problem**: DuckDB extensions often need complex integration tests (like running an Oracle container) that are not supported by the standard `extension-ci-tools` reusable workflows, or require specific OS/Environment setups that are wasteful to run for every build matrix entry.

**Solution**:
1.  **Main Pipeline**: Uses `duckdb/extension-ci-tools` reusable workflow for cross-platform compilation, artifact generation, and unit tests.
2.  **Integration Pipeline**: A separate workflow file (e.g., `integration-tests.yml`) that runs specifically on Linux (or where docker is available) to run `make integration`.

**When to Use**: When your extension requires external services (databases, APIs) for testing that are heavy or platform-specific.

## Triple-Point Update Pattern

**Context**: Updating DuckDB version.

**Problem**: Updating the DuckDB version is not just about changing one file.

**Solution**:
Synchronized update of:
1.  **Submodules**: `duckdb` (core) and `extension-ci-tools` (build system). Both must match the target version tag (e.g., `v1.4.3`).
2.  **Workflow Inputs**: `main-distribution-pipeline.yml` inputs `duckdb_version` and `ci_tools_version`.
3.  **Build Config**: Sometimes `extension_config.cmake` or `CMakeLists.txt` if version-specific flags change (though often stable).

## Secret-Dependent Automation Pattern

**Context**: `duckdb-update-check.yml`

**Problem**: `GITHUB_TOKEN` cannot modify `.github/workflows/` files.

**Solution**: Use a PAT (`WORKFLOW_UPDATE_TOKEN`) for git operations in the update workflow.

**Code Example**:
```yaml
      - name: Checkout
        uses: actions/checkout@v4
        with:
          token: ${{ secrets.WORKFLOW_UPDATE_TOKEN }}
```
