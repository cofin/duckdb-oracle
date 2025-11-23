# Oracle

> **Community Extension**: This is an unofficial, community-maintained extension. It is not affiliated with or endorsed by DuckDB Labs or Oracle Corporation.

This repository provides a native Oracle extension for DuckDB. It allows:

- Attaching an Oracle database with `ATTACH ... (TYPE oracle)` and querying tables directly.
- Table/query functions: `oracle_scan`, `oracle_query`, `oracle_execute`.
- Wallet helper: `oracle_attach_wallet`.
- Cache maintenance: `oracle_clear_cache`.
- Smart schema resolution with automatic current schema detection.
- Scalable metadata enumeration for large schemas (100K+ tables).

## Platform Support

> This extension uses [Oracle Instant Client](https://www.oracle.com/database/technologies/instant-client/downloads.html) and supports the following platforms: `linux_amd64`, `linux_arm64`, `osx_amd64` (Intel), `osx_arm64` (Apple Silicon M1/M2/M3), and `windows_amd64`.
>
> **WebAssembly (WASM) is not supported** as Oracle Instant Client requires native system libraries that cannot run in the browser sandbox. The builds `wasm_mvp`, `wasm_eh`, and `wasm_threads` are excluded from distribution.

## Quickstart (DuckDB ≥ v1.4.1)

```sql
-- one-time install from your extension repo (unsigned example)
INSTALL oracle;
LOAD oracle;

-- attach an Oracle database (read-only)
ATTACH 'user/password@//host:1521/service' AS ora (TYPE oracle);

-- query a table
SELECT * FROM ora.HR.EMPLOYEES WHERE employee_id = 101;
```

## Authentication Methods

The Oracle extension supports three authentication methods:

### 1. Connection String (Quick Start)

```sql
ATTACH 'user/password@host:1521/service' AS ora (TYPE oracle);
```

**Best for:** Development, quick testing

### 2. Secret Manager (Recommended for Scripts)

Store credentials in secrets, specify connection details in ATTACH:

```sql
-- Create a secret with just credentials
CREATE SECRET oracle_creds (
    TYPE oracle,
    USER 'scott',
    PASSWORD 'tiger'
);

-- Attach using EZConnect syntax + secret
ATTACH 'localhost:1521/XEPDB1' AS ora (TYPE oracle, SECRET oracle_creds);

-- Or use TNS alias (requires tnsnames.ora)
ATTACH 'PRODDB' AS prod (TYPE oracle, SECRET oracle_creds);

-- Multiple environments with different credentials
CREATE SECRET dev_creds (TYPE oracle, USER 'dev_user', PASSWORD 'dev_pass');
CREATE SECRET prod_creds (TYPE oracle, USER 'prod_user', PASSWORD 'prod_pass');

ATTACH 'dev.example.com:1521/DEVDB' AS dev (TYPE oracle, SECRET dev_creds);
ATTACH 'prod.example.com:1521/PRODDB' AS prod (TYPE oracle, SECRET prod_creds);
```

**Best for:** CI/CD pipelines, scripts with multiple databases, when you need credential reusability without embedding passwords

**Secret Parameters:**

- `HOST` (optional, default: `localhost`) - Oracle server hostname
- `PORT` (optional, default: `1521`) - Oracle listener port
- `SERVICE` or `DATABASE` (required) - Oracle service name
- `USER` (required) - Oracle username
- `PASSWORD` (required) - Oracle password
- `WALLET_PATH` (optional) - Path to Oracle Wallet directory

### 3. Oracle Wallet Integration

Oracle Wallet securely stores credentials and connection strings. The extension can leverage your existing Oracle Wallet configuration.

```sql
-- Set wallet location (contains ewallet.p12, tnsnames.ora, sqlnet.ora)
SELECT oracle_attach_wallet('/path/to/wallet');

-- Attach using TNS alias from wallet (credentials auto-loaded from wallet)
ATTACH 'PRODDB_HIGH' AS prod (TYPE oracle);

-- Example: Oracle Autonomous Database
SELECT oracle_attach_wallet('/home/app/wallet_autonomous');
ATTACH 'myatp_high' AS atp (TYPE oracle);
ATTACH 'myatp_medium' AS atp_medium (TYPE oracle);

-- Wallet + explicit username (useful when wallet has multiple schemas)
ATTACH 'admin@PRODDB_HIGH' AS prod_admin (TYPE oracle);

-- Combine wallet with secret for additional flexibility
CREATE SECRET app_user (TYPE oracle, USER 'app_schema', PASSWORD 'app_pass');
ATTACH 'PRODDB_HIGH' AS prod (TYPE oracle, SECRET app_user);
```

**Best for:**

- Production deployments with Oracle Wallet
- Oracle Autonomous Database (ATP/ADW)
- Enterprise environments with centralized credential management
- When using Oracle TNS aliases for connection failover/load balancing

**How it works:**

1. `oracle_attach_wallet()` sets the `TNS_ADMIN` environment variable
2. Oracle Instant Client reads `tnsnames.ora` for connection aliases
3. `sqlnet.ora` configures wallet location
4. Credentials are automatically loaded from `ewallet.p12` or `cwallet.sso`

### Functions

| Function | Description |
| --- | --- |
| `oracle_query(conn, sql)` | Runs arbitrary SQL/PLSQL against Oracle (returns result set). |
| `oracle_execute(conn, sql)` | Executes arbitrary SQL without expecting result set (DDL, DML, PL/SQL blocks). |
| `oracle_scan(conn, schema, table)` | Scans a table (`SELECT * FROM schema.table`). |
| `oracle_attach_wallet(path)` | Sets `TNS_ADMIN` to the wallet directory (for Autonomous DB etc.). |
| `oracle_clear_cache()` | Clears cached Oracle metadata/connections for attached DBs. |

**Example: oracle_execute()**

```sql
-- Execute stored procedure
SELECT oracle_execute('user/pass@db', 'BEGIN hr_pkg.update_salaries(1.05); END;');

-- Run DDL
SELECT oracle_execute('user/pass@db', 'CREATE INDEX idx_emp_dept ON employees(department_id)');

-- Execute DML
SELECT oracle_execute('user/pass@db', 'DELETE FROM temp_table WHERE created < SYSDATE - 7');
-- Returns: "Statement executed successfully (N rows affected)"
```

**Security Warning**: `oracle_execute()` does not use prepared statements. Never concatenate user input into SQL strings. Use DuckDB SecretManager for credentials and validate all inputs to prevent SQL injection.

### Attach options (TYPE oracle)

```
ATTACH 'user/password@//host:port/service' AS ora (
  TYPE oracle,
  enable_pushdown=true,
  prefetch_rows=200,
  array_size=256,
  read_only
);
```

Options map to settings below; unknown options are ignored.

### Extension settings (set at runtime)

| Setting | Default | What it does |
| --- | --- | --- |
| `oracle_enable_pushdown` | `false` | Push down filters/projection into Oracle. |
| `oracle_enable_spatial_conversion` | `true` | Automatically convert SDO_GEOMETRY to WKT strings. |
| `oracle_lazy_schema_loading` | `true` | Load only current schema by default (faster attach for large schemas). |
| `oracle_metadata_object_types` | `TABLE,VIEW,SYNONYM,MATERIALIZED VIEW` | Object types to enumerate (comma-separated). |
| `oracle_metadata_result_limit` | `10000` | Max objects enumerated (0=unlimited, beyond limit accessible via on-demand loading). |
| `oracle_use_current_schema` | `true` | Resolve unqualified table names to current schema first. |
| `oracle_prefetch_rows` | `200` | OCI prefetch rows per round-trip. |
| `oracle_prefetch_memory` | `0` | OCI prefetch memory in bytes (0 = auto). |
| `oracle_array_size` | `256` | Rows fetched per OCI iteration. |
| `oracle_connection_cache` | `true` | Reuse OCI connections inside an attached DB. |
| `oracle_connection_limit` | `8` | Max cached connections. |
| `oracle_debug_show_queries` | `false` | Log generated Oracle SQL. |

Example:

```sql
SET oracle_enable_pushdown=true;
SET oracle_prefetch_rows=500;
```

## Advanced Features

### Schema Resolution and Current Schema Context

The Oracle extension automatically detects the current schema when you attach and intelligently resolves unqualified table names, matching Oracle's native behavior.

```sql
-- Connected as HR user
ATTACH 'hr/password@host:1521/service' AS ora (TYPE oracle);

-- Current schema auto-detected: HR
-- Unqualified table names resolve to current schema first
SELECT * FROM ora.EMPLOYEES;  -- Resolves to ora.HR.EMPLOYEES

-- Explicit qualification still works
SELECT * FROM ora.HR.EMPLOYEES;
SELECT * FROM ora.SYS.DUAL;

-- Toggle behavior if needed
SET oracle_use_current_schema = false;  -- Require explicit schema qualification
```

### Metadata Scalability for Large Schemas

For Oracle databases with thousands of tables, the extension uses lazy schema loading to provide instant attach times:

```sql
-- Lazy loading enabled by default (recommended for large schemas)
ATTACH 'user/pass@large_db' AS ora (TYPE oracle);
-- Attaches in <5 seconds regardless of schema size

-- Only current schema enumerated, but all tables accessible
SELECT * FROM ora.CURRENT_SCHEMA.TABLE_1;      -- In enumerated list
SELECT * FROM ora.CURRENT_SCHEMA.TABLE_99999;  -- Beyond limit, loaded on-demand

-- Control object types enumerated
SET oracle_metadata_object_types = 'TABLE,VIEW';  -- Exclude synonyms/materialized views

-- Adjust enumeration limit for better autocomplete
SET oracle_metadata_result_limit = 50000;  -- Enumerate more objects (slower attach)

-- Disable lazy loading for full schema visibility (slow for large schemas)
SET oracle_lazy_schema_loading = false;
ATTACH 'user/pass@db' AS ora_full (TYPE oracle);
```

**How it works:**

- First 10,000 objects (tables/views/synonyms) enumerated for autocomplete/discovery
- Tables beyond limit still accessible via on-demand loading
- Warning logged when limit reached, but no functionality lost
- Set limit to 0 for unlimited enumeration (not recommended for 100K+ schemas)

### Synonym Resolution

The extension automatically resolves Oracle synonyms to their target tables:

```sql
-- Private and public synonyms work transparently
SELECT * FROM ora.SCHEMA.MY_SYNONYM;   -- Resolves to target table
SELECT * FROM ora.SCHEMA.PUB_SYNONYM;  -- Public synonym resolution

-- Priority: private synonyms > public synonyms
```

### Wallet / Autonomous Database

```sql
CALL oracle_attach_wallet('/path/to/wallet_dir');
-- then use ezconnect or tnsnames entries from that wallet
ATTACH 'user/pass@myadb_tp' AS adb (TYPE oracle);
```

## Working with Spatial Data

The Oracle extension automatically converts Oracle Spatial `SDO_GEOMETRY` types to WKT (Well-Known Text) strings, enabling spatial analysis with DuckDB Spatial functions.

### Prerequisites

- Oracle Standard Edition or higher (requires Oracle JVM for SDO_UTIL functions)
- Oracle Spatial enabled in your database
- For spatial analysis: DuckDB Spatial extension (`INSTALL spatial; LOAD spatial;`)

### Quick Example

```sql
-- Attach Oracle database with spatial data
ATTACH 'user/password@host:1521/service' AS ora (TYPE oracle);

-- Query spatial columns (returns WKT strings)
SELECT parcel_id, geometry FROM ora.gis.parcels LIMIT 5;
-- Result: geometry column contains "POINT(1 2)", "POLYGON((0 0, ...))", etc.

-- Use with DuckDB Spatial extension for analysis
LOAD spatial;
SELECT
    parcel_id,
    ST_Area(ST_GeomFromText(geometry)) AS area,
    ST_GeometryType(ST_GeomFromText(geometry)) AS geom_type
FROM ora.gis.parcels
WHERE ST_Area(ST_GeomFromText(geometry)) > 1000;
```

### Supported Oracle Spatial Types

The extension detects and converts the following Oracle Spatial types:

- `SDO_GEOMETRY` (standard Oracle Spatial type)
- `MDSYS.SDO_GEOMETRY` (fully qualified schema name)

All spatial types are automatically converted to WKT format using Oracle's `SDO_UTIL.TO_WKTGEOMETRY()` function.

### Configuration

```sql
-- Disable automatic spatial conversion (returns raw SDO_GEOMETRY representation)
SET oracle_enable_spatial_conversion = false;

-- Enable spatial conversion (default behavior)
SET oracle_enable_spatial_conversion = true;

-- Configure during ATTACH
ATTACH 'user/pass@host:1521/service' AS ora (
    TYPE oracle,
    enable_spatial_conversion = true
);
```

### Spatial Data Workflow

#### Step 1: Attach Oracle database

```sql
ATTACH 'gis_user/password@gis-server:1521/GISDB' AS gis (TYPE oracle);
```

#### Step 2: Explore spatial tables

```sql
-- List tables with geometry columns
SELECT table_schema, table_name, column_name, data_type
FROM gis.information_schema.columns
WHERE data_type LIKE '%GEOMETRY%';
```

#### Step 3: Query spatial data

```sql
-- Spatial data is returned as WKT strings
SELECT * FROM gis.spatial_schema.parcels LIMIT 10;
```

#### Step 4: Perform spatial analysis

```sql
LOAD spatial;

-- Calculate areas
SELECT
    parcel_id,
    owner_name,
    ST_Area(ST_GeomFromText(geometry)) AS area_sqm
FROM gis.spatial_schema.parcels
ORDER BY area_sqm DESC
LIMIT 100;

-- Spatial filtering
SELECT COUNT(*)
FROM gis.spatial_schema.buildings
WHERE ST_Within(
    ST_GeomFromText(location),
    ST_GeomFromText('POLYGON((...))')  -- bounding box
);

-- Export to GeoParquet
INSTALL spatial;
LOAD spatial;

COPY (
    SELECT
        parcel_id,
        ST_GeomFromText(geometry) AS geometry,
        land_use,
        assessed_value
    FROM gis.spatial_schema.parcels
) TO 'parcels.parquet' (FORMAT PARQUET);
```

### Limitations

- **Oracle Express Edition**: Not supported (requires Oracle JVM for WKT conversion functions)
- **Read-only**: No INSERT/UPDATE of spatial data (extension is read-only overall)
- **Advanced types**: `SDO_TOPO_GEOMETRY`, `SDO_GEORASTER` not supported
- **Spatial indexes**: Oracle spatial indexes are not pushed down (queries run without index hints)
- **Large geometries**: Complex geometries with >4KB WKT representation may require Oracle-side simplification

### Troubleshooting Spatial Data

#### Error: "ORA-13199: SDO_UTIL function not found"

This means Oracle Spatial or JVM is not available:

- Check Oracle edition: `SELECT * FROM v$version;` (must not be Express)
- Verify Oracle Spatial: `SELECT comp_name, status FROM dba_registry WHERE comp_name = 'Spatial';`
- Verify JVM: `SELECT comp_name, status FROM dba_registry WHERE comp_name LIKE '%Java%';`

#### Geometry column shows raw object representation

Ensure `oracle_enable_spatial_conversion = true` (default):

```sql
SELECT current_setting('oracle_enable_spatial_conversion');
-- Should return: true
```

#### WKT strings appear truncated

For very large geometries, use Oracle-side simplification:

```sql
-- In Oracle, simplify geometry before querying
CREATE VIEW simplified_parcels AS
SELECT
    parcel_id,
    SDO_UTIL.SIMPLIFY(geometry, 0.5) AS geometry
FROM parcels;

-- Query from DuckDB
SELECT * FROM ora.spatial_schema.simplified_parcels;
```

## Build from source

Requirements: CMake ≥3.10, C++17 compiler, vcpkg (for OpenSSL), Oracle Instant Client (Basic + SDK).

```sh
# install Instant Client and set ORACLE_HOME (contains sdk/include/oci.h, lib/libclntsh.so)
export ORACLE_HOME=/opt/oracle/instantclient_23_6

# bootstrap vcpkg (not vendored)
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
export VCPKG_TOOLCHAIN_PATH="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake"

# build
make

# run tests (SQL suite)
make test
```

Outputs (release):

- `build/release/duckdb` — DuckDB shell with oracle extension preloaded
- `build/release/test/unittest` — DuckDB tests (extension linked)
- `build/release/extension/oracle/oracle.duckdb_extension` — loadable binary

## Troubleshooting

- **OCI not found**: ensure `ORACLE_HOME` points to the Instant Client root (`sdk/include/oci.h`, `lib/libclntsh.so`). The CI helper defaults to `/usr/share/oracle/instantclient_23_26`.
- **libaio missing on Ubuntu 24.04**: install `libaio1t64` (or `libaio-dev`) and ensure `libaio.so.1` exists.
- **Pushdown issues**: set `SET oracle_debug_show_queries=true` to see generated SQL; disable with `SET oracle_enable_pushdown=false`.
- **Stale metadata/connection**: `SELECT oracle_clear_cache();`

## Limits (current)

- Read-only; write/COPY not implemented.
- Views treated as tables only if present in `ALL_TABLES`.
- No transaction management; auto-commit reads.

## License

Apache-2.0 (DuckDB extension template heritage). Instant Client is governed by Oracle’s license; users must obtain it separately.
