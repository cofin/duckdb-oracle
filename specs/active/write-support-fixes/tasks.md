# Tasks: Write Support Fixes

## Completed Tasks

- [x] Build extension and run unit tests
- [x] Run integration tests with Oracle container
- [x] Fix version detection after cache clear
- [x] Fix NUMBER/TIMESTAMP buffer corruption (DECIMAL handling)
- [x] Fix filter pushdown configuration (filter_pushdown=false)
- [x] Enable pushdown by default (oracle_enable_pushdown=true)
- [x] Fix pushdown query column duplication bug (names vector duplication)
- [x] Fix BOUND_COLUMN_REF expression type handling (type 228 not just 227)
- [x] **Fix RAW column returning wrong data**
  - Implemented column mapping in `OracleInitGlobal` and `OracleQueryFunction`
  - Handles cases where DuckDB prunes columns but `bind_data` (query) returns more columns (e.g. filters)
- [x] Fix clang-tidy errors in `oracle_write.cpp`
- [x] Run full integration test suite after bug fix
- [x] Remove debug output before final commit

## In Progress

- [ ] Commit changes

## Pending

- [ ] None