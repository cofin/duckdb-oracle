# Claude Agent System: DuckDB Oracle Extension

**Version**: 2.0
**Last Updated**: Friday, November 22, 2025

This document is the **single source of truth** for the Claude Code multi-agent workflow in this project.

## Philosophy

This system is built on **"Continuous Knowledge Capture"** - ensuring documentation evolves with code.

## Agent Architecture

Claude uses a **multi-agent system** where specialized agents handle specific phases:

| Agent | File | Mission |
|-------|------|---------|
| **PRD** | `.claude/agents/prd.md` | Requirements analysis, PRD creation, task breakdown |
| **Expert** | `.claude/agents/expert.md` | Implementation with deep C++17, DuckDB, and OCI knowledge |
| **Testing** | `.claude/agents/testing.md` | Comprehensive test creation (SQL tests, integration tests) |
| **Docs & Vision** | `.claude/agents/docs-vision.md` | Documentation, quality gate, knowledge capture |

## Workflow

### Sequential Development Phases

1. **Phase 1: PRD** - Agent creates workspace in `specs/active/{{slug}}/`
2. **Phase 2: Expert Research** - Research patterns, libraries, best practices
3. **Phase 3: Implementation** - Expert writes production code
4. **Phase 4: Testing** - Testing agent creates comprehensive tests (auto-invoked by Expert)
5. **Phase 5: Documentation** - Docs & Vision updates guides (auto-invoked after Testing)
6. **Phase 6: Quality Gate** - Full validation and knowledge capture
7. **Phase 7: Archive** - Workspace moved to `specs/archive/`

## Workspace Structure

```sh
specs/active/{{slug}}/
├── prd.md          # Product Requirements Document
├── tasks.md        # Implementation checklist
├── recovery.md     # Session resume instructions
├── research/       # Research findings
└── tmp/            # Temporary files
```

## Project Context

**Language**: C++17
**Framework**: DuckDB Extension Framework + Oracle Call Interface (OCI)
**Architecture**: Storage extension with catalog integration
**Test Framework**: DuckDB SQL test runner + Integration tests (containerized Oracle)
**Build Tool**: Make (CMake wrapper)
**Documentation**: Markdown + Doxygen comments

## Development Commands

### Build

```bash
make release              # Build extension in release mode
make debug                # Build extension in debug mode
make clean                # Clean build artifacts
make clean-all            # Remove all build dirs (use when switching generators)
```

### Test

```bash
make test                 # Run SQL test suite
make integration          # Build + run tests against containerized Oracle
ORACLE_IMAGE=gvenzl/oracle-free:23-slim make integration
```

### Quality

```bash
make tidy-check          # Run clang-tidy checks
make configure_ci        # Install Oracle Instant Client for CI/local
```

### Running Single Tests

```bash
# Run specific test file
build/release/test/unittest test/sql/test_oracle_attach.test

# Run with debug output
build/release/duckdb -unsigned < test/sql/test_oracle_attach.test
```

### Build Outputs

- `build/release/duckdb` - DuckDB shell with oracle extension preloaded
- `build/release/test/unittest` - Test runner
- `build/release/extension/oracle/oracle.duckdb_extension` - Loadable extension binary

## Code Quality Standards

### C++ Standards

- **Version**: C++17
- **Style**: Follow DuckDB conventions (symlinked `.clang-format`, `.clang-tidy`)
- **Headers**: Use `#pragma once` for header guards
- **Namespaces**: All code in `namespace duckdb {}`
- **Memory**: Use DuckDB's smart pointers (`unique_ptr<T>`, `shared_ptr<T>`, `make_uniq<T>()`)
- **Strings**: Use DuckDB's `string` type, not `std::string` directly
- **Errors**: Throw `IOException`, `BinderException`, or other DuckDB exceptions

### Documentation

- **Style**: Doxygen-compatible C++ comments
- **Public APIs**: Document all public methods with `//! Brief description`
- **Inline comments**: Use `//` for implementation notes
- **Header files**: Include purpose and usage examples

Example:
```cpp
//! Connects to Oracle database using OCI
//! \param connection_string Oracle connection string (user/pass@host:port/service)
//! \return void, throws IOException on failure
void Connect(const string &connection_string);
```

