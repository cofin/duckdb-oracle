# Tasks: Advanced Oracle Extension Features

**Status**: Planning Complete
**Created**: 2025-11-22

## Task Overview

Total: 26 tasks across 7 phases

```
Phase 1: Planning & Research          [COMPLETED]
Phase 2: Expert Research               [4 tasks]
Phase 3: Core Implementation           [12 tasks]
Phase 4: Integration                   [4 tasks]
Phase 5: Testing (Auto-invoked)        [6 tasks]
Phase 6: Documentation (Auto-invoked)  [3 tasks]
Phase 7: Quality Gate (Auto-invoked)   [3 tasks]
```

---

## Phase 1: Planning & Research [COMPLETED]

- [x] Create PRD workspace structure
- [x] Research BigQuery extension patterns
- [x] Research Oracle metadata views
- [x] Research SYS_CONTEXT usage
- [x] Write comprehensive PRD
- [x] Risk analysis complete
- [x] Task breakdown complete

---

## Phase 2: Expert Research [NOT STARTED]

**Owner:** Expert Agent
**Estimated Tasks:** 4

### Task 2.1: Research OCI Execution Patterns

**Files to Examine:**

- `/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp` (OracleBindInternal function)
- Oracle OCI documentation for OCIStmtExecute modes

**Research Questions:**

- What is the call sequence for executing non-returning SQL?
- How to extract affected row count from OCI_ATTR_ROW_COUNT?
- How to handle PL/SQL blocks with OUT parameters?
- What error codes indicate DDL vs DML vs invalid syntax?

**Deliverable:** `research/oci-execution-patterns.md` with code snippets

- [ ] Document OCIStmtExecute call sequence for DDL
- [ ] Document row count extraction pattern
- [ ] Document PL/SQL block execution pattern
- [ ] Document error handling for different statement types

### Task 2.2: Research DuckDB Catalog Architecture

**Files to Examine:**

- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_schema_entry.cpp`
- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_table_entry.cpp`
- DuckDB source: `src/catalog/catalog_entry/view_catalog_entry.hpp`

**Research Questions:**

- How to create ViewCatalogEntry for Oracle views?
- Can we reuse TableCatalogEntry for materialized views?
- How to implement lazy schema loading in DefaultGenerator?
- What caching patterns exist in DuckDB catalog system?

**Deliverable:** `research/catalog-architecture.md` with recommendations

- [ ] Document view catalog entry creation pattern
- [ ] Document materialized view handling strategy
- [ ] Document lazy loading implementation approach
- [ ] Recommend caching strategy for on-demand schemas

### Task 2.3: Research Synonym Resolution Logic

**Files to Examine:**

- Oracle documentation for ALL_SYNONYMS view
- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_catalog_state.hpp`

**Research Questions:**

- How to query synonyms efficiently (public vs private)?
- How to detect circular synonym references?
- Should synonyms be cached or resolved at query time?
- How to handle remote database link synonyms (not supported)?

**Deliverable:** `research/synonym-resolution.md` with SQL queries

- [ ] Document ALL_SYNONYMS query pattern
- [ ] Provide circular reference detection algorithm
- [ ] Recommend synonym caching strategy
- [ ] Document error handling for unresolvable synonyms

### Task 2.4: Benchmark Large Schema Performance

**Files to Create:**

- `research/benchmark-large-schema.sql` - Synthetic schema generation
- `research/benchmark-results.md` - Performance measurements

**Research Questions:**

- How long does current implementation take to attach to 1000 table schema?
- What is the memory footprint during attach?
- Which metadata queries are the bottlenecks?
- What indexes exist on ALL_OBJECTS, ALL_TABLES?

**Deliverable:** Baseline performance measurements and bottleneck identification

- [ ] Create SQL script to generate 1000 table test schema
- [ ] Measure attach time with current implementation
- [ ] Profile metadata query performance
- [ ] Identify bottlenecks (query time vs network vs memory)

---

## Phase 3: Core Implementation [NOT STARTED]

**Owner:** Expert Agent
**Dependencies:** Phase 2 complete

### Feature 1: oracle_execute Function (Priority 1)

#### Task 3.1: Implement OracleExecute Scalar Function

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp`

**Implementation Steps:**

1. Add `OracleExecuteBind()` function:

