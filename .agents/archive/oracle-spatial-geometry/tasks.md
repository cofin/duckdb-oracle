# Implementation Tasks: Oracle Spatial Geometry Support

**Workspace**: `specs/active/oracle-spatial-geometry/`
**PRD**: [prd.md](prd.md)
**Last Updated**: 2025-11-22

## Phase 1: Research & Planning ✅

- [x] Research Oracle SDO_GEOMETRY structure and conversion APIs
- [x] Research DuckDB Spatial extension GEOMETRY type
- [x] Review BigQuery extension spatial implementation
- [x] Document WKT/WKB conversion approach
- [x] Create PRD and workspace structure

## Phase 2: Type Detection & Schema Introspection

### Task 2.1: Update Type Mapping
**File**: `src/storage/oracle_table_entry.cpp`
**Function**: `MapOracleColumn()`

- [ ] Add SDO_GEOMETRY type detection in `MapOracleColumn()`
- [ ] Handle both `"SDO_GEOMETRY"` and `"MDSYS.SDO_GEOMETRY"` type names
- [ ] Initially map to VARCHAR (WKT string representation)
- [ ] Add unit test for type detection

**Acceptance Criteria**:
```sql
-- Query all_tab_columns on table with SDO_GEOMETRY
-- Extension should recognize column as spatial type
SELECT column_name, data_type FROM duckdb_columns(...);
-- Should return: geometry | VARCHAR (Phase 2) or GEOMETRY (Phase 3)
```