### Testing

- **Framework**: DuckDB SQL test runner (`.test` files)
- **Integration Tests**: Docker/Podman Oracle container (`scripts/test_integration.sh`)
- **Coverage target**: Test all modified functionality
- **Test types required**:
  1. Smoke tests (connection errors, require oracle)
  2. Integration tests (real Oracle container)
  3. Edge cases (NULL, empty, errors)
  4. Type mapping tests (Oracle types → DuckDB types)
  5. Pushdown tests (filter pushdown verification)

### Code Patterns

- **RAII**: Always use RAII for OCI handles (see `OracleConnection`)
- **Error handling**: Use `CheckOCIError()` for all OCI calls
- **Mutex protection**: Protect shared state (see `OracleCatalogState::lock`)
- **Lazy loading**: Defer expensive operations (see `OracleTableEntry::GetColumns()`)
- **Secret Management**: Use DuckDB SecretManager for credential storage (see `oracle_secret.cpp`)

### Pattern: Oracle Spatial Type Conversion

When converting Oracle proprietary types to portable formats:

**Problem**: Oracle SDO_GEOMETRY is an object type that cannot be directly fetched via OCI as a standard type.

**Solution**: Use Oracle's SQL conversion functions and query rewriting.

```cpp
// Step 1: Detect spatial types in column introspection
static LogicalType MapOracleColumn(const string &data_type, idx_t precision, idx_t scale, idx_t char_len) {
    auto upper = StringUtil::Upper(data_type);

    if (upper == "SDO_GEOMETRY" || upper == "MDSYS.SDO_GEOMETRY") {
        return LogicalType::VARCHAR; // WKT strings
    }
    // ... other mappings
}

// Step 2: Store metadata about original Oracle types
struct OracleColumnMetadata {
    string column_name;
    string oracle_data_type;
    bool is_spatial;

    OracleColumnMetadata(const string &name, const string &data_type)
        : column_name(name), oracle_data_type(data_type),
          is_spatial(data_type == "SDO_GEOMETRY" || data_type == "MDSYS.SDO_GEOMETRY") {
    }
};

// Step 3: Rewrite queries to use Oracle conversion functions
TableFunction OracleTableEntry::GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) {
    string column_list;
    for (size_t i = 0; i < columns.Physical().size(); i++) {
        auto &col = columns.Physical()[i];
        auto quoted_col = KeywordHelper::WriteQuoted(col.Name(), '"');

        if (column_metadata[i].is_spatial) {
            // Convert SDO_GEOMETRY to WKT using Oracle built-in function
            column_list += StringUtil::Format("SDO_UTIL.TO_WKTGEOMETRY(%s) AS %s",
                                              quoted_col.c_str(), quoted_col.c_str());
        } else {
            column_list += quoted_col;
        }
    }

    auto query = StringUtil::Format("SELECT %s FROM %s.%s", column_list.c_str(), ...);
    // ... continue with bind data setup
}

// Step 4: Fetch converted data as standard types (VARCHAR for WKT)
// No special OCI handling needed - fetched as regular VARCHAR
```

**Key Points:**
- Leverage Oracle's built-in conversion functions (SDO_UTIL.TO_WKTGEOMETRY, TO_WKBGEOMETRY)
- Store original type metadata for query rewriting decisions
- Generate explicit SELECT column lists (replace `SELECT *`)
- Preserve column names and ordering with AS clauses
- Use VARCHAR for WKT strings (user can cast to GEOMETRY with ST_GeomFromText)

**Trade-offs:**
- **Pros**: Simple implementation, portable, human-readable output
- **Cons**: Requires Oracle JVM, string overhead for large geometries
- **Future**: Add WKB (binary) mode for 30% performance improvement

**Related Settings:**
```cpp
// oracle_settings.hpp
bool enable_spatial_conversion = true;  // Toggle spatial conversion
```

**Testing Strategy:**
```sql
-- Smoke test without Oracle connection
require oracle
query I
SELECT current_setting('oracle_enable_spatial_conversion');
----
true

-- Integration test with Oracle container
ATTACH 'user/pass@host:1521/service' AS ora (TYPE oracle);
SELECT geometry FROM ora.gis.parcels;  -- Returns WKT strings
```

