# PRD: Oracle Spatial Geometry Support

**Feature**: Add support for Oracle Spatial SDO_GEOMETRY types in DuckDB Oracle Extension
**Created**: 2025-11-22
**Status**: Active
**Workspace**: `specs/active/oracle-spatial-geometry/`

## Executive Summary

Add support for Oracle Spatial's SDO_GEOMETRY type family to the DuckDB Oracle extension, enabling seamless querying of spatial data from Oracle databases. This feature will convert Oracle spatial types to DuckDB GEOMETRY types using WKT/WKB interchange formats, similar to the approach used in the BigQuery extension.

## Background

### Oracle Spatial Data Types

Oracle Spatial provides the **SDO_GEOMETRY** object type for storing geometric data. Key characteristics:

- **Structure**: Object type with attributes:
  - `SDO_GTYPE` - Geometry type identifier (point, line, polygon, etc.)
  - `SDO_SRID` - Spatial reference system identifier
  - `SDO_POINT` - Optimized point storage (x, y, z coordinates)
  - `SDO_ELEM_INFO` - VARRAY describing element interpretation
  - `SDO_ORDINATES` - VARRAY containing coordinate values

- **Common Types**:
  - 2D/3D/4D Points, LineStrings, Polygons
  - Multi-geometries (MultiPoint, MultiLineString, MultiPolygon)
  - GeometryCollections
  - Curves, Surfaces, Solids (advanced types)

### Oracle Conversion Functions

Oracle provides built-in conversion utilities via `SDO_UTIL` package:

**Export Functions** (Oracle → Standard Formats):

- `SDO_UTIL.TO_WKTGEOMETRY(geometry)` - Returns WKT as CLOB
- `SDO_UTIL.TO_WKBGEOMETRY(geometry)` - Returns WKB as BLOB
- `SDO_GEOMETRY.Get_WKT()` - Object method returning WKT
- `SDO_GEOMETRY.Get_WKB()` - Object method returning WKB

**Import Functions** (Standard Formats → Oracle):

- `SDO_UTIL.FROM_WKTGEOMETRY(wkt_clob)` - Creates SDO_GEOMETRY from WKT
- `SDO_UTIL.FROM_WKBGEOMETRY(wkb_blob)` - Creates SDO_GEOMETRY from WKB

**Limitations**:

- WKT/WKB conversion requires Oracle JVM (not available in Oracle Express Edition)
- Functions throw errors if JVM not enabled on Autonomous Database Serverless

### DuckDB Spatial Extension

DuckDB Spatial provides GEOMETRY type support with comprehensive functions:

**Core Functions**:

- `ST_GeomFromText(wkt)` / `ST_GeomFromWKB(wkb)` - Parse standard formats
- `ST_AsText(geom)` / `ST_AsWKB(geom)` - Export to standard formats
- `ST_GeometryType(geom)` - Identify geometry type
- Rich spatial analysis functions (area, distance, intersection, etc.)

**Type System**:

- Generic `GEOMETRY` type
- Specialized columnar types: `POINT_2D`, `LINESTRING_2D`, `POLYGON_2D`, `BOX_2D`

### Reference Implementation: BigQuery Extension

