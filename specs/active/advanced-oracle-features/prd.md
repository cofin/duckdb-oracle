# PRD: Advanced Oracle Extension Features

**Status**: Planning
**Created**: 2025-11-22
**Owner**: PRD Agent

## Overview

This PRD covers three interconnected features that enhance the Oracle extension for enterprise-scale usage:

1. **Arbitrary SQL Execution Functions** - `oracle_execute()` for executing stored procedures, DDL, and other non-returning SQL
2. **Metadata Schema Scalability** - Support for Oracle schemas with 100,000+ tables and ~1,000,000 database objects
3. **Schema Resolution / Current Schema Context** - Intelligent default schema detection and unqualified table name resolution

These features address critical gaps in the current Oracle extension that limit its use in large enterprise environments with complex database schemas.

## Problem Statement

### Current Limitations

1. **Missing Execute Function**
   - `oracle_query()` exists but requires result set (SELECT statements only)
   - No way to execute Oracle stored procedures, packages, or DDL statements
   - Cannot call PL/SQL blocks that don't return cursors
   - Limits automation and database administration workflows

2. **Metadata Enumeration Performance**
   - Current implementation loads ALL schemas and ALL tables upfront
   - `ListSchemas()` queries `all_users` (potentially thousands of schemas)
   - `ListTables()` queries `all_tables` per schema (potentially 100K+ tables)
   - Attach operation with large schema takes minutes and consumes excessive memory
   - Only supports tables (ignores views, materialized views, synonyms)

3. **Schema Resolution UX**
   - Users must fully qualify table names: `SELECT * FROM ora.HR.EMPLOYEES`
   - No default to current user's schema context
   - Oracle applications expect `SELECT * FROM EMPLOYEES` to work when connected as HR user
   - Mismatch between Oracle SQL conventions and DuckDB extension behavior

## Goals

### Primary Goals

1. **Enable Arbitrary SQL Execution**
   - Support Oracle stored procedure calls
   - Execute DDL statements (CREATE, ALTER, DROP)
   - Run PL/SQL blocks and anonymous blocks
   - Return execution status and row counts where applicable

2. **Achieve Sub-5-Second Attach for Large Schemas**
   - Lazy load metadata only when accessed
   - Default to current user's schema for initial enumeration
   - Support on-demand schema expansion
   - Handle 100,000+ tables without memory exhaustion

3. **Improve Schema Resolution UX**
   - Auto-detect current schema from Oracle session
   - Resolve unqualified table names to current schema first
   - Maintain backward compatibility with fully qualified names
   - Provide settings to control resolution behavior

### Secondary Goals

- Support views, materialized views, and synonyms as queryable objects
- Maintain consistent performance regardless of schema size
- Provide clear error messages for schema resolution ambiguity
- Document security implications of arbitrary SQL execution

## Target Users

- **Data Engineers**: Building ETL pipelines that call Oracle stored procedures
- **Database Administrators**: Running DDL and administrative SQL from DuckDB
- **Analysts**: Querying large enterprise Oracle databases with 10K+ tables
- **Application Developers**: Migrating Oracle applications to hybrid DuckDB/Oracle architecture

## Technical Scope

### Feature 1: Arbitrary SQL Execution Functions

#### oracle_execute()

**Function Signature:**

```sql
oracle_execute(connection_string VARCHAR, sql_statement VARCHAR) -> VARCHAR
```

**Behavior:**

- Execute arbitrary Oracle SQL without expecting result set
- Handle DDL (CREATE TABLE, DROP INDEX, etc.)
- Execute stored procedures and packages
- Run PL/SQL blocks (BEGIN...END)
- Return execution status message

**Implementation:**

- Reuse existing OCI connection logic from `OracleBindInternal`
- Call `OCIStmtExecute()` without `OCI_DESCRIBE_ONLY` mode
- Extract row count from `OCI_ATTR_ROW_COUNT` attribute
- Return formatted message: "Statement executed successfully (N rows affected)"
- Throw `IOException` on OCI errors

**Example Usage:**

```sql
-- Execute stored procedure
SELECT oracle_execute('user/pass@db', 'BEGIN hr_pkg.update_salaries(1.05); END;');

-- Run DDL
SELECT oracle_execute('user/pass@db', 'CREATE INDEX idx_emp_dept ON employees(department_id)');

-- Call procedure with parameters
SELECT oracle_execute('user/pass@db', 'CALL archive_old_orders(2020)');
```

