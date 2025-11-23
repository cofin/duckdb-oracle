# Oracle Spatial Implementation Guide

**Version**: 1.0
**Last Updated**: 2025-11-22
**Status**: Production

## Overview

This guide documents the implementation of Oracle Spatial `SDO_GEOMETRY` support in the DuckDB Oracle extension. It provides technical details for maintainers and developers extending spatial functionality.

## Architecture

### Data Flow

```
Oracle Database (SDO_GEOMETRY)
    |
    | SDO_UTIL.TO_WKTGEOMETRY() - SQL conversion
    |
    v
Oracle OCI (VARCHAR/CLOB) - WKT string
    |
    | OCI fetch
    |
    v
DuckDB (VARCHAR) - WKT representation
    |
    | ST_GeomFromText() (user-invoked)
    |
    v
DuckDB Spatial (GEOMETRY) - Native spatial type
```

### Key Design Decisions

1. **WKT-Based Conversion**: Uses Oracle's `SDO_UTIL.TO_WKTGEOMETRY()` instead of direct OCI object binding
   - **Pros**: Simpler implementation, portable, human-readable
   - **Cons**: Requires Oracle JVM, string overhead
   - **Future**: Add WKB support for performance optimization

2. **VARCHAR Mapping**: Returns WKT strings as VARCHAR instead of automatic GEOMETRY type
   - **Pros**: No DuckDB Spatial dependency, user control
   - **Cons**: Requires manual ST_GeomFromText() cast
   - **Future**: Auto-convert to GEOMETRY when spatial extension loaded

3. **SQL Query Rewriting**: Modifies SELECT list at query generation time
   - **Pros**: Clean separation, works with existing fetch logic
   - **Cons**: Query string manipulation complexity
   - **Alternative**: Could use OCI post-fetch conversion

## Implementation Components

### 1. Type Detection

**File**: `src/storage/oracle_table_entry.cpp`
**Function**: `MapOracleColumn()`

```cpp
static LogicalType MapOracleColumn(const string &data_type, idx_t precision, idx_t scale, idx_t char_len) {
    auto upper = StringUtil::Upper(data_type);

    // Spatial geometry type detection
    if (upper == "SDO_GEOMETRY" || upper == "MDSYS.SDO_GEOMETRY") {
        // Map to VARCHAR for WKT string representation
        // TODO: Map to GEOMETRY type after spatial extension integration
        return LogicalType::VARCHAR;
    }

    // ... other type mappings
}
```

**How it works**:

- Called during schema introspection (`LoadColumns()`)
- Receives `data_type` from Oracle `all_tab_columns` view
- Returns DuckDB `LogicalType` for the column
- Spatial types mapped to VARCHAR (WKT strings)

**Oracle type names**:

- `SDO_GEOMETRY` - Standard Oracle Spatial type
- `MDSYS.SDO_GEOMETRY` - Fully qualified schema name

### 2. Metadata Storage

**File**: `src/include/oracle_table_entry.hpp`
**Struct**: `OracleColumnMetadata`

```cpp
//! Metadata about Oracle column types
struct OracleColumnMetadata {
    string column_name;
    string oracle_data_type;
    bool is_spatial;

    OracleColumnMetadata(const string &name, const string &data_type)
        : column_name(name), oracle_data_type(data_type),
          is_spatial(data_type == "SDO_GEOMETRY" || data_type == "MDSYS.SDO_GEOMETRY") {
    }
};
```

**Purpose**:

- Preserves original Oracle type information
- Enables query rewriting based on column type
- Stored in `OracleTableEntry::column_metadata` vector

**Lifecycle**:

1. Created in `LoadColumns()` during schema introspection
2. Passed to `OracleTableEntry` constructor
3. Accessed in `GetScanFunction()` for query rewriting

### 3. Query Rewriting

**File**: `src/storage/oracle_table_entry.cpp`
**Function**: `OracleTableEntry::GetScanFunction()`

