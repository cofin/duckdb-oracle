# Architecture Guide: duckdb-oracle

**Last Updated**: 2025-10-30

## Overview

This project is a C++ extension for DuckDB. It follows the standard architectural pattern for DuckDB extensions, leveraging DuckDB's internal APIs to add custom functionality.

## Project Structure

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

## Core Components

- **`src/oracle_extension.cpp`**: The main entry point for the extension. It contains the `Load` function that DuckDB calls when the extension is loaded, and it's where custom functions are registered.
- **`CMakeLists.txt`**: Defines the build process, linking against DuckDB and other dependencies.
- **`vcpkg.json`**: Manages C++ dependencies using vcpkg.
- **`test/sql/`**: Contains SQL test files that are used to verify the extension's functionality.

## Design Patterns

- **DuckDB Extension API**: The core of the extension is built using the C++ API provided by DuckDB. This involves creating scalar or table functions, registering them with the database instance, and using DuckDB's data structures (like `Vector` and `Value`) to manipulate data.
- **CMake for Build Management**: The project uses CMake to handle the complexities of building a C++ project, especially one that links against a larger library like DuckDB.

## Data Flow

1. A user executes a SQL query that calls the `oracle_query` function (e.g., `SELECT * FROM oracle_query('connection_string', 'SELECT * FROM users')`).
2. DuckDB's parser identifies the function call and looks it up in its catalog.
3. Finding that the function is provided by this extension, DuckDB invokes the `OracleQueryBind` function to prepare the Oracle OCI statement and define return types.
4. DuckDB then invokes `OracleQueryFunction` which executes the OCI statement, fetches results row by row, and fills the DuckDB `DataChunk`.
5. DuckDB presents the result to the user.
