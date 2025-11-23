# Advanced Oracle Extension Features

**Status**: Planning Complete
**Created**: 2025-11-22
**Phase**: Ready for Expert Research (Phase 2)

## Overview

This workspace contains the complete planning for three interconnected features that enable enterprise-scale Oracle database integration:

1. **oracle_execute()** - Execute stored procedures, DDL, and PL/SQL blocks
2. **Metadata Scalability** - Support 100,000+ table schemas with lazy loading
3. **Schema Resolution** - Intelligent current schema detection and table name resolution

## Quick Navigation

| Document | Purpose |
|----------|---------|
| [prd.md](prd.md) | Comprehensive Product Requirements Document |
| [tasks.md](tasks.md) | 26 tasks across 7 implementation phases |
| [recovery.md](recovery.md) | Agent resumption guide with technical decisions |
| [research/](research/) | Research findings directory (Phase 2) |

## Key Features

### Feature 1: oracle_execute()

Execute arbitrary Oracle SQL without requiring result sets:

```sql
-- Execute stored procedure
SELECT oracle_execute('user/pass@db', 'BEGIN hr_pkg.update_salaries(1.05); END;');

-- Run DDL
SELECT oracle_execute('user/pass@db', 'CREATE INDEX idx_emp ON employees(dept_id)');

-- Call procedure
SELECT oracle_execute('user/pass@db', 'CALL archive_old_orders(2020)');
```

**Status**: Designed, not implemented
**Complexity**: LOW
**Files to modify**: `oracle_extension.cpp`

### Feature 2: Metadata Scalability

Lazy-load Oracle schemas to handle 100,000+ tables:

```sql
-- Enable lazy loading (default)
SET oracle_lazy_schema_loading = true;

-- Attach completes in <5 seconds regardless of schema size
ATTACH 'user/pass@large_db' AS ora (TYPE oracle);

-- Enumerate specific object types
SET oracle_metadata_object_types = 'TABLE,VIEW,SYNONYM';

-- Limit metadata query results
SET oracle_metadata_result_limit = 10000;
```

**Status**: Designed, not implemented
**Complexity**: MEDIUM
**Files to modify**: `oracle_catalog_state.hpp`, `oracle_catalog.cpp`, `oracle_schema_entry.cpp`

### Feature 3: Schema Resolution

Auto-detect current schema and resolve unqualified table names:

```sql
-- Connected as HR user
ATTACH 'hr/pass@db' AS ora (TYPE oracle);

-- Current schema auto-detected (HR)
SELECT * FROM ora.EMPLOYEES;  -- Resolves to ora.HR.EMPLOYEES

-- Still works with explicit qualification
SELECT * FROM ora.HR.EMPLOYEES;

-- Toggle behavior
SET oracle_use_current_schema = false;
```

**Status**: Designed, not implemented
**Complexity**: MEDIUM
**Files to modify**: `oracle_schema_entry.cpp`, `oracle_catalog.cpp`

## Implementation Plan

### Phase Breakdown

```
Phase 1: Planning & Research          [COMPLETED]
    ├─ PRD creation                    ✓
    ├─ Research BigQuery pattern       ✓
    ├─ Research Oracle metadata        ✓
    └─ Risk analysis                   ✓

Phase 2: Expert Research               [NEXT - 4 tasks]
    ├─ OCI execution patterns
    ├─ Catalog architecture
    ├─ Synonym resolution
    └─ Performance benchmarking

Phase 3: Core Implementation           [12 tasks]
    ├─ Feature 1: oracle_execute       (4 tasks)
    ├─ Feature 2: Metadata scalability (5 tasks)
    └─ Feature 3: Schema resolution    (3 tasks)

Phase 4: Integration                   [4 tasks]
    ├─ Register settings
    ├─ Update GetOracleSettings
    ├─ Integrate DetectCurrentSchema
    └─ End-to-end testing

Phase 5: Testing (Auto)                [6 tasks]
    ├─ SQL tests for oracle_execute
    ├─ Large schema performance tests
    ├─ Synonym resolution tests
    ├─ Schema resolution tests
    ├─ Multi-object-type tests
    └─ Performance benchmarking

Phase 6: Documentation (Auto)          [3 tasks]
    ├─ Update CLAUDE.md
    ├─ Create scalability guide
    └─ Create security guide

Phase 7: Quality Gate (Auto)           [3 tasks]
    ├─ Validate acceptance criteria
    ├─ Performance validation
    └─ Archive workspace
```

### Current Status

- **Completed**: Phase 1 (Planning & Research)
- **Next**: Phase 2 (Expert Research)
- **Blocked**: No blockers

## Performance Targets

| Metric | Target | Validation |
|--------|--------|------------|
| Attach time (100K tables, lazy ON) | < 5 seconds | Phase 5 benchmark |
| Attach time (1K tables, lazy OFF) | < 60 seconds | Phase 5 benchmark |
| Memory usage (attach) | < 500MB | Phase 5 profiling |
| Metadata query count (lazy ON) | < 10 queries | Phase 5 monitoring |
| Schema resolution overhead | < 1ms | Phase 5 benchmark |

