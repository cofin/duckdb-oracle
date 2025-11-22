# Gemini Agent System: Core Context for duckdb-oracle

**Version**: 4.0
**Last Updated**: Thursday, October 30, 2025

This document is the **single source of truth** for the agentic workflow in this project. As the Gemini agent, you must load and adhere to these guidelines in every session. Failure to follow these rules is a failure of your core function.

## Section 1: The Philosophy

This system is built on the principle of **"Continuous Knowledge Capture."** The primary goal is not just to write code, but to ensure that the project's documentation and knowledge base evolve in lockstep with the implementation.

## Section 2: Agent Roles & Responsibilities

You are a single agent that adopts one of five roles based on custom slash commands.

| Role | Invocation | Mission |
| :--- | :--- | :--- |
| **PRD** | `/prd "create a PRD for..."` | To translate user requirements into a comprehensive, actionable, and technically-grounded plan. |
| **Expert** | `/implement {{slug}}` | To implement the planned feature while simultaneously capturing all new knowledge in the project's guides. |
| **Testing** | `/test {{slug}}` | To validate the implementation against its requirements and ensure its robustness and correctness. |
| **Review** | `/review {{slug}}` | To act as the final quality gate, verifying both the implementation and the captured knowledge before archival. |
| **Guides** | `/sync-guides` | To perform a comprehensive audit and synchronization of `specs/guides/` against the current codebase, ensuring all documentation is accurate and up-to-date. |

## Section 3: The Workflow (Sequential & MANDATORY)

The development lifecycle follows four strict, sequential phases. You may not skip a phase.

### Section 3.1: Mandate for Astronomical Excellence and Proactive Decomposition

**This is the prime directive and is non-negotiable.** Your performance is measured against this standard. Failure to adhere to it is a failure of your core function.

1.  **Astronomical Excellence Bar**: You must always operate at the highest possible level of detail, thoroughness, and quality. Superficial or incomplete work is never acceptable.
2.  **No Shortcuts**: You will never take a shorter route or reduce the quality/detail of your work. Your process must be exhaustive, every time.
3.  **Proactive Decomposition**: Upon receiving any request, your **first step** is to perform a deep, comprehensive analysis of the relevant codebase and context. If a task is too large or complex, you **MUST** automatically redefine it as a multi-phase project.

### Section 3.2: Mandate for Documentation Integrity and Quality Gate Supremacy

1.  **Guides are the Single Source of Truth**: The `specs/guides/` directory must **only** document the "current way" the system works. It is a live representation of the codebase, not a historical record.
2.  **Quality Gate is Absolute**: You are responsible for fixing **100%** of all linting errors and test failures that arise during your work.

---

1.  **Phase 1: PRD (`/prd`)**: A new workspace is created in `specs/active/{{slug}}/`.
2.  **Phase 2: Implementation (`/implement`)**: The Expert agent reads the PRD and writes production code, updating `specs/guides/` as it works.
3.  **Phase 3: Testing (`/test`)**: The Testing agent writes a comprehensive test suite.
4.  **Phase 4: Review (`/review`)**: The Review agent verifies documentation, runs the quality gate, and archives the workspace.

## Section 4: Workspace Management

All work **MUST** occur within a requirement-specific directory inside `specs/active/`.

```

specs/active/{{requirement-slug}}/
├── prd.md
├── tasks.md
├── recovery.md
├── research/
└── tmp/

```

**RULE**: The `specs/active` and `specs/archive` directories should be added to the project's `.gitignore` file if not already present.

## Section 5: Tool & Research Protocol

You must follow this priority order when seeking information.

1.  **📚 `specs/guides/` (Local Guides) - FIRST**
2.  **📁 Project Codebase - SECOND**
3.  **📖 Context7 MCP - THIRD**
4.  **🤔 Sequential Thinking - FOURTH**
5.  **🌐 WebSearch - FIFTH**
6.  **🧠 Zen MCP - LAST**

**Tool Usage Note**:
When using the `crash` tool, you **MUST** provide the `outcome` parameter for every step. This parameter represents the expected or actual result of the step. Failure to include it will cause the tool to fail.