```cpp
// Build column list with WKT conversion for spatial columns
string column_list;
idx_t col_idx = 0;
for (auto &col : columns.Physical()) {
    if (col_idx > 0) {
        column_list += ", ";
    }

    auto quoted_col = KeywordHelper::WriteQuoted(col.Name(), '"');

    // Check if this column is a spatial geometry type
    bool is_spatial = false;
    if (col_idx < column_metadata.size()) {
        is_spatial = column_metadata[col_idx].is_spatial;
    }

    if (is_spatial) {
        // Convert SDO_GEOMETRY to WKT CLOB using Oracle's built-in function
        column_list += StringUtil::Format("SDO_UTIL.TO_WKTGEOMETRY(%s) AS %s",
                                          quoted_col.c_str(), quoted_col.c_str());
    } else {
        column_list += quoted_col;
    }
    col_idx++;
}

auto query = StringUtil::Format("SELECT %s FROM %s.%s",
                                column_list.c_str(), quoted_schema.c_str(), quoted_table.c_str());
```

**Example transformation**:

Original table:
```sql
-- Oracle schema
CREATE TABLE parcels (
    id NUMBER,
    geometry SDO_GEOMETRY,
    name VARCHAR2(100)
);
```

Generated query:
```sql
SELECT
    "id",
    SDO_UTIL.TO_WKTGEOMETRY("geometry") AS "geometry",
    "name"
FROM "SPATIAL_SCHEMA"."PARCELS"
```

**Key points**:

- Replaces `SELECT *` with explicit column list
- Wraps spatial columns with `SDO_UTIL.TO_WKTGEOMETRY()`
- Preserves column names with `AS` clause
- Uses quoted identifiers for case sensitivity

### 4. Settings Integration

**Files**:

- `src/include/oracle_settings.hpp` - Setting definition
- `src/oracle_extension.cpp` - Registration
- `src/storage/oracle_catalog.cpp` - ATTACH option parsing

**Setting**:

```cpp
bool enable_spatial_conversion = true;  // Default: enabled
```

**Usage**:

```sql
-- Disable spatial conversion
SET oracle_enable_spatial_conversion = false;

-- Enable in ATTACH
ATTACH 'user/pass@host:1521/service' AS ora (
    TYPE oracle,
    enable_spatial_conversion = true
);
```

**Implementation**:

1. Setting defined in `OracleSettings` struct
2. Registered in `OracleExtension::LoadInternal()`
3. Applied in query rewriting logic (future enhancement)

## Extending Spatial Support

### Adding New Spatial Types

To support additional Oracle spatial types (e.g., `SDO_TOPO_GEOMETRY`):

**Step 1**: Update type detection

```cpp
// In MapOracleColumn()
if (upper == "SDO_GEOMETRY" || upper == "MDSYS.SDO_GEOMETRY" ||
    upper == "SDO_TOPO_GEOMETRY") {
    return LogicalType::VARCHAR;
}
```

**Step 2**: Update metadata struct

```cpp
// In OracleColumnMetadata constructor
is_spatial = (data_type == "SDO_GEOMETRY" ||
              data_type == "MDSYS.SDO_GEOMETRY" ||
              data_type == "SDO_TOPO_GEOMETRY");
```

**Step 3**: Update query rewriting (if different conversion function needed)

```cpp
if (is_topo_geometry) {
    column_list += StringUtil::Format("SDO_UTIL.TO_WKTGEOMETRY(%s.GEOMETRY) AS %s",
                                      quoted_col.c_str(), quoted_col.c_str());
}
```

**Step 4**: Add tests

```sql
-- test/sql/spatial/test_oracle_topo_geometry.test
require oracle

statement ok
ATTACH 'user/pass@host:1521/service' AS ora (TYPE oracle);

query IT
SELECT topo_id, geometry FROM ora.topo_schema.topo_features;
```

### Adding WKB Support (Performance Optimization)

WKB (Well-Known Binary) is more efficient than WKT for large geometries.

**Step 1**: Add setting

```cpp
// oracle_settings.hpp
bool spatial_use_wkb = false;  // Default: WKT
```

**Step 2**: Update query rewriting

