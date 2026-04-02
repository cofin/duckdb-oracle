# Pattern Analysis: DuckDB Version Update

## 1. Workflow Architecture Patterns

### Separation of Concerns
The project uses two distinct CI pipelines:
1.  **Distribution Pipeline** (`main-distribution-pipeline.yml`):
    *   **Purpose**: Cross-platform compilation, artifact generation, and standard unit testing.
    *   **Mechanism**: Reuses upstream `duckdb/extension-ci-tools`.
    *   **Key Characteristic**: Does NOT run heavy integration tests (Docker containers).
2.  **Integration Pipeline** (`oracle-ci.yml`):
    *   **Purpose**: Deep testing with real Oracle database.
    *   **Mechanism**: Custom workflow running `make integration`.
    *   **Key Characteristic**: Linux-only (usually), requires Docker service.

**Insight**: Do NOT delete `oracle-ci.yml`. It is critical for quality assurance. Rename to `integration-tests.yml` to clarify intent.

## 2. Version Update Patterns

### Triple-Point Update
Updating DuckDB requires changes in three locations:
1.  **Submodules**: `duckdb` and `extension-ci-tools` must be updated via git.
2.  **Workflow Configuration**: `main-distribution-pipeline.yml` has hardcoded version pins (inputs to reusable workflows).
3.  **Build Configuration**: `CMakeLists.txt` or `extension_config.cmake` might have version checks (though usually handled by submodule include).

### Automated Updates
*   **Trigger**: `duckdb-update-check.yml` (Daily Cron).
*   **Blocker**: Requires `WORKFLOW_UPDATE_TOKEN` because `GITHUB_TOKEN` cannot modify `.github/workflows/` files.
*   **Pattern**: Create branch -> Update Submodule -> Update Workflow File -> PR -> Auto-Merge.

## 3. Extension CI Tools Pattern
*   DuckDB maintains `extension-ci-tools` with tags/branches matching DuckDB releases (e.g., `v1.4.1`, `v1.4.3`).
*   **Best Practice**: Always pin `extension-ci-tools` to the SAME version tag as the DuckDB core to ensure ABI compatibility and build toolchain alignment.

## 4. Key Findings for v1.4.3 Update
*   **Target**: DuckDB v1.4.3 (LTS).
*   **Missing Link**: The `oracle-ci.yml` redundancy assumption in original PRD was incorrect. It serves a distinct purpose.
*   **Action**: Rename `oracle-ci.yml` -> `integration-tests.yml`.