**Security Considerations:**

- No prepared statement support (SQL injection risk)
- Document best practices: use SecretManager, avoid user input in SQL
- Consider parameterized version in future enhancement

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp` - Add `OracleExecuteBind()` and `OracleExecuteFunction()`
- `/home/cody/code/other/duckdb-oracle/src/include/oracle_table_function.hpp` - Add execution context struct if needed

### Feature 2: Metadata Schema Scalability

#### Lazy Schema Loading

**New Settings:**

```sql
SET oracle_lazy_schema_loading = true;  -- Default: true
SET oracle_metadata_object_types = 'TABLE,VIEW,SYNONYM,MVIEW';  -- Default: 'TABLE,VIEW,SYNONYM,MVIEW'
SET oracle_metadata_result_limit = 10000;  -- Default: 10000
```

**Behavior Changes:**

1. **Current Schema Detection**
   - Query `SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA')` on connection
   - Store in `OracleCatalogState::current_schema`
   - Use as default schema for enumeration

2. **Lazy Schema Enumeration**
   - When `oracle_lazy_schema_loading = true`:
     - `ListSchemas()` returns only `[current_schema]`
     - Explicitly referenced schemas loaded on-demand
   - When `oracle_lazy_schema_loading = false`:
     - Existing behavior (load all from `all_users`)

3. **Multi-Object-Type Support**
   - Query `ALL_OBJECTS` instead of `ALL_TABLES`
   - Filter by object types from `oracle_metadata_object_types` setting
   - Supported types: `TABLE`, `VIEW`, `SYNONYM`, `MATERIALIZED VIEW`
   - Default: All types enabled (most complete behavior from day one)

4. **Synonym Resolution**
   - When table lookup fails, check `ALL_SYNONYMS`
   - Resolve synonym target: `SELECT table_owner, table_name FROM all_synonyms WHERE synonym_name = ?`
   - Create table entry for resolved target
   - Handle public synonyms (owner = 'PUBLIC')

5. **Pagination / Limiting with On-Demand Fallback (Hybrid Approach)**
   - Enumerate up to `oracle_metadata_result_limit` objects for discovery/autocomplete (default: 10,000)
   - When catalog lookup fails, attempt **on-demand table loading** via direct query to `all_objects`
   - Log warning when enumeration limit reached, but continue to work via on-demand loading
   - Users can set to 0 for unlimited enumeration (at their own risk with very large schemas)

   **Rationale**: This hybrid approach provides:
   - Fast attach times (enumerate only first 10K objects)
   - Complete table access (on-demand loading for tables beyond limit)
   - Good UX (autocomplete works for common tables, all tables still accessible)
   - No silent failures (tables beyond limit are still queryable)

**Implementation:**

**OracleCatalogState Changes:**

```cpp
class OracleCatalogState {
public:
    string current_schema;  // NEW: Detected from SYS_CONTEXT

    void DetectCurrentSchema();  // NEW: Query current schema on connect
    vector<string> ListSchemas(bool lazy = true);  // Modified: lazy parameter
    vector<string> ListObjects(const string &schema, const string &object_types);  // NEW: Replaces ListTables
    optional<pair<string, string>> ResolveTableOrSynonym(const string &schema, const string &name);  // NEW
    bool ObjectExists(const string &schema, const string &object_name, const string &object_type);  // NEW: On-demand check
};
```

**Oracle Metadata Queries:**

