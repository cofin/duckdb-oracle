# Recovery Guide: Advanced Oracle Extension Features

**Status**: Planning Complete
**Phase**: Phase 1 Complete, Ready for Phase 2
**Last Updated**: 2025-11-22

## Quick Start

This workspace contains the PRD and implementation plan for three advanced Oracle extension features:

1. `oracle_execute()` function for arbitrary SQL execution
2. Metadata scalability for 100K+ table schemas
3. Schema resolution with current schema context

**Current State:**

- PRD complete and comprehensive
- Task breakdown complete (26 tasks across 7 phases)
- Research questions identified for Expert agent
- No code changes yet

**Next Steps:**

- Expert agent: Begin Phase 2 research (4 tasks)
- Focus areas: OCI execution patterns, catalog architecture, synonym resolution

## Workspace Structure

```
/home/cody/code/other/duckdb-oracle/specs/active/advanced-oracle-features/
├── prd.md          # Comprehensive requirements document
├── tasks.md        # 26 tasks across 7 phases
├── recovery.md     # This file
└── research/       # Research findings (to be created by Expert)
```

## Files Modified (So Far)

**None** - Planning phase only

## Files to Be Modified (Phase 3+)

### Core Implementation Files

1. **`/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp`**
   - Add `oracle_execute()` scalar function
   - Register 4 new settings
   - Update `GetOracleSettings()` function

2. **`/home/cody/code/other/duckdb-oracle/src/include/oracle_catalog_state.hpp`**
   - Add `current_schema` field
   - Add `DetectCurrentSchema()` method
   - Add `ListObjects()` method (replaces ListTables)
   - Add `ResolveSynonym()` method
   - Add resolution cache

3. **`/home/cody/code/other/duckdb-oracle/src/storage/oracle_catalog.cpp`**
   - Implement lazy schema loading logic
   - Implement current schema detection
   - Implement multi-object-type queries

4. **`/home/cody/code/other/duckdb-oracle/src/storage/oracle_schema_entry.cpp`**
   - Modify table lookup to check current schema first
   - Integrate synonym resolution

5. **`/home/cody/code/other/duckdb-oracle/src/include/oracle_settings.hpp`**
   - Add 4 new settings fields

### Test Files to Create

6. **`/home/cody/code/other/duckdb-oracle/test/sql/test_oracle_execute.test`**
7. **`/home/cody/code/other/duckdb-oracle/test/sql/test_lazy_metadata.test`**
8. **`/home/cody/code/other/duckdb-oracle/test/sql/test_synonyms.test`**
9. **`/home/cody/code/other/duckdb-oracle/test/sql/test_schema_resolution.test`**
10. **`/home/cody/code/other/duckdb-oracle/test/sql/test_metadata_object_types.test`**
11. **`/home/cody/code/other/duckdb-oracle/test/integration/large_schema_setup.sql`**

## Agent-Specific Instructions

### For Expert Agent (Phase 2-4)

**Your Mission:** Research, implement, and integrate advanced Oracle features

**Phase 2: Research (Current Phase)**

Start with these 4 research tasks:

1. **Task 2.1: OCI Execution Patterns**
   - Read `/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp` lines 168-320 (OracleBindInternal)
   - Research OCI documentation for `OCIStmtExecute` non-describe mode
   - Answer: How to execute DDL/PL/SQL and extract row count?
   - Create: `research/oci-execution-patterns.md`

2. **Task 2.2: Catalog Architecture**
   - Read DuckDB source for ViewCatalogEntry pattern
   - Read `/home/cody/code/other/duckdb-oracle/src/storage/oracle_table_entry.cpp`
   - Answer: How to create catalog entries for views/materialized views?
   - Create: `research/catalog-architecture.md`

3. **Task 2.3: Synonym Resolution**
   - Research Oracle ALL_SYNONYMS view structure
   - Design synonym resolution algorithm (handle circular refs)
   - Answer: Cache synonyms or resolve at query time?
   - Create: `research/synonym-resolution.md`

4. **Task 2.4: Benchmark Large Schema**
   - Create SQL script to generate 1000 test tables
   - Measure current attach time and memory usage
   - Identify bottlenecks in metadata queries
   - Create: `research/benchmark-large-schema.sql` and `research/benchmark-results.md`

**Phase 3: Implementation**

After research complete, implement in this order:

1. Feature 1: `oracle_execute()` (tasks 3.1-3.4) - Quick win
2. Feature 2: Metadata scalability (tasks 3.5-3.9) - Foundational
3. Feature 3: Schema resolution (tasks 3.10-3.12) - Depends on #2

**Key Implementation Notes:**

- Reuse existing OCI connection patterns from `OracleBindInternal`
- All metadata queries need mutex protection (see `OracleCatalogState::lock`)
- Use DuckDB's `StringUtil::Format()` for SQL string building
- Throw `IOException` for OCI errors with `CheckOCIError()`

**Testing Strategy:**

