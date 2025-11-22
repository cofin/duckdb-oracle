# Claude Agent System: DuckDB Oracle Extension

**Version**: 2.0
**Last Updated**: Friday, November 22, 2025

This document is the **single source of truth** for the Claude Code multi-agent workflow in this project.

## Philosophy

This system is built on **"Continuous Knowledge Capture"** - ensuring documentation evolves with code.

## Agent Architecture

Claude uses a **multi-agent system** where specialized agents handle specific phases:

| Agent | File | Mission |
|-------|------|---------|
| **PRD** | `.claude/agents/prd.md` | Requirements analysis, PRD creation, task breakdown |
| **Expert** | `.claude/agents/expert.md` | Implementation with deep C++17, DuckDB, and OCI knowledge |
| **Testing** | `.claude/agents/testing.md` | Comprehensive test creation (SQL tests, integration tests) |
| **Docs & Vision** | `.claude/agents/docs-vision.md` | Documentation, quality gate, knowledge capture |

## Workflow

### Sequential Development Phases

1. **Phase 1: PRD** - Agent creates workspace in `specs/active/{{slug}}/`
2. **Phase 2: Expert Research** - Research patterns, libraries, best practices
3. **Phase 3: Implementation** - Expert writes production code
4. **Phase 4: Testing** - Testing agent creates comprehensive tests (auto-invoked by Expert)
5. **Phase 5: Documentation** - Docs & Vision updates guides (auto-invoked after Testing)
6. **Phase 6: Quality Gate** - Full validation and knowledge capture
7. **Phase 7: Archive** - Workspace moved to `specs/archive/`

## Workspace Structure

```sh
specs/active/{{slug}}/
├── prd.md          # Product Requirements Document
├── tasks.md        # Implementation checklist
├── recovery.md     # Session resume instructions
├── research/       # Research findings
└── tmp/            # Temporary files
```

## Project Context

**Language**: C++17
**Framework**: DuckDB Extension Framework + Oracle Call Interface (OCI)
**Architecture**: Storage extension with catalog integration
**Test Framework**: DuckDB SQL test runner + Integration tests (containerized Oracle)
**Build Tool**: Make (CMake wrapper)
**Documentation**: Markdown + Doxygen comments

## Development Commands

### Build

```bash
make release              # Build extension in release mode
make debug                # Build extension in debug mode
make clean                # Clean build artifacts
make clean-all            # Remove all build dirs (use when switching generators)
```

### Test

```bash
make test                 # Run SQL test suite
make integration          # Build + run tests against containerized Oracle
ORACLE_IMAGE=gvenzl/oracle-free:23-slim make integration
```

### Quality

```bash
make tidy-check          # Run clang-tidy checks
make configure_ci        # Install Oracle Instant Client for CI/local
```

### Running Single Tests

```bash
# Run specific test file
build/release/test/unittest test/sql/test_oracle_attach.test

# Run with debug output
build/release/duckdb -unsigned < test/sql/test_oracle_attach.test
```

### Build Outputs

- `build/release/duckdb` - DuckDB shell with oracle extension preloaded
- `build/release/test/unittest` - Test runner
- `build/release/extension/oracle/oracle.duckdb_extension` - Loadable extension binary

## Code Quality Standards

### C++ Standards

- **Version**: C++17
- **Style**: Follow DuckDB conventions (symlinked `.clang-format`, `.clang-tidy`)
- **Headers**: Use `#pragma once` for header guards
- **Namespaces**: All code in `namespace duckdb {}`
- **Memory**: Use DuckDB's smart pointers (`unique_ptr<T>`, `shared_ptr<T>`, `make_uniq<T>()`)
- **Strings**: Use DuckDB's `string` type, not `std::string` directly
- **Errors**: Throw `IOException`, `BinderException`, or other DuckDB exceptions

### Documentation

- **Style**: Doxygen-compatible C++ comments
- **Public APIs**: Document all public methods with `//! Brief description`
- **Inline comments**: Use `//` for implementation notes
- **Header files**: Include purpose and usage examples

Example:
```cpp
//! Connects to Oracle database using OCI
//! \param connection_string Oracle connection string (user/pass@host:port/service)
//! \return void, throws IOException on failure
void Connect(const string &connection_string);
```

### Testing

- **Framework**: DuckDB SQL test runner (`.test` files)
- **Integration Tests**: Docker/Podman Oracle container (`scripts/test_integration.sh`)
- **Coverage target**: Test all modified functionality
- **Test types required**:
  1. Smoke tests (connection errors, require oracle)
  2. Integration tests (real Oracle container)
  3. Edge cases (NULL, empty, errors)
  4. Type mapping tests (Oracle types → DuckDB types)
  5. Pushdown tests (filter pushdown verification)

### Code Patterns

- **RAII**: Always use RAII for OCI handles (see `OracleConnection`)
- **Error handling**: Use `CheckOCIError()` for all OCI calls
- **Mutex protection**: Protect shared state (see `OracleCatalogState::lock`)
- **Lazy loading**: Defer expensive operations (see `OracleTableEntry::GetColumns()`)

