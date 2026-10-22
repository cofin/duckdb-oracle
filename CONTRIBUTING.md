# Contributing

Contributions to the DuckDB Oracle extension are welcome.

## Prerequisites

- **CMake**: 3.15 or newer
- **C++ Compiler**: C++17 compatible (GCC, Clang, or MSVC)
- **Python**: 3.11 or newer
- **Oracle Instant Client Basic** *(optional, for live integration tests only)*: Building the extension and running unit tests require no external Oracle headers or libraries.

## Building & Testing

1. **Clone the repository with submodules**:
   ```bash
   git clone --recursive https://github.com/cofin/duckdb-oracle.git
   cd duckdb-oracle
   ```

2. **Build the extension**:
   ```bash
   make release
   ```

3. **Run unit tests** (no Oracle database or client required):
   ```bash
   make test-unit
   ```

4. **Run live integration tests** (requires Docker or Podman):
   ```bash
   make integration
   ```
   On Linux, `./scripts/setup_oci_linux.sh` (or `make configure_oci`) downloads Oracle Instant Client Basic into `./oracle_sdk`. On macOS or Windows, install Oracle Instant Client Basic (19c, 21c, or 23ai) and set `OCI_LIB_DIR` or `ORACLE_HOME`.

## Code Style

Format and lint C++ changes before opening a pull request:

```bash
make format
make tidy-check
```

## Pull Requests & Issues

- Create a feature branch from `main` and use conventional commit messages (e.g., `feat: ...`, `fix: ...`, `docs: ...`).
- When reporting an issue on GitHub, include your DuckDB version, extension version, operating system, Oracle Database version, and a minimal SQL reproduction.