```cpp
static void OracleExecuteBind(ClientContext &context, ScalarFunctionBindInput &input,
                               FunctionData &bind_data) {
    // Validate connection string and SQL parameters
    // Store in bind data structure
}
```

2. Add `OracleExecuteFunction()` scalar function:

```cpp
static void OracleExecuteFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto connection_string = args.data[0].GetValue(0).ToString();
    auto sql = args.data[1].GetValue(0).ToString();

    // Create OCI connection
    // Execute SQL without OCI_DESCRIBE_ONLY
    // Extract row count
    // Return status message
}
```

3. Register function in `LoadInternal()`:

```cpp
auto oracle_execute_func = ScalarFunction("oracle_execute",
    {LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR,
    OracleExecuteFunction, OracleExecuteBind);
loader.RegisterFunction(oracle_execute_func);
```

**Acceptance Criteria:**

- [ ] Function compiles without errors
- [ ] Function registered and callable from SQL
- [ ] Accepts connection string and SQL parameters

#### Task 3.2: Implement OCI Execution Logic

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp`

**Implementation:**

1. Create OCI connection (reuse existing pattern from OracleBindInternal)
2. Prepare statement: `OCIStmtPrepare(stmthp, errhp, sql, OCI_NTV_SYNTAX, OCI_DEFAULT)`
3. Execute statement: `OCIStmtExecute(svchp, stmthp, errhp, 1, 0, NULL, NULL, OCI_COMMIT_ON_SUCCESS)`
4. Extract row count:

```cpp
ub4 row_count = 0;
OCIAttrGet(stmthp, OCI_HTYPE_STMT, &row_count, 0, OCI_ATTR_ROW_COUNT, errhp);
```

5. Format result: `StringUtil::Format("Statement executed successfully (%llu rows affected)", row_count)`

**Acceptance Criteria:**

- [ ] DDL statements execute successfully (CREATE TABLE, DROP INDEX)
- [ ] DML statements return correct row count
- [ ] PL/SQL blocks execute without errors
- [ ] OCI errors throw IOException with descriptive message

#### Task 3.3: Add Error Handling and Status Messages

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp`

**Implementation:**

1. Wrap OCI calls with `CheckOCIError()`
2. Handle different statement types:
   - DDL: "Statement executed successfully"
   - DML: "Statement executed successfully (N rows affected)"
   - PL/SQL: "Block executed successfully"
3. Extract error messages from OCI error handle
4. Clean up OCI handles in exception cases (RAII pattern)

**Acceptance Criteria:**

- [ ] Clear error messages on syntax errors
- [ ] Clear error messages on permission errors
- [ ] OCI handles cleaned up on exception
- [ ] Error includes Oracle error code (ORA-XXXXX)

#### Task 3.4: Write Unit Tests for oracle_execute

**Files to Create:**

- `/home/cody/code/other/duckdb-oracle/test/sql/test_oracle_execute.test`

**Test Cases:**

```sql
# test_oracle_execute.test

# Test DDL execution
statement ok
SELECT oracle_execute('${ORACLE_CONN}', 'CREATE TABLE test_exec (id NUMBER, name VARCHAR2(100))');

# Test DML with row count
query I
SELECT oracle_execute('${ORACLE_CONN}', 'INSERT INTO test_exec VALUES (1, ''Alice'')');
----
Statement executed successfully (1 rows affected)

# Test PL/SQL block
statement ok
SELECT oracle_execute('${ORACLE_CONN}', 'BEGIN DBMS_OUTPUT.PUT_LINE(''Hello''); END;');

# Test error handling
statement error
SELECT oracle_execute('${ORACLE_CONN}', 'INVALID SQL SYNTAX');

# Cleanup
statement ok
SELECT oracle_execute('${ORACLE_CONN}', 'DROP TABLE test_exec');
```

**Acceptance Criteria:**

- [ ] All test cases pass
- [ ] Tests cover DDL, DML, PL/SQL, errors
- [ ] Tests clean up created objects

### Feature 2: Metadata Scalability (Priority 2)

#### Task 3.5: Add Current Schema Detection

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/include/oracle_catalog_state.hpp`
- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_catalog.cpp`

**Implementation:**

1. Add field to `OracleCatalogState`:

```cpp
class OracleCatalogState {
public:
    string current_schema;  // Detected from SYS_CONTEXT
    void DetectCurrentSchema();
};
```