## Project Structure

```
duckdb-oracle/
├── src/
│   ├── oracle_extension.cpp           # Entry point, registration
│   ├── oracle_connection.cpp          # OCI connection wrapper
│   ├── storage/
│   │   ├── oracle_catalog.cpp         # Catalog implementation
│   │   ├── oracle_schema_entry.cpp    # Schema discovery
│   │   ├── oracle_table_entry.cpp     # Table metadata + column mapping
│   │   ├── oracle_storage_extension.cpp
│   │   ├── oracle_transaction.cpp
│   │   └── oracle_transaction_manager.cpp
│   └── include/
│       ├── oracle_catalog_state.hpp   # Shared state per attached DB
│       ├── oracle_settings.hpp        # Extension settings
│       ├── oracle_table_function.hpp  # Query execution (OracleBindData)
│       └── *.hpp
├── test/
│   ├── sql/*.test                     # DuckDB SQL tests
│   └── integration/init_sql/*.sql     # Oracle container setup
├── scripts/
│   ├── setup_oci_linux.sh            # Install Oracle Instant Client
│   └── test_integration.sh           # Integration test runner
├── CMakeLists.txt
├── Makefile
└── vcpkg.json
```

## Key Architectural Patterns

### Extension Registration Flow

1. `oracle_extension.cpp` - Entry point, registers storage extension + table functions
2. `OracleStorageExtension` - Attaches to DuckDB's catalog system (`ATTACH ... TYPE oracle`)
3. `CreateOracleCatalog()` - Creates catalog backed by `OracleCatalogState`

### Storage Layer (DuckDB Catalog Integration)

- **OracleCatalogState** (`oracle_catalog_state.hpp`): Shared state per attached database
  - Holds connection string, settings, OracleConnection instance
  - Caches schema/table metadata with mutex protection
  - Global registry pattern for `oracle_clear_cache()` function

- **OracleCatalog** → **OracleSchemaEntry** → **OracleTableEntry**: Hierarchical catalog
  - Schema discovery: `SELECT username FROM all_users`
  - Table discovery: `SELECT table_name FROM all_tables WHERE owner = ?`
  - Column introspection: `SELECT * FROM all_tab_columns WHERE owner = ? AND table_name = ?`

- **OracleTableEntry**: Lazy column metadata loading
  - Maps Oracle types to DuckDB LogicalTypes in `MapOracleColumn()`
  - NUMBER → DECIMAL/DOUBLE, DATE/TIMESTAMP → TIMESTAMP, CHAR/VARCHAR/CLOB → VARCHAR, BLOB/RAW → BLOB

### Query Execution (Table Functions)

- **OracleBindData** (`oracle_table_function.hpp`): Holds OCI handles + query state
  - `OracleContext` - OCI environment/service/statement/error handles (RAII cleanup)
  - `base_query` - Original query template
  - `query` - Transformed query after pushdown
  - Settings snapshot (prefetch_rows, array_size, etc.)

- **OracleQueryFunction** (`oracle_extension.cpp`): Fetch loop
  - Uses OCI array fetch with configurable `array_size`
  - Converts OCI types → DuckDB vectors (handles NULL indicators, timestamps, LOBs)
  - Supports pushdown filters via `OraclePushdownComplexFilter()`

### Filter Pushdown

When `oracle_enable_pushdown=true`:

1. DuckDB calls `OraclePushdownComplexFilter()` with LogicalGet + filter expressions
2. Traverses DuckDB expression tree (comparisons, AND/OR, column refs, constants)
3. Generates Oracle SQL WHERE clause
4. Rewrites `base_query` → `query` with added predicates

### Connection Management

- **OracleConnection** (`oracle_connection.hpp`): Wrapper around OCI handles
  - `Connect()` - `OCIEnvCreate()` + `OCILogon()`
  - `Query()` - Execute + fetch all rows as strings (metadata queries)
  - Error handling via `CheckOCIError()` (throws IOException with Oracle error text)

- Connection pooling: `OracleCatalogState` reuses single connection if `connection_cache=true`
- Wallet support: `oracle_attach_wallet('/path')` sets `TNS_ADMIN` env variable

## Oracle Instant Client Setup

### Environment Variable

The build requires `ORACLE_HOME` pointing to Instant Client root:

```sh
export ORACLE_HOME=/path/to/instantclient_23_6  # must contain sdk/include/oci.h and lib/libclntsh.so
```

Falls back to `/usr/share/oracle/instantclient_23_26` if unset.

### CI Script

`scripts/setup_oci_linux.sh` downloads and extracts Instant Client Basic + SDK to `oracle_sdk/`.

### Runtime Dependencies

- Linux: `libaio1` (Ubuntu 24.04 uses `libaio1t64`)
- Libraries: `libclntsh.so` must be in `LD_LIBRARY_PATH` or system lib path

## Extension Settings

Configurable via `SET` or `ATTACH` options:

- `oracle_enable_pushdown` (bool, default false) - Push filters to Oracle
- `oracle_prefetch_rows` (idx_t, default 200) - OCI prefetch hint
- `oracle_prefetch_memory` (idx_t, default 0) - OCI prefetch memory (0=auto)
- `oracle_array_size` (idx_t, default 256) - Rows per OCIStmtFetch iteration
- `oracle_connection_cache` (bool, default true) - Reuse OCI connections
- `oracle_connection_limit` (idx_t, default 8) - Max cached connections
- `oracle_debug_show_queries` (bool, default false) - Log generated Oracle SQL

Settings live in `OracleSettings` struct, applied via `OracleCatalogState::ApplyOptions()`.

## MCP Tools Available

### Context7

- **Purpose**: Up-to-date library documentation
- **Usage**: Research DuckDB APIs, OCI patterns during planning/implementation
- **Commands**:

  ```python
  mcp__context7__resolve-library-id(libraryName="duckdb")
  mcp__context7__get-library-docs(
      context7CompatibleLibraryID="/duckdb/duckdb",
      topic="storage extensions",
      page=1
  )
  ```

### Zen MCP

- **zen.planner**: Multi-step planning with revision capabilities
- **zen.chat**: Collaborative thinking and brainstorming
- **zen.thinkdeep**: Deep architectural analysis
- **zen.analyze**: Code quality and performance analysis
- **zen.debug**: Systematic debugging workflow
- **zen.consensus**: Multi-model consensus for decisions

### WebSearch

- **Purpose**: Modern best practices and industry standards
- **Usage**: Research novel problems and architectural decisions

## Common Development Patterns

### Adding New Oracle Type Mappings

1. Extend `MapOracleColumn()` in `src/storage/oracle_table_entry.cpp`
2. Add OCI type constant (e.g., `SQLT_JSON`) to `oracle_extension.cpp` if needed
3. Update fetch logic in `OracleQueryFunction()` to handle conversion
4. Add SQL test in `test/sql/` for type roundtrip

Example:
```cpp
// In MapOracleColumn()
case SQLT_JSON:
    return LogicalType::VARCHAR; // Fallback to VARCHAR for JSON
```

### Adding New Table Functions

Register in `LoadInternal()` in `oracle_extension.cpp`:

```cpp
ExtensionUtil::RegisterFunction(instance,
    TableFunction("new_function", {args}, new_func, new_bind));
```

### Extending Pushdown Support

Modify `OraclePushdownComplexFilter()` in `oracle_extension.cpp`:

- Add new expression type cases (currently handles COMPARE, CONJUNCTION, COLUMN_REF, CONSTANT)
- Update operator mapping (`ExpressionTypeToString()`)

## Known Limitations

- Read-only (no INSERT/UPDATE/DELETE/COPY support)
- No transaction management (auto-commit reads only)
- Views only visible if present in `ALL_TABLES`
- Some Oracle types fallback to VARCHAR (e.g., XMLTYPE, SDO_GEOMETRY)
- No parallel query execution (single OCI connection per scan)

## Anti-Patterns (Avoid These)

### C++ Anti-Patterns

❌ **Manual memory management** - Use DuckDB smart pointers
❌ **std::string directly** - Use DuckDB's `string` type
❌ **Ignoring OCI errors** - Always use `CheckOCIError()`
❌ **Forgetting mutex locks** - Protect shared state
❌ **Synchronous metadata calls in hot path** - Use lazy loading

### Architecture Anti-Patterns

❌ **Blocking calls in scan functions** - Prefetch and batch
❌ **N+1 metadata queries** - Cache schema/table lists
❌ **Memory leaks in OCI handles** - RAII cleanup
❌ **Hardcoded connection strings** - Use settings system

## Dependencies

### Core

- **DuckDB**: Extension framework (linked via CMake)
- **Oracle Instant Client**: Basic + SDK (system-provided)
- **OpenSSL**: vcpkg-managed dependency

### Development

- CMake ≥3.10
- C++17 compiler (GCC, Clang)
- vcpkg (for OpenSSL)
- Oracle Instant Client 19c/21c/23c
- libaio (Linux)

## Version Control

### Branching

- `main` - Stable branch
- `oracle-extension` - Current development branch
- Feature branches: `feature/{{slug}}`

### Commit Messages

Follow conventional commits:
- `feat:` - New features
- `fix:` - Bug fixes
- `docs:` - Documentation only
- `refactor:` - Code refactoring
- `test:` - Test additions/fixes
- `chore:` - Build/tooling changes

Example:
```
feat: Add BLOB streaming support with chunked reads

- Implement OCILobRead2 for large BLOB handling
- Add streaming buffer management
- Update type mapping for BLOB/CLOB
- Add integration test with 10MB BLOB
```

## Knowledge Capture Protocol

After every significant feature:

1. Update `specs/guides/` with new patterns
2. Ensure all public APIs documented
3. Add working code examples
4. Update this AGENTS.md if workflow improves

---

**To start a new feature:**

1. Use PRD agent to create requirements
2. Expert agent implements and orchestrates testing
3. Docs & Vision agent handles quality gate and archival

**To debug issues:**

Use Expert agent with zen.debug tool for systematic troubleshooting.