### Pattern: Column Metadata Tracking

When you need to preserve additional information about columns beyond DuckDB's ColumnDefinition:

**Problem**: DuckDB's `ColumnDefinition` only stores name and `LogicalType`. Oracle-specific metadata (original type name, is_spatial flag) needs separate storage.

**Solution**: Create parallel metadata structure.

```cpp
// Define metadata struct
struct OracleColumnMetadata {
    string column_name;
    string oracle_data_type;  // Original Oracle type: "NUMBER", "SDO_GEOMETRY", etc.
    bool is_spatial;          // Computed flag for quick checks

    OracleColumnMetadata(const string &name, const string &data_type)
        : column_name(name), oracle_data_type(data_type),
          is_spatial(data_type == "SDO_GEOMETRY" || data_type == "MDSYS.SDO_GEOMETRY") {
    }
};

// Store in table entry
class OracleTableEntry : public TableCatalogEntry {
private:
    vector<OracleColumnMetadata> column_metadata;  // Parallel to columns
    // ...
};

// Populate during column loading
static void LoadColumns(OracleCatalogState &state, const string &schema, const string &table,
                        vector<ColumnDefinition> &columns, vector<OracleColumnMetadata> &metadata) {
    // Query all_tab_columns
    for (auto &row : result.rows) {
        auto col_name = row[0];
        auto data_type = row[1];  // Original Oracle type

        // Create DuckDB column
        auto logical = MapOracleColumn(data_type, precision, scale, data_len);
        columns.push_back(ColumnDefinition(col_name, logical));

        // Store original metadata
        metadata.emplace_back(col_name, data_type);
    }
}

// Access in query generation
if (col_idx < column_metadata.size()) {
    bool is_spatial = column_metadata[col_idx].is_spatial;
    if (is_spatial) {
        // Special handling for spatial columns
    }
}
```

**Key Points:**
- Keep metadata vector in sync with columns vector (same size, same order)
- Populate both in same function (`LoadColumns`)
- Pass metadata to table entry constructor
- Access via index: `column_metadata[col_idx]`

**Memory Efficiency:**
- Overhead: ~40 bytes per column (string + bool + padding)
- Trade-off: Simplifies query generation logic

### Pattern: Lazy Schema Loading with On-Demand Fallback

When implementing catalog systems that must handle large schemas (100K+ tables):

**Problem**: Enumerating all schemas and tables upfront causes slow attach times and high memory usage for large Oracle databases.

**Solution**: Lazy schema loading with on-demand table lookup fallback.

```cpp
// Step 1: Detect current schema on attach
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

// Step 2: Lazy schema enumeration
vector<string> OracleCatalogState::ListSchemas() {
    lock_guard<mutex> guard(lock);

    if (settings.lazy_schema_loading) {
        // Return only current schema (fast)
        if (!current_schema.empty()) {
            return {current_schema};
        }
    }

    // Full enumeration (slow for large DBs)
    // ... existing code to query all_users
}

// Step 3: Object enumeration with limit
vector<string> OracleCatalogState::ListObjects(const string &schema, const string &object_types) {
    lock_guard<mutex> guard(lock);

    string query = StringUtil::Format(
        "SELECT object_name FROM all_objects "
        "WHERE owner = UPPER(%s) AND object_type IN (%s) "
        "ORDER BY object_name",
        Value(schema).ToSQLString().c_str(),
        object_types.c_str()
    );

    // Apply result limit to prevent memory exhaustion
    if (settings.metadata_result_limit > 0) {
        query = StringUtil::Format(
            "SELECT * FROM (%s) WHERE ROWNUM <= %lu",
            query.c_str(), (unsigned long)settings.metadata_result_limit
        );
    }

    auto result = connection->Query(query);
    // ... cache and return objects
}

// Step 4: On-demand loading for tables beyond enumeration limit
bool OracleCatalogState::ObjectExists(const string &schema, const string &object_name,
                                      const string &object_types) {
    lock_guard<mutex> guard(lock);
    EnsureConnection();

    auto query = StringUtil::Format(
        "SELECT 1 FROM all_objects "
        "WHERE owner = UPPER(%s) AND object_name = UPPER(%s) AND object_type IN (%s)",
        Value(schema).ToSQLString().c_str(),
        Value(object_name).ToSQLString().c_str(),
        object_types.c_str()
    );

    auto result = connection->Query(query);
    return !result.rows.empty();
}

// Step 5: Table generator with fallback chain
unique_ptr<CatalogEntry> OracleTableGenerator::CreateDefaultEntry(
    ClientContext &context, const string &entry_name) override {

    // Try direct lookup in enumerated list
    auto table = TryCreateTableEntry(schema.name, entry_name);
    if (table) {
        return table;
    }

    // Try on-demand loading (handles tables beyond enumeration limit)
    if (state->ObjectExists(schema.name, entry_name, "'TABLE', 'VIEW', 'MATERIALIZED VIEW'")) {
        return TryCreateTableEntry(schema.name, entry_name);
    }

    // Try synonym resolution
    string target_schema, target_table;
    bool found = false;
    state->ResolveSynonym(schema.name, entry_name, target_schema, target_table, found);
    if (found) {
        return TryCreateTableEntry(target_schema, target_table);
    }

    return nullptr;
}
```

