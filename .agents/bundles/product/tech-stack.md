---
type: Guide
title: Tech Stack
description: C++17, DuckDB 2.0 Extension API, lazy runtime OCI loader (19c/21c/23ai), CMake/Ninja build system, and test harnesses.
status: stable
tags: [cpp, duckdb, oracle, oci, cmake, tech-stack]
generated:
  by: flow/0.27.0
  at: 2026-11-07T17:36:00Z
verified:
  - by: agent:flow-setup
    at: 2026-11-07T17:36:00Z
    commit: 36539f3
sources:
  - id: cmake-lists
    resource: CMakeLists.txt
    title: C++17 CMake build and CMAKE_DL_LIBS link target
  - id: ext-config
    resource: extension_config.cmake
    title: DuckDB extension load and static link registration
  - id: makefile
    resource: Makefile
    title: Build, lint, unit test, integration test, and version bump targets
---

# Tech Stack

<!-- truth: start -->
- Language: C++17 (`CMAKE_CXX_STANDARD 17`) for extension source, Bash scripts for CI/integration environment setup[^cmake-lists].
- Core APIs: DuckDB 2.0 Extension API (`DUCKDB_CPP_EXTENSION_ENTRY(oracle, loader)`, `duckdb::Identifier`), in-tree Oracle Call Interface C ABI (`src/include/oracle_oci_api.hpp` + `src/oracle_oci_api.cpp`) with lazy runtime `dlopen`/`LoadLibraryA` resolution for Oracle Instant Client 19c, 21c, and 23ai[^cmake-lists].
- Build System: CMake (`>= 3.15`), Ninja, Make, `extension-ci-tools` submodule (`v2.0-cyanoptera`), linked only with `${CMAKE_DL_LIBS}`[^cmake-lists][^ext-config].
- Tooling: vcpkg (`vcpkg.json`), `bump-my-version` via `uvx`, `clang-format`, `clang-tidy`[^makefile].
- Test Suites: DuckDB `unittest`, `test/unit_tests` (runs without Oracle Instant Client installed), containerized integration tests (`test/integration_tests` via `gvenzl/oracle-free:23-slim`)[^makefile].
<!-- truth: end -->

## Components & Dependencies
- **DuckDB**: Upstream submodule pinned to `v2.0.0` (`d54cbbe3df17e8e5ce68312f483b4155836112aa`, extension `v0.3.1`)[^ext-config].
- **OCI Runtime Loader**: In-tree `oracle_oci_api.hpp` / `oracle_oci_api.cpp` dynamically resolving 22 OCI functions at runtime; Linux integration test runtime setup via `scripts/setup_oci_linux.sh` (`basic.zip` only)[^cmake-lists].
- **Packaging & CI**: GitHub Actions workflows in `.github/workflows/` (`v2.0-cyanoptera` reusable workflows) and `description.yml` aligned with `duckdb/community-extensions`[^makefile].

[^cmake-lists]: `CMakeLists.txt` — C++17 CMake build and CMAKE_DL_LIBS link target.
[^ext-config]: `extension_config.cmake` — DuckDB extension load and static link registration.
[^makefile]: `Makefile` — Build, lint, unit test, integration test, and version bump targets.