2. Implement `DetectCurrentSchema()`:

```cpp
void OracleCatalogState::DetectCurrentSchema() {
    lock_guard<mutex> guard(lock);
    EnsureConnection();
    auto result = connection->Query(
        "SELECT SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA') FROM DUAL"
    );
    if (!result.rows.empty() && !result.rows[0].empty()) {
        current_schema = result.rows[0][0];
    }
}
```

3. Call in `OracleCatalog::Initialize()`:

```cpp
void OracleCatalog::Initialize(bool load_builtin) {
    state->DetectCurrentSchema();  // NEW
    state->EnsureConnection();
    DuckCatalog::Initialize(false);
    // ...
}
```

**Acceptance Criteria:**

- [ ] Current schema detected on connection
- [ ] Schema stored in OracleCatalogState
- [ ] Works for different connected users

#### Task 3.6: Implement Lazy Schema Loading

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_catalog.cpp`
- `/home/cody/code/other/duckdb-oracle/src/include/oracle_settings.hpp`

**Implementation:**

1. Add setting to `OracleSettings`:

```cpp
struct OracleSettings {
    bool lazy_schema_loading = true;  // NEW
};
```

2. Modify `OracleCatalogState::ListSchemas()`:

```cpp
vector<string> OracleCatalogState::ListSchemas() {
    lock_guard<mutex> guard(lock);

    if (settings.lazy_schema_loading) {
        // Return only current schema
        if (!current_schema.empty()) {
            return {current_schema};
        }
    }

    // Existing behavior: load all schemas
    if (!schema_cache.empty()) {
        return schema_cache;
    }
    // ... existing code
}
```

3. Register setting in `OracleExtension::Load()`:

```cpp
config.AddExtensionOption("oracle_lazy_schema_loading",
    "Load only current schema by default", LogicalType::BOOLEAN,
    Value::BOOLEAN(true));