**Key Points:**
- Enumerate only current schema by default (configurable via `oracle_lazy_schema_loading`)
- Limit object enumeration to prevent memory exhaustion (default: 10,000 objects)
- Fall back to on-demand `ObjectExists()` query when table not in enumerated list
- Log warning when limit reached, but continue working (no silent failures)
- Users can still query all tables, just won't see them in autocomplete

**Performance:**
- Attach time: <5 seconds for any schema size (with lazy loading)
- On-demand lookup: <10ms per table (single query)
- Memory: O(enumeration limit) instead of O(total tables)

**Settings:**
```cpp
bool lazy_schema_loading = true;           // Enumerate only current schema
idx_t metadata_result_limit = 10000;       // Max objects enumerated
string metadata_object_types = "TABLE,VIEW,SYNONYM,MATERIALIZED VIEW";
```

### Pattern: Oracle Execute Function (Non-Returning SQL)

When implementing a function to execute Oracle SQL without expecting a result set:

**Problem**: `oracle_query()` requires SELECT statements that return data. No way to execute DDL, DML, or PL/SQL blocks.

**Solution**: Create separate `oracle_execute()` scalar function with OCI execution.

```cpp
static void OracleExecuteFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &conn_vec = args.data[0];
    auto &sql_vec = args.data[1];

    UnaryExecutor::Execute<string_t, string_t>(
        conn_vec, result, args.size(),
        [&](string_t conn_str) {
            auto connection_string = conn_str.GetString();
            auto sql = sql_vec.GetValue(0).ToString();

            // Create OCI environment and connection
            OCIEnv *env = nullptr;
            OCIError *err = nullptr;
            OCISvcCtx *svc = nullptr;
            OCIStmt *stmt = nullptr;

            // Initialize OCI (error handling omitted for brevity)
            OCIEnvCreate(&env, OCI_DEFAULT, nullptr, nullptr, nullptr, nullptr, 0, nullptr);
            OCIHandleAlloc(env, (void**)&err, OCI_HTYPE_ERROR, 0, nullptr);

            // Connect to Oracle
            // ... parse connection string, OCILogon2

            // Execute statement
            OCIHandleAlloc(env, (void**)&stmt, OCI_HTYPE_STMT, 0, nullptr);
            OCIStmtPrepare(stmt, err, (text*)sql.c_str(), sql.size(),
                          OCI_NTV_SYNTAX, OCI_DEFAULT);
            OCIStmtExecute(svc, stmt, err, 1, 0, nullptr, nullptr, OCI_COMMIT_ON_SUCCESS);

            // Extract row count for DML statements
            ub4 row_count = 0;
            OCIAttrGet(stmt, OCI_HTYPE_STMT, &row_count, 0, OCI_ATTR_ROW_COUNT, err);

            // Format result message
            string result_msg = StringUtil::Format(
                "Statement executed successfully (%lu rows affected)",
                (unsigned long)row_count
            );

            // Cleanup OCI handles (RAII pattern recommended)
            OCIHandleFree(stmt, OCI_HTYPE_STMT);
            OCILogoff(svc, err);
            OCIHandleFree(err, OCI_HTYPE_ERROR);
            OCIHandleFree(env, OCI_HTYPE_ENV);

            return StringVector::AddString(result, result_msg);
        }
    );
}

// Register as scalar function
auto oracle_execute = ScalarFunction(
    "oracle_execute",
    {LogicalType::VARCHAR, LogicalType::VARCHAR},
    LogicalType::VARCHAR,
    OracleExecuteFunction
);
ExtensionUtil::RegisterFunction(instance, oracle_execute);
```

