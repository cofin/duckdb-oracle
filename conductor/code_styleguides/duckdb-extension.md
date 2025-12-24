# DuckDB Extension C++ Style Guide

## General Principles
- **Adhere to DuckDB Core:** Extensions must strictly follow the coding style, patterns, and conventions of the core DuckDB codebase.
- **Modern C++:** Use C++17 features where appropriate, but prioritize readability and compatibility with DuckDB's internal APIs.

## Naming Conventions
- **Files:** Use `snake_case` for filenames (e.g., `oracle_extension.cpp`, `oracle_table_entry.hpp`).
- **Classes/Structs:** Use `CamelCase` (e.g., `OracleConnection`, `OracleBindData`).
- **Functions:** Use `CamelCase` for methods and function names (e.g., `Connect`, `CheckOCIError`).
- **Variables:** Use `snake_case` for local variables and member variables (e.g., `connection_string`, `row_count`).
- **Constants/Macros:** Use `UPPER_SNAKE_CASE` (e.g., `ORACLE_DEFAULT_PORT`).

## Memory Management
- **Smart Pointers:** ALWAYS use DuckDB's smart pointers (`unique_ptr<T>`, `shared_ptr<T>`) for resource management.
- **Allocators:** Use `make_uniq<T>()` and `make_shared<T>()` instead of `new`.
- **RAII:** Use RAII (Resource Acquisition Is Initialization) wrappers for all external resources (like OCI handles) to ensure proper cleanup on exceptions.

## Error Handling
- **Exceptions:** Use DuckDB's exception hierarchy. Throw `IOException`, `BinderException`, `InvalidInputException`, etc.
- **Messages:** Error messages should be descriptive and actionable.
- **No Segfaults:** Validate all inputs and external API return codes (OCI) to prevent crashes.

## DuckDB Specifics
- **Namespace:** All code must be contained within `namespace duckdb`.
- **Strings:** Use DuckDB's `string` type (often `std::string` or `duckdb::string_t` depending on context), not raw `char*` unless interacting with C APIs.
- **Types:** Use DuckDB's internal types (`idx_t`, `data_ptr_t`) instead of standard C types where applicable.
- **Casts:** Avoid C-style casts. Use `static_cast`, `reinterpret_cast`, or `const_cast`.

## Formatting
- **Clang-Format:** All code must pass `make format` (which typically runs `clang-format`).
- **Clang-Tidy:** Code should pass `make tidy-check` to ensure quality and catch potential issues.
