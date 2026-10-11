---
type: Guide
title: Tech Stack
---

# Tech Stack

<!-- truth: start -->
- Language: C++ (C++11/C++17) for extension source, Bash scripts for CI/environment setup.
- Core APIs: DuckDB Extension API, Oracle Call Interface (OCI / Oracle Instant Client 19c/21c/23ai).
- Build System: CMake, Ninja, Make, `extension-ci-tools` submodule.
- Tooling: vcpkg (`vcpkg.json`), `bump-my-version` via `uvx`, `clang-format`, `clang-tidy`.
- Test Suites: DuckDB `unittest`, `test/unit_tests`, containerized integration tests (`test/integration_tests` via `gvenzl/oracle-free:23-slim`).
<!-- truth: end -->

## Components & Dependencies
- **DuckDB**: Upstream submodule pinned to v1.5.5.
- **OCI SDK**: Linux Instant Client setup via `scripts/setup_oci_linux.sh`, macOS via `scripts/setup_oci_macos.sh`, Windows via `scripts/setup_oci_windows.ps1`.
- **Packaging & CI**: GitHub Actions workflows in `.github/workflows/`.