**Key Points:**
- Use `OCI_COMMIT_ON_SUCCESS` flag for auto-commit
- Extract row count from `OCI_ATTR_ROW_COUNT` for DML statements
- Return formatted status message (not result set)
- Proper RAII cleanup of all OCI handles
- Throw `IOException` with Oracle error messages on failure

**Security Warning:**
Document prominently that this function does not use prepared statements. Users must validate input to prevent SQL injection.

**Usage Examples:**
```sql
-- DDL
SELECT oracle_execute('user/pass@db', 'CREATE INDEX idx_emp ON employees(dept_id)');

-- PL/SQL
SELECT oracle_execute('user/pass@db', 'BEGIN hr_pkg.update_salaries(1.05); END;');

-- DML
SELECT oracle_execute('user/pass@db', 'DELETE FROM temp WHERE created < SYSDATE - 7');
-- Returns: "Statement executed successfully (N rows affected)"
```

### Pattern: Current Schema Context Resolution

When implementing schema resolution that matches Oracle's native behavior:

**Problem**: DuckDB requires fully qualified names (`ora.HR.EMPLOYEES`), but Oracle users expect unqualified names (`EMPLOYEES`) to work when connected to a schema.

**Solution**: Auto-detect current schema and resolve unqualified names with priority.

```cpp
// Detect current schema on attach
void OracleCatalog::Initialize(bool load_builtin) {
    state->DetectCurrentSchema();  // Query SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA')
    state->EnsureConnection();
    DuckCatalog::Initialize(false);
    // ...
}

// Resolve with current schema priority
unique_ptr<CatalogEntry> OracleTableGenerator::CreateDefaultEntry(
    ClientContext &context, const string &entry_name) override {

    if (state->settings.use_current_schema) {
        // Try current schema first
        auto table = TryCreateTableEntry(state->GetCurrentSchema(), entry_name);
        if (table) {
            return table;
        }
    }

    // Fall back to schema from query or all schemas
    // ... existing lookup logic
}
```

**Key Points:**
- Query `SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA')` once on attach
- Cache current schema in catalog state
- Check current schema first when resolving unqualified names
- Configurable via `oracle_use_current_schema` setting
- Maintains backward compatibility (can be disabled)

**Benefits:**
- Matches Oracle's native resolution behavior
- Reduces schema qualification verbosity
- Works transparently with lazy schema loading

### Pattern: DuckDB Secret Manager Integration

When adding secret support to a DuckDB extension:

