# Write Support — Implementation Details

## Overview

The extension supports writing data to Oracle via DuckDB's `COPY` interface using OCI Array Bind for high-throughput batch inserts.

## Data Flow

```
DuckDB COPY statement
  → OracleWriteBind()      # Parse options, map types
  → OracleWriteInitGlobal() # Prepare INSERT statement
  → OracleWriteInitLocal()  # Per-thread buffer allocation
  → OracleWriteSink()       # Bind DataChunk columns, execute
  → OracleWriteFinalize()   # Commit transaction
```

## Key Structures

```cpp
struct OracleWriteBindData {
  vector<string> column_names;     // Target column names
  vector<LogicalType> column_types; // DuckDB types
  vector<string> oracle_types;      // Oracle column types (NUMBER, VARCHAR2, etc.)
  vector<ub2> bind_types;           // OCI native bind types (SQLT_INT, etc.)
};

class OracleWriteLocalState {
  vector<vector<char>> bind_buffers;     // Data buffers (batch × element_size)
  vector<vector<sb2>> indicator_buffers; // NULL indicators
  vector<vector<ub2>> length_buffers;    // Data lengths per element
  vector<OCIBind*> binds;               // OCI bind handles
};
```

## Type Mapping (DuckDB → OCI)

| DuckDB LogicalType | OCI Bind Type | Element Size | Notes |
|--------------------|---------------|-------------|-------|
| TINYINT | `SQLT_INT` | 8 bytes | Promoted to int64_t |
| SMALLINT | `SQLT_INT` | 8 bytes | Promoted to int64_t |
| INTEGER | `SQLT_INT` | 8 bytes | Promoted to int64_t |
| BIGINT | `SQLT_INT` | 8 bytes | Native int64_t |
| FLOAT | `SQLT_BDOUBLE` | 8 bytes | Promoted to double |
| DOUBLE | `SQLT_BDOUBLE` | 8 bytes | Native double |
| DATE | `SQLT_ODT` | sizeof(OCIDate) | Oracle datetime |
| TIMESTAMP | `SQLT_ODT` | sizeof(OCIDate) | Oracle datetime |
| TIMESTAMP_TZ | `SQLT_ODT` | sizeof(OCIDate) | Oracle datetime |
| BLOB | `SQLT_BIN` | Dynamic | Binary data |
| All others | `SQLT_CHR` | String | ToString() conversion |

## Native vs String Binding

**Reads use string binding** (SQLT_STR) to avoid Column Shift corruption.
**Writes use native binding** where possible because:
- We control buffer allocation and layout
- Each column has a contiguous buffer: `STANDARD_VECTOR_SIZE × element_size`
- No inter-column alignment issues
- Native binding avoids string→number parsing overhead on Oracle side

## Buffer Management

- Buffers allocated per-thread in `OracleWriteInitLocal()`
- Batch size = `STANDARD_VECTOR_SIZE` (DuckDB's vector size, typically 2048)
- Each column gets: data buffer + indicator buffer + length buffer
- Buffers reused across batches within the same thread

## Supported Oracle Target Types

Write support has been tested with:
- **Basic types**: INT, VARCHAR2, NUMBER, DATE, TIMESTAMP
- **Oracle 23ai VECTOR**: Serialized to string, Oracle parses
- **Spatial SDO_GEOMETRY**: WKT string insertion
- **LOBs**: CLOB, BLOB via string/binary paths

## Integration Tests

- `test_write_basic.test` — INT, VARCHAR, DATE, TIMESTAMP roundtrip
- `test_write_vector.test` — Oracle 23ai VECTOR type
- `test_write_spatial.test` — SDO_GEOMETRY insertion
- `test_write_lobs.test` — LOB/BLOB/CLOB handling
