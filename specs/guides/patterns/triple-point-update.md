# Triple-Point Update Pattern

**Context**: Updating DuckDB version.

**Problem**: Updating the DuckDB version is not just about changing one file.

**Solution**:
Synchronized update of:
1.  **Submodules**: `duckdb` (core) and `extension-ci-tools` (build system). Both must match the target version tag (e.g., `v1.4.3`).
2.  **Workflow Inputs**: `main-distribution-pipeline.yml` inputs `duckdb_version` and `ci_tools_version`.
3.  **Build Config**: Sometimes `extension_config.cmake` or `CMakeLists.txt` if version-specific flags change (though often stable).