```cpp
// 1. Define secret parameters structure
struct OracleSecretParameters {
    string host = "localhost";
    idx_t port = 1521;
    string service;     // required
    string user;        // required
    string password;    // required
};

// 2. Create secret parsing function
OracleSecretParameters ParseOracleSecret(const CreateSecretInput &input) {
    OracleSecretParameters params;
    // Extract parameters from input.options map
    auto host_lookup = input.options.find("host");
    if (host_lookup != input.options.end()) {
        params.host = host_lookup->second.ToString();
    }
    // ... parse other parameters
    return params;
}

// 3. Validate required parameters
void ValidateOracleSecret(const OracleSecretParameters &params) {
    if (params.user.empty()) {
        throw InvalidInputException("Oracle secret missing required parameter: USER");
    }
    // ... validate other required fields
}

// 4. Create secret function
unique_ptr<BaseSecret> CreateOracleSecretFromConfig(ClientContext &context, CreateSecretInput &input) {
    auto params = ParseOracleSecret(input);
    ValidateOracleSecret(params);

    // Build KeyValueSecret
    auto secret = make_uniq<KeyValueSecret>(scope, type, provider, name);
    secret->redact_keys = {"password"};  // Passwords auto-redacted
    // ... set parameters
    return std::move(secret);
}

// 5. Register secret type and function in extension LoadInternal()
SecretType secret_type;
secret_type.name = "oracle";
secret_type.deserializer = KeyValueSecret::Deserialize<KeyValueSecret>;
secret_type.default_provider = "config";
ExtensionUtil::RegisterSecretType(instance, secret_type);

CreateSecretFunction secret_function = {"oracle", "config", CreateOracleSecretFromConfig};
ExtensionUtil::RegisterFunction(instance, secret_function);

// 6. Retrieve secrets in ATTACH function
static unique_ptr<Catalog> OracleAttach(...) {
    string connection_string;

    if (!info.path.empty()) {
        // Use provided connection string
        connection_string = info.path;
    } else {
        // Retrieve from secret manager
        auto &secret_manager = SecretManager::Get(context);
        auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);

        // Check for named secret
        shared_ptr<KeyValueSecret> secret;
        auto secret_opt = options.options.find("secret");
        if (secret_opt != options.options.end()) {
            auto secret_name = secret_opt->second.ToString();
            secret = dynamic_pointer_cast<KeyValueSecret>(
                secret_manager.GetSecretByName(transaction, secret_name, "oracle"));
        } else {
            // Use default secret
            secret = dynamic_pointer_cast<KeyValueSecret>(
                secret_manager.GetSecretByName(transaction, "__default_oracle", "oracle"));
        }

        if (!secret) {
            throw BinderException("No Oracle secret found. Create with: CREATE SECRET (TYPE oracle, ...)");
        }

        connection_string = BuildConnectionStringFromSecret(*secret);
    }
    // ... continue with existing attach logic
}
```

**Key Points:**
- Use `KeyValueSecret` for structured parameter storage
- Set `redact_keys` to hide sensitive values in output
- Validate all required parameters in secret creation
- Provide clear error messages with usage examples
- Maintain backward compatibility with existing auth methods

## Project Structure

```
duckdb-oracle/
├── src/
│   ├── oracle_extension.cpp           # Entry point, registration
│   ├── oracle_connection.cpp          # OCI connection wrapper
│   ├── oracle_secret.cpp              # Secret Manager integration
│   ├── storage/
│   │   ├── oracle_catalog.cpp         # Catalog implementation
│   │   ├── oracle_schema_entry.cpp    # Schema discovery
│   │   ├── oracle_table_entry.cpp     # Table metadata + column mapping
│   │   ├── oracle_storage_extension.cpp
│   │   ├── oracle_transaction.cpp
│   │   └── oracle_transaction_manager.cpp
│   └── include/
│       ├── oracle_catalog_state.hpp   # Shared state per attached DB
│       ├── oracle_settings.hpp        # Extension settings
│       ├── oracle_table_function.hpp  # Query execution (OracleBindData)
│       ├── oracle_secret.hpp          # Secret parameters and functions
│       └── *.hpp
├── test/
│   ├── sql/*.test                     # DuckDB SQL tests
│   └── integration/init_sql/*.sql     # Oracle container setup
├── scripts/
│   ├── setup_oci_linux.sh            # Install Oracle Instant Client (Linux)
│   ├── setup_oci_macos.sh            # Install Oracle Instant Client (macOS)
│   ├── setup_oci_windows.ps1         # Install Oracle Instant Client (Windows)
│   └── test_integration.sh           # Integration test runner
├── CMakeLists.txt
├── Makefile
└── vcpkg.json
```

## Key Architectural Patterns

### Extension Registration Flow

1. `oracle_extension.cpp` - Entry point, registers storage extension + table functions
2. `OracleStorageExtension` - Attaches to DuckDB's catalog system (`ATTACH ... TYPE oracle`)
3. `CreateOracleCatalog()` - Creates catalog backed by `OracleCatalogState`

### Storage Layer (DuckDB Catalog Integration)

- **OracleCatalogState** (`oracle_catalog_state.hpp`): Shared state per attached database
  - Holds connection string, settings, OracleConnection instance
  - Caches schema/table metadata with mutex protection
  - Global registry pattern for `oracle_clear_cache()` function

