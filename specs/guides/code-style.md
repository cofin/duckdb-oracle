# Code Style Guide: duckdb-oracle

**Last Updated**: 2025-10-30

## Language & Version

- **Language**: C++
- **Version**: C++11 or newer, as required by DuckDB.

## Formatting

The project uses `clang-format` for C++ code formatting, consistent with DuckDB's own style. The configuration is typically found in the `.clang-format` file within the `duckdb` submodule.

The formatting script also handles Python code, though this project is primarily C++.

## Documentation

C++ code should be documented using Doxygen-style comments. This is especially important for public functions and classes that form the API of the extension.

### Example

```cpp
/**
 * @brief An example function that says hello.
 * @param name The name to say hello to.
 * @return A string saying hello.
 */
std::string SayHello(const std::string& name);
```

## Naming Conventions

- **Classes and Structs**: `PascalCase`
- **Functions and Methods**: `PascalCase`
- **Variables**: `snake_case`
- **Constants**: `UPPER_SNAKE_CASE`

## Linting

The project uses `clang-tidy` for deeper static analysis. This helps to catch common errors and enforce best practices.

To run the linter (requires OCI SDK setup):

```bash
make tidy-check
```

## Auto-Formatting

To automatically format your code to match the project's style, run:

```bash
make format
```

To check for formatting issues without applying them, run:

```bash
make format-check
```
