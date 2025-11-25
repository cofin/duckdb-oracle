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

## In Progress

- [ ] **Fix RAW column returning wrong data**
  - Query: `SELECT raw_data FROM ora.DUCKDB_TEST.mixed_types WHERE id = 1`
  - Expected: `\xDE\xAD\xBE\xEF`
  - Actual: `1`
  - Root cause: projection_ids not populated, all columns selected from Oracle but only subset bound to DuckDB output
  - Need to investigate why `projection_pushdown = true` but `projection_ids.size() = 0`

## Pending

- [ ] Run clang-tidy to ensure code quality
- [ ] Run full integration test suite after bug fix
- [ ] Remove debug output before final commit
- [ ] Commit changes with proper message

## Bug Analysis

### Current Issue: RAW Column Alignment

Debug output shows:
```
[oracle] pushdown: projection_ids.size()=0, original_names.size()=5
```

The query `SELECT raw_data FROM mixed_types WHERE id = 1` should have `projection_ids = [1]` (column index 1 = RAW_DATA), but it's empty.

**Hypothesis**: Either:
1. `projection_pushdown = true` is not being set correctly on the TableFunction
2. DuckDB's catalog integration doesn't populate projection_ids for storage extensions
3. The bind data doesn't have the right column metadata for DuckDB to compute projection

**Investigation needed**:
1. Check `tf.projection_pushdown` value in `GetScanFunction()`
2. Compare with postgres_scanner implementation
3. Check if `get.column_ids` contains projection info instead

## Integration Test Results

| Test | Status | Assertions |
|------|--------|------------|
| test_write_vector.test | PASS | 8/8 |
| test_write_spatial.test | PASS | - |
| test_views_matviews.test | PASS | 27/27 |
| test_json.test | PASS | 16/16 (skipped CLOB test) |
| test_write_basic.test | PASS | 18/18 |
| test_mixed_columns.test | FAIL | 9/10 |
