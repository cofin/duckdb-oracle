# DuckDB Oracle Extension

This extension allows DuckDB to directly read from and write to Oracle databases. It uses the Oracle Call Interface (OCI) for high-performance data transfer.

> **Disclaimer**: This is a community-maintained extension, not an official product of DuckDB or Oracle.

## Quick Start

**1. Install & Load**

> **Note**: This extension is currently **unsigned**. You must start DuckDB with `-unsigned` to load it.

First, download the extension for your platform (e.g., Linux x86_64) from the [GitHub Release](https://github.com/cofin/duckdb-oracle/releases):

```bash
wget https://github.com/cofin/duckdb-oracle/releases/download/v0.0.3/oracle-v0.0.3-duckdb-v1.4.2-linux-x86_64.duckdb_extension
```

Then start DuckDB and install the local file:

```bash
./duckdb -unsigned
```

```sql
INSTALL 'oracle-v0.0.3-duckdb-v1.4.2-linux-x86_64.duckdb_extension';
LOAD oracle;
```

**2. Attach**

```sql
-- Basic connection
ATTACH 'user/password@//localhost:1521/FREEPDB1' AS ora (TYPE oracle);

-- Using Secrets (Recommended)
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

**4. Write (Insert/Copy)**

```sql
-- Insert from DuckDB query
INSERT INTO ora.target_table SELECT * FROM source_parquet_file;

-- Copy to Oracle
COPY (SELECT * FROM my_table) TO 'target_table' (FORMAT ORACLE, SECRET my_oracle);
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
| `oracle_prefetch_rows` | `1024` | Rows to prefetch per round-trip. |
| `oracle_array_size` | `256` | Batch size for OCI fetch/bind. |
| `oracle_enable_spatial_types` | `true` | Map `SDO_GEOMETRY` to `GEOMETRY` type. |
| `oracle_connection_cache` | `true` | Enable connection pooling. |

## Authentication

### Secrets (Recommended)

```sql
CREATE SECRET prod (TYPE oracle, USER 'admin', PASSWORD 'secret', SERVICE 'PROD');
ATTACH '' AS prod_db (TYPE oracle, SECRET prod);
```

### Oracle Wallet

```sql
-- Point to wallet directory containing tnsnames.ora and ewallet.p12
SELECT oracle_attach_wallet('/path/to/wallet');
ATTACH 'TNS_ALIAS' AS ora (TYPE oracle);
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

- **Transaction Management**: Currently auto-commits read/write operations.
- **Views**: Visible only if present in `ALL_TABLES` (standard behavior).
