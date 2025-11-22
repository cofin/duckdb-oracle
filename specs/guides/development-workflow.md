# Development Workflow: duckdb-oracle

**Last Updated**: 2025-11-22

## Setup

### Prerequisites

- A C++ compiler (like g++, clang, or MSVC)
- CMake
- Make (or Ninja)
- Python 3
- **Oracle Instant Client SDK**: You must download and extract the Oracle Instant Client (Basic + SDK) and set `ORACLE_HOME` environment variable to the directory.
- **Oracle Instant Client SDK**: You must have the Oracle Instant Client SDK installed and the OCI headers/libraries available.
    - Set `ORACLE_HOME` to your installation directory if not in a standard location.
    - Example: `/usr/lib/oracle/19.6/client64`

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

## Code Review Checklist

- Does the new code follow the style guide (`make format` clean)?
- Is the new functionality covered by SQL tests?
- Are all tests passing?
- Is the C++ code well-documented with Doxygen-style comments?
- Does the change introduce any unnecessary dependencies?
