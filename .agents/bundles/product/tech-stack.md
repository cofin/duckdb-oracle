---
type: Guide
title: Tech Stack
description: C++17, DuckDB 2.0 Extension API, lazy runtime OCI loader (19c/21c/23ai), CMake/Ninja build system, and test harnesses.
tags: [cpp, duckdb, oracle, oci, cmake, tech-stack]
status: stable
---

# Tech Stack

<!-- truth: start -->
- Language: C++17 (`CMAKE_CXX_STANDARD 17`) for extension source, Bash scripts for CI/integration environment setup.
- Core APIs: DuckDB 2.0 Extension API (`DUCKDB_CPP_EXTENSION_ENTRY(oracle, loader)`, `duckdb::Identifier`), in-tree Oracle Call Interface C ABI (`src/include/oracle_oci_api.hpp` + `src/oracle_oci_api.cpp`) with lazy runtime `dlopen`/`LoadLibraryA` resolution for Oracle Instant Client 19c, 21c, and 23ai.
- Build System: CMake (`>= 3.15`), Ninja, Make, `extension-ci-tools` submodule (`v2.0-cyanoptera`), linked only with `${CMAKE_DL_LIBS}`.
- Tooling: vcpkg (`vcpkg.json`), `bump-my-version` via `uvx`, `clang-format`, `clang-tidy`.
- Test Suites: DuckDB `unittest`, `test/unit_tests` (runs without Oracle Instant Client installed), containerized integration tests (`test/integration_tests` via `gvenzl/oracle-free:23-slim`).
<!-- truth: end -->

## Components & Dependencies
- **DuckDB**: Upstream submodule pinned to `v2.0.0` (`d54cbbe3df17e8e5ce68312f483b4155836112aa`, extension `v0.3.0`).
- **OCI Runtime Loader**: In-tree `oracle_oci_api.hpp` / `oracle_oci_api.cpp` dynamically resolving 22 OCI functions at runtime; Linux integration test runtime setup via `scripts/setup_oci_linux.sh` (`basic.zip` only).
- **Packaging & CI**: GitHub Actions workflows in `.github/workflows/` (`v2.0-cyanoptera` reusable workflows) and `description.yml` aligned with `duckdb/community-extensions`.
