# DuckDB Oracle Extension

This extension allows DuckDB to directly read from and write to Oracle databases. It uses the Oracle Call Interface (OCI) for high-performance data transfer.

> **Disclaimer**: This is a community-maintained extension, not an official product of DuckDB or Oracle.

## Quick Start

**1. Install & Load**

> **Note**: This extension is currently **unsigned**. You must start DuckDB with `-unsigned` to load it.
>
> **Supported DuckDB version**: v1.5.4 — see [Compatibility Matrix](docs/COMPATIBILITY.md) for details.

```bash
./duckdb -unsigned
```

```sql
-- Configure custom repository (hosted on GitHub Pages)
SET custom_extension_repository = 'https://cofin.github.io/duckdb-oracle';

-- Install and load
INSTALL oracle;
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
| `oracle_metadata_result_limit` | `10000` | Bounded metadata discovery limit; `0` uses the bounded default. |

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

**Requirements**: CMake 3.10+, C++17, Oracle Instant Client (Basic + SDK).

```bash
# Set ORACLE_HOME to your Instant Client directory
export ORACLE_HOME=/opt/oracle/instantclient_23_6

# Build
make

# Run Tests (Unit + Integration)
make test
make integration
```

## Limitations

- **Transaction Management**: Oracle writes are statement-atomic: a successful DuckDB write statement commits to
  Oracle, a failed write statement rolls back its Oracle work, and Oracle writes are rejected inside explicit DuckDB
  transaction blocks until true cross-system transaction integration exists.
- **Views**: Visible only if present in `ALL_TABLES` (standard behavior).