The [DuckDB BigQuery extension](https://github.com/hafenkran/duckdb-bigquery) handles GEOGRAPHY types by:

1. Fetching BigQuery GEOGRAPHY columns as WKT strings
2. Users explicitly cast to GEOMETRY: `SELECT ST_GeomFromText(geom_column) FROM ...`
3. No automatic type conversion (VARCHAR fallback)

## Problem Statement

Currently, the Oracle extension maps SDO_GEOMETRY columns to VARCHAR:

```cpp
// In MapOracleColumn() - oracle_table_entry.cpp:41
return LogicalType::VARCHAR; // Fallback for unknown types
```

This results in:

- ❌ Spatial data returned as opaque object representations
- ❌ No integration with DuckDB Spatial functions
- ❌ Users cannot perform spatial analysis on Oracle geodata
- ❌ Manual conversion required, if possible at all

## Goals

### Primary Goals

1. **Automatic Detection**: Identify SDO_GEOMETRY columns during schema introspection
2. **WKT Conversion**: Convert Oracle spatial data to WKT strings automatically
3. **GEOMETRY Mapping**: Map SDO_GEOMETRY → DuckDB GEOMETRY type
4. **Spatial Analysis**: Enable use of DuckDB Spatial functions on Oracle geodata
5. **Robust Handling**: Handle NULLs, empty geometries, and conversion errors gracefully

### Non-Goals

1. ❌ Support for Oracle Spatial advanced types (e.g., SDO_TOPO_GEOMETRY, SDO_GEORASTER)
2. ❌ Writing spatial data back to Oracle (extension is read-only)
3. ❌ Direct OCI object type binding (use SQL conversion instead)
4. ❌ Spatial index pushdown to Oracle
5. ❌ Support for Oracle Express Edition (no JVM/WKT conversion)

## Technical Approach

### Phase 1: Type Detection

**Modify `MapOracleColumn()` in [oracle_table_entry.cpp:14](src/storage/oracle_table_entry.cpp#L14)**:

```cpp
static LogicalType MapOracleColumn(const string &data_type, idx_t precision, idx_t scale, idx_t char_len) {
    auto upper = StringUtil::Upper(data_type);

    // New geometry type detection
    if (upper == "SDO_GEOMETRY" || upper == "MDSYS.SDO_GEOMETRY") {
        return LogicalType::VARCHAR; // WKT string representation
        // TODO Phase 3: Return GEOMETRY type after spatial extension integration
    }

    // Existing type mappings...
}
```

**Query Modification**: No changes needed; `all_tab_columns.data_type` returns `"SDO_GEOMETRY"` for spatial columns.

### Phase 2: WKT Conversion in Query Execution

**Modify query generation in `OracleTableEntry::GetScanFunction()` ([oracle_table_entry.cpp:111](src/storage/oracle_table_entry.cpp#L111))**:

Current:

```cpp
auto query = StringUtil::Format("SELECT * FROM %s.%s", quoted_schema.c_str(), quoted_table.c_str());
```

New approach - wrap SDO_GEOMETRY columns:

```cpp
// Build column list with SDO_UTIL.TO_WKTGEOMETRY() for spatial columns
string column_list;
for (size_t i = 0; i < columns.Physical().size(); i++) {
    if (i > 0) column_list += ", ";

    auto &col = columns.Physical()[i];
    auto quoted_col = KeywordHelper::WriteQuoted(col.Name(), '"');

    if (IsSpatialColumn(col)) {
        // Convert SDO_GEOMETRY to WKT CLOB
        column_list += StringUtil::Format("SDO_UTIL.TO_WKTGEOMETRY(%s) AS %s",
                                          quoted_col.c_str(), quoted_col.c_str());
    } else {
        column_list += quoted_col;
    }
}

auto query = StringUtil::Format("SELECT %s FROM %s.%s",
                                column_list.c_str(), quoted_schema.c_str(), quoted_table.c_str());
```

**Helper Function**:

```cpp
static bool IsSpatialColumn(const ColumnDefinition &col) {
    // Check if column was originally SDO_GEOMETRY type
    // Store original Oracle type in ColumnDefinition metadata or parallel structure
}
```

### Phase 3: GEOMETRY Type Integration

**Require DuckDB Spatial Extension**:

Add spatial extension as a dependency and load it automatically:

```cpp
// In oracle_extension.cpp LoadInternal()
void OracleExtension::LoadInternal(DuckDB &db, LoadedExtensionInfo &load_info) {
    // Load spatial extension if available
    auto &db_instance = *db.instance;
    ExtensionHelper::LoadExternalExtension(db_instance, "spatial");

    // Continue with Oracle extension registration...
}
```

**Update Type Mapping**:

```cpp
if (upper == "SDO_GEOMETRY" || upper == "MDSYS.SDO_GEOMETRY") {
    return LogicalType::GEOMETRY(); // Native spatial type
}
```

**Post-Fetch Conversion**:

In `OracleQueryFunction` fetch loop, convert WKT strings → GEOMETRY:

```cpp
// For GEOMETRY columns, parse WKT
if (col_type.id() == LogicalTypeId::GEOMETRY) {
    auto wkt_str = /* fetched WKT string from OCI */;
    auto geom = ST_GeomFromText(wkt_str); // Use spatial extension function
    // Store in result vector
}
```

### Phase 4: Error Handling

**Conversion Failures**:

- Catch `SDO_UTIL.TO_WKTGEOMETRY` errors (invalid geometry, missing JVM)
- Return NULL for failed conversions
- Log warning with geometry identifier

**NULL Handling**:

- Oracle NULL → DuckDB NULL (existing OCI indicator handling)
- Empty geometries: `"GEOMETRYCOLLECTION EMPTY"` → valid GEOMETRY

**Type Validation**:

- Validate WKT before parsing to GEOMETRY
- Fallback to VARCHAR if spatial extension unavailable

### Phase 5: Settings and Configuration

**New Settings** (via `OracleSettings`):

```cpp
// oracle_settings.hpp
struct OracleSettings {
    // Existing settings...

    bool enable_spatial_conversion = true;  // Convert SDO_GEOMETRY to GEOMETRY
    bool spatial_use_wkb = false;           // Use WKB instead of WKT (performance)
    bool spatial_strict_validation = false;  // Fail on invalid geometries vs NULL
};
```

**Usage**:

```sql
SET oracle_enable_spatial_conversion = true;
SET oracle_spatial_use_wkb = false; -- WKT is more debuggable

ATTACH 'user/pass@host:1521/service' AS oracle_db (TYPE oracle);
SELECT geom_column FROM oracle_db.spatial_schema.parcels;
```

## Implementation Details

### File Changes

| File | Changes | Lines Est. |
|------|---------|------------|
| `src/storage/oracle_table_entry.cpp` | Add spatial type detection, query rewriting | +80 |
| `src/include/oracle_table_entry.hpp` | Store original Oracle types metadata | +20 |
| `src/oracle_extension.cpp` | Spatial extension loading, GEOMETRY fetch handling | +120 |
| `src/include/oracle_settings.hpp` | New spatial settings | +10 |
| `src/oracle_connection.cpp` | CLOB/BLOB handling for WKT/WKB | +60 |

**Total**: ~290 lines of new C++ code

### OCI API Usage

**CLOB Handling** (for WKT strings):

```cpp
// Bind CLOB locator
OCILobLocator *lob_loc;
OCIDescriptorAlloc(env, (void**)&lob_loc, OCI_DTYPE_LOB, 0, 0);
OCIDefineByPos(stmt, &define, err, col_idx, &lob_loc, 0, SQLT_CLOB, ...);

// Read CLOB content
oraub8 lob_len;
OCILobGetLength2(svc, err, lob_loc, &lob_len);
char *buffer = new char[lob_len + 1];
OCILobRead2(svc, err, lob_loc, &lob_len, 0, 1, buffer, lob_len, OCI_ONE_PIECE, ...);
```

**BLOB Handling** (for WKB - Phase 2 optimization):

Similar to CLOB, using `SQLT_BLOB` and `OCILobRead2()`.

### DuckDB Spatial Integration

**Dependency Management**:

1. Check if spatial extension is loaded: `ExtensionHelper::IsExtensionLoaded("spatial")`
2. Auto-load if available: `ExtensionHelper::LoadExternalExtension(db, "spatial")`
3. If unavailable: Warn user, fall back to VARCHAR type

**Function Calls**:

```cpp
// Call spatial extension functions from C++
auto &catalog = Catalog::GetSystemCatalog(context);
auto &func_catalog = catalog.GetCatalogEntry(context, CatalogType::SCALAR_FUNCTION_ENTRY, "main", "ST_GeomFromText");
auto &func = func_catalog.Cast<ScalarFunctionCatalogEntry>();

// Invoke with WKT string
vector<Value> args = {Value(wkt_string)};
auto result = func.functions.GetFunctionByArguments(context, args)->Execute(...);
```

Alternatively, generate SQL for conversion:

```cpp
// Let DuckDB's SQL engine handle conversion
query = "SELECT ST_GeomFromText(col1), col2, ... FROM (SELECT SDO_UTIL.TO_WKTGEOMETRY(...) AS col1, ...)";
```

## Testing Strategy

### Unit Tests (DuckDB SQL Test Framework)

**Test File**: `test/sql/spatial/test_oracle_spatial_geometry.test`

```sql
# Test: Detect SDO_GEOMETRY columns
require oracle

statement ok
ATTACH 'oracle://test_user:test_pass@localhost:1521/ORCLPDB1' AS ora_db;

query I
SELECT column_name, data_type
FROM ora_db.information_schema.columns
WHERE table_schema = 'SPATIAL_DATA' AND table_name = 'PARCELS';
----
PARCEL_ID INTEGER
GEOMETRY GEOMETRY

# Test: Fetch spatial data as WKT
query IT
SELECT parcel_id, ST_AsText(geometry)
FROM ora_db.spatial_data.parcels
WHERE parcel_id = 1;
----
1 POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))

# Test: NULL geometry handling
query I
SELECT COUNT(*)
FROM ora_db.spatial_data.parcels
WHERE geometry IS NULL;
----
2

# Test: Spatial analysis with DuckDB functions
query R
SELECT ST_Area(geometry)
FROM ora_db.spatial_data.parcels
WHERE parcel_id = 1;
----
100.0
```

### Integration Tests

**Test File**: `test/integration/init_sql/spatial_setup.sql`

```sql
-- Create Oracle Spatial test schema
CREATE USER spatial_test IDENTIFIED BY spatial_test;
GRANT CONNECT, RESOURCE TO spatial_test;
GRANT CREATE TABLE TO spatial_test;

-- Create test table with SDO_GEOMETRY
CREATE TABLE spatial_test.test_geometries (
    id NUMBER PRIMARY KEY,
    name VARCHAR2(100),
    point_geom SDO_GEOMETRY,
    line_geom SDO_GEOMETRY,
    poly_geom SDO_GEOMETRY
);

-- Insert test data with various geometry types
INSERT INTO spatial_test.test_geometries VALUES (
    1,
    'Point Test',
    SDO_GEOMETRY(2001, NULL, SDO_POINT_TYPE(1.0, 2.0, NULL), NULL, NULL),
    NULL,
    NULL
);

INSERT INTO spatial_test.test_geometries VALUES (
    2,
    'Polygon Test',
    NULL,
    NULL,
    SDO_UTIL.FROM_WKTGEOMETRY('POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))')
);

-- Insert NULL geometry
INSERT INTO spatial_test.test_geometries VALUES (
    3,
    'NULL Test',
    NULL, NULL, NULL
);

COMMIT;
```

**Test Script**: `test/integration/test_spatial.sh`

```bash
#!/bin/bash
set -e

# Start Oracle container with spatial support
docker run -d --name oracle-spatial \
    -e ORACLE_PWD=oracle \
    -p 1521:1521 \
    gvenzl/oracle-free:23-slim

# Wait for Oracle to start
sleep 60

# Run spatial setup SQL
sqlplus sys/oracle@localhost:1521/FREEPDB1 as sysdba @test/integration/init_sql/spatial_setup.sql

# Run DuckDB tests
build/release/duckdb -unsigned < test/sql/spatial/test_oracle_spatial_geometry.test

# Cleanup
docker stop oracle-spatial
docker rm oracle-spatial
```

### Edge Cases

| Test Case | Expected Behavior |
|-----------|-------------------|
| Empty geometry | Return `GEOMETRYCOLLECTION EMPTY` |
| Invalid WKT | Return NULL, log warning |
| 3D/4D geometries | Preserve Z/M coordinates in WKT |
| Large geometries (>1MB WKT) | Stream CLOB data incrementally |
| Missing spatial extension | Fall back to VARCHAR, warn user |
| Oracle Express (no JVM) | Error with helpful message |
| Mixed SRID in table | Preserve SRID in WKT: `SRID=4326;POINT(...)` |
| GeometryCollection | Handle nested collections recursively |

## Documentation Requirements

### User-Facing Documentation

**File**: `README.md` (add "Working with Spatial Data" section)

```markdown
## Working with Spatial Data

The Oracle extension automatically converts Oracle Spatial `SDO_GEOMETRY` types to DuckDB `GEOMETRY` types, enabling spatial analysis with DuckDB Spatial functions.

### Prerequisites

1. Oracle database with Oracle Spatial enabled
2. Oracle JVM installed (required for WKT conversion)
3. DuckDB Spatial extension loaded

### Example Usage

```sql
LOAD spatial;
ATTACH 'user/pass@host:1521/service' AS oracle_db (TYPE oracle);

-- Query spatial data
SELECT
    parcel_id,
    ST_Area(geometry) AS area,
    ST_AsText(geometry) AS wkt
FROM oracle_db.gis.parcels
WHERE ST_Intersects(geometry, ST_MakeEnvelope(0, 0, 100, 100));
```

### Configuration

```sql
-- Disable automatic spatial conversion
SET oracle_enable_spatial_conversion = false;

-- Use WKB for better performance (when supported)
SET oracle_spatial_use_wkb = true;
```

### Limitations

- Oracle Express Edition not supported (requires JVM for conversion)
- Advanced types (SDO_TOPO_GEOMETRY, SDO_GEORASTER) not supported
- Spatial indexes not pushed down to Oracle
- Read-only (no INSERT/UPDATE of spatial data)

```

### Developer Documentation

**File**: `specs/guides/spatial-implementation.md`

```markdown
# Oracle Spatial Implementation Guide

## Architecture

SDO_GEOMETRY → WKT (Oracle) → VARCHAR (OCI) → GEOMETRY (DuckDB)

## Key Components

1. **Type Detection**: `MapOracleColumn()` identifies SDO_GEOMETRY
2. **Query Rewriting**: Wraps spatial columns with `SDO_UTIL.TO_WKTGEOMETRY()`
3. **CLOB Handling**: Reads WKT strings via OCI CLOB locators
4. **Type Conversion**: Parses WKT to GEOMETRY using spatial extension

## Adding New Spatial Types

To support additional Oracle spatial types:

1. Add type detection in `MapOracleColumn()`
2. Add conversion function in query rewriting
3. Update tests with new type examples
4. Document limitations in README
```

## Dependencies

### New Dependencies

1. **DuckDB Spatial Extension** (runtime)
   - Auto-loaded when spatial columns detected
   - Fallback to VARCHAR if unavailable
   - No build-time dependency

### Existing Dependencies (No Changes)

- Oracle Instant Client (Basic + SDK)
- DuckDB Extension Framework
- OpenSSL (via vcpkg)

## Performance Considerations

### Conversion Overhead

- **WKT Parsing**: ~10-50 µs per geometry (DuckDB Spatial)
- **Oracle Conversion**: ~5-20 µs per geometry (SDO_UTIL.TO_WKTGEOMETRY)
- **CLOB Fetch**: ~1-5 µs per geometry (OCI)

**Total**: ~20-100 µs per geometry (acceptable for analytical queries)

### Optimizations

1. **Batch Fetching**: Use existing `array_size` setting (default 256)
2. **WKB Option**: Add `oracle_spatial_use_wkb = true` for binary transfer (~30% faster)
3. **Column Pruning**: Respect `projection_pushdown` (skip unused spatial columns)
4. **Lazy Conversion**: Only parse WKT when GEOMETRY type accessed

### Benchmark Targets

- 10K geometries/sec fetch rate (simple points)
- 1K geometries/sec for complex polygons (>100 vertices)
- <5% overhead vs fetching as VARCHAR

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Oracle JVM not available | High - No conversion possible | Clear error message, check for JVM in ATTACH |
| Large WKT strings (>10MB) | Medium - Memory pressure | Stream CLOB incrementally, add size limit setting |
| Invalid geometries in Oracle | Low - Conversion fails | Return NULL, log warning, add validation setting |
| Spatial extension not installed | Medium - Feature unavailable | Auto-install if possible, fallback to VARCHAR |
| Performance regression | Low - Conversion overhead | Make conversion optional via setting |

## Success Criteria

### Functional

- ✅ Detect SDO_GEOMETRY columns correctly
- ✅ Convert to WKT without data loss
- ✅ Parse WKT to GEOMETRY type
- ✅ Support all basic geometry types (Point, LineString, Polygon, Multi*)
- ✅ Handle NULLs and empty geometries
- ✅ Pass all integration tests

### Performance

- ✅ <100 µs per geometry conversion
- ✅ No memory leaks in CLOB handling
- ✅ Support geometries up to 100MB WKT

### Quality

- ✅ 100% test coverage for new code
- ✅ All clang-tidy checks pass
- ✅ Documentation complete and accurate
- ✅ No breaking changes to existing functionality

## Future Enhancements

### Phase 2 Features (Post-MVP)

1. **WKB Support**: Use `SDO_UTIL.TO_WKBGEOMETRY()` for better performance
2. **Spatial Index Hints**: Add Oracle hints for spatial index usage
3. **Filter Pushdown**: Push spatial predicates to Oracle (e.g., `SDO_FILTER`, `SDO_RELATE`)
4. **Advanced Types**: Support SDO_TOPO_GEOMETRY, SDO_GEORASTER (if feasible)
5. **Write Support**: INSERT/UPDATE with GEOMETRY → SDO_GEOMETRY conversion

### Integration Opportunities

1. **GeoParquet Export**: `COPY (SELECT * FROM oracle_db.spatial.parcels) TO 'output.parquet' (FORMAT PARQUET)`
2. **Spatial Joins**: Join Oracle spatial tables with local Parquet/CSV geodata
3. **Visualization**: Integration with DuckDB Spatial's visualization tools

## References

### Documentation

- [Oracle Spatial Developer's Guide](https://docs.oracle.com/en/database/oracle/oracle-database/23/spatl/sdo_util-from_wktgeometry.html)
- [Oracle SDO_GEOMETRY Object Type](https://docs.oracle.com/database/121/SPATL/sdo_geometry-object-type.htm)
- [OCI Object-Relational Data Types](https://docs.oracle.com/en/database/oracle/oracle-database/18/lnoci/object-relational-data-types-in-oci.html)
- [DuckDB Spatial Extension Documentation](https://github.com/duckdb/duckdb-spatial)
- [Stack Overflow: Oracle SDO_GEOMETRY to WKT](https://stackoverflow.com/questions/44832223/oracle-converting-sdo-geometry-to-wkt)

### Code References

- [BigQuery Extension Geometry Support](https://github.com/hafenkran/duckdb-bigquery#working-with-geometries)
- [oracle_table_entry.cpp:14](src/storage/oracle_table_entry.cpp#L14) - Type mapping
- [oracle_table_entry.cpp:111](src/storage/oracle_table_entry.cpp#L111) - Query generation

---

**Next Steps**: See `tasks.md` for implementation checklist.