- Write unit tests as you implement (in same commit)
- Use `make test` to run SQL test suite
- Use `make integration` for containerized Oracle tests
- Auto-invoke Testing agent when core implementation complete

**Build Commands:**

```bash
make clean
make debug                    # Development builds
build/debug/test/unittest test/sql/test_oracle_execute.test
```

### For Testing Agent (Phase 5)

**Your Mission:** Comprehensive testing of all three features

**Entry Condition:** Expert completes Phase 4 (Integration)

**Test Creation Priority:**

1. `test_oracle_execute.test` - DDL, DML, PL/SQL, errors
2. `test_lazy_metadata.test` - Performance with large schema
3. `test_synonyms.test` - Private, public, broken synonyms
4. `test_schema_resolution.test` - Current schema priority
5. `test_metadata_object_types.test` - Views, mviews, tables

**Test Requirements:**

- Use DuckDB SQL test format (`.test` files)
- Include smoke tests, integration tests, edge cases
- Test both success and error paths
- Verify performance targets (attach < 5 seconds)

**Performance Benchmarks:**

- Attach time vs schema size (100, 1K, 10K tables)
- Memory usage during attach
- Metadata query count
- Document results in workspace

**Auto-invoke Docs & Vision agent when all tests pass**

### For Docs & Vision Agent (Phase 6-7)

**Your Mission:** Documentation, quality gate, knowledge capture

**Entry Condition:** Testing agent completes Phase 5

**Phase 6: Documentation Tasks**

1. **Update CLAUDE.md**
   - Add oracle_execute pattern to "Common Development Patterns"
   - Add metadata scalability pattern
   - Document new settings

2. **Create Guides**
   - `specs/guides/metadata-scalability.md` - Large schema best practices
   - `specs/guides/oracle-execute-security.md` - SQL injection mitigation

3. **Update README.md**
   - Add oracle_execute to function list
   - Document new settings with examples
   - Add security warnings

**Phase 7: Quality Gate**

Run through quality checklist:

- [ ] All acceptance criteria met (from prd.md)
- [ ] All tests passing
- [ ] Performance targets met (attach < 5 seconds)
- [ ] Security warnings prominent in docs
- [ ] Code examples tested
- [ ] No memory leaks (valgrind clean)

**Archive Workspace:**

```bash
mkdir -p /home/cody/code/other/duckdb-oracle/specs/archive/advanced-oracle-features/
mv /home/cody/code/other/duckdb-oracle/specs/active/advanced-oracle-features/* \
   /home/cody/code/other/duckdb-oracle/specs/archive/advanced-oracle-features/
```

Update this recovery.md with:

- **Status**: COMPLETED
- **Completion Date**: YYYY-MM-DD
- **Performance Results**: Link to benchmarks
- **Lessons Learned**: Key insights for future work

## Key Technical Decisions

### Decision 1: Lazy Loading Default

**Decision:** Enable `oracle_lazy_schema_loading` by default
**Rationale:** Large schemas (100K+ tables) overwhelm current implementation
**Trade-off:** Breaking change - users must explicitly load other schemas
**Mitigation:** Clear documentation, setting to disable, migration guide

### Decision 2: oracle_execute Security Model

**Decision:** No prepared statement support in v1
**Rationale:** Complexity vs timeline, user responsibility for input validation
**Trade-off:** SQL injection risk if misused
**Mitigation:** Prominent security warnings, recommend SecretManager, future enhancement

### Decision 3: Synonym Resolution Timing

**Decision:** Resolve synonyms at table lookup time, not enumeration
**Rationale:** Synonyms can point to remote DBs (unsupported), avoid breaking enumeration
**Trade-off:** Extra query per synonym access (mitigated by caching)
**Mitigation:** Cache resolved synonyms, document behavior

### Decision 4: Current Schema Detection

**Decision:** Detect schema once at attach time, cache result
**Rationale:** Performance - avoid SYS_CONTEXT query on every table lookup
**Trade-off:** Stale if user runs ALTER SESSION SET CURRENT_SCHEMA
**Mitigation:** Document behavior, provide oracle_clear_cache() to refresh

### Decision 5: Hybrid Metadata Enumeration (On-Demand Fallback)

**Decision:** Enumerate up to 10,000 objects, but support on-demand loading for tables beyond limit
**Rationale:** Prevents silent failures when schemas exceed enumeration limit
**Approach:**
- Enumerate first 10,000 objects (for autocomplete/discovery)
- When table lookup fails, query `all_objects` directly to check existence
- Create table entry on-demand if object exists
- Log warning but continue working

**Benefits:**
- Fast attach times (enumerate only 10K objects)
- Complete table access (all tables queryable, even beyond limit)
- Good UX (autocomplete works for common tables)
- No silent failures (users never get "table not found" for existing tables)

**Trade-off:** One extra query per table access for tables beyond enumeration limit
**Mitigation:** Single-table lookups are fast (<10ms), acceptable for occasional access

