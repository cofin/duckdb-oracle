# Technology Stack: DuckDB Oracle Extension

## Language & Standards
- **Primary Language**: C++ (C++17 in practice; DuckDB minimum is C++11)
- **Header Guards**: `#pragma once`
- **Namespace**: All code in `duckdb::` namespace

## Build System
- **Generator**: CMake 3.10+ (Ninja preferred when available)
- **Build Wrapper**: GNU Make
- **Package Manager**: vcpkg (OpenSSL 3.0.8)
- **Toolchain**: GCC/Clang (Linux), AppleClang (macOS), MSVC (Windows)

## Key Build Commands
```bash
make release              # Production build
make debug                # Debug build with symbols
make test                 # Unit tests only (no Oracle needed)
make integration          # Full suite with Oracle container
make configure_ci         # Install OCI prerequisites
make format               # Run clang-format
make tidy-check           # Run clang-tidy static analysis
```

## Primary Dependencies
- **DuckDB Extension API**: v1.4.4 (pinned in CI workflows)
- **DuckDB CI Tools**: v1.4.4 (`extension-ci-tools` submodule)
- **Oracle Call Interface (OCI)**: Instant Client SDK 23.6
- **OpenSSL**: 3.0.8 (via vcpkg)

## Testing
- **Unit Tests**: DuckDB `sqllogictest` framework (`test/unit_tests/`)
- **Integration Tests**: Require Oracle container (`test/integration_tests/`)
- **Oracle Container**: `gvenzl/oracle-free:23-slim` (Oracle 23ai Free)
- **Test Runner**: `build/release/duckdb_unittest`

## Format & Lint
- **Formatter**: `clang-format` (DuckDB style rules)
- **Linter**: `clang-tidy`

## CI/CD (GitHub Actions)
| Workflow | Trigger | Purpose |
|----------|---------|---------|
| `integration-tests.yml` | Push/PR | Build + test on Linux with OCI |
| `main-distribution-pipeline.yml` | Auto | Multi-platform release via DuckDB CI tools |
| `release-unsigned.yml` | Manual | Semver-tagged unsigned release builds |
| `auto-tag.yml` | Version change | Auto-tag on version updates |
| `duckdb-update-check.yml` | Scheduled | Monitor upstream DuckDB releases |

## Platform Support
| Platform | Status | Notes |
|----------|--------|-------|
| Linux x86_64 | Supported | Primary CI target |
| Linux aarch64 | Supported | ARM64 builds |
| macOS arm64 | Supported | Apple Silicon only |
| Windows x86_64 | Supported | MSVC builds |
| macOS x86_64 | Excluded | No OCI Instant Client for Intel Mac |
| WebAssembly | Excluded | Cannot link OCI native libraries |

## Extension Metadata
- **Version**: 1.0.0
- **License**: Apache-2.0
- **Registry**: DuckDB Community Extensions (`description.yml`)