```sql
-- Detect current schema (called in EnsureConnection)
SELECT SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA') FROM DUAL;

-- List objects with type filtering (enumeration with limit)
SELECT object_name, object_type
FROM all_objects
WHERE owner = UPPER(?)
  AND object_type IN ('TABLE', 'VIEW', 'SYNONYM', 'MATERIALIZED VIEW')
  AND ROWNUM <= ?
ORDER BY object_name;

-- On-demand object existence check (when not in enumerated list)
SELECT 1
FROM all_objects
WHERE owner = UPPER(?)
  AND object_name = UPPER(?)
  AND object_type IN ('TABLE', 'VIEW', 'MATERIALIZED VIEW');

-- Resolve synonym
SELECT table_owner, table_name
FROM all_synonyms
WHERE synonym_name = UPPER(?)
  AND (owner = UPPER(?) OR owner = 'PUBLIC');
```

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/include/oracle_catalog_state.hpp` - Add current_schema field, new methods
- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_catalog.cpp` - Modify ListSchemas/ListTables logic
- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_schema_entry.cpp` - Use new ListObjects method
- `/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp` - Register new settings

### Feature 3: Schema Resolution / Current Schema Context

**New Setting:**

```sql
SET oracle_use_current_schema = true;  -- Default: true
```

**Behavior:**

When `oracle_use_current_schema = true`:

- Unqualified table names resolve to `current_schema` first
- Example: Connected as HR user, `SELECT * FROM ora.EMPLOYEES` works without `ora.HR.EMPLOYEES`
- Falls back to explicit schema if specified
- Throws ambiguity error if table exists in multiple schemas and no qualifier

**Schema Resolution Logic:**

```
Query: SELECT * FROM ora.EMPLOYEES
Steps:
1. Check if "EMPLOYEES" exists in current_schema (HR)
   - Query: SELECT 1 FROM all_objects WHERE owner = 'HR' AND object_name = 'EMPLOYEES'
