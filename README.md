# DuckDB Oracle Extension

This extension allows DuckDB to directly read from and write to Oracle databases. It uses the Oracle Call Interface (OCI) for high-performance data transfer.

> **Disclaimer**: This is a community-maintained extension, not an official product of DuckDB or Oracle.

## Quick Start

**1. Install & Load**

> **Note**: `duckdb-oracle` resolves the Oracle Instant Client runtime library (`libclntsh.so` on Linux, `libclntsh.dylib` on macOS, `oci.dll` on Windows) **lazily at connection time** (`ATTACH`, `oracle_scan`, `oracle_query`, `oracle_execute`). Installing and loading the extension (`LOAD oracle;`) requires **no** Oracle client libraries installed, and the extension is compatible with **Oracle Instant Client Basic 19c, 21c, or 23ai** (set `OCI_LIB_DIR`, `ORACLE_HOME`, or add the Instant Client directory to `LD_LIBRARY_PATH` / `DYLD_LIBRARY_PATH` / `PATH` before opening a connection).
>
> **Supported DuckDB version**: v2.0.0 — see [Compatibility Matrix](docs/COMPATIBILITY.md) for details.

**Option A: DuckDB Community Extensions (Signed)**

```sql
INSTALL oracle FROM community;
LOAD oracle;
```

**Option B: Remote Custom Extension Repository (Unsigned)**

Start DuckDB with `duckdb -unsigned`:

```sql
-- Configure custom repository (GitHub Pages or raw gh-pages fallback)
SET custom_extension_repository = 'https://cofin.github.io/duckdb-oracle';
-- Or: SET custom_extension_repository = 'https://raw.githubusercontent.com/cofin/duckdb-oracle/gh-pages';

-- Install and load
INSTALL oracle;
LOAD oracle;
```

**Option C: Offline / Local Installation (GitHub Releases)**