**Code Location**: [oracle_table_entry.cpp:14-42](src/storage/oracle_table_entry.cpp#L14-L42)

### Task 2.2: Store Original Oracle Type Metadata
**File**: `src/include/oracle_table_entry.hpp`

- [ ] Add structure to track original Oracle types per column
- [ ] Modify `LoadColumns()` to store `data_type` string for each column
- [ ] Create helper function `IsSpatialColumn(ColumnDefinition)`
- [ ] Update `OracleTableEntry` constructor to preserve metadata

**Data Structure**:
```cpp
struct OracleColumnMetadata {
    string column_name;
    string oracle_data_type;  // "SDO_GEOMETRY", "NUMBER", etc.
    bool is_spatial;
};

class OracleTableEntry {
    vector<OracleColumnMetadata> column_metadata;
    // ...
};
```

**Acceptance Criteria**:
- Metadata accessible in `GetScanFunction()`
- No regression in existing type mappings
- Memory overhead <100 bytes per column

## Phase 3: Query Rewriting for WKT Conversion

### Task 3.1: Implement Column-Level Query Rewriting
**File**: `src/storage/oracle_table_entry.cpp`
**Function**: `OracleTableEntry::GetScanFunction()`

- [ ] Replace `SELECT *` with explicit column list
- [ ] Wrap SDO_GEOMETRY columns with `SDO_UTIL.TO_WKTGEOMETRY(col)`
- [ ] Preserve column names in SELECT list
- [ ] Handle quoted column names correctly
- [ ] Add setting `oracle_enable_spatial_conversion` (default true)

**Example Generated Query**:
```sql
-- Original table: parcels (id NUMBER, geometry SDO_GEOMETRY, name VARCHAR2)
-- Generated query:
SELECT
    "id",
    SDO_UTIL.TO_WKTGEOMETRY("geometry") AS "geometry",
    "name"
FROM "SPATIAL_SCHEMA"."PARCELS"
```

**Code Location**: [oracle_table_entry.cpp:101-124](src/storage/oracle_table_entry.cpp#L101-L124)

**Acceptance Criteria**:
- Query compiles and executes on Oracle
- WKT strings returned in geometry column
- No SQL injection vulnerabilities
- Handles edge cases: zero columns, all spatial columns, no spatial columns

### Task 3.2: Handle JVM Availability
**File**: `src/oracle_connection.cpp`

- [ ] Add function `CheckSpatialSupport()` to test for JVM availability
- [ ] Execute test query: `SELECT SDO_UTIL.TO_WKTGEOMETRY(NULL) FROM dual`
- [ ] Cache result in `OracleCatalogState`
- [ ] Throw helpful error on Oracle Express (no JVM)

**Error Message**:
```
IOException: Oracle Spatial WKT conversion requires Oracle JVM.
Not supported on Oracle Express Edition.
Consider upgrading to Oracle Standard Edition or using VARCHAR columns.
```

**Acceptance Criteria**:
- Clear error message when JVM unavailable
- Test runs only once per connection
- No performance impact when JVM available

## Phase 4: OCI CLOB Handling

### Task 4.1: Implement CLOB Fetch Logic
**File**: `src/oracle_extension.cpp`
**Function**: `OracleQueryFunction()`

- [ ] Detect VARCHAR columns from spatial conversion (check metadata)
- [ ] Bind CLOB locator with `OCIDefineByPos(..., SQLT_CLOB, ...)`
- [ ] Implement `ReadCLOB()` helper function
- [ ] Handle CLOB length up to 100MB
- [ ] Test with large geometries (>1MB WKT)

**Helper Function**:
```cpp
static string ReadCLOB(OCISvcCtx *svc, OCIError *err, OCILobLocator *lob_loc) {
    oraub8 lob_len;
    CheckOCIError(OCILobGetLength2(svc, err, lob_loc, &lob_len), err, "OCILobGetLength2");

    if (lob_len == 0) {
        return "";
    }

    auto buffer = make_unsafe_uniq_array<char>(lob_len + 1);
    oraub8 bytes_read = lob_len;
    CheckOCIError(OCILobRead2(svc, err, lob_loc, &bytes_read, nullptr, 1,
                              buffer.get(), lob_len, OCI_ONE_PIECE, nullptr, nullptr,
                              0, SQLCS_IMPLICIT),
                  err, "OCILobRead2");

    buffer[lob_len] = '\0';
    return string(buffer.get(), lob_len);
}
```

**Code Location**: ~[oracle_extension.cpp:200-250](src/oracle_extension.cpp#L200-L250) (new code)

**Acceptance Criteria**:
- Fetches WKT strings correctly
- No memory leaks (valgrind clean)
- Handles NULL CLOBs → DuckDB NULL
- Handles empty CLOBs → empty string

### Task 4.2: Integrate CLOB Fetch into Scan Loop
**File**: `src/oracle_extension.cpp`

- [ ] Modify `OracleQueryFunction()` to check if column is spatial
- [ ] For spatial columns, call `ReadCLOB()` instead of string conversion
- [ ] Store WKT string in VARCHAR vector
- [ ] Add error handling for CLOB read failures

**Acceptance Criteria**:
- WKT strings appear in query results
- No regression for non-spatial columns
- Performance: <5% overhead vs non-spatial queries

## Phase 5: GEOMETRY Type Integration

### Task 5.1: Load DuckDB Spatial Extension
**File**: `src/oracle_extension.cpp`
**Function**: `LoadInternal()`

- [ ] Check if spatial extension is available
- [ ] Auto-load spatial extension: `ExtensionHelper::LoadExternalExtension(db, "spatial")`
- [ ] Handle case where spatial extension not installed
- [ ] Fall back to VARCHAR if spatial unavailable

**Code**:
```cpp
void OracleExtension::LoadInternal(DuckDB &db, LoadedExtensionInfo &load_info) {
    auto &db_instance = *db.instance;

    // Try to load spatial extension
    try {
        ExtensionHelper::LoadExternalExtension(db_instance, "spatial");
    } catch (Exception &e) {
        // Spatial not available, log warning
        // Will use VARCHAR fallback for SDO_GEOMETRY
    }

    // Continue with Oracle extension registration...
}
```

**Acceptance Criteria**:
- Spatial extension loads if available
- No error if spatial extension missing
- Extension works with and without spatial

### Task 5.2: Update Type Mapping to GEOMETRY
**File**: `src/storage/oracle_table_entry.cpp`

- [ ] Change SDO_GEOMETRY mapping from VARCHAR to GEOMETRY
- [ ] Only use GEOMETRY if spatial extension loaded
- [ ] Add runtime check: `ExtensionHelper::IsExtensionLoaded("spatial")`

**Code**:
```cpp
static LogicalType MapOracleColumn(const string &data_type, idx_t precision, idx_t scale, idx_t char_len, ClientContext &context) {
    auto upper = StringUtil::Upper(data_type);

    if (upper == "SDO_GEOMETRY" || upper == "MDSYS.SDO_GEOMETRY") {
        if (ExtensionHelper::IsExtensionLoaded(context, "spatial")) {
            return LogicalType::GEOMETRY();
        } else {
            // Fallback to VARCHAR
            return LogicalType::VARCHAR;
        }
    }
    // ...
}
```

**Acceptance Criteria**:
- Returns GEOMETRY when spatial loaded
- Returns VARCHAR when spatial not loaded
- No crashes when spatial extension missing

### Task 5.3: WKT → GEOMETRY Conversion in Fetch Loop
**File**: `src/oracle_extension.cpp`

- [ ] For GEOMETRY columns, parse WKT string using `ST_GeomFromText()`
- [ ] Call spatial extension function from C++
- [ ] Handle WKT parsing errors (return NULL)
- [ ] Store parsed GEOMETRY in result vector

**Approach**: Let DuckDB SQL engine handle conversion by wrapping query:

```cpp
// Instead of calling ST_GeomFromText() from C++, generate SQL:
string wrapped_query = StringUtil::Format(
    "SELECT ST_GeomFromText(col1) AS col1, col2, ... FROM (%s)",
    original_query.c_str()
);
```

**Alternative**: Direct C++ call to spatial functions (more complex, better performance)

**Acceptance Criteria**:
- GEOMETRY values queryable with spatial functions
- Invalid WKT returns NULL (not error)
- Performance: <50 µs per geometry conversion

## Phase 6: Settings and Configuration

### Task 6.1: Add Spatial Settings
**File**: `src/include/oracle_settings.hpp`

- [ ] Add `oracle_enable_spatial_conversion` (bool, default true)
- [ ] Add `oracle_spatial_use_wkb` (bool, default false) - for future WKB support
- [ ] Add `oracle_spatial_strict_validation` (bool, default false)
- [ ] Register settings in `OracleSettings::Register()`

**Settings Definitions**:
```cpp
struct OracleSettings {
    // Existing settings...

    bool enable_spatial_conversion = true;
    bool spatial_use_wkb = false;
    bool spatial_strict_validation = false;

    static void Register(DatabaseInstance &db) {
        // ...
        auto enable_spatial_conversion_setting = make_uniq<BooleanSetting>(
            "oracle_enable_spatial_conversion",
            "Automatically convert Oracle SDO_GEOMETRY to DuckDB GEOMETRY",
            true
        );
        db.config.AddExtensionOption("oracle_enable_spatial_conversion", std::move(enable_spatial_conversion_setting));
        // ...
    }
};
```

**Acceptance Criteria**:
- Settings accessible via `SET oracle_enable_spatial_conversion = false`
- Settings applied in `GetScanFunction()`
- Default behavior: conversion enabled

## Phase 7: Error Handling

### Task 7.1: Handle Conversion Failures
**File**: `src/oracle_extension.cpp`

- [ ] Wrap CLOB read in try-catch
- [ ] Return NULL on CLOB read error
- [ ] Log warning with row identifier
- [ ] Add `oracle_debug_show_queries` logging for generated SQL

**Error Cases**:
1. CLOB read fails (OCI error)
2. WKT parsing fails (invalid geometry)
3. Oracle conversion fails (invalid SDO_GEOMETRY)

**Logging**:
```cpp
if (OracleSettings::debug_show_queries) {
    std::cerr << "Oracle spatial query: " << query << std::endl;
}

if (clob_read_failed) {
    std::cerr << "Warning: Failed to read geometry for row " << row_id << std::endl;
    // Return NULL
}
```

**Acceptance Criteria**:
- No crashes on invalid data
- Warnings logged for debugging
- NULL returned for failed conversions

### Task 7.2: Validate Settings Combinations
**File**: `src/storage/oracle_catalog.cpp`

- [ ] Check for invalid setting combinations in `ATTACH`
- [ ] Warn if spatial conversion enabled but spatial extension not loaded
- [ ] Error if WKB mode requested (not implemented yet)

**Acceptance Criteria**:
- Clear error messages for invalid settings
- No silent failures

## Phase 8: Testing

### Task 8.1: Create SQL Unit Tests
**File**: `test/sql/spatial/test_oracle_spatial_geometry.test`

- [ ] Test: Detect SDO_GEOMETRY column type
- [ ] Test: Fetch simple POINT geometry
- [ ] Test: Fetch POLYGON geometry
- [ ] Test: Fetch NULL geometries
- [ ] Test: Empty geometry (`GEOMETRYCOLLECTION EMPTY`)
- [ ] Test: Multi-geometries (MULTIPOINT, MULTIPOLYGON)
- [ ] Test: Spatial analysis with `ST_Area()`, `ST_Distance()`
- [ ] Test: Spatial joins with local data
- [ ] Test: Disable conversion with setting
- [ ] Test: Spatial extension not loaded (fallback to VARCHAR)

**Total**: 10+ test cases

**Acceptance Criteria**:
- All tests pass on Oracle Standard Edition
- Tests skip gracefully on Oracle Express (no JVM)
- 100% code coverage for new functions

### Task 8.2: Create Integration Tests
**File**: `test/integration/init_sql/spatial_setup.sql`

- [ ] Create Oracle test schema with SDO_GEOMETRY tables
- [ ] Insert test data: points, lines, polygons, multi-geometries, NULLs
- [ ] Use `SDO_UTIL.FROM_WKTGEOMETRY()` for test data insertion
- [ ] Add 3D geometry test cases (Z coordinate)
- [ ] Add large geometry test (>1000 vertices)

**File**: `test/integration/test_spatial.sh`

- [ ] Start Oracle container (gvenzl/oracle-free:23-slim)
- [ ] Run setup SQL script
- [ ] Execute DuckDB spatial tests
- [ ] Verify results
- [ ] Cleanup container

**Acceptance Criteria**:
- Integration tests run in CI
- Tests complete in <5 minutes
- No false positives/negatives

### Task 8.3: Performance Benchmarks
**File**: `test/sql/spatial/benchmark_spatial.test`

- [ ] Benchmark: Fetch 10K simple points
- [ ] Benchmark: Fetch 1K complex polygons
- [ ] Benchmark: Spatial join (1K × 1K geometries)
- [ ] Compare: VARCHAR vs GEOMETRY fetch times
- [ ] Measure: Memory usage for large geometries

**Targets**:
- 10K points/sec fetch rate
- 1K polygons/sec fetch rate
- <5% overhead vs VARCHAR

**Acceptance Criteria**:
- Performance meets targets
- No memory leaks (valgrind)
- No regressions vs baseline

## Phase 9: Documentation

### Task 9.1: User Documentation
**File**: `README.md`

- [ ] Add "Working with Spatial Data" section
- [ ] Document prerequisites (Oracle JVM, Spatial extension)
- [ ] Provide example queries
- [ ] Document settings
- [ ] List limitations (no Express, read-only, no advanced types)
- [ ] Add troubleshooting section

**Acceptance Criteria**:
- Clear, concise examples
- All features documented
- Limitations clearly stated

### Task 9.2: Developer Documentation
**File**: `specs/guides/spatial-implementation.md`

- [ ] Document architecture (SDO_GEOMETRY → WKT → GEOMETRY)
- [ ] Explain OCI CLOB handling
- [ ] Provide extension points for future enhancements
- [ ] Document adding new spatial types
- [ ] Add code examples

**Acceptance Criteria**:
- Sufficient detail for future maintainers
- Code examples compile and run

### Task 9.3: Inline Code Documentation
**Files**: All modified C++ files

- [ ] Add Doxygen comments to all new public functions
- [ ] Document function parameters and return values
- [ ] Add usage examples in comments
- [ ] Document error conditions

**Example**:
```cpp
//! Reads a CLOB from Oracle and returns as string
//! \param svc Oracle service context handle
//! \param err Oracle error handle
//! \param lob_loc CLOB locator
//! \return WKT string from CLOB, empty string if NULL
//! \throws IOException on OCI error
static string ReadCLOB(OCISvcCtx *svc, OCIError *err, OCILobLocator *lob_loc);
```

**Acceptance Criteria**:
- All public APIs documented
- Doxygen builds without warnings

## Phase 10: Quality Gate

### Task 10.1: Code Quality Checks
**Commands**: `make tidy-check`, `make format`

- [ ] Run clang-tidy: zero warnings
- [ ] Run clang-format: all files formatted
- [ ] Check for memory leaks: valgrind clean
- [ ] Review all TODO/FIXME comments
- [ ] Remove debug logging

**Acceptance Criteria**:
- All quality checks pass
- No compiler warnings
- No static analysis warnings

### Task 10.2: Cross-Platform Testing
**Platforms**: Linux, macOS (if applicable)

- [ ] Build and test on Ubuntu 24.04
- [ ] Build and test on Ubuntu 22.04
- [ ] Test with Oracle 19c, 21c, 23c
- [ ] Test with different Oracle Instant Client versions

**Acceptance Criteria**:
- Builds and tests pass on all platforms
- No platform-specific bugs

### Task 10.3: Regression Testing
**Scope**: Ensure no breaking changes

- [ ] Run full test suite: `make test`
- [ ] Run integration tests: `make integration`
- [ ] Test existing Oracle functionality (non-spatial tables)
- [ ] Verify performance: no regression in non-spatial queries

**Acceptance Criteria**:
- All existing tests pass
- No performance regression (<2%)

## Phase 11: Archive & Knowledge Capture

### Task 11.1: Archive Workspace
**Files**: Move to `specs/archive/oracle-spatial-geometry/`

- [ ] Copy final PRD, tasks, recovery guide
- [ ] Include research notes
- [ ] Add completion summary
- [ ] Link to commit SHA

**Completion Summary Template**:
```markdown
# Oracle Spatial Geometry Support - Completion Summary

**Completed**: 2025-11-XX
**Commits**: abc123..def456
**Lines Changed**: +290 / -10

## Delivered Features
- ✅ SDO_GEOMETRY type detection
- ✅ WKT conversion via SDO_UTIL
- ✅ GEOMETRY type integration
- ✅ Comprehensive test coverage

## Known Limitations
- Oracle Express not supported (no JVM)
- Read-only (no INSERT/UPDATE)
- Advanced types not supported

## Future Enhancements
- WKB support for better performance
- Spatial filter pushdown
```

### Task 11.2: Update Knowledge Guides
**File**: `specs/guides/spatial-implementation.md`

- [ ] Document final architecture
- [ ] Add code examples
- [ ] Document lessons learned
- [ ] Update CLAUDE.md if workflow improved

**Acceptance Criteria**:
- Guides up-to-date
- Future developers can extend feature

---

## Execution Checklist

**To start implementation**:
```bash
# Use Expert agent to execute tasks
/implement oracle-spatial-geometry
```

**Phase Completion Order**:
1. ✅ Phase 1: Research & Planning
2. Phase 2: Type Detection (Tasks 2.1-2.2)
3. Phase 3: Query Rewriting (Tasks 3.1-3.2)
4. Phase 4: CLOB Handling (Tasks 4.1-4.2)
5. Phase 5: GEOMETRY Integration (Tasks 5.1-5.3)
6. Phase 6: Settings (Task 6.1)
7. Phase 7: Error Handling (Tasks 7.1-7.2)
8. Phase 8: Testing (Tasks 8.1-8.3)
9. Phase 9: Documentation (Tasks 9.1-9.3)
10. Phase 10: Quality Gate (Tasks 10.1-10.3)
11. Phase 11: Archive (Tasks 11.1-11.2)

**Dependencies**:
- Phase 5 requires Phase 4 complete
- Phase 8 requires Phases 2-7 complete
- Phase 10 requires all previous phases

**Estimated Effort**: 2-3 development sessions
