# Contributing to DuckDB Oracle Extension

Thank you for your interest in contributing to the DuckDB Oracle Extension! We welcome contributions from the community.

## Development Environment Setup

### Prerequisites

- **CMake**: 3.15 or newer
- **C++ Compiler**: C++17 compatible (GCC, Clang, MSVC)
- **Python**: 3.11 or newer
- **DuckDB**: The repository includes DuckDB (`v2.0.0`) as a submodule
- **Oracle Instant Client Basic** *(optional, for live integration tests only)*: Building the extension and running unit tests require **zero** external Oracle SDK headers or libraries (`libclntsh` is loaded lazily via `dlopen`/`LoadLibraryA` when opening a live Oracle connection).

### Setup Steps

1.  **Clone the repository**:
    ```bash
    git clone --recursive https://github.com/cofin/duckdb-oracle.git
    cd duckdb-oracle
    ```

2.  **Build the Extension** (no Oracle Instant Client needed):
    ```bash
    make release
    ```

3.  *(Optional)* **Install Oracle Instant Client Basic for Live Integration Tests**:
    -   **Linux**: Run `./scripts/setup_oci_linux.sh` (or `make configure_oci`), which downloads Oracle Instant Client Basic to `./oracle_sdk`.
    -   **macOS / Windows**: Download Oracle Instant Client Basic (19c, 21c, or 23ai) and set `OCI_LIB_DIR` or `ORACLE_HOME` (or add to `DYLD_LIBRARY_PATH` / `PATH`).

## Running Tests

We use DuckDB's SQL test runner.

1.  **Run Unit Tests** (Smoke tests, no Oracle DB required):
    ```bash
    make test-unit
    ```

2.  **Run Integration Tests** (Requires Docker/Podman):
    ```bash
    make integration
    ```
    This will spin up a local Oracle Database container and run the full test suite against it.

## Code Style

We follow DuckDB's coding standards.

-   **Formatting**: Run `make format` to automatically format C++ code.
-   **Linting**: Run `make tidy-check` to run clang-tidy.

Please ensure your code passes these checks before submitting a PR.

## Submitting a Pull Request

1.  Fork the repository.
2.  Create a new branch for your feature or fix.
3.  Commit your changes (please use conventional commits, e.g., `feat: add new function`).
4.  Push to your fork and submit a Pull Request.
5.  Ensure all CI checks pass.

## Reporting Issues

Please use the GitHub Issues tracker to report bugs or request features. Include as much detail as possible, including:
-   DuckDB version
-   Extension version
-   Platform/OS
-   Oracle Database version
-   Reproduction steps
