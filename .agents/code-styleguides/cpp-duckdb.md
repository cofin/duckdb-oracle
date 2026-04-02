# C++ Style Guide — DuckDB Oracle Extension

## Formatting & Tooling

- **Formatter**: `clang-format` — run `make format` before every commit
- **Linter**: `clang-tidy` — run `make tidy-check` for static analysis
- **Standard**: C++17 features are acceptable; DuckDB's minimum is C++11

## Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Classes / Structs | `PascalCase` | `OracleConnectionManager` |
| Functions / Methods | `PascalCase` | `OracleWriteBind()` |
| Variables | `snake_case` | `bind_buffers` |
| Constants / Macros | `UPPER_SNAKE_CASE` | `STANDARD_VECTOR_SIZE` |
| File names | `snake_case.cpp/.hpp` | `oracle_write.cpp` |
| Namespaces | `snake_case` | `duckdb` |

## Headers

- Use `#pragma once` (no include guards)
- All code lives in the `duckdb::` namespace
- Include DuckDB headers via `duckdb.hpp` or specific `duckdb/*.hpp`

## Memory Management

- Prefer `duckdb::unique_ptr<>` over raw `new`/`delete`
- Use RAII wrappers for OCI handles (environment, service, session, statement)
- OCI resources must be freed in destructors — never rely on manual cleanup

## Error Handling

- Throw `duckdb::Exception` or `duckdb::BinderException` with descriptive messages
- OCI errors: extract OCI error text and wrap in DuckDB exceptions
- Never silently swallow OCI return codes — always check `OCI_SUCCESS`, `OCI_ERROR`, etc.

## OCI-Specific Rules

- **Bind types**: Use `SQLT_STR` (string binding) for NUMBER, DATE, and TIMESTAMP columns during array fetch to avoid "Column Shift" memory corruption
- **Native binds for writes**: Use `SQLT_INT`, `SQLT_BDOUBLE`, `SQLT_ODT`, `SQLT_BIN` in write paths where buffer layouts are controlled
- **Thread safety**: Never share `OCISvcCtx` across threads — use connection pooling with per-thread handles
- **Timeouts**: `OCILogon` has no hard timeout — always use session pooling with explicit timeout configuration

## DuckDB Extension Patterns

- Use `DataChunk` and `Vector` for batch data processing
- Implement `CopyFunction` interface for write support (Bind → InitGlobal → InitLocal → Sink → Finalize)
- Implement filter pushdown via `ComplexFilterPushdown` callback
- Use `SecretManager` for credentials — never concatenate user input into SQL
- Register settings via `ExtensionUtil::RegisterSetting` with proper defaults

## Documentation

- Doxygen-style comments (`///` or `/** */`) for public API functions
- Inline comments for non-obvious logic, especially OCI quirks
- Document buffer sizes, alignment requirements, and OCI type mappings

## Testing

- SQL logic tests (`.test` files) using DuckDB's `sqllogictest` framework
- Unit tests: `test/unit_tests/` — no Oracle container required
- Integration tests: `test/integration_tests/` — require `gvenzl/oracle-free:23-slim`
- Use `require oracle` directive at the top of test files
- Use `${ORACLE_CONNECTION_STRING}` placeholder for connection strings in integration tests
