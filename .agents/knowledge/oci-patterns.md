# OCI Patterns & Gotchas

## Array Fetch (Read Path)

The extension uses OCI Array Fetch for batch reads. Key decisions:

### String Binding Strategy
- **Problem**: OCI Array Fetch with native types (SQLT_INT, SQLT_FLT) for NUMBER/DATE/TIMESTAMP causes "Column Shift" — memory corruption where column N's data bleeds into column N+1's buffer.
- **Root Cause**: OCI buffer alignment requirements differ per type; array fetch doesn't respect inter-column padding.
- **Solution**: Bind all columns as `SQLT_STR` (string) during reads. DuckDB handles type conversion from strings. Performance cost is minimal vs. corruption risk.
- **Exception**: LOBs use `SQLT_LBI` because string conversion truncates.

### Array Size Configuration
- `oracle_array_size` (default: 200) — rows fetched per OCI round-trip
- `oracle_prefetch_rows` (default: 200) — OCI client-side prefetch hint
- Larger values = fewer round-trips but more memory per fetch

## Array Bind (Write Path)

Write operations use native OCI bind types where safe:

| DuckDB Type | OCI Bind Type | Buffer Size |
|------------|--------------|-------------|
| TINYINT/SMALLINT/INTEGER/BIGINT | `SQLT_INT` | `sizeof(int64_t)` |
| FLOAT/DOUBLE | `SQLT_BDOUBLE` | `sizeof(double)` |
| DATE/TIMESTAMP/* | `SQLT_ODT` | `sizeof(OCIDate)` |
| BLOB | `SQLT_BIN` | Dynamic |
| Everything else | `SQLT_CHR` | String conversion |

**Why native types are safe for writes but not reads**: In the write path, we control buffer allocation and layout. Each column gets a contiguous buffer of `STANDARD_VECTOR_SIZE × element_size`. No inter-column alignment issues.

## Connection Pooling

### Why Not OCILogon
- `OCILogon` has **no timeout** — hangs indefinitely if Oracle is slow/unreachable
- Under concurrent load, multiple `OCILogon` calls can deadlock

### Current Implementation
- `OracleConnectionManager` singleton with per-connection-string pools
- Pool limit: 8 connections (configurable)
- Thread-safe: mutex + condition_variable per pool
- RAII: `OracleConnectionHandle` destructor returns connection to pool

### OCISvcCtx Thread Safety
- Oracle documentation claims `OCISvcCtx` is thread-safe
- In practice, sharing across threads causes intermittent crashes
- **Rule**: One `OCISvcCtx` per thread, always

## Spatial Data (SDO_GEOMETRY)

- Converted to WKT via `SDO_UTIL.TO_WKTGEOMETRY()` server-side
- Returned as `VARCHAR` (or `GEOMETRY` if DuckDB spatial extension loaded)
- **Gotcha**: Complex geometries can exceed 4KB VARCHAR fetch buffer
- Solution: Check for truncation, handle oversized results gracefully

## Oracle Version Detection

```cpp
struct OracleVersionInfo {
  int major, minor, patch;
  bool supports_json_type;           // 21c+ (major >= 21)
  bool supports_vector;              // 23ai+ (major >= 23)
  bool supports_vector_serialize;    // 23.4+ (major >= 23 && minor >= 4)
};
```

- Detected at connect time from `v$version`
- Stored in `OracleCatalogState` for the lifetime of the attachment
- Drives query rewriting: e.g., VECTOR columns only scanned if `supports_vector`

## NULL Handling

- OCI requires `sb2` indicator arrays for every bound column
- Indicator value `-1` = NULL, `0` = value present
- **Critical**: Missing indicator arrays cause silent data corruption (NULLs read as garbage)
- Always allocate indicator buffers alongside data buffers
