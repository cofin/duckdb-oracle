# Testing Guide: duckdb-oracle

**Last Updated**: 2025-10-30

## Test Framework

The project uses DuckDB's built-in SQL testing framework. This involves writing SQL scripts that are executed by a special `unittest` runner.

## Running Tests

The entire test suite can be run using Make:

```bash
# Build in release mode and run tests
make test

# Or, to build in debug mode and run tests:
make debug
./build/debug/test/unittest "test/*"
```

## Test Structure

Tests are located in the `test/sql/` directory. Each `.test` file represents a test case or a group of related test cases.

```
test/
└── sql/
    ├─── ...
    └─── new_feature.test
```

## Writing Tests

Tests are written in SQL. The test runner parses special comments to understand the structure of the test.

- A test case is defined by a `----` separator.
- The expected result of a query is provided immediately after the query.

### Example

```sql
-- file: test/sql/sample.test

-- A simple test case for the oracle function
-- The name of the test case is optional
statement ok
SELECT oracle('world');
----
Oracle world 🐥
```

## Test Standards

- Every new function or feature must have a corresponding SQL test file in `test/sql/`.
- Tests should cover the "happy path" as well as edge cases like `NULL` inputs, empty strings, and invalid data types.
- Each test file should be self-contained.

## Continuous Integration

Tests are automatically run by the GitHub Actions workflow defined in `.github/workflows/MainDistributionPipeline.yml` on every push and pull request to the `main` branch.
