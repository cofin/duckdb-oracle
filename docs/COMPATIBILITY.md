# DuckDB Compatibility Matrix

This document tracks compatibility between the DuckDB Oracle extension (`duckdb-oracle`), DuckDB versions, and Oracle Instant Client runtimes.

## DuckDB Version Compatibility

| DuckDB Version | Extension Version | Status | Tested Date | Notes |
|----------------|-------------------|--------|-------------|-------|
| v1.4.1         | v0.1.0            | ✅ Compatible | 2025-11-24 | Initial Release |
| v1.4.3         | v0.1.0 (dev)      | ✅ Compatible | 2025-12-28 | Upgrade to DuckDB 1.4.3 LTS |
| v1.5.1         | v0.2.0            | ✅ Compatible | 2026-04-03 | GEOMETRY with CRS, JSON type mapping, dead code cleanup |
| v1.5.4         | v0.2.1            | ✅ Compatible | 2026-06-23 | DuckDB v1.5.4 upgrade |
| v1.5.5         | v0.2.2            | ✅ Compatible | 2026-07-22 | DuckDB v1.5.5 upgrade |
| v1.5.6         | v0.2.3            | ✅ Compatible | 2026-10-11 | DuckDB v1.5.6 upgrade |
| v2.0.0         | v0.3.0            | ✅ Compatible | 2026-10-21 | DuckDB v2.0.0 LTS (`Cyanoptera`), optimizer callbacks (`get_bind_info`, `cardinality`, `to_string`), lazy runtime OCI loading (`dlopen`/`LoadLibrary`), Community Extensions readiness |

## Oracle Instant Client Runtime Compatibility

Starting in `v0.3.0`, `duckdb-oracle` compiles with in-tree OCI C ABI declarations (`src/include/oracle_oci_api.hpp`) and zero build-time `DT_NEEDED` dependency on `libclntsh`. When opening an Oracle connection (`ATTACH`, `oracle_scan`, `oracle_query`, `oracle_execute`), the extension dynamically loads Oracle Instant Client at runtime across:

- **Oracle Instant Client 23ai** (`libclntsh.so.23.1` / `libclntsh.dylib` / `oci.dll`)
- **Oracle Instant Client 21c** (`libclntsh.so.21.1` / `libclntsh.dylib` / `oci.dll`)
- **Oracle Instant Client 19c** (`libclntsh.so.19.1` / `libclntsh.dylib` / `oci.dll`)

### Library Search Order

1. `OCI_LIB_DIR` environment variable (`$OCI_LIB_DIR/<library>`)
2. `ORACLE_HOME` environment variable (`$ORACLE_HOME/lib/<library>` and `$ORACLE_HOME/<library>`)
3. System dynamic loader search path (`LD_LIBRARY_PATH` / `/etc/ld.so.conf` on Linux, `DYLD_LIBRARY_PATH` on macOS, `PATH` on Windows)
4. Standard local install directories (`./oracle_sdk/instantclient_*`, `/usr/lib/oracle/*/client64/lib`, `/opt/oracle/instantclient_*`)

## Publishing to DuckDB Community Extensions (`duckdb/community-extensions`)

Because `LOAD oracle;` requires zero external Oracle libraries at load time, `duckdb-oracle` is compatible with the automated `duckdb/community-extensions` build and documentation verification pipeline (`INSTALL oracle FROM community; LOAD oracle;`):

1. Fork `https://github.com/duckdb/community-extensions`.
2. Copy [`description.yml`](../description.yml) to `extensions/oracle/description.yml` in the `community-extensions` repository.
3. Replace `repo.ref: HEAD` in `extensions/oracle/description.yml` with the full 40-character commit SHA of the `cofin/duckdb-oracle` release commit.
4. Open a pull request against `duckdb/community-extensions` (`main` branch).
