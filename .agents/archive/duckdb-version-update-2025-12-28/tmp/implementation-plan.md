## Implementation Plan

### Files to Create/Modify

1.  `.github/workflows/integration-tests.yml` (Renamed from `oracle-ci.yml`)
2.  `.github/workflows/duckdb-update-check.yml` (Fix deprecated syntax)
3.  `duckdb` submodule (Verify/Update)
4.  `extension-ci-tools` submodule (Update)
5.  `docs/COMPATIBILITY.md` (Update)
6.  `docs/SETUP.md` (Create)

### Pattern-Guided Implementation Steps

**Step 1: Renaming Workflow (Split-Pipeline Pattern)**
-   Rename `.github/workflows/oracle-ci.yml` to `.github/workflows/integration-tests.yml`.
-   Update `name` field in YAML to "Integration Tests".

**Step 2: Fixing Automation (Secret-Dependent Automation)**
-   Update `duckdb-update-check.yml` to use `echo "key=value" >> $GITHUB_OUTPUT`.
-   Verify `WORKFLOW_UPDATE_TOKEN` is used (it is already in the file, just need to document it).

**Step 3: Triple-Point Update**
-   Ensure `duckdb` submodule is at v1.4.3.
-   Update `extension-ci-tools` to v1.4.3.
-   Ensure `main-distribution-pipeline.yml` uses v1.4.3.

**Step 4: Pattern Compliance**
-   Grep for `Substrait` in `src/`.
