# Development Workflow: duckdb-oracle

**Last Updated**: 2025-11-22

## Setup

### Prerequisites

- A C++ compiler (like g++, clang, or MSVC)
- CMake
- Make (or Ninja)
- Python 3
- **Oracle Instant Client SDK**: Install the Basic + SDK packages and expose headers/libs via `ORACLE_HOME` (e.g., `/usr/lib/oracle/19.6/client64`). The CMakeLists.txt auto-detects `ORACLE_HOME` and falls back to `/usr/share/oracle/instantclient_23_26` if unset.
    - `ORACLE_HOME` must contain `sdk/include/oci.h` and `lib/libclntsh.so`.

### Installation

1.  **Clone the repository with submodules:**
    ```sh
    git clone --recurse-submodules https://github.com/<you>/duckdb-oracle.git
    cd duckdb-oracle
    ```

2.  **Set up vcpkg (if not already done):**
    Follow the instructions in the `vcpkg/` submodule to bootstrap vcpkg.

3.  **Build the project:**
    ```sh
    make
    ```
    This will build the extension and a local DuckDB shell with the extension pre-loaded.

## Development Process

### 1. Planning Phase

```bash
gemini /prd "feature description"
```

This creates a workspace in `specs/active/feature-slug/` to plan the new feature.

### 2. Implementation Phase

```bash
gemini /implement feature-slug
```

This is where you'll write the C++ code for the feature in the `src/` directory and add corresponding SQL tests in `test/sql/`.

### 3. Review Phase

```bash
gemini /review feature-slug
```

This final step involves a quality gate, knowledge capture, and archival of the completed work.

## Common Tasks

### Build

```bash
# Build in release mode (default)
make

# Build in debug mode
make debug
```

Notes:
- The `Makefile` drives an out-of-source CMake build under `./build/{release,debug}` (CMake >= 3.10).
- OCI linking is performed through `ORACLE_HOME`; pass a different SDK by exporting that variable before `make`.

### Test

```bash
# Run the SQL test suite
make test
```

### Lint

```bash
# Apply formatting to the code
make format
```

## Git Workflow

- **Branching**: Create feature branches from `main`.
- **Commits**: Use descriptive commit messages.
- **Pull Requests**: Open a pull request on GitHub to merge changes into `main`. The CI will automatically run tests.
- **Updating DuckDB**: To pull in the latest changes from the DuckDB submodule, run `git submodule update --remote --merge`.

## Oracle Connectivity Quick Reference

- Connection strings are passed straight to `OCILogon` as the database argument; include credentials in the EZCONNECT style: `user/password@//host:port/service`.
- Wallet-based auth uses `oracle_attach_wallet('/path/to/wallet')`, which sets `TNS_ADMIN` for the current process before issuing `oracle_query`/`oracle_scan`.
- Pushdown and tuning knobs (set via `SET`): `oracle_enable_pushdown`, `oracle_prefetch_rows`, `oracle_prefetch_memory`, `oracle_array_size`, `oracle_debug_show_queries`, `oracle_connection_cache`, `oracle_connection_limit`.
- Maintenance: `SELECT oracle_clear_cache();` clears cached schema/connection state for attached Oracle databases.

## Code Review Checklist

- Does the new code follow the style guide (`make format` clean)?
- Is the new functionality covered by SQL tests?
- Are all tests passing?
- Is the C++ code well-documented with Doxygen-style comments?
- Does the change introduce any unnecessary dependencies?