```

**Acceptance Criteria:**

- [ ] Setting registered and configurable
- [ ] When enabled, only current schema returned
- [ ] When disabled, all schemas returned (backward compatibility)

#### Task 3.7: Replace ListTables with ListObjects

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/include/oracle_catalog_state.hpp`
- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_catalog.cpp`

**Implementation:**

1. Add object_types setting:

```cpp
struct OracleSettings {
    string metadata_object_types = "TABLE,VIEW,SYNONYM,MVIEW";  // NEW - all types by default
    idx_t metadata_result_limit = 10000;  // NEW - reasonable default protection
};
```

2. Replace `ListTables()` with `ListObjects()`:

```cpp
vector<string> OracleCatalogState::ListObjects(const string &schema, const string &object_types) {
    lock_guard<mutex> guard(lock);

    auto cache_key = schema + ":" + object_types;
    auto entry = object_cache.find(cache_key);
    if (entry != object_cache.end()) {
        return entry->second;
    }

    EnsureConnection();

    // Build IN clause from comma-separated object_types
    auto types = StringUtil::Split(object_types, ',');
    string types_sql = "'" + StringUtil::Join(types, "','") + "'";

    string query = StringUtil::Format(
        "SELECT object_name FROM all_objects "
        "WHERE owner = UPPER(%s) AND object_type IN (%s) "
        "ORDER BY object_name",
        Value(schema).ToSQLString().c_str(),
        types_sql.c_str()
    );

    if (settings.metadata_result_limit > 0) {
        query = StringUtil::Format(
            "SELECT * FROM (%s) WHERE ROWNUM <= %llu",
            query.c_str(), settings.metadata_result_limit
        );
    }

    auto result = connection->Query(query);
    vector<string> objects;
    for (auto &row : result.rows) {
        if (!row.empty()) {
            objects.push_back(row[0]);
        }
    }

    object_cache.emplace(cache_key, objects);
    return objects;
}
```

3. Update schema entry to use `ListObjects()`:

```cpp
// In OracleTableGenerator::GetDefaultEntries()
return state->ListObjects(schema.name, state->settings.metadata_object_types);
```

**Acceptance Criteria:**

- [ ] ListObjects returns tables when object_types="TABLE"
- [ ] Returns views when object_types="VIEW"
- [ ] Returns multiple types when object_types="TABLE,VIEW"
- [ ] Respects metadata_result_limit
- [ ] Results cached to avoid repeated queries

#### Task 3.8: Implement Synonym Resolution

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/include/oracle_catalog_state.hpp`
- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_catalog.cpp`

**Implementation:**

1. Add synonym resolution method:

```cpp
optional<pair<string, string>> OracleCatalogState::ResolveSynonym(
    const string &schema, const string &synonym_name) {

    lock_guard<mutex> guard(lock);
    EnsureConnection();

    auto query = StringUtil::Format(
        "SELECT table_owner, table_name FROM all_synonyms "
        "WHERE synonym_name = UPPER(%s) "
        "AND (owner = UPPER(%s) OR owner = 'PUBLIC') "
        "ORDER BY CASE WHEN owner = UPPER(%s) THEN 0 ELSE 1 END",
        Value(synonym_name).ToSQLString().c_str(),
        Value(schema).ToSQLString().c_str(),
        Value(schema).ToSQLString().c_str()
    );

    auto result = connection->Query(query);
    if (result.rows.empty()) {
        return std::nullopt;
    }

    return std::make_pair(result.rows[0][0], result.rows[0][1]);
}
```

2. Use in table lookup:

```cpp
// In OracleTableGenerator::CreateDefaultEntry()
unique_ptr<CatalogEntry> CreateDefaultEntry(ClientContext &context, const string &entry_name) override {
    // Try direct table lookup first
    auto table = OracleTableEntry::Create(catalog, schema, schema.name, entry_name, state);
    if (table) {
        return table;
    }

    // Try synonym resolution
    auto resolved = state->ResolveSynonym(schema.name, entry_name);
    if (resolved) {
        auto [target_schema, target_table] = *resolved;
        return OracleTableEntry::Create(catalog, schema, target_schema, target_table, state);
    }

    return nullptr;
}
```

**Acceptance Criteria:**

- [ ] Private synonyms resolve to target table
- [ ] Public synonyms resolve when no private synonym exists
- [ ] Synonym priority: private > public
- [ ] Returns nullopt for non-existent synonyms

#### Task 3.9: Add Metadata Result Limiting with On-Demand Fallback

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp`
- `/home/cody/code/other/duckdb-oracle/src/include/oracle_catalog_state.hpp`
- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_schema_entry.cpp`

**Implementation:**

1. Register setting:

```cpp
config.AddExtensionOption("oracle_metadata_result_limit",
    "Maximum rows returned from metadata queries (0=unlimited)",
    LogicalType::UBIGINT, Value::UBIGINT(10000));
```

2. Apply in `ListObjects()` (already implemented in Task 3.7)

3. Add on-demand object existence check method to `OracleCatalogState`:

```cpp
bool OracleCatalogState::ObjectExists(const string &schema, const string &object_name,
                                       const string &object_type) {
    lock_guard<mutex> guard(lock);
    EnsureConnection();

    auto query = StringUtil::Format(
        "SELECT 1 FROM all_objects "
        "WHERE owner = UPPER(%s) AND object_name = UPPER(%s) "
        "AND object_type IN (%s)",
        Value(schema).ToSQLString().c_str(),
        Value(object_name).ToSQLString().c_str(),
        object_type.c_str()
    );

    auto result = connection->Query(query);
    return !result.rows.empty();
}
```

4. Modify table lookup to use on-demand loading when not enumerated:

```cpp
// In OracleTableGenerator::CreateDefaultEntry()
unique_ptr<CatalogEntry> CreateDefaultEntry(ClientContext &context, const string &entry_name) override {
    // Try direct table lookup in enumerated list first
    auto table = OracleTableEntry::Create(catalog, schema, schema.name, entry_name, state);
    if (table) {
        return table;
    }

    // Try on-demand loading if not in enumerated list (handles tables beyond limit)
    if (state->ObjectExists(schema.name, entry_name, "'TABLE', 'VIEW', 'MATERIALIZED VIEW'")) {
        return OracleTableEntry::Create(catalog, schema, schema.name, entry_name, state);
    }

    // Try synonym resolution as fallback
    auto resolved = state->ResolveSynonym(schema.name, entry_name);
    if (resolved) {
        auto [target_schema, target_table] = *resolved;
        return OracleTableEntry::Create(catalog, schema, target_schema, target_table, state);
    }

    return nullptr;
}
```

5. Log warning when limit reached (but continue working):

```cpp
if (settings.metadata_result_limit > 0 && objects.size() >= settings.metadata_result_limit) {
    fprintf(stderr, "[oracle] Warning: Metadata enumeration limit reached (%llu objects). "
            "Tables beyond this limit are still accessible via on-demand loading, "
            "but may not appear in autocomplete. Increase oracle_metadata_result_limit "
            "or filter with oracle_metadata_object_types for better discovery.\n",
            settings.metadata_result_limit);
}
```

**Acceptance Criteria:**

- [ ] Setting registered and configurable
- [ ] Limit applied via ROWNUM in SQL for enumeration
- [ ] Warning logged when limit reached with accurate message
- [ ] On-demand loading works for tables beyond enumeration limit
- [ ] No silent failures - all tables accessible regardless of limit
- [ ] Default limit prevents memory exhaustion during attach
- [ ] On-demand queries are fast (single table lookup)

### Feature 3: Schema Resolution (Priority 3)

#### Task 3.10: Implement Schema Resolution Priority

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_schema_entry.cpp`
- `/home/cody/code/other/duckdb-oracle/src/include/oracle_settings.hpp`

