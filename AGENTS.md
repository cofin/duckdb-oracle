# AGENTS.md — `duckdb-oracle`

Native C++17 DuckDB extension (`v0.3.1`, targeting DuckDB `v2.0.0` LTS on `main` and DuckDB `v1.5.6` on `feat/duckdb-v1.5.6`) providing direct read and write access to Oracle databases via in-tree OCI C ABI declarations and lazy runtime dynamic loading (`dlopen`/`LoadLibraryA`).

## Context Pointers

- **OKF v0.2 Bundle Root**: [.agents/bundles/index.md](.agents/bundles/index.md)
- **Project Workflow & Verification Gates**: [.agents/bundles/knowledge/workflow.md](.agents/bundles/knowledge/workflow.md)
- **Knowledge Base Index**: [.agents/bundles/knowledge/index.md](.agents/bundles/knowledge/index.md)
- **Project-Local Skills & Subagents**: [.agents/skills/index.md](.agents/skills/index.md)
  - Oracle OCI (`OCI`): [.agents/skills/oracle-oci/SKILL.md](.agents/skills/oracle-oci/SKILL.md) | [.agents/skills/review-oracle-oci/SKILL.md](.agents/skills/review-oracle-oci/SKILL.md)
  - DuckDB Extension (`DDB`): [.agents/skills/duckdb-extension/SKILL.md](.agents/skills/duckdb-extension/SKILL.md) | [.agents/skills/review-duckdb-extension/SKILL.md](.agents/skills/review-duckdb-extension/SKILL.md)

## Canonical Developer Commands

```bash
# Install Oracle Instant Client runtime for local integration tests (Linux)
make configure_oci

# Build release or debug extension (requires zero Oracle SDK installed)
make release
make debug

# Run fast offline unit / smoke tests (zero Oracle SDK or container required)
make test-unit

# Run a single sqllogictest file during tight TDD loops
./build/release/test/unittest "test/unit_tests/test_public_surface.test"

# Run clang-format and clang-tidy verification
make lint

# Run full containerized Oracle 23ai integration test suite (gvenzl/oracle-free:23-slim)
make integration
SKIP_BUILD=1 make integration

# Bump extension version in description.yml
make bump-version bump=patch
```

## Non-Negotiable Invariants

- **Zero Compile-Time / Load-Time OCI SDK Dependency**: Declare all OCI symbols in-tree (`src/include/oracle_oci_api.hpp`), link only against `${CMAKE_DL_LIBS}` in `CMakeLists.txt`, and load `libclntsh.so` / `libclntsh.dylib` / `oci.dll` lazily on first connection (`src/oracle_oci_api.cpp`). `make test-unit` and `LOAD oracle;` must succeed without Oracle Instant Client installed.
- **DuckDB 2.0 Vector Cardinality**: Always call `output.SetChildCardinality(count)` (never `SetCardinalityUnsafe(count)`) after mutating flat vectors via `FlatVector::GetDataMutable`, `FlatVector::SetNull`, or `StringVector::AddString`.
- **RAII Handle & Pool Safety**: Keep `std::shared_ptr<OCIEnv> env_owner` as the first member of `OracleContext`, wrap statements in `OCIHandlePtr<OCIStmt>`, roll back uncommitted transactions (`OCITransRollback`) and check `OCIPing` on connection return, and never open nested metadata connections while holding a pooled connection when `oracle_connection_limit = 1`.
- **Git & Release Hygiene**: Use signed Conventional Commits (`<type>(<scope>): <summary>`). Never create or mutate Git tags, never push without explicit user instruction, and never overwrite `variegata:` with `vcpkg_commit`.

<!-- project-customization: start -->
<!-- project-customization: end -->
