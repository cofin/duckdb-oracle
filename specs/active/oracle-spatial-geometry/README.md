# Oracle Spatial Geometry Support - Workspace

**Created**: 2025-11-22
**Status**: Ready for Implementation
**Feature**: Add SDO_GEOMETRY support to DuckDB Oracle Extension

## Workspace Contents

### Core Documents

- **[prd.md](prd.md)** - Product Requirements Document
  - Executive summary and background
  - Technical approach (WKT conversion, CLOB handling, GEOMETRY integration)
  - Testing strategy
  - Performance considerations
  - Complete architecture specification

- **[tasks.md](tasks.md)** - Implementation Task Breakdown
  - 11 phases with detailed tasks
  - Acceptance criteria for each task
  - Code locations and function names
  - Estimated effort: 2-3 development sessions

- **[recovery.md](recovery.md)** - Session Recovery Guide
  - Quick context for resuming work
  - Phase-by-phase resumption instructions
  - Common issues and solutions
  - Development commands reference

### Supporting Directories

- **`research/`** - Research findings and references
- **`tmp/`** - Temporary files (cleaned during archival)

## Quick Start

### To Implement This Feature

```bash
# Use Expert agent to execute tasks
/implement oracle-spatial-geometry
```

### To Review Requirements

```bash
cat specs/active/oracle-spatial-geometry/prd.md
```

### To Check Progress

```bash
grep "\[x\]" specs/active/oracle-spatial-geometry/tasks.md | wc -l  # Completed
grep "\[ \]" specs/active/oracle-spatial-geometry/tasks.md | wc -l  # Remaining
```

## Feature Overview

### What It Does

Enables querying Oracle Spatial `SDO_GEOMETRY` data from DuckDB, automatically converting to DuckDB `GEOMETRY` type for spatial analysis.

**Before**:
```sql
SELECT geometry FROM oracle_db.gis.parcels;
-- Returns: VARCHAR with opaque representation
```

**After**:
```sql
SELECT
    parcel_id,
    ST_Area(geometry) AS area,
    ST_AsText(geometry) AS wkt
FROM oracle_db.gis.parcels
WHERE ST_Intersects(geometry, ST_MakeEnvelope(0, 0, 100, 100));
-- Returns: Usable GEOMETRY type with spatial functions
```

### Technical Approach

```
┌─────────────┐      ┌──────────────┐      ┌─────────┐      ┌──────────┐
│ SDO_GEOMETRY│ ───> │ WKT (Oracle) │ ───> │   CLOB  │ ───> │ GEOMETRY │
│   (Oracle)  │      │   SQL Conv   │      │  (OCI)  │      │ (DuckDB) │
└─────────────┘      └──────────────┘      └─────────┘      └──────────┘
                     SDO_UTIL.TO_WKT     OCILobRead2     ST_GeomFromText
```

## Implementation Phases

1. ✅ **Research & Planning** - Complete (PRD, tasks, recovery)
2. ⏳ **Type Detection** - Identify SDO_GEOMETRY columns
3. ⏳ **Query Rewriting** - Wrap with WKT conversion
4. ⏳ **CLOB Handling** - Fetch WKT strings via OCI
5. ⏳ **GEOMETRY Integration** - Parse to spatial type
6. ⏳ **Settings** - Add configuration options
7. ⏳ **Error Handling** - Robust failure handling
8. ⏳ **Testing** - Comprehensive test coverage
9. ⏳ **Documentation** - User and developer docs
10. ⏳ **Quality Gate** - Final validation
11. ⏳ **Archive** - Move to specs/archive/

## Key Files to Modify

| File | Lines Added | Purpose |
|------|-------------|---------|
| `src/storage/oracle_table_entry.cpp` | +80 | Type detection, query rewriting |
| `src/include/oracle_table_entry.hpp` | +20 | Metadata storage |
| `src/oracle_extension.cpp` | +120 | CLOB handling, GEOMETRY conversion |
| `src/include/oracle_settings.hpp` | +10 | New settings |
| `src/oracle_connection.cpp` | +60 | JVM detection |

**Total**: ~290 lines of new C++ code

## Testing Coverage

- **Unit Tests**: 10+ SQL test cases (`test/sql/spatial/*.test`)
- **Integration Tests**: Containerized Oracle with real SDO_GEOMETRY data
- **Performance Benchmarks**: 10K geometries/sec target
- **Edge Cases**: NULL, empty, invalid, large geometries (>1MB)

## Dependencies

- **DuckDB Spatial Extension** (runtime, auto-loaded)
- **Oracle Instant Client** (existing)
- **Oracle JVM** (required for WKT conversion, not available in Express)

## Success Criteria

- ✅ Detect SDO_GEOMETRY columns correctly
- ✅ Convert to WKT without data loss
- ✅ Parse WKT to GEOMETRY type
- ✅ Support all basic geometry types (Point, LineString, Polygon, Multi*)
- ✅ Handle NULLs and empty geometries
- ✅ <100 µs per geometry conversion
- ✅ 100% test coverage
- ✅ All quality checks pass

## References

### Documentation Sources

- [Oracle Spatial Developer's Guide](https://docs.oracle.com/en/database/oracle/oracle-database/23/spatl/sdo_util-from_wktgeometry.html)
- [Oracle SDO_GEOMETRY Object Type](https://docs.oracle.com/database/121/SPATL/sdo_geometry-object-type.htm)
- [OCI Object-Relational Data Types](https://docs.oracle.com/en/database/oracle/oracle-database/18/lnoci/object-relational-data-types-in-oci.html)
- [DuckDB Spatial Extension](https://github.com/duckdb/duckdb-spatial)
- [BigQuery Extension Geometry Support](https://github.com/hafenkran/duckdb-bigquery#working-with-geometries)
- [Stack Overflow: Oracle to WKT](https://stackoverflow.com/questions/44832223/oracle-converting-sdo-geometry-to-wkt)

### Related Specifications

- WKT (Well-Known Text): ISO/IEC 13249-3:2016
- WKB (Well-Known Binary): ISO/IEC 13249-3:2016
- Oracle Spatial: Oracle Database 19c-23c

---

**Next Action**: Run `/implement oracle-spatial-geometry` to start implementation.
