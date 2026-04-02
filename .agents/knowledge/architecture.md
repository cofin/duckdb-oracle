# Architecture — DuckDB Oracle Extension

## Extension Type

This is a **DuckDB Storage Extension** — it plugs into DuckDB's catalog and storage layer to make Oracle databases appear as attached databases. Users interact via `ATTACH` and standard SQL.

## Core Components

### Entry Point (`oracle_extension.cpp`)
- Registers the `oracle` storage extension type
- Registers table functions: `oracle_query`, `oracle_execute`
- Registers settings: `oracle_array_size`, `oracle_prefetch_rows`, `oracle_enable_pushdown`, `oracle_lazy_schema_loading`, `oracle_metadata_result_limit`
- Registers secret type: `oracle` (DSN, user, password, wallet)

### Connection Layer

```
OracleConnectionManager (singleton, thread-safe)
  └── OracleConnectionPool (one per connection string)
       └── OracleConnectionHandle (RAII wrapper, per-thread)
            └── OracleContext
                 ├── OCIEnv     — OCI environment handle
                 ├── OCISvcCtx  — Service context
                 ├── OCISession — Session handle
                 └── OCIStmt    — Statement handle
```

- **Pool limit**: 8 connections per connection string (default)
- **Thread safety**: Mutex + condition variable per pool
- **Cleanup**: RAII destructors on `OracleConnectionHandle`

### Catalog Layer

```
OracleCatalog (inherits Catalog)
  └── OracleCatalogState (shared state)
       ├── schema_cache    — Known schemas
       ├── table_cache     — Known tables per schema
       ├── object_cache    — Column metadata per table
       └── OracleVersionInfo — Feature detection
```

- Lazy loading: only current schema enumerated at attach time
- On-demand fallback for cross-schema references
- Configurable metadata limit (default 10K objects)

### Read Path (Scan)

1. `ATTACH` → `OracleAttach()` creates `OracleCatalogState`
2. `SELECT` → `OracleTableEntry::GetFunction()` returns scan function
3. `OracleBind()` → prepares query, detects column types via Oracle metadata
4. `OracleScanState` → allocates OCI buffers for array fetch
5. `OracleQueryFunction()` → fetches batches into DuckDB `DataChunk`
6. `OraclePushdownComplexFilter()` → rewrites WHERE for server-side execution

### Write Path (COPY)

1. `COPY ... TO` → `OracleWriteBind()` — maps DuckDB types to OCI bind types
2. `OracleWriteInitGlobal()` → prepares `INSERT INTO ... VALUES (:1, :2, ...)`
3. `OracleWriteInitLocal()` → per-thread buffer allocation (batch = STANDARD_VECTOR_SIZE)
4. `OracleWriteSink()` → binds DataChunk columns using native type handlers
5. `OracleWriteFinalize()` → commits transaction

### Type System

```cpp
enum class OracleTypeCategory {
  STANDARD,    // VARCHAR, CHAR, NCHAR
  NUMERIC,     // NUMBER, FLOAT, BINARY_FLOAT/DOUBLE
  TEMPORAL,    // DATE, TIMESTAMP*
  SPATIAL,     // SDO_GEOMETRY → WKT conversion
  VECTOR,      // Oracle 23ai VECTOR → DuckDB LIST
  JSON,        // Oracle 21c+ JSON type
  LOB_CLOB,    // CLOB/NCLOB → TO_CHAR fallback
  LOB_BLOB,    // BLOB/BFILE → RAWTOHEX fallback
  RAW,         // RAW → RAWTOHEX
  XML,         // XMLTYPE → XMLSERIALIZE
  UNKNOWN      // Fallback to VARCHAR
};
```

- `OracleColumnMetadata` carries type info + `needs_server_conversion` flag
- `RequiresQueryRewrite()` checks version info to decide SQL rewriting

## File Organization

```
src/
├── include/                    # All headers (.hpp)
│   ├── oracle_extension.hpp    # Entry point
│   ├── oracle_connection*.hpp  # Connection management
│   ├── oracle_catalog*.hpp     # Catalog/schema layer
│   ├── oracle_table_*.hpp      # Table entry and scan
│   ├── oracle_write.hpp        # Write support
│   ├── oracle_secret.hpp       # Secret management
│   └── oracle_settings.hpp     # Configuration
├── oracle_extension.cpp        # Extension registration
├── oracle_connection.cpp       # Connection operations
├── oracle_connection_manager.cpp # Pool management
├── oracle_secret.cpp           # Secret parsing
└── storage/                    # Storage extension impl
    ├── oracle_catalog.cpp
    ├── oracle_schema_entry.cpp
    ├── oracle_storage_extension.cpp
    ├── oracle_table_entry.cpp
    ├── oracle_transaction*.cpp
    └── oracle_write.cpp        # Write/COPY implementation
```