DuckDB derives both the installed extension name and the C++ initialization entrypoint (`oracle_duckdb_cpp_init`) from the filename stem before the first `.`. When installing from a local file downloaded from [GitHub Releases](https://github.com/cofin/duckdb-oracle/releases), extract `oracle.duckdb_extension` from the platform archive (`oracle-windows_amd64.zip`, `oracle-linux_amd64.tar.gz`, `oracle-linux_arm64.tar.gz`, or `oracle-osx_arm64.tar.gz`)—or rename `oracle-<platform>.duckdb_extension` to `oracle.duckdb_extension`—before running `INSTALL`:

```sql
INSTALL 'C:/path/to/oracle.duckdb_extension';
LOAD oracle;
```

**2. Attach**

```sql
CREATE SECRET my_oracle (
    TYPE oracle,
    USER 'scott',
    PASSWORD 'tiger',
    HOST 'localhost',
    PORT 1521,
    SERVICE 'FREEPDB1'
);
ATTACH '' AS ora (TYPE oracle, SECRET my_oracle);
```

Direct credential strings remain supported for `oracle_query`, `oracle_scan`, and `oracle_execute`, but prefer secrets and attached aliases for normal use.

**3. Query (Read)**

```sql
-- Direct table access (Schema is auto-detected)
SELECT * FROM ora.employees LIMIT 5;

-- Join with local data
SELECT e.last_name, d.department_name
FROM ora.employees e
JOIN local_departments d ON e.department_id = d.id;

-- Spatial Data (SDO_GEOMETRY returns as GEOMETRY or WKT)
SELECT id, ST_Area(geom) FROM ora.gis_parcels;
```

**4. Write**

```sql
-- Insert from DuckDB query
INSERT INTO ora.target_table SELECT * FROM source_parquet_file;
```

## Features

- **High Performance**: Uses OCI Array Fetch and Array Bind for batch processing.
- **Spatial Support**: Maps `SDO_GEOMETRY` to DuckDB `GEOMETRY` (or WKT).
- **Vector Support**: Maps Oracle 23ai `VECTOR` to `LIST(FLOAT)`.
- **Smart Schema**: Auto-detects current schema and resolves synonyms.
- **Pushdown**: Pushes `WHERE` clauses and column projections to Oracle.

## Configuration

Set these variables to tune performance or behavior:

| Setting | Default | Description |
|---------|---------|-------------|
| `oracle_enable_pushdown` | `true` | Push filters/projections to Oracle. |
| `oracle_prefetch_rows` | `1024` | Rows to prefetch per round-trip; validated between 1 and 1,000,000. |
| `oracle_prefetch_memory` | `0` | OCI prefetch memory in bytes; `0` lets OCI choose, otherwise max 1 GiB. |
| `oracle_array_size` | `256` | Batch size for OCI fetch/bind; validated between 1 and DuckDB's vector capacity. |
| `oracle_enable_spatial_types` | `true` | Map `SDO_GEOMETRY` to `GEOMETRY` type. |
| `oracle_connection_cache` | `true` | Enable connection pooling. |
| `oracle_connection_limit` | `8` | Maximum cached Oracle sessions per connection key; validated between 1 and 1024. |
| `oracle_call_timeout_ms` | `30000` | Per-round-trip OCI call timeout in milliseconds; `0` disables timeout, max 3,600,000 (1 hour). |
| `oracle_stmt_cache_size` | `20` | Per-session OCI statement cache size; `0` disables caching, max 1024. |
| `oracle_lazy_schema_loading` | `true` | Lazily load schema and object metadata on demand. |
| `oracle_use_current_schema` | `true` | Auto-detect `CURRENT_SCHEMA` and resolve unqualified object names. |
| `oracle_metadata_object_types` | `'TABLE,VIEW,SYNONYM,MATERIALIZED VIEW'` | Comma-separated Oracle object types discovered via `ALL_OBJECTS`. |
| `oracle_metadata_result_limit` | `10000` | Bounded metadata discovery limit; `0` uses the bounded default. |

### Views, Materialized Views, and Synonyms

Tables, views, materialized views, and synonyms are automatically discovered via `ALL_OBJECTS` (controlled by `oracle_metadata_object_types`) and can be queried directly from an attached catalog via `SELECT * FROM ora.<view_name>` (or `SELECT * FROM ora.<schema>.<view_name>`) as well as through `oracle_query()`.

### Flashback Queries (`as_of_scn` / `as_of_timestamp`)

`oracle_scan` accepts optional `as_of_scn` (`UBIGINT`) or `as_of_timestamp` (`VARCHAR`) named parameters (mutually exclusive) to execute Oracle Flashback queries:

```sql
SELECT * FROM oracle_scan('ora', 'HR', 'EMPLOYEES', as_of_scn := 12345678);
SELECT * FROM oracle_scan('ora', 'HR', 'EMPLOYEES', as_of_timestamp := '2026-01-01 00:00:00');
```

## Authentication

### Secrets (Recommended)

```sql
CREATE SECRET prod (TYPE oracle, USER 'admin', PASSWORD 'secret', SERVICE 'PROD');
ATTACH '' AS prod_db (TYPE oracle, SECRET prod);
```

`oracle_query` and `oracle_execute` run raw Oracle SQL. Treat those SQL strings as trusted input; do not build them by interpolating untrusted user values.

### Oracle Wallet

```sql
-- Point WALLET_PATH to a local directory containing tnsnames.ora and ewallet.p12.
CREATE SECRET adb_prod (
    TYPE oracle,
    USER 'admin',
    PASSWORD 'secret',
    SERVICE 'adb_alias',
    WALLET_PATH '/path/to/wallet'
);
ATTACH '' AS ora (TYPE oracle, SECRET adb_prod);
```

## Development

**Requirements**: CMake 3.15+, C++17 compiler. No Oracle Instant Client SDK is required to compile the extension or run unit tests; Oracle Instant Client Basic (19c, 21c, or 23ai) is only needed at runtime when running live database integration tests.

```bash
# Build (no Oracle SDK required)
make release

# Run Unit Tests (no Oracle DB or Instant Client required)
make test-unit

# Run Integration Tests (installs Linux Instant Client Basic & launches Oracle container)
make integration
```

## Limitations

- **Transaction Management**: Oracle writes are statement-atomic: a successful DuckDB write statement commits to
  Oracle, a failed write statement rolls back its Oracle work, and Oracle writes are rejected inside explicit DuckDB
  transaction blocks until true cross-system transaction integration exists.
- **Object Discovery**: Tables, views, materialized views, and synonyms are discovered via `ALL_OBJECTS` according to `oracle_metadata_object_types` (default `'TABLE,VIEW,SYNONYM,MATERIALIZED VIEW'`) and can be queried directly as `ora.<object_name>` or via `oracle_query()`.