**Implementation:**

1. Add setting:

```cpp
struct OracleSettings {
    bool use_current_schema = true;  // NEW
};
```

2. Modify table lookup to check current schema first:

```cpp
unique_ptr<CatalogEntry> OracleTableGenerator::CreateDefaultEntry(
    ClientContext &context, const string &entry_name) override {

    if (state->settings.use_current_schema &&
        schema.name != state->current_schema) {
        // Try current schema first
        auto current_table = TryCreateTableEntry(
            state->current_schema, entry_name);
        if (current_table) {
            return current_table;
        }
    }

    // Try schema from query
    auto table = TryCreateTableEntry(schema.name, entry_name);
    if (table) {
        return table;
    }

    // Try synonym resolution
    // ... (existing synonym logic)
}
```

**Acceptance Criteria:**

- [ ] Unqualified tables resolve to current schema when setting enabled
- [ ] Falls back to other schemas if not found in current
- [ ] Setting can be disabled for backward compatibility

#### Task 3.11: Add Schema Resolution Caching

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/include/oracle_catalog_state.hpp`

**Implementation:**

1. Add resolution cache:

```cpp
class OracleCatalogState {
private:
    unordered_map<string, pair<string, string>> resolution_cache;  // table_name -> (schema, table)
};
```

2. Cache lookups:

```cpp
optional<pair<string, string>> OracleCatalogState::ResolveTable(const string &table_name) {
    lock_guard<mutex> guard(lock);

    auto cached = resolution_cache.find(table_name);
    if (cached != resolution_cache.end()) {
        return cached->second;
    }

    // Perform resolution logic
    // ...

    // Cache result
    resolution_cache.emplace(table_name, result);
    return result;
}
```

**Acceptance Criteria:**

- [ ] Resolution results cached
- [ ] Cache cleared on `oracle_clear_cache()`
- [ ] No measurable query overhead from resolution

#### Task 3.12: Write Unit Tests for Schema Resolution

**Files to Create:**

- `/home/cody/code/other/duckdb-oracle/test/sql/test_schema_resolution.test`

**Test Cases:**

```sql
# Test current schema resolution
query I
SET oracle_use_current_schema = true;
SELECT * FROM ora.EMPLOYEES LIMIT 1;  # Assumes connected as HR

# Test explicit schema qualification
query I
SELECT * FROM ora.HR.EMPLOYEES LIMIT 1;

# Test fallback when not in current schema
query I
SELECT * FROM ora.SYS.DUAL;  # Not in HR schema

# Test setting disabled
statement ok
SET oracle_use_current_schema = false;

statement error
SELECT * FROM ora.EMPLOYEES;  # Should fail without qualification
```

**Acceptance Criteria:**

- [ ] All test cases pass
- [ ] Tests verify current schema priority
- [ ] Tests verify explicit qualification still works
- [ ] Tests verify setting toggle behavior

---

## Phase 4: Integration [NOT STARTED]

**Owner:** Expert Agent
**Dependencies:** Phase 3 complete

### Task 4.1: Register All New Settings

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp`

**Settings to Register:**