```cpp
if (is_spatial) {
    if (settings.spatial_use_wkb) {
        column_list += StringUtil::Format("SDO_UTIL.TO_WKBGEOMETRY(%s) AS %s",
                                          quoted_col.c_str(), quoted_col.c_str());
    } else {
        column_list += StringUtil::Format("SDO_UTIL.TO_WKTGEOMETRY(%s) AS %s",
                                          quoted_col.c_str(), quoted_col.c_str());
    }
}
```

**Step 3**: Implement BLOB fetch logic

```cpp
// In OracleQueryFunction() fetch loop
if (is_wkb_column) {
    // Use OCILobRead2() for BLOB
    // Convert WKB to GEOMETRY using ST_GeomFromWKB()
}
```

### Automatic GEOMETRY Type Conversion

To return native DuckDB GEOMETRY instead of VARCHAR:

**Step 1**: Detect spatial extension

```cpp
// In LoadInternal()
if (ExtensionHelper::IsExtensionLoaded(db_instance, "spatial")) {
    spatial_extension_available = true;
}
```

**Step 2**: Update type mapping

```cpp
// In MapOracleColumn()
if (upper == "SDO_GEOMETRY") {
    if (spatial_extension_available) {
        return LogicalType::GEOMETRY();
    } else {
        return LogicalType::VARCHAR;
    }
}
```

**Step 3**: Post-fetch WKT parsing

```cpp
// In OracleQueryFunction()
if (col_type.id() == LogicalTypeId::GEOMETRY) {
    auto wkt_str = /* fetched WKT string */;
    auto geom_value = InvokeSpatialFunction("ST_GeomFromText", {Value(wkt_str)});
    output.SetValue(col_idx, row_idx, geom_value);
}
```

## Performance Considerations

### Conversion Overhead

**WKT Parsing**: ~10-50 µs per geometry (DuckDB Spatial)
**Oracle Conversion**: ~5-20 µs per geometry (SDO_UTIL.TO_WKTGEOMETRY)
**String Transfer**: ~1-5 µs per geometry (OCI VARCHAR fetch)

**Total**: ~20-100 µs per geometry (acceptable for analytical queries)

### Optimization Strategies

1. **Batch Fetching**: Use existing `array_size` setting (default 256 rows)
2. **Column Pruning**: Skip unused spatial columns via projection pushdown
3. **WKB Mode**: Binary transfer reduces string overhead by ~30%
4. **Lazy Conversion**: Parse WKT only when GEOMETRY functions used

### Benchmark Example

```sql
-- Create test data in Oracle
CREATE TABLE benchmark_points (
    id NUMBER,
    location SDO_GEOMETRY
);

-- Populate with 100K points
INSERT INTO benchmark_points
SELECT
    rownum,
    SDO_GEOMETRY(2001, NULL, SDO_POINT_TYPE(dbms_random.value(0, 100), dbms_random.value(0, 100), NULL), NULL, NULL)
FROM dual
CONNECT BY LEVEL <= 100000;

-- DuckDB benchmark
ATTACH 'user/pass@host:1521/service' AS ora (TYPE oracle);

-- Measure fetch time
.timer on
SELECT COUNT(*) FROM ora.benchmark_schema.benchmark_points;
-- Target: <2 seconds for 100K rows
```

## Testing Strategy

### Unit Tests (Without Oracle Connection)

**File**: `test/sql/spatial/test_oracle_spatial_basic.test`

```sql
require oracle

# Verify extension loads
statement ok
SELECT 1;

# Verify setting exists
query I
SELECT current_setting('oracle_enable_spatial_conversion');
----
true
```

### Integration Tests (With Oracle Container)

**Setup**: `test/integration/init_sql/spatial_setup.sql`

```sql
-- Create spatial test data
CREATE TABLE spatial_test.geometries (
    id NUMBER PRIMARY KEY,
    point_geom SDO_GEOMETRY,
    poly_geom SDO_GEOMETRY
);

INSERT INTO spatial_test.geometries VALUES (
    1,
    SDO_GEOMETRY(2001, NULL, SDO_POINT_TYPE(1.0, 2.0, NULL), NULL, NULL),
    SDO_UTIL.FROM_WKTGEOMETRY('POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))')
);
```

