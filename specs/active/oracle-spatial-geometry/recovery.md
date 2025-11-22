# Recovery Guide: Oracle Spatial Geometry Support

**Workspace**: `specs/active/oracle-spatial-geometry/`
**Last Updated**: 2025-11-22

## Purpose

This document enables any agent (Expert, Testing, Docs) to resume work on the Oracle Spatial Geometry feature at any phase.

## Quick Context

### What We're Building

Add support for Oracle Spatial's `SDO_GEOMETRY` type to the DuckDB Oracle extension, enabling spatial analysis on Oracle geodata using DuckDB Spatial functions.

**Core Flow**: `SDO_GEOMETRY` (Oracle) → WKT String (SQL conversion) → CLOB (OCI) → VARCHAR/GEOMETRY (DuckDB)

### Key Design Decisions

1. **WKT-Based Conversion**: Use Oracle's `SDO_UTIL.TO_WKTGEOMETRY()` instead of direct OCI object binding
   - Simpler implementation
   - Portable across Oracle versions
   - Human-readable debugging

2. **Two-Phase Type Mapping**:
   - Phase 1: SDO_GEOMETRY → VARCHAR (WKT strings)
   - Phase 2: SDO_GEOMETRY → GEOMETRY (after spatial extension integration)

3. **SQL Query Rewriting**: Replace `SELECT *` with explicit column list, wrapping spatial columns

   ```sql
   SELECT SDO_UTIL.TO_WKTGEOMETRY("geom") AS "geom", "other_col" FROM table
   ```

4. **Optional Conversion**: Settings allow disabling spatial conversion (fallback to VARCHAR)

### Technical Challenges

| Challenge | Solution |
|-----------|----------|
| Identifying spatial columns | Store original Oracle type metadata in `OracleTableEntry` |
| CLOB handling | Implement `ReadCLOB()` helper using OCI LOB APIs |
| Spatial extension dependency | Auto-load if available, fallback to VARCHAR if not |
| Oracle Express (no JVM) | Detect at ATTACH time, throw clear error |
| Large geometries | Stream CLOB incrementally, add size limit setting |
| Invalid geometries | Return NULL, log warning (unless strict mode) |

## Project Structure

### Workspace Files

```
specs/active/oracle-spatial-geometry/
├── prd.md           # Full requirements, architecture, testing strategy
├── tasks.md         # Phase-by-phase implementation checklist
├── recovery.md      # This file
├── research/        # Research findings
│   └── .gitkeep
└── tmp/             # Temporary files (cleaned by Docs & Vision)
    └── .gitkeep
```

### Code Files to Modify

| File | Purpose | Key Functions |
|------|---------|---------------|
| `src/storage/oracle_table_entry.cpp` | Type mapping, query generation | `MapOracleColumn()`, `GetScanFunction()` |
| `src/include/oracle_table_entry.hpp` | Column metadata storage | `OracleTableEntry` class |
| `src/oracle_extension.cpp` | Fetch logic, CLOB handling | `OracleQueryFunction()`, spatial extension loading |
| `src/include/oracle_settings.hpp` | Settings definitions | `OracleSettings` struct |
| `src/oracle_connection.cpp` | JVM detection | `CheckSpatialSupport()` (new) |

### Test Files to Create

| File | Purpose |
|------|---------|
| `test/sql/spatial/test_oracle_spatial_geometry.test` | Unit tests for spatial functionality |
| `test/integration/init_sql/spatial_setup.sql` | Oracle test schema setup |
| `test/integration/test_spatial.sh` | Integration test runner |
| `test/sql/spatial/benchmark_spatial.test` | Performance benchmarks |

## Implementation Phases

### Completed Phases ✅

- [x] **Phase 1: Research & Planning** - PRD, tasks, recovery guide created

### Current Phase

**Phase 2: Type Detection & Schema Introspection**

**What to do**:

1. Modify `MapOracleColumn()` to detect SDO_GEOMETRY types
2. Add metadata storage for original Oracle types
3. Test type detection with sample Oracle table

**Files to edit**:

- [src/storage/oracle_table_entry.cpp:14-42](src/storage/oracle_table_entry.cpp#L14-L42)
- [src/include/oracle_table_entry.hpp](src/include/oracle_table_entry.hpp)

**Test**:

```sql
-- Create Oracle table with SDO_GEOMETRY
CREATE TABLE test_spatial (id NUMBER, geom SDO_GEOMETRY);

-- Check DuckDB detects type
ATTACH 'oracle://...' AS ora;
DESCRIBE ora.test_schema.test_spatial;
-- Should show: geom | VARCHAR (or GEOMETRY after Phase 3)
```

### Upcoming Phases

**Phase 3: Query Rewriting for WKT Conversion**

- Modify `GetScanFunction()` to generate column-specific SELECT list
- Wrap SDO_GEOMETRY columns with `SDO_UTIL.TO_WKTGEOMETRY()`
- Add setting `oracle_enable_spatial_conversion`

**Phase 4: OCI CLOB Handling**

- Implement `ReadCLOB()` helper function
- Integrate CLOB fetch into `OracleQueryFunction()`
- Test with large geometries (>1MB WKT)

**Phase 5: GEOMETRY Type Integration**

- Load DuckDB Spatial extension automatically
- Change type mapping to `LogicalType::GEOMETRY()`
- Parse WKT strings to GEOMETRY in fetch loop

**Phase 6-11**: Settings, error handling, testing, documentation, quality gate, archive

See [tasks.md](tasks.md) for detailed checklist.

## How to Resume Work

### For Expert Agent

**Starting new phase**:

```bash
# 1. Read current phase tasks
cat specs/active/oracle-spatial-geometry/tasks.md | grep "\[ \]" | head -5

# 2. Review PRD for context
cat specs/active/oracle-spatial-geometry/prd.md | grep "## Technical Approach" -A 50

# 3. Start implementing
# - Modify files as per tasks.md
# - Run tests after each task
# - Update tasks.md checkboxes as you complete
```

**Resuming mid-phase**:

1. Check last completed task in `tasks.md`
2. Review code changes since last commit
3. Continue from next unchecked task

### For Testing Agent

**Creating tests**:

1. Review test strategy in [prd.md - Testing Strategy](prd.md#testing-strategy)
2. Create test files in `test/sql/spatial/`
3. Run tests: `build/release/test/unittest test/sql/spatial/*.test`
4. Add integration tests in `test/integration/`

**Test data requirements**:

- Oracle table with SDO_GEOMETRY columns
- Mix of geometry types: Point, LineString, Polygon, Multi*
- Edge cases: NULL, empty, invalid, large (>1000 vertices)

### For Docs & Vision Agent

**Documentation tasks**:

1. Update [README.md](../../../README.md) - Add "Working with Spatial Data" section
2. Create `specs/guides/spatial-implementation.md` - Developer guide
3. Add inline Doxygen comments to new functions
4. Quality gate: Run checks, validate tests

**Archive checklist**:

- All tasks in `tasks.md` completed
- All tests passing
- Documentation complete
- No compiler/static analysis warnings
- Move workspace to `specs/archive/oracle-spatial-geometry/`

## Key Resources

### Documentation

**Oracle Spatial**:

- [SDO_GEOMETRY Object Type](https://docs.oracle.com/database/121/SPATL/sdo_geometry-object-type.htm)
- [SDO_UTIL Package Functions](https://docs.oracle.com/en/database/oracle/oracle-database/23/spatl/sdo_util-from_wktgeometry.html)
- [OCI Object-Relational Data Types](https://docs.oracle.com/en/database/oracle/oracle-database/18/lnoci/object-relational-data-types-in-oci.html)
- [Stack Overflow: Oracle SDO_GEOMETRY to WKT](https://stackoverflow.com/questions/44832223/oracle-converting-sdo-geometry-to-wkt)

**DuckDB Spatial**:

- [DuckDB Spatial Extension](https://github.com/duckdb/duckdb-spatial)
- [ST_GeomFromText Documentation](https://github.com/duckdb/duckdb-spatial/blob/main/docs/functions.md)

**Reference Implementation**:

- [BigQuery Extension Geometry Support](https://github.com/hafenkran/duckdb-bigquery#working-with-geometries)

### Code References

**Type Mapping**: [oracle_table_entry.cpp:14-42](src/storage/oracle_table_entry.cpp#L14-L42)

```cpp
static LogicalType MapOracleColumn(const string &data_type, idx_t precision, idx_t scale, idx_t char_len);
```

**Query Generation**: [oracle_table_entry.cpp:101-124](src/storage/oracle_table_entry.cpp#L101-L124)

```cpp
TableFunction OracleTableEntry::GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data);
```

**Fetch Loop**: [oracle_extension.cpp:~200-300](src/oracle_extension.cpp) (approximate)

```cpp
static void OracleQueryFunction(ClientContext &context, TableFunctionInput &data, DataChunk &output);
```

### MCP Tools Available

**Context7** - For up-to-date library docs:

```python
mcp__context7__get-library-docs(
    context7CompatibleLibraryID="/duckdb/duckdb-spatial",
    topic="ST_GeomFromText WKT conversion",
    page=1
)
```

**Zen Tools** - For problem solving:

- `zen.debug` - Systematic debugging (if OCI errors, CLOB issues)
- `zen.chat` - Brainstorming spatial pushdown optimization
- `zen.thinkdeep` - Architecture review (e.g., WKT vs WKB trade-offs)

**WebSearch** - For latest Oracle docs:

```python
WebSearch(query="Oracle 23c SDO_UTIL.TO_WKTGEOMETRY performance")
```

## Common Issues and Solutions

### Issue: "ORA-13199: SDO_UTIL function not found"

**Cause**: Oracle Spatial not installed or JVM disabled

**Solution**:

1. Check Oracle edition: `SELECT * FROM v$version;` (must not be Express)
2. Verify JVM: `SELECT comp_name, status FROM dba_registry WHERE comp_name = 'JServer JAVA Virtual Machine';`
3. Add detection code in `CheckSpatialSupport()` (Phase 3, Task 3.2)

### Issue: Memory leak in CLOB handling

**Cause**: OCI descriptors not freed

**Solution**:

```cpp
// Always free CLOB locators
OCIDescriptorFree(lob_loc, OCI_DTYPE_LOB);
```

Use RAII wrapper or smart pointers for OCI handles.

### Issue: WKT parsing fails for valid geometries

**Cause**: DuckDB Spatial extension not loaded or WKT format mismatch

**Solution**:

1. Check extension: `SELECT * FROM duckdb_extensions() WHERE extension_name = 'spatial';`
2. Validate WKT: `SELECT ST_GeomFromText('POINT(1 2)');` (should not error)
3. Log raw WKT string for debugging: `SET oracle_debug_show_queries = true;`

### Issue: Performance regression (>5% overhead)

**Cause**: CLOB fetch per-row instead of batch, or excessive WKT parsing

**Solution**:

1. Verify `array_size` setting used: Check OCI array fetch enabled
2. Profile with `perf`: Identify hot path
3. Consider WKB mode (Phase 2 enhancement): `oracle_spatial_use_wkb = true`

### Issue: Tests fail on Oracle 19c but pass on 23c

**Cause**: `SDO_UTIL.TO_WKTGEOMETRY()` differences across versions

**Solution**:

1. Check Oracle Spatial version: `SELECT SDO_VERSION FROM dual;`
2. Test WKT output format manually: `SELECT SDO_UTIL.TO_WKTGEOMETRY(...) FROM dual;`
3. Add version-specific test cases or normalize WKT format

## Development Commands

### Build

```bash
make release              # Build extension in release mode
make debug                # Build with debug symbols
make clean                # Clean build artifacts
```

### Test

```bash
# Run all tests
make test

# Run specific test file
build/release/test/unittest test/sql/spatial/test_oracle_spatial_geometry.test

# Run with debug output
build/release/duckdb -unsigned < test/sql/spatial/test_oracle_spatial_geometry.test

# Integration tests (requires Oracle container)
make integration
```

### Quality Checks

```bash
make tidy-check           # clang-tidy static analysis
make format               # clang-format code formatting
valgrind --leak-check=full build/release/test/unittest  # Memory leak check
```

### Oracle Setup (Local Testing)

```bash
# Start Oracle container
docker run -d --name oracle-test \
    -e ORACLE_PWD=oracle \
    -p 1521:1521 \
    gvenzl/oracle-free:23-slim

# Connect to Oracle
sqlplus sys/oracle@localhost:1521/FREEPDB1 as sysdba

# Create test schema (see test/integration/init_sql/spatial_setup.sql)
```

## Next Steps

**Immediate Actions** (for resuming agent):

1. **Read current phase tasks**: `cat specs/active/oracle-spatial-geometry/tasks.md | grep "## Phase 2" -A 30`
2. **Check completion status**: Count `[x]` vs `[ ]` checkboxes in current phase
3. **Review code context**: Read files listed in current phase tasks
4. **Start next task**: Implement, test, commit, update tasks.md

**Success Criteria for Phase Completion**:

- All tasks in phase checked `[x]` in tasks.md
- Tests pass for implemented functionality
- Code compiles without warnings
- Commit with descriptive message (e.g., `feat: Add SDO_GEOMETRY type detection`)

**Handoff to Next Agent**:

- Update tasks.md with completed checkboxes
- Commit all changes
- Add notes to `tmp/notes.md` if needed (challenges, decisions, blockers)
- Next agent runs recovery protocol (reads this file, continues next phase)

---

**Questions? Issues?**

Refer to:

- [prd.md](prd.md) - Full requirements and architecture
- [tasks.md](tasks.md) - Detailed task breakdown
- [CLAUDE.md](../../../CLAUDE.md) - Project-wide development guide
- `.claude/agents/expert.md` - Expert agent instructions

**Contact**: Leave notes in `specs/active/oracle-spatial-geometry/tmp/notes.md`
