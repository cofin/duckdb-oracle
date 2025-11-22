# Oracle

This repository provides a native Oracle extension for DuckDB. It allows:

- Attaching an Oracle database with `ATTACH ... (TYPE oracle)` and querying tables directly.
- Table/query functions: `oracle_scan`, `oracle_query`.
- Wallet helper: `oracle_attach_wallet`.
- Cache maintenance: `oracle_clear_cache`.

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

### Functions

| Function | Description |
| --- | --- |
| `oracle_query(conn, sql)` | Runs arbitrary SQL/PLSQL against Oracle. |
| `oracle_scan(conn, schema, table)` | Scans a table (`SELECT * FROM schema.table`). |
| `oracle_attach_wallet(path)` | Sets `TNS_ADMIN` to the wallet directory (for Autonomous DB etc.). |
| `oracle_clear_cache()` | Clears cached Oracle metadata/connections for attached DBs. |

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

### Wallet / Autonomous Database

```sql
CALL oracle_attach_wallet('/path/to/wallet_dir');
-- then use ezconnect or tnsnames entries from that wallet
ATTACH 'user/pass@myadb_tp' AS adb (TYPE oracle);
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
