# Contributing to DuckDB Oracle Extension

Thank you for your interest in contributing to the DuckDB Oracle Extension! We welcome contributions from the community.

## Development Environment Setup

### Prerequisites

- **CMake**: 3.10 or newer
- **C++ Compiler**: C++17 compatible (GCC, Clang, MSVC)
- **Python**: 3.11 or newer
- **Oracle Instant Client**: Basic and SDK packages
- **DuckDB**: The repository includes DuckDB as a submodule

### Setup Steps

1.  **Clone the repository**:
    ```bash
    git clone --recursive https://github.com/duckdb/duckdb-oracle.git
    cd duckdb-oracle
    ```

2.  **Install Oracle Instant Client**:
    -   **Linux**: Run `./scripts/setup_oci_linux.sh`
    -   **macOS (ARM64)**: Run `./scripts/setup_oci_macos.sh`
    -   **Windows**: Run `powershell ./scripts/setup_oci_windows.ps1`

    The scripts will install the client to `./oracle_sdk` and print the environment variables you need to export.

3.  **Export Environment Variables**:
    ```bash
    export ORACLE_HOME=$(pwd)/oracle_sdk/instantclient_23_6 # Adjust version as needed
    export LD_LIBRARY_PATH=$ORACLE_HOME:$LD_LIBRARY_PATH    # Linux
    export DYLD_LIBRARY_PATH=$ORACLE_HOME:$DYLD_LIBRARY_PATH # macOS
    ```

4.  **Build the Extension**:
    ```bash
    make release
    ```

## Running Tests

We use DuckDB's SQL test runner.

1.  **Run Unit Tests** (Smoke tests, no Oracle DB required):
    ```bash
    make test
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