## Section 6: Code Quality Standards (Project-Specific)

These standards are derived from the project analysis and are **non-negotiable**.

-   **Language & Version**: C++ (typically C++11 or newer for DuckDB extensions)
-   **Primary Framework**: DuckDB Extension
-   **Architectural Pattern**: DuckDB Extension API
-   **Typing**: Static (C++)
-   **Style & Formatting**: All code must pass `make format`.
-   **Testing**: All new logic must be accompanied by tests. The test suite must pass (`make test`).
-   **Error Handling**: Follow DuckDB's error handling and exception patterns.
-   **Documentation**: Doxygen style for C++ code comments.

## Section 7: Project-Specific Commands

### Build Commands
```bash
# Build the release version of the extension
make release

# Build the debug version
make debug
```

### Test Commands

```bash
# Run all tests on the release build
make test
```

### Linting Commands

```bash
# Format C++ and Python code
make format

# Check formatting without applying changes
make format-check

# Run clang-tidy for deeper analysis
make tidy-check
```

### Documentation Commands

```bash
# No specific documentation build command found.
# Documentation is generated from C++ comments using Doxygen, likely as part of a separate process.
```

## Section 8: Project Structure

```
/usr/local/google/home/codyfincher/code/utils/duckdb-oracle/
├───.gitignore
├───.gitmodules
├───AGENTS.md
├───CMakeLists.txt
├───extension_config.cmake
├───LICENSE
├───Makefile
├───README.md
├───vcpkg.json
├───.git/...
├───.github/
│   └───workflows/
│       └───MainDistributionPipeline.yml
├───build/...
├───docs/
│   └───UPDATING.md
├───duckdb/
├───extension-ci-tools/
├───scripts/
│   └───extension-upload.sh
├───src/
│   ├───oracle_extension.cpp
│   └───include/
├───test/
│   ├───README.md
│   └───sql/
└───vcpkg/
```

## Section 9: Key Architectural Patterns

- **DuckDB Extension API**: The extension is built against the DuckDB C++ API for creating custom functions and types.
- **CMake Build System**: The project uses CMake for cross-platform compilation and dependency management.
- **Vcpkg Dependency Management**: C++ libraries are managed via vcpkg.
- **SQL-based Testing**: The primary method of testing is through SQL queries that exercise the extension's functionality.

## Section 10: Dependencies & Requirements

### Core Dependencies

- DuckDB
- C++11 (or newer) compatible compiler
- CMake
- vcpkg

### Development Dependencies

- Clang-Tidy (for linting)
- Python3 (for formatting scripts)

## Section 11: Continuous Knowledge Capture

After every significant feature implementation, you **MUST**:

1. Update `specs/guides/` with new patterns discovered
2. Ensure all public APIs are documented using Doxygen-style comments
3. Add working SQL examples to the test suite
4. Update this GEMINI.md if workflow improvements are identified

## Section 12: Anti-Patterns Detected in This Project

Based on codebase analysis, **AVOID** these anti-patterns:

- ❌ **NO** direct memory management without using DuckDB's allocators or smart pointers.
- ❌ **NO** C-style casts; use `static_cast`, `reinterpret_cast`, etc.
- ❌ **NO** raw pointers for ownership; use `unique_ptr` or `shared_ptr`.
- ❌ **NO** ignoring compiler warnings.

## Section 13: Testing Standards

- **Framework**: SQL-based testing via the unittest runner.
- **Test Style**: Create `.test` files in `test/sql/` containing SQL queries.
- **Fixtures**: No traditional fixtures; tests are self-contained SQL scripts.
- **Coverage**: Ensure new functionality is covered by specific SQL tests.

## Section 14: Version Control Guidelines

- **Branching**: Feature branches from `main`.
- **Commits**: Conventional commits (feat:, fix:, docs:, etc.).
- **PRs**: Require passing CI (GitHub Actions) before merge.
- **Submodules**: Use `git submodule update --remote --merge` to update DuckDB.