```cpp
// In OracleExtension::Load()
config.AddExtensionOption("oracle_lazy_schema_loading", "...", LogicalType::BOOLEAN, Value::BOOLEAN(true));
config.AddExtensionOption("oracle_metadata_object_types", "...", LogicalType::VARCHAR, Value("TABLE,VIEW,SYNONYM,MVIEW"));
config.AddExtensionOption("oracle_metadata_result_limit", "...", LogicalType::UBIGINT, Value::UBIGINT(10000));
config.AddExtensionOption("oracle_use_current_schema", "...", LogicalType::BOOLEAN, Value::BOOLEAN(true));
```

**Acceptance Criteria:**

- [ ] All 4 settings registered
- [ ] Settings accessible via `SET` command
- [ ] Settings applied in `GetOracleSettings()`

### Task 4.2: Update GetOracleSettings to Apply New Settings

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/oracle_extension.cpp` (GetOracleSettings function)

**Implementation:**

```cpp
static OracleSettings GetOracleSettings(ClientContext &context, OracleCatalogState *state) {
    OracleSettings settings;
    if (state) {
        settings = state->settings;
    }

    Value option_value;

    // ... existing settings

    if (context.TryGetCurrentSetting("oracle_lazy_schema_loading", option_value)) {
        settings.lazy_schema_loading = option_value.GetValue<bool>();
    }
    if (context.TryGetCurrentSetting("oracle_metadata_object_types", option_value)) {
        settings.metadata_object_types = option_value.ToString();
    }
    if (context.TryGetCurrentSetting("oracle_metadata_result_limit", option_value)) {
        settings.metadata_result_limit = static_cast<idx_t>(option_value.GetValue<int64_t>());
    }
    if (context.TryGetCurrentSetting("oracle_use_current_schema", option_value)) {
        settings.use_current_schema = option_value.GetValue<bool>();
    }

    return settings;
}
```

**Acceptance Criteria:**

- [ ] All new settings read from context
- [ ] Settings applied to OracleCatalogState
- [ ] Settings accessible in catalog operations

### Task 4.3: Integrate DetectCurrentSchema into Catalog Initialization

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/src/storage/oracle_catalog.cpp`

**Implementation:**

```cpp
void OracleCatalog::Initialize(bool load_builtin) {
    // Detect current schema before connection
    state->DetectCurrentSchema();

    // Attempt connection early to fail fast
    state->EnsureConnection();

    DuckCatalog::Initialize(false);
    GetSchemaCatalogSet().SetDefaultGenerator(make_uniq<OracleSchemaGenerator>(*this, state));
}
```

**Acceptance Criteria:**

- [ ] Current schema detected on every attach
- [ ] Schema available before metadata enumeration
- [ ] Connection errors still fail fast

### Task 4.4: End-to-End Integration Testing

**Files to Create:**

- `/home/cody/code/other/duckdb-oracle/test/sql/test_integration_advanced.test`

**Test Scenarios:**

1. Test all three features together:
   - Attach with lazy loading
   - Execute stored procedure with `oracle_execute`
   - Query table without schema qualification

2. Test setting combinations:
   - Lazy loading enabled + current schema resolution
   - Lazy loading disabled + explicit qualification
   - Metadata object types = "TABLE,VIEW"

3. Test error cases:
   - Invalid SQL in `oracle_execute`
   - Ambiguous table reference
   - Synonym pointing to non-existent table

**Acceptance Criteria:**

- [ ] All integration scenarios pass
- [ ] Features work together correctly
- [ ] Error messages are clear and actionable

---

## Phase 5: Testing [NOT STARTED]

**Owner:** Testing Agent (Auto-invoked by Expert)
**Dependencies:** Phase 4 complete

### Task 5.1: Create SQL Tests for oracle_execute

**Files to Create:**

- `/home/cody/code/other/duckdb-oracle/test/sql/test_oracle_execute.test`

**Test Coverage:**

- DDL statements (CREATE, ALTER, DROP)
- DML statements with row counts
- PL/SQL blocks (anonymous blocks, procedures)
- Error handling (syntax errors, permission errors)
- Secret Manager integration

**Acceptance Criteria:**

- [ ] All test cases pass
- [ ] Code coverage > 80% for oracle_execute function

### Task 5.2: Create Large Schema Performance Tests

**Files to Create:**

