# Testing Guide: duckdb-oracle

**Last Updated**: 2025-11-22

## Test Framework

The project uses DuckDB's built-in SQL testing framework. This involves writing SQL scripts that are executed by a special `unittest` runner.

**Note on CI/CD**: Because the CI environment does not have access to a running Oracle Database instance, the automated tests in `test/sql/*.test` are primarily **negative tests** or checks that verify the extension loads correctly and handles connection errors gracefully.

## Integration Tests (Docker)

For robust testing against a real Oracle Database (including Vector and JSON types), use the integration test script. This requires Docker or Podman.

```bash
# Runs integration tests against a local container
make integration
```

This script:
1. Starts an Oracle Database container (gvenzl/oracle-free:23-slim).
2. Waits for readiness.
3. Runs setup SQL (creating types, users).
4. Executes DuckDB tests against this container using `unittest`.

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
    ├─── manual_verification.md # Instructions for live DB testing
    └─── test_oracle_query.test # Automated (negative) tests
```

## Writing Tests

Tests are written in SQL. The test runner parses special comments to understand the structure of the test.

- A test case is defined by a `----` separator.
- The expected result of a query is provided immediately after the query.
- Use `statement error` for tests that are expected to fail (e.g., due to connection issues in CI).

### Example (Automated Test)

```sql
-- file: test/sql/test_oracle_query.test

-- load the extension
require oracle

-- We expect an error because the connection string is invalid/unreachable in CI
statement error
SELECT * FROM oracle_query('dummy/dummy@//localhost:1521/dummy', 'SELECT 1 FROM DUAL');
----
IO Error
```

## Test Standards

- Every new function or feature must have a corresponding SQL test file in `test/sql/`.
- Tests should cover the "happy path" as well as edge cases like `NULL` inputs, empty strings, and invalid data types.
- Each test file should be self-contained.

## Continuous Integration

Tests are automatically run by the GitHub Actions workflow defined in `.github/workflows/main-distribution-pipeline.yml` on every push and pull request to the `main` branch.