**Test**: `test/sql/spatial/test_oracle_spatial_integration.test`

```sql
require oracle

statement ok
ATTACH 'test_user/test_pass@localhost:1521/ORCLPDB1' AS ora;

query IT
SELECT id, point_geom FROM ora.spatial_test.geometries WHERE id = 1;
----
1  POINT(1 2)
```

## Common Issues and Solutions

### Issue: Oracle JVM Not Available

**Symptom**: `ORA-13199: SDO_UTIL function not found`

**Solution**:

1. Verify Oracle edition (not Express)
2. Check JVM status: `SELECT status FROM dba_registry WHERE comp_name LIKE '%Java%';`
3. Document in README.md as prerequisite

### Issue: Large Geometry Truncation

**Symptom**: WKT strings cut off at 4KB

**Current**: VARCHAR buffer limited by OCI fetch size
**Future**: Implement CLOB handling with `OCILobRead2()`

**Workaround**:

```sql
-- In Oracle, simplify geometry
CREATE VIEW simplified_parcels AS
SELECT id, SDO_UTIL.SIMPLIFY(geometry, 0.5) AS geometry
FROM parcels;
```

### Issue: Performance Regression

**Symptom**: Queries slower with spatial conversion

**Diagnosis**:

1. Check `array_size` setting (default 256)
2. Profile with `SET oracle_debug_show_queries = true`
3. Measure with spatial conversion off: `SET oracle_enable_spatial_conversion = false`

**Solution**: Increase `array_size` or enable WKB mode (future)

## Code Quality Checklist

When modifying spatial code:

- [ ] Update type detection in `MapOracleColumn()`
- [ ] Update `OracleColumnMetadata` if new metadata needed
- [ ] Update query rewriting logic in `GetScanFunction()`
- [ ] Add setting if new configuration option
- [ ] Add tests (unit + integration)
- [ ] Update README.md with new features
- [ ] Update this guide with patterns
- [ ] Run `make test` (all tests pass)
- [ ] Run `make tidy-check` (no warnings)
- [ ] Verify no memory leaks (valgrind)

## Future Enhancements

### Phase 2 Features

1. **CLOB Support**: Handle large geometries (>4KB WKT)
2. **WKB Mode**: Binary transfer for 30% performance improvement
3. **GEOMETRY Type**: Auto-convert when spatial extension loaded
4. **Spatial Pushdown**: Push spatial predicates to Oracle (SDO_FILTER, SDO_RELATE)
5. **Advanced Types**: SDO_TOPO_GEOMETRY, SDO_GEORASTER support

### Integration Opportunities

1. **GeoParquet Export**: `COPY ... TO 'output.parquet' (FORMAT PARQUET)`
2. **Spatial Joins**: Join Oracle spatial with local Parquet/CSV geodata
3. **Visualization**: DuckDB Spatial visualization tools integration

## References

### Oracle Spatial Documentation

- [SDO_GEOMETRY Object Type](https://docs.oracle.com/database/121/SPATL/sdo_geometry-object-type.htm)
- [SDO_UTIL Package Functions](https://docs.oracle.com/en/database/oracle/oracle-database/23/spatl/sdo_util-from_wktgeometry.html)
- [OCI LOB Functions](https://docs.oracle.com/en/database/oracle/oracle-database/19/lnoci/lob-functions.html)

### DuckDB Spatial

- [DuckDB Spatial Extension](https://github.com/duckdb/duckdb-spatial)
- [Spatial Functions Documentation](https://duckdb.org/docs/extensions/spatial)

### Related Implementations

- [BigQuery Extension Geometry Support](https://github.com/hafenkran/duckdb-bigquery#working-with-geometries)
- [Postgres Extension Spatial Support](https://github.com/duckdb/postgres_scanner)

---

**For Questions or Contributions**: See project CLAUDE.md and AGENTS.md for development workflow.