- `/home/cody/code/other/duckdb-oracle/test/integration/large_schema_setup.sql`
- `/home/cody/code/other/duckdb-oracle/test/sql/test_lazy_metadata.test`
- `/home/cody/code/other/duckdb-oracle/test/sql/test_on_demand_loading.test`

**Test Scenarios:**

1. Generate 1000 table schema with SQL script
2. Attach with lazy loading enabled - measure time
3. Attach with lazy loading disabled - measure time
4. Verify metadata query count (should be < 10 with lazy loading)
5. **Test on-demand loading:** Create schema with 15,000 tables, set limit to 10,000, verify tables 10,001-15,000 are still queryable

**Test Cases for On-Demand Loading:**

```sql
# test_on_demand_loading.test

# Create test schema with tables beyond enumeration limit
statement ok
SET oracle_metadata_result_limit = 100;

# Attach - should enumerate only first 100 tables
statement ok
ATTACH 'user/pass@db' AS ora (TYPE oracle);

# Query a table that is in the enumerated list (within first 100 alphabetically)
query I
SELECT COUNT(*) FROM ora.HR.TABLE_AAA;

# Query a table that is NOT in the enumerated list (beyond 100th table alphabetically)
# This should work via on-demand loading
query I
SELECT COUNT(*) FROM ora.HR.TABLE_ZZZ_999;

# Verify warning was logged about enumeration limit
# (check stderr output contains "Metadata enumeration limit reached")

# Verify on-demand loading doesn't cache unnecessarily
# (repeat query should still use on-demand, not add to enumerated list)
```

**Acceptance Criteria:**

- [ ] Lazy loading completes in <5 seconds for 1000 tables
- [ ] Non-lazy loading completes in <60 seconds for 1000 tables
- [ ] Memory usage < 500MB regardless of mode
- [ ] On-demand loading works for tables beyond enumeration limit
- [ ] No "table not found" errors for existing tables beyond limit
- [ ] Warning message accurately describes on-demand loading availability

### Task 5.3: Create Synonym Resolution Tests

**Files to Create:**

- `/home/cody/code/other/duckdb-oracle/test/sql/test_synonyms.test`

**Test Coverage:**

