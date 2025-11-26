# PRD: Write Support Fixes and Filter Pushdown

## Overview

This work addresses issues discovered during Oracle write support testing and enables filter pushdown by default for better performance.

## Background

The DuckDB Oracle extension recently added write support (COPY TO Oracle). During integration testing, several issues were discovered related to:

1. Type handling during fetch operations
2. Filter pushdown configuration causing silent filter drops
3. Default settings not optimized for performance

## Requirements

### Functional Requirements

1. **DECIMAL Type Support**: Oracle NUMBER columns must be correctly fetched and converted to DuckDB DECIMAL types
2. **Filter Correctness**: WHERE clauses must be correctly applied (either pushed to Oracle or applied client-side)
3. **Pushdown Performance**: Filter pushdown should be enabled by default for better query performance
4. **Version Detection**: Oracle version must be re-detected after cache clear for proper type handling

### Non-Functional Requirements

1. **Performance**: Queries with WHERE clauses should push filters to Oracle to avoid full table scans
2. **Correctness**: All integration tests must pass
3. **Code Quality**: Code must pass clang-tidy checks

## Technical Design

### Filter Pushdown Architecture

DuckDB provides two mechanisms for filter handling:

1. **`filter_pushdown` (bool)**: When true, DuckDB extracts simple predicates into `TableFilter` objects. The scan function must check and apply these filters.

2. **`pushdown_complex_filter` (callback)**: Called with filter expressions. The extension can:
   - Translate expressions to remote SQL (push to Oracle)
   - Remove translated filters from the vector
   - Leave untranslated filters for DuckDB to apply client-side

**Decision**: Use `pushdown_complex_filter` exclusively:

- Set `filter_pushdown = false` (we don't implement `TableFilter` handling)
- Set `pushdown_complex_filter = OraclePushdownComplexFilter`
- Enable `oracle_enable_pushdown = true` by default

This matches industry standard for remote DB connectors (Postgres FDW, Spark JDBC, Trino).

### Type Mapping

| Oracle Type | DuckDB Type | Fetch Method |
|-------------|-------------|--------------|
| NUMBER | DOUBLE | Parse string to double |
| NUMBER(p) | DECIMAL(p,0) | Value::DefaultCastAs |
| NUMBER(p,s) | DECIMAL(p,s) | Value::DefaultCastAs |
| VARCHAR2 | VARCHAR | Direct string copy |
| DATE | TIMESTAMP | ParseOciTimestamp |
| TIMESTAMP | TIMESTAMP | ParseOciTimestamp |
| RAW | BLOB | Direct binary copy |
| VECTOR | LIST<FLOAT> | ParseVectorJsonToList |

## Known Issues

### Critical: Pushdown Query Column Duplication

When pushdown is enabled, the `OraclePushdownComplexFilter` function generates queries with duplicated columns. This causes:

- 10 columns instead of 5
- Missing WHERE clauses
- Buffer misalignment causing wrong values

**Status**: Investigation in progress. See recovery.md for details.

## Testing Strategy

Integration tests in `test/integration_tests/`:

- `test_write_basic.test` - Basic type roundtrip
- `test_write_vector.test` - VECTOR type support
- `test_mixed_columns.test` - Mixed type column alignment
- `test_views_matviews.test` - View support
- `test_json.test` - JSON type support

## Success Criteria

1. All integration tests pass
2. Filter pushdown works correctly (WHERE clauses applied)
3. No type conversion errors
4. clang-tidy passes