2. If found, resolve to ora.HR.EMPLOYEES
3. If not found, check ALL schemas (existing behavior)
4. If still not found, check synonyms
5. Throw error if not found or ambiguous
```

**Implementation:**

**Catalog Resolution Override:**

- Modify `OracleSchemaGenerator::CreateDefaultEntry()` to check current schema first
- Add schema resolution priority queue: [current_schema, explicitly_qualified, all_schemas]
- Cache resolution results to avoid repeated metadata queries

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_schema_entry.cpp` - Modify table lookup logic
- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_catalog.cpp` - Add schema resolution method
- `/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp` - Register setting

## Acceptance Criteria

### Feature 1: oracle_execute

- [ ] Function registered and callable from SQL
- [ ] Executes PL/SQL blocks successfully
- [ ] Executes DDL statements (CREATE, DROP, ALTER)
- [ ] Returns execution status with row count
- [ ] Throws clear error messages on OCI failure
- [ ] Works with SecretManager credentials
- [ ] Documentation includes security warnings

### Feature 2: Metadata Scalability

- [ ] Attach to schema with 100K tables completes in <5 seconds
- [ ] `oracle_lazy_schema_loading` setting controls behavior
- [ ] Only current schema enumerated by default
- [ ] On-demand schema loading works when referenced
- [ ] Views, materialized views enumerated with setting
- [ ] Synonyms resolve to target table correctly
- [ ] Public synonyms work
- [ ] Metadata query limit prevents memory exhaustion

### Feature 3: Schema Resolution

- [ ] Current schema auto-detected on connection
- [ ] Unqualified table names resolve to current schema
- [ ] Fully qualified names still work
- [ ] `oracle_use_current_schema` setting toggles behavior
- [ ] Clear error on ambiguous table references
- [ ] Backward compatible with existing queries

## Implementation Phases

### Phase 1: Planning & Research (COMPLETED)

- [x] PRD creation
- [x] Research BigQuery extension pattern
- [x] Research Oracle metadata views
- [x] Research SYS_CONTEXT usage
- [x] Risk analysis
- [x] Task breakdown

### Phase 2: Expert Research

**Expert Agent Tasks:**

- Research DuckDB catalog entry creation patterns for views/synonyms
- Investigate table generator caching strategies
- Research OCI execution modes (non-describe execution)
- Review DuckDB schema resolution hooks
- Prototype synonym resolution logic

**Deliverables:**

- Research findings in `research/` directory
- Code snippets for OCI execution pattern
- Catalog architecture recommendations

### Phase 3: Core Implementation

**Feature 1: oracle_execute (Priority 1)**

- Implement `OracleExecuteFunction` scalar function
- Add OCI execution without describe mode
- Extract row count from statement handle
- Error handling and status message formatting
- Unit tests for DDL and PL/SQL execution

**Feature 2: Metadata Scalability (Priority 2)**

- Add `current_schema` detection to `OracleCatalogState`
- Implement lazy schema loading with setting
- Replace `ListTables` with `ListObjects` (multi-type support)
- Add synonym resolution logic
- Implement metadata query pagination

**Feature 3: Schema Resolution (Priority 3)**

- Add `oracle_use_current_schema` setting
- Modify schema generator to prioritize current schema
- Implement resolution priority queue
- Cache resolution results

### Phase 4: Integration

- Register all new settings in `OracleExtension::Load()`
- Update `CreateOracleCatalog` to call `DetectCurrentSchema()`
- Integrate synonym resolution into table entry creation
- End-to-end testing with all features enabled

### Phase 5: Testing (Auto-invoked)

**Testing Agent Tasks:**

- Create SQL tests for `oracle_execute` function
- Create integration tests with 100K table synthetic schema
- Test synonym resolution (public and private)
- Test schema resolution priority
- Performance benchmark: attach time with lazy loading
- Backward compatibility tests (lazy loading disabled)

**Test Files:**

- `test/sql/test_oracle_execute.test`
- `test/sql/test_lazy_metadata.test`
- `test/sql/test_schema_resolution.test`
- `test/sql/test_synonyms.test`
- `test/integration/large_schema_setup.sql`

### Phase 6: Documentation (Auto-invoked)

**Docs & Vision Agent Tasks:**

- Update `CLAUDE.md` with new patterns
- Document security considerations for `oracle_execute`
- Add metadata scalability guide to `specs/guides/`
- Update README with new settings
- Add code examples for all three features

### Phase 7: Quality Gate & Archive (Auto-invoked)

- All acceptance criteria validated
- Performance benchmarks pass
- Security review complete
- Documentation complete
- Move workspace to `specs/archive/advanced-oracle-features/`

## Dependencies

### Internal Dependencies

- DuckDB Extension Framework (catalog system, table functions)
- Existing Oracle extension infrastructure (OCI connection, OracleBindData)
- DuckDB SecretManager (credential management)

### External Dependencies

- Oracle Instant Client (OCI library)
- Oracle database 19c+ (testing environment)
- Docker/Podman (containerized Oracle for integration tests)

### Code Dependencies

- `oracle_extension.cpp` - Function registration, settings
- `oracle_catalog_state.hpp/cpp` - Shared state, metadata queries
- `oracle_schema_entry.cpp` - Schema and table generation
- `oracle_table_entry.cpp` - Table metadata loading
- `oracle_connection.hpp/cpp` - OCI connection wrapper

## Risks & Mitigations

### Risk 1: Performance - Metadata Queries (HIGH)

**Risk:** ALL_OBJECTS query with 1,000,000+ objects could timeout or consume excessive memory

**Impact:** Attach operation fails or becomes unusable

**Mitigation:**

- Implement query result limiting with `ROWNUM`
- Use WHERE clause filtering by object type
- Add pagination support for metadata queries
- Default to lazy loading (only current schema)
- Log warnings when limits are reached

**Validation:** Load test with synthetic 100K table schema, measure attach time and memory usage

### Risk 2: Breaking Changes (MEDIUM)

**Risk:** Lazy schema loading changes default behavior - users expect all schemas visible

**Impact:** Existing queries break if they reference non-current schemas

**Mitigation:**

- Make lazy loading configurable via setting
- Default to lazy loading but document clearly
- Provide migration guide for disabling lazy loading
- Maintain backward compatibility mode

**Validation:** Run backward compatibility test suite with lazy loading disabled

### Risk 3: SQL Injection - oracle_execute (HIGH)

**Risk:** Arbitrary SQL execution without parameter binding opens SQL injection vector

**Impact:** Security vulnerability if user input concatenated into SQL

**Mitigation:**

- Document security warnings prominently
- Recommend using SecretManager for credentials
- Provide code examples with safe patterns
- Consider prepared statement API in future version
- Add linting rule to detect string concatenation in SQL

**Validation:** Security review, penetration testing, documentation audit

### Risk 4: Synonym Resolution Complexity (MEDIUM)

**Risk:** ALL_SYNONYMS can point to remote databases, views, or non-existent objects

**Impact:** Table lookup fails or produces confusing errors

**Mitigation:**

- Graceful fallback: log warning and skip if target unresolvable
- Clear error messages indicating synonym target
- Document synonym limitations (remote DB links not supported)
- Handle circular synonym references (detection + error)

**Validation:** Integration tests with public/private synonyms, broken synonyms, circular references

### Risk 5: Current Schema Context Loss (LOW)

**Risk:** User runs `ALTER SESSION SET CURRENT_SCHEMA` in SQL, changing context unexpectedly

**Impact:** Schema resolution uses stale cached schema

**Mitigation:**

- Cache schema only on initial connection
- Document behavior: current schema detected at attach time
- Provide `oracle_clear_cache()` to refresh metadata
- Consider detecting schema change in future version

**Validation:** Test schema switching scenarios, document expected behavior

## Research Questions for Expert

### OCI Execution Patterns

1. What is the correct OCI call sequence for executing non-returning SQL?
   - Is `OCIStmtExecute()` without `OCI_DESCRIBE_ONLY` sufficient?
   - How to extract affected row count for DML statements?
   - How to handle PL/SQL OUT parameters?

2. How to differentiate DDL vs DML vs SELECT in OCI?
   - Should we auto-detect and handle differently?
   - What statement types return row counts?

### Catalog Architecture

3. How to extend DuckDB catalog entry types for views and materialized views?
   - Can we reuse `TableCatalogEntry` or need `ViewCatalogEntry`?
   - How to lazy-load view definitions?

4. What is the best pattern for on-demand schema loading?
   - Should we override `Catalog::GetSchema()` to load schemas lazily?
   - How to cache loaded schemas without upfront enumeration?

5. How to implement synonym resolution transparently?
   - Should synonyms appear as separate catalog entries?
   - Or resolve at query time to target table?

### Performance Optimization

6. What metadata query optimization strategies exist?
   - Can we use Oracle's result cache hints?
   - Should we batch metadata queries (multiple tables at once)?
   - What indexes exist on ALL_OBJECTS, ALL_SYNONYMS?

7. How to benchmark large schema performance?
   - Tools for generating synthetic 100K table schema?
   - Profiling attach operation to identify bottlenecks?

## Success Metrics

### Performance Metrics

- Attach time for 100K table schema: <5 seconds (target: 2 seconds)
- Metadata query count during attach: <10 (with lazy loading)
- Memory usage during attach: <500MB (regardless of schema size)
- Query performance with schema resolution: No measurable overhead (<1ms)

### Functional Metrics

- `oracle_execute` success rate: 100% for valid SQL
- Synonym resolution accuracy: 100% for accessible targets
- Schema resolution correctness: 100% for unambiguous references
- Backward compatibility: 100% with lazy loading disabled

### Adoption Metrics

- User feedback on schema resolution UX (target: positive)
- Bug reports related to metadata scalability (target: 0 critical)
- Security incidents from `oracle_execute` (target: 0)

## References

### Similar Features in Other Extensions

- BigQuery extension: `bigquery_query()`, `bigquery_execute()` functions
- PostgreSQL extension: Schema resolution logic
- MySQL extension: Multi-object-type catalog support

### Oracle Documentation

- [SYS_CONTEXT Function](https://docs.oracle.com/en/database/oracle/oracle-database/19/sqlrf/SYS_CONTEXT.html)
- [Data Dictionary Views](https://docs.oracle.com/en/database/oracle/oracle-database/19/cncpt/data-dictionary-and-dynamic-performance-views.html)
- [ALL_OBJECTS View](https://docs.oracle.com/cd/B28359_01/server.111/b28320/statviews_1140.htm)
- [Managing Synonyms](https://docs.oracle.com/cd/B19306_01/server.102/b14231/views.htm)

### Research Sources

- [DuckDB BigQuery Extension](https://github.com/hafenkran/duckdb-bigquery)
- [BigQuery DuckDB Community Extensions](https://duckdb.org/community_extensions/extensions/bigquery)
- [Oracle Metadata Performance](https://stackoverflow.com/questions/24082812/oracle-query-for-displaying-view-meta-data)
- [Current Schema Detection](https://stackoverflow.com/questions/69688166/how-to-check-the-current-schemas-and-change-the-default-schema-in-pl-sql)

### Internal Documentation

- `/home/cody/code/other/duckdb-oracle/CLAUDE.md` - Agent system and project standards
- `/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp` - Existing extension implementation
- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_catalog.cpp` - Catalog implementation
- `/home/cody/code/other/duckdb-oracle/src/include/oracle_catalog_state.hpp` - Shared state structure

---

**Next Steps:** Expert agent to begin Phase 2 research, focusing on OCI execution patterns and catalog architecture investigation.
