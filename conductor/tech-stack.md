# Technology Stack

## Core Development
- **Language:** C++17 (Follows DuckDB extension requirements)
- **Framework:** DuckDB Extension Framework
- **Interface:** Oracle Call Interface (OCI) - Primary SDK for high-performance Oracle database interaction

## Dependencies
- **Oracle Instant Client:** Required at build time (SDK) and runtime (Libraries) for OCI support.
- **OpenSSL:** Managed via vcpkg for secure connection support.

## Build & Infrastructure
- **Build System:** CMake (Minimum 3.10)
- **Dependency Manager:** vcpkg
- **Build Tool:** GNU Make (wrapper around CMake)

## Quality Assurance & Testing
- **SQL Tests:** DuckDB SQL test runner (`.test` files) for regression and unit testing.
- **Integration Tests:** Shell-based test suite (`scripts/test_integration.sh`) utilizing containerized Oracle instances (Docker/Podman).
- **Static Analysis:** `clang-tidy` for code quality and `clang-format` for consistent style.

## CI/CD
- **Automation:** GitHub Actions for multi-platform distribution pipelines (Linux, macOS, Windows) and automated testing.