- **OracleCatalog** → **OracleSchemaEntry** → **OracleTableEntry**: Hierarchical catalog
  - Schema discovery: `SELECT username FROM all_users`
  - Table discovery: `SELECT table_name FROM all_tables WHERE owner = ?`
  - Column introspection: `SELECT * FROM all_tab_columns WHERE owner = ? AND table_name = ?`

- **OracleTableEntry**: Lazy column metadata loading
  - Maps Oracle types to DuckDB LogicalTypes in `MapOracleColumn()`
  - NUMBER → DECIMAL/DOUBLE, DATE/TIMESTAMP → TIMESTAMP, CHAR/VARCHAR/CLOB → VARCHAR, BLOB/RAW → BLOB
  - SDO_GEOMETRY → VARCHAR (WKT string representation via query rewriting)

### Query Execution (Table Functions)

- **OracleBindData** (`oracle_table_function.hpp`): Holds OCI handles + query state
  - `OracleContext` - OCI environment/service/statement/error handles (RAII cleanup)
  - `base_query` - Original query template
  - `query` - Transformed query after pushdown
  - Settings snapshot (prefetch_rows, array_size, etc.)

- **OracleQueryFunction** (`oracle_extension.cpp`): Fetch loop
  - Uses OCI array fetch with configurable `array_size`
  - Converts OCI types → DuckDB vectors (handles NULL indicators, timestamps, LOBs)
  - Supports pushdown filters via `OraclePushdownComplexFilter()`

### Filter Pushdown

When `oracle_enable_pushdown=true`:

1. DuckDB calls `OraclePushdownComplexFilter()` with LogicalGet + filter expressions
2. Traverses DuckDB expression tree (comparisons, AND/OR, column refs, constants)
3. Generates Oracle SQL WHERE clause
4. Rewrites `base_query` → `query` with added predicates

### Connection Management

- **OracleConnection** (`oracle_connection.hpp`): Wrapper around OCI handles
  - `Connect()` - `OCIEnvCreate()` + `OCILogon()`
  - `Query()` - Execute + fetch all rows as strings (metadata queries)
  - Error handling via `CheckOCIError()` (throws IOException with Oracle error text)

- Connection pooling: `OracleCatalogState` reuses single connection if `connection_cache=true`
- Wallet support: `oracle_attach_wallet('/path')` sets `TNS_ADMIN` env variable

## Oracle Instant Client Setup

### Environment Variable

The build requires `ORACLE_HOME` pointing to Instant Client root:

```sh
export ORACLE_HOME=/path/to/instantclient_23_6  # must contain sdk/include/oci.h and lib/libclntsh.so
```

Falls back to `/usr/share/oracle/instantclient_23_26` if unset.

### CI Script

`scripts/setup_oci_linux.sh` downloads and extracts Instant Client Basic + SDK to `oracle_sdk/`.

### Runtime Dependencies

- Linux: `libaio1` (Ubuntu 24.04 uses `libaio1t64`)
- Libraries: `libclntsh.so` must be in `LD_LIBRARY_PATH` or system lib path

## Extension Settings

Configurable via `SET` or `ATTACH` options:

- `oracle_enable_pushdown` (bool, default false) - Push filters to Oracle
- `oracle_prefetch_rows` (idx_t, default 200) - OCI prefetch hint
- `oracle_prefetch_memory` (idx_t, default 0) - OCI prefetch memory (0=auto)
- `oracle_array_size` (idx_t, default 256) - Rows per OCIStmtFetch iteration
- `oracle_connection_cache` (bool, default true) - Reuse OCI connections
- `oracle_connection_limit` (idx_t, default 8) - Max cached connections
- `oracle_debug_show_queries` (bool, default false) - Log generated Oracle SQL

Settings live in `OracleSettings` struct, applied via `OracleCatalogState::ApplyOptions()`.

## MCP Tools Available

### Context7

- **Purpose**: Up-to-date library documentation
- **Usage**: Research DuckDB APIs, OCI patterns during planning/implementation
- **Commands**:

  ```python
  mcp__context7__resolve-library-id(libraryName="duckdb")
  mcp__context7__get-library-docs(
      context7CompatibleLibraryID="/duckdb/duckdb",
      topic="storage extensions",
      page=1
  )
  ```

