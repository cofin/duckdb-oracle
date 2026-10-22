# DuckDB Oracle Extension

The `oracle` extension connects DuckDB directly to Oracle databases for reading and writing tables. It supports native catalog attachment, filter and projection pushdown, spatial geometry, vector embeddings, and batched array reads and writes.

**Supported DuckDB version**: v2.0.0 — see [Compatibility](docs/COMPATIBILITY.md) for version history.

## Installation

### Community Extensions

```sql
INSTALL oracle FROM community;
LOAD oracle;
```

### Custom Extension Repository

To install directly from this repository's release channel, start DuckDB with `duckdb -unsigned`:

```sql
SET custom_extension_repository = 'https://cofin.github.io/duckdb-oracle';
INSTALL oracle;
LOAD oracle;
```

### Local Archive

Download the archive for your platform from [GitHub Releases](https://github.com/cofin/duckdb-oracle/releases), extract `oracle.duckdb_extension`, and install it from disk:

```sql
INSTALL '/path/to/oracle.duckdb_extension';
LOAD oracle;
```

### Oracle Client Requirement

Installing and loading the extension requires no Oracle libraries on the host. When opening a database connection, the extension loads Oracle Instant Client Basic (19c, 21c, or 23ai) on demand from `OCI_LIB_DIR`, `ORACLE_HOME`, or the system library path (`LD_LIBRARY_PATH` on Linux, `DYLD_LIBRARY_PATH` on macOS, `PATH` on Windows).

## Connecting

Configure credentials with a DuckDB secret and attach the database as a catalog:

```sql
CREATE SECRET my_oracle (
    TYPE oracle,
    HOST 'localhost',
    PORT 1521,
    SERVICE 'FREEPDB1',
    USER 'scott',
    PASSWORD 'tiger'
);

ATTACH '' AS ora (TYPE oracle, SECRET my_oracle);
```

### Oracle Wallet & Autonomous Database

Any connection—including Oracle Autonomous Database and TLS-secured instances—can authenticate with an Oracle Wallet directory containing `tnsnames.ora` and wallet files:

```sql
CREATE SECRET wallet_oracle (
    TYPE oracle,
    SERVICE 'myadb_high',
    USER 'admin',
    PASSWORD 'secret',
    WALLET_PATH '/path/to/wallet'
);

ATTACH '' AS ora_cloud (TYPE oracle, SECRET wallet_oracle);
```

External password store wallets (`cwallet.sso`) are also supported without specifying a username or password in the secret.

## Querying & Writing

### Reading Tables, Views, and Synonyms

Attached schemas, tables, views, materialized views, and synonyms can be queried like native DuckDB tables. Filters and column projections are pushed down to Oracle automatically:

```sql
-- Query the default schema
SELECT empno, ename, sal
FROM ora.emp
WHERE deptno = 10;

-- Join Oracle tables with local files
SELECT e.ename, d.dname
FROM ora.scott.emp e
JOIN 'departments.parquet' d ON e.deptno = d.deptno;
```

### Direct SQL Queries & Commands

Use `oracle_query` to run custom SQL directly on Oracle, and `oracle_execute` to run DDL or DML statements:

```sql
SELECT * FROM oracle_query('ora', 'SELECT empno, ename FROM scott.emp WHERE sal > 2000');

SELECT oracle_execute('ora', 'TRUNCATE TABLE scott.staging_emp');
```

> `oracle_query` and `oracle_execute` run raw SQL on the remote database. Treat the query string as trusted input.

### Flashback Queries

Query historical snapshots by System Change Number (SCN) or timestamp using `oracle_scan`:

```sql
SELECT * FROM oracle_scan('ora', 'SCOTT', 'EMP', as_of_scn := 12345678);
SELECT * FROM oracle_scan('ora', 'SCOTT', 'EMP', as_of_timestamp := '2026-01-01 00:00:00');
```

### Spatial & Vector Data

Oracle `MDSYS.SDO_GEOMETRY` columns map to DuckDB `GEOMETRY`, and Oracle 23ai `VECTOR` columns map to `LIST(FLOAT)`:

```sql
-- Spatial query using DuckDB spatial functions
SELECT id, ST_Area(geom) FROM ora.gis_parcels;

-- Vector embeddings from Oracle 23ai
SELECT id, embedding FROM ora.document_chunks LIMIT 10;
```

### Writing Data

Insert rows from any DuckDB table, view, or file into an attached Oracle table using batched array binding:

```sql
INSERT INTO ora.scott.target_table
SELECT * FROM 'source_data.parquet';
```

## Configuration

| Setting | Default | Description |
|---------|---------|-------------|
| `oracle_enable_pushdown` | `true` | Push filters and column projections to Oracle. |
| `oracle_prefetch_rows` | `1024` | Rows prefetched per network round-trip (`1`–`1000000`). |
| `oracle_prefetch_memory` | `0` | Prefetch buffer size in bytes (`0` lets the client choose, max `1 GiB`). |
| `oracle_array_size` | `256` | Batch size for array fetches and writes (`1`–`2048`). |
| `oracle_enable_spatial_types` | `true` | Map `SDO_GEOMETRY` columns to DuckDB `GEOMETRY` (false returns WKT text). |
| `oracle_connection_cache` | `true` | Pool and reuse Oracle sessions across queries. |
| `oracle_connection_limit` | `8` | Maximum pooled sessions per connection target (`1`–`1024`). |
| `oracle_call_timeout_ms` | `30000` | Per-call network timeout in milliseconds (`0` disables timeout, max `1 hour`). |
| `oracle_stmt_cache_size` | `20` | Prepared statements cached per session (`0` disables caching, max `1024`). |
| `oracle_lazy_schema_loading` | `true` | Load schema and table metadata on demand rather than upfront. |
| `oracle_use_current_schema` | `true` | Resolve unqualified table names against the session's `CURRENT_SCHEMA`. |
| `oracle_metadata_object_types` | `'TABLE,VIEW,SYNONYM,MATERIALIZED VIEW'` | Comma-separated object types exposed in the catalog. |
| `oracle_metadata_result_limit` | `10000` | Maximum catalog objects returned per discovery query. |

## Development

Building the extension and running unit tests require only CMake (3.15+) and a C++17 compiler—no Oracle client headers or libraries are needed at build time.

```bash
# Build the release binary
make release

# Run offline unit tests
make test-unit

# Run live integration tests (requires Docker or Podman)
make integration
```

## Notes

- **Transactions**: Write statements (`INSERT INTO`) are atomic per statement—each statement commits on completion or rolls back on failure. Writing to Oracle tables inside an explicit DuckDB `BEGIN ... COMMIT` block is not supported.
- **Disclaimer**: This is an independent community-maintained extension and is not affiliated with or endorsed by Oracle or DuckDB Labs.
