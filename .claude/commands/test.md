Invoke the Testing agent to create comprehensive test suites.

**What this does:**
- Reads implementation from `specs/active/{slug}/recovery.md`
- Creates .test files using DuckDB test framework
- Tests edge cases and error conditions
- Validates test coverage

**Usage:**
```
/test oracle-table-scan
```

**Or for most recent spec:**
```
/test
```

**The Testing agent will:**
1. Read PRD for acceptance criteria
2. Review implementation in recovery.md
3. Read specs/guides/testing-agent.md for guidance
4. Create .test files in test/sql/oracle/:
   - test_connection.test (connection lifecycle)
   - test_scalar_functions.test (scalar functions)
   - test_table_functions.test (table functions)
   - test_edge_cases.test (NULL, empty, large data)
   - test_error_handling.test (error conditions)
5. Test with DuckDB test framework format:
```sql
# name: test/sql/oracle/test_example.test
# description: Test Oracle function
# group: [oracle]

statement ok
LOAD oracle;

query I
SELECT oracle_function(42);
----
42
```
6. Run tests: `make test`
7. Verify 100% pass rate
8. Update workspace with results

**Note:** This command is typically not needed manually because `/implement` automatically invokes the Testing agent. Use this only if you need to:
- Re-create tests after manual code changes
- Add additional test coverage
- Debug test failures

**After testing:**
- All tests passing (100%)
- Edge cases covered
- Ready for documentation