## New Settings

| Setting | Type | Default | Description |
|---------|------|---------|-------------|
| `oracle_lazy_schema_loading` | BOOLEAN | true | Load only current schema by default |
| `oracle_metadata_object_types` | VARCHAR | "TABLE" | Object types to enumerate (TABLE,VIEW,SYNONYM,MVIEW) |
| `oracle_metadata_result_limit` | BIGINT | 10000 | Max rows from metadata queries (0=unlimited) |
| `oracle_use_current_schema` | BOOLEAN | true | Resolve unqualified names to current schema first |

## Risk Summary

| Risk | Severity | Mitigation |
|------|----------|------------|
| Metadata query timeout | HIGH | ROWNUM limiting, lazy loading |
| Breaking changes | MEDIUM | Configurable settings, documentation |
| SQL injection | HIGH | Security warnings, best practices guide |
| Synonym resolution complexity | MEDIUM | Graceful fallback, error messages |
| Schema context loss | LOW | Document behavior, cache invalidation |

## Files to Be Modified

### Core Implementation (Phase 3)

1. `/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp`
2. `/home/cody/code/other/duckdb-oracle/src/include/oracle_catalog_state.hpp`
3. `/home/cody/code/other/duckdb-oracle/src/storage/oracle_catalog.cpp`
4. `/home/cody/code/other/duckdb-oracle/src/storage/oracle_schema_entry.cpp`
5. `/home/cody/code/other/duckdb-oracle/src/include/oracle_settings.hpp`

### Test Files (Phase 5)

6. `/home/cody/code/other/duckdb-oracle/test/sql/test_oracle_execute.test`
7. `/home/cody/code/other/duckdb-oracle/test/sql/test_lazy_metadata.test`
8. `/home/cody/code/other/duckdb-oracle/test/sql/test_synonyms.test`
9. `/home/cody/code/other/duckdb-oracle/test/sql/test_schema_resolution.test`
10. `/home/cody/code/other/duckdb-oracle/test/sql/test_metadata_object_types.test`
11. `/home/cody/code/other/duckdb-oracle/test/integration/large_schema_setup.sql`

## Next Steps

**For Expert Agent:**

Start Phase 2 research with these tasks:

1. **Task 2.1**: Research OCI execution patterns for DDL/PL/SQL
   - Read `oracle_extension.cpp` lines 168-320
   - Document OCIStmtExecute non-describe mode
   - Create `research/oci-execution-patterns.md`

2. **Task 2.2**: Research DuckDB catalog architecture
   - Investigate ViewCatalogEntry patterns
   - Document lazy loading strategies
   - Create `research/catalog-architecture.md`

3. **Task 2.3**: Design synonym resolution logic
   - Research ALL_SYNONYMS view
   - Handle circular references
   - Create `research/synonym-resolution.md`

4. **Task 2.4**: Benchmark large schema performance
   - Generate 1000 table test schema
   - Measure baseline attach time
   - Create `research/benchmark-results.md`

## References

- [PRD](prd.md) - Complete requirements and technical scope
- [Tasks](tasks.md) - Detailed task breakdown with acceptance criteria
- [Recovery Guide](recovery.md) - Agent-specific instructions and technical decisions
- [BigQuery Extension](https://github.com/hafenkran/duckdb-bigquery) - Reference implementation
- [Oracle SYS_CONTEXT](https://docs.oracle.com/en/database/oracle/oracle-database/19/sqlrf/SYS_CONTEXT.html)
- [Oracle Data Dictionary](https://docs.oracle.com/en/database/oracle/oracle-database/19/cncpt/data-dictionary-and-dynamic-performance-views.html)

## Sources

Research sources used in planning:

- [BigQuery DuckDB Extension](https://duckdb.org/community_extensions/extensions/bigquery)
- [DuckDB BigQuery GitHub](https://github.com/hafenkran/duckdb-bigquery)
- [Oracle Data Dictionary Views](https://docs.oracle.com/en/database/oracle/oracle-database/19/cncpt/data-dictionary-and-dynamic-performance-views.html)
- [Oracle SYS_CONTEXT Function](https://docs.oracle.com/en/database/oracle/oracle-database/19/sqlrf/SYS_CONTEXT.html)
- [Managing Oracle Synonyms](https://docs.oracle.com/cd/B19306_01/server.102/b14231/views.htm)
- [Stack Overflow: Oracle Current Schema](https://stackoverflow.com/questions/69688166/how-to-check-the-current-schemas-and-change-the-default-schema-in-pl-sql)

---

**Workspace Created**: 2025-11-22
**Ready for**: Expert Agent (Phase 2)
**Expected Completion**: Phase 7 (Quality Gate & Archive)