- Private synonyms (user-owned)
- Public synonyms (PUBLIC-owned)
- Synonym priority (private > public)
- Broken synonyms (target doesn't exist)
- Circular synonym references

**Acceptance Criteria:**

- [ ] All synonym test cases pass
- [ ] Error messages indicate synonym target

### Task 5.4: Create Schema Resolution Tests

**Files to Create:**

- `/home/cody/code/other/duckdb-oracle/test/sql/test_schema_resolution.test`

**Test Coverage:**

- Current schema detection
- Unqualified table resolution
- Explicit schema qualification
- Setting toggle (use_current_schema on/off)
- Ambiguous table references

**Acceptance Criteria:**

- [ ] All resolution test cases pass
- [ ] No measurable performance overhead

### Task 5.5: Create Multi-Object-Type Tests

**Files to Create:**

- `/home/cody/code/other/duckdb-oracle/test/sql/test_metadata_object_types.test`

**Test Coverage:**

- Query tables with metadata_object_types="TABLE"
- Query views with metadata_object_types="VIEW"
- Query both with metadata_object_types="TABLE,VIEW"
- Materialized views with metadata_object_types="MVIEW"

**Acceptance Criteria:**

- [ ] All object types enumerated correctly
- [ ] Setting filters objects as expected

### Task 5.6: Performance Benchmarking

**Files to Create:**

- `/home/cody/code/other/duckdb-oracle/test/benchmark/benchmark_metadata.sql`

**Benchmarks:**

- Attach time vs schema size (100, 1K, 10K, 100K tables)
- Query time with schema resolution vs without
- Memory usage during attach
- Metadata query count

**Acceptance Criteria:**

- [ ] Benchmark results documented
- [ ] All performance targets met
- [ ] Bottlenecks identified and addressed

---

## Phase 6: Documentation [NOT STARTED]

**Owner:** Docs & Vision Agent (Auto-invoked after Testing)
**Dependencies:** Phase 5 complete

### Task 6.1: Update Project Documentation

**Files to Modify:**

- `/home/cody/code/other/duckdb-oracle/CLAUDE.md`
- `/home/cody/code/other/duckdb-oracle/README.md`

**Documentation Updates:**

- Add oracle_execute function to API reference
- Document new settings (lazy_schema_loading, metadata_object_types, etc.)
- Add security considerations section
- Update code examples

**Acceptance Criteria:**

- [ ] All new features documented
- [ ] Security warnings prominent
- [ ] Code examples tested and accurate

### Task 6.2: Create Metadata Scalability Guide

**Files to Create:**

- `/home/cody/code/other/duckdb-oracle/specs/guides/metadata-scalability.md`

**Guide Contents:**

- Problem: Large schema performance
- Solution: Lazy loading architecture
- Configuration: Settings and best practices
- Troubleshooting: Common issues and solutions

**Acceptance Criteria:**

- [ ] Guide covers all scalability features
- [ ] Includes performance tuning recommendations
- [ ] Clear examples for different schema sizes

### Task 6.3: Create Security Best Practices Guide

**Files to Create:**

- `/home/cody/code/other/duckdb-oracle/specs/guides/oracle-execute-security.md`

**Guide Contents:**

- SQL injection risks
- Safe usage patterns
- SecretManager integration
- Input validation recommendations
- Future: Prepared statement API

**Acceptance Criteria:**

- [ ] Security risks clearly explained
- [ ] Mitigation strategies documented
- [ ] Code examples demonstrate safe usage

---

## Phase 7: Quality Gate & Archive [NOT STARTED]

**Owner:** Docs & Vision Agent (Auto-invoked after Documentation)
**Dependencies:** Phase 6 complete

### Task 7.1: Validate All Acceptance Criteria

**Validation Checklist:**

Feature 1 (oracle_execute):

- [ ] Function callable from SQL
- [ ] DDL execution works
- [ ] PL/SQL blocks execute
- [ ] Error messages clear
- [ ] Security warnings in docs

Feature 2 (Metadata Scalability):

- [ ] Attach to 100K table schema < 5 seconds
- [ ] Lazy loading setting works
- [ ] Current schema detected
- [ ] Views/synonyms enumerated
- [ ] Result limiting prevents memory exhaustion

Feature 3 (Schema Resolution):

- [ ] Current schema auto-detected
- [ ] Unqualified names resolve correctly
- [ ] Setting toggles behavior
- [ ] No performance overhead
- [ ] Backward compatible

**Acceptance Criteria:**

- [ ] All feature acceptance criteria met
- [ ] All tests passing
- [ ] Documentation complete

### Task 7.2: Performance Validation

**Performance Targets:**

- [ ] Attach time (100K tables): < 5 seconds
- [ ] Metadata query count (with lazy loading): < 10
- [ ] Memory usage (attach): < 500MB
- [ ] Query overhead (schema resolution): < 1ms

**Validation:**

- Run benchmark suite
- Compare against baseline
- Document results in specs/archive/

### Task 7.3: Archive Workspace

**Archival Steps:**

1. Create archive directory:

```bash
mkdir -p /home/cody/code/other/duckdb-oracle/specs/archive/advanced-oracle-features/
```

2. Move workspace files:

```bash
mv /home/cody/code/other/duckdb-oracle/specs/active/advanced-oracle-features/* \
   /home/cody/code/other/duckdb-oracle/specs/archive/advanced-oracle-features/
```

3. Update recovery.md with final status:

```markdown
**Status**: COMPLETED
**Completion Date**: YYYY-MM-DD
**Performance Results**: [Link to benchmark results]
**Lessons Learned**: [Key insights]
```

4. Update CLAUDE.md if workflow improvements identified

**Acceptance Criteria:**

- [ ] Workspace archived
- [ ] Recovery guide updated
- [ ] Knowledge captured in guides
- [ ] CLAUDE.md updated if needed

---

## Summary

**Total Tasks**: 26

- Phase 1: 7 tasks (COMPLETED)
- Phase 2: 4 tasks (Expert Research)
- Phase 3: 12 tasks (Core Implementation)
- Phase 4: 4 tasks (Integration)
- Phase 5: 6 tasks (Testing - Auto)
- Phase 6: 3 tasks (Documentation - Auto)
- Phase 7: 3 tasks (Quality Gate - Auto)

**Current Status**: Ready for Phase 2 (Expert Research)

**Next Action**: Expert agent to begin research tasks 2.1-2.4
