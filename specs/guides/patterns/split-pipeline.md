# Split-Pipeline CI Pattern

**Context**: Used in `integration-tests.yml` vs `main-distribution-pipeline.yml`.

**Problem**: DuckDB extensions often need complex integration tests (like running an Oracle container) that are not supported by the standard `extension-ci-tools` reusable workflows, or require specific OS/Environment setups that are wasteful to run for every build matrix entry.

**Solution**:
1.  **Main Pipeline**: Uses `duckdb/extension-ci-tools` reusable workflow for cross-platform compilation, artifact generation, and unit tests.
2.  **Integration Pipeline**: A separate workflow file (e.g., `integration-tests.yml`) that runs specifically on Linux (or where docker is available) to run `make integration`.

**When to Use**: When your extension requires external services (databases, APIs) for testing that are heavy or platform-specific.