### Zen MCP

- **zen.planner**: Multi-step planning with revision capabilities
- **zen.chat**: Collaborative thinking and brainstorming
- **zen.thinkdeep**: Deep architectural analysis
- **zen.analyze**: Code quality and performance analysis
- **zen.debug**: Systematic debugging workflow
- **zen.consensus**: Multi-model consensus for decisions

### WebSearch

- **Purpose**: Modern best practices and industry standards
- **Usage**: Research novel problems and architectural decisions

## Common Development Patterns

### Adding New Oracle Type Mappings

1. Extend `MapOracleColumn()` in `src/storage/oracle_table_entry.cpp`
2. Add OCI type constant (e.g., `SQLT_JSON`) to `oracle_extension.cpp` if needed
3. Update fetch logic in `OracleQueryFunction()` to handle conversion
4. Add SQL test in `test/sql/` for type roundtrip

Example:
```cpp
// In MapOracleColumn()
case SQLT_JSON:
    return LogicalType::VARCHAR; // Fallback to VARCHAR for JSON
```

### Adding New Table Functions

Register in `LoadInternal()` in `oracle_extension.cpp`:

```cpp
ExtensionUtil::RegisterFunction(instance,
    TableFunction("new_function", {args}, new_func, new_bind));
```

### Extending Pushdown Support

Modify `OraclePushdownComplexFilter()` in `oracle_extension.cpp`:

- Add new expression type cases (currently handles COMPARE, CONJUNCTION, COLUMN_REF, CONSTANT)
- Update operator mapping (`ExpressionTypeToString()`)

## Known Limitations

- Read-only (no INSERT/UPDATE/DELETE/COPY support)
- No transaction management (auto-commit reads only)
- Views only visible if present in `ALL_TABLES`
- Some Oracle types fallback to VARCHAR (e.g., XMLTYPE, SDO_GEOMETRY)
- No parallel query execution (single OCI connection per scan)

## Anti-Patterns (Avoid These)

### C++ Anti-Patterns

❌ **Manual memory management** - Use DuckDB smart pointers
❌ **std::string directly** - Use DuckDB's `string` type
❌ **Ignoring OCI errors** - Always use `CheckOCIError()`
❌ **Forgetting mutex locks** - Protect shared state
❌ **Synchronous metadata calls in hot path** - Use lazy loading

### Architecture Anti-Patterns

❌ **Blocking calls in scan functions** - Prefetch and batch
❌ **N+1 metadata queries** - Cache schema/table lists
❌ **Memory leaks in OCI handles** - RAII cleanup
❌ **Hardcoded connection strings** - Use settings system

## Dependencies

### Core

- **DuckDB**: Extension framework (linked via CMake)
- **Oracle Instant Client**: Basic + SDK (system-provided)
- **OpenSSL**: vcpkg-managed dependency

### Development

- CMake ≥3.10
- C++17 compiler (GCC, Clang)
- vcpkg (for OpenSSL)
- Oracle Instant Client 19c/21c/23c
- libaio (Linux)

## Version Control

### Branching

- `main` - Stable branch
- `oracle-extension` - Current development branch
- Feature branches: `feature/{{slug}}`

### Commit Messages

Follow conventional commits:
- `feat:` - New features
- `fix:` - Bug fixes
- `docs:` - Documentation only
- `refactor:` - Code refactoring
- `test:` - Test additions/fixes
- `chore:` - Build/tooling changes

Example:
```
feat: Add BLOB streaming support with chunked reads

- Implement OCILobRead2 for large BLOB handling
- Add streaming buffer management
- Update type mapping for BLOB/CLOB
- Add integration test with 10MB BLOB
```

## Knowledge Capture Protocol

After every significant feature:

1. Update `specs/guides/` with new patterns
2. Ensure all public APIs documented
3. Add working code examples
4. Update this AGENTS.md if workflow improves

---

**To start a new feature:**

1. Use PRD agent to create requirements
2. Expert agent implements and orchestrates testing
3. Docs & Vision agent handles quality gate and archival

**To debug issues:**

Use Expert agent with zen.debug tool for systematic troubleshooting.
