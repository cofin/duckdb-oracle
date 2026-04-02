# Recovery Document: Write Support Fixes and Filter Pushdown

**Session Date**: 2025-11-25
**Branch**: feat/release-unsigned-stuff
**Status**: COMPLETED

## Summary

All issues related to write support and filter pushdown have been resolved. The extension now passes all integration tests and clang-tidy checks.

## Issues Fixed

### 1. RAW Column Returning Wrong Data (FIXED)
**Problem**: `SELECT raw_data FROM ... WHERE id = 1` returned `1` (the ID) instead of the BLOB data.
**Root Cause**: DuckDB's optimizer pruned the `ID` column from the scan output (because it was only used for the filter, which was pushed down). However, our `OraclePushdownComplexFilter` logic kept `ID` in the SQL query (to support the WHERE clause). This caused a mismatch: the SQL returned 2 columns (ID, RAW_DATA), but DuckDB expected 1 column (RAW_DATA). The identity mapping in `OracleQueryFunction` caused `ID` to be written into the `RAW_DATA` output vector.
**Solution**:
- Implemented `column_mapping` in `OracleScanState`.
- In `OracleInitGlobal`, we map DuckDB's requested `column_ids` to the actual columns present in `bind_data` (the SQL query).
- In `OracleQueryFunction`, we use this mapping to read from the correct buffer.
- This robustly handles cases where the SQL query returns more columns than DuckDB strictly needs for the scan output.

### 2. Internal Error in ResolveTypes (FIXED/AVOIDED)
**Problem**: Attempting to manually reduce `returned_types` in `OraclePushdownComplexFilter` caused an assertion failure (`Attempted to access index 2 within vector of size 2`) because `column_ids` still referred to original table indices.
**Solution**: Reverted the changes to `returned_types` in `OraclePushdownComplexFilter`. We allow `LogicalGet` to retain the full table schema, and handle column selection at runtime via `column_mapping`.

### 3. Clang-Tidy Errors (FIXED)
**Problem**: `src/storage/oracle_write.cpp` had numerous linting errors (C-style casts, missing braces, etc.).
**Solution**: Applied clang-tidy fixes:
- Replaced C-style casts with `reinterpret_cast`, `static_cast`, or `const_cast` (with string copy safety).
- Added braces to single-statement blocks.
- Added `override` specifiers.
- Fixed narrowing conversions.

### 4. Other Fixes (Previously Completed)
- DECIMAL type handling.
- Filter pushdown configuration (`filter_pushdown=false`).
- Version detection after cache clear.
- LIST type handling for VECTOR.
- Pushdown default enabled.
- Column duplication bug.
- BOUND_COLUMN_REF handling.

## Validation

- `make release`: Success.
- `make test`: All unit tests passed.
- `scripts/test_integration.sh`: All integration tests passed (including `test_mixed_columns.test` with RAW and TIMESTAMP checks).
- `make tidy-check`: Passed.

## Next Steps

- Merge changes to main branch.
- Tag release.