## Performance Targets

### Metadata Scalability

- **Attach Time (100K tables, lazy loading ON):** < 5 seconds (target: 2 seconds)
- **Attach Time (1K tables, lazy loading OFF):** < 60 seconds
- **Memory Usage (attach):** < 500MB regardless of schema size
- **Metadata Query Count (lazy loading ON):** < 10 queries

### Schema Resolution

- **Resolution Overhead:** < 1ms per query (negligible)
- **Cache Hit Rate:** > 95% for repeated table access

### oracle_execute

- **Execution Time:** Same as native Oracle (no overhead)
- **Error Response Time:** < 100ms for OCI errors

## Risk Mitigation Status

| Risk | Severity | Mitigation Status | Notes |
|------|----------|-------------------|-------|
| Metadata query timeout | HIGH | Planned | ROWNUM limiting in Task 3.9 |
| Breaking changes | MEDIUM | Planned | Configurable setting, docs |
| SQL injection | HIGH | Planned | Security guide in Phase 6 |
| Synonym resolution complexity | MEDIUM | Planned | Error handling in Task 3.8 |
| Schema context loss | LOW | Documented | Known limitation |

## Dependencies

### External Systems

- Oracle Instant Client 19c/21c/23c (OCI library)
- Oracle database 19c+ (testing)
- Docker/Podman (integration tests)

### DuckDB APIs

- `TableFunction` - For oracle_execute return value
- `ScalarFunction` - For oracle_execute function type
- `Catalog` / `CatalogEntry` - For metadata enumeration
- `DefaultGenerator` - For lazy loading pattern
- `SecretManager` - For credential management

## Common Issues & Solutions

### Issue: Attach hangs with large schema

**Cause:** Lazy loading disabled, enumerating all 100K tables
**Solution:** Enable lazy loading: `SET oracle_lazy_schema_loading = true`
**Prevention:** Default setting is true in new installations

### Issue: Table not found after attach

**Cause:** Lazy loading enabled, table in different schema
**Solution 1:** Explicitly qualify: `SELECT * FROM ora.OTHER_SCHEMA.TABLE`
**Solution 2:** Disable lazy loading: `SET oracle_lazy_schema_loading = false`
**Solution 3:** Reference schema first: `SELECT * FROM ora.OTHER_SCHEMA.DUMMY` (forces load)

### Issue: oracle_execute returns "ORA-00900: invalid SQL statement"

**Cause:** SQL syntax error or unsupported statement type
**Solution:** Verify SQL in Oracle SQL*Plus first, check OCI documentation
**Debug:** Enable `SET oracle_debug_show_queries = true` to see executed SQL

### Issue: Synonym not resolving

**Cause:** Synonym points to inaccessible table or remote database link
**Solution:** Check synonym target: `SELECT table_owner, table_name FROM all_synonyms WHERE synonym_name = 'SYN'`
**Limitation:** Remote DB link synonyms not supported

## Research References

### Oracle Documentation

- [SYS_CONTEXT Function](https://docs.oracle.com/en/database/oracle/oracle-database/19/sqlrf/SYS_CONTEXT.html)
- [ALL_OBJECTS View](https://docs.oracle.com/cd/B28359_01/server.111/b28320/statviews_1140.htm)
- [ALL_SYNONYMS View](https://docs.oracle.com/cd/B19306_01/server.102/b14231/views.htm)
- [Data Dictionary Views](https://docs.oracle.com/en/database/oracle/oracle-database/19/cncpt/data-dictionary-and-dynamic-performance-views.html)

### DuckDB Community Extensions

- [BigQuery Extension](https://github.com/hafenkran/duckdb-bigquery) - Reference for execute function pattern
- [BigQuery Extension Docs](https://duckdb.org/community_extensions/extensions/bigquery)

### Stack Overflow References

- [Current Schema Detection](https://stackoverflow.com/questions/69688166/how-to-check-the-current-schemas-and-change-the-default-schema-in-pl-sql)
- [Oracle Metadata Views](https://stackoverflow.com/questions/24082812/oracle-query-for-displaying-view-meta-data)

## Contact / Escalation

**For Questions:**

- Read `prd.md` for comprehensive requirements
- Read `tasks.md` for detailed implementation steps
- Check `CLAUDE.md` for project-wide patterns

**For Blockers:**

- Document in `research/blockers.md`
- Consider alternative approaches
- Consult DuckDB documentation or source code

---

**Status Summary:**

- Phase 1: Planning & Research - COMPLETED
- Phase 2: Expert Research - READY TO START
- Phase 3: Core Implementation - NOT STARTED
- Phase 4: Integration - NOT STARTED
- Phase 5: Testing - NOT STARTED
- Phase 6: Documentation - NOT STARTED
- Phase 7: Quality Gate - NOT STARTED

**Next Action:** Expert agent begins Task 2.1 (OCI Execution Patterns Research)
