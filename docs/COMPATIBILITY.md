# Compatibility

This document tracks compatibility between the `oracle` extension, DuckDB releases, and Oracle Instant Client libraries.

## Oracle Client Runtime

Installing and loading the extension (`LOAD oracle;`) requires no Oracle libraries on the host. When opening a database connection, the extension loads Oracle Instant Client Basic (19c, 21c, or 23ai) on demand.

### Library Search Order

The runtime loader searches for `libclntsh.so` (Linux), `libclntsh.dylib` (macOS), or `oci.dll` (Windows) in the following order:

1. `OCI_LIB_DIR` (`$OCI_LIB_DIR/<library>`)
2. `ORACLE_HOME` (`$ORACLE_HOME/lib/<library>` and `$ORACLE_HOME/<library>`)
3. System library path (`LD_LIBRARY_PATH` or `ldconfig` on Linux, `DYLD_LIBRARY_PATH` on macOS, `PATH` on Windows)
4. Common installation directories (`./oracle_sdk/instantclient_*`, `/usr/lib/oracle/*/client64/lib`, `/opt/oracle/instantclient_*`)

### Supported Oracle Databases

Because connections use standard Oracle Call Interface client libraries, database version support follows Oracle's client-server interoperability matrix:
- **Oracle Instant Client 23ai**: Connects to Oracle Database 19c, 21c, and 23ai.
- **Oracle Instant Client 21c**: Connects to Oracle Database 12.1, 12.2, 18c, 19c, and 21c.
- **Oracle Instant Client 19c**: Connects to Oracle Database 11.2.0.4, 12.1, 12.2, 18c, 19c, and 21c.

Local instances, on-premises deployments, cloud databases, and Oracle Autonomous Database (via wallet or TLS) are all supported.

## DuckDB Version Compatibility

| DuckDB Version | Extension Version | Status | Tested Date | Notes |
|----------------|-------------------|--------|-------------|-------|
| v1.4.1         | v0.1.0            | ✅ Compatible | 2025-11-24 | Initial release |
| v1.4.3         | v0.1.0 (dev)      | ✅ Compatible | 2025-12-28 | DuckDB 1.4.3 upgrade |
| v1.5.1         | v0.2.0            | ✅ Compatible | 2026-04-03 | Spatial geometry, JSON type mapping, secret manager |
| v1.5.4         | v0.2.1            | ✅ Compatible | 2026-06-23 | DuckDB v1.5.4 upgrade |
| v1.5.5         | v0.2.2            | ✅ Compatible | 2026-07-22 | DuckDB v1.5.5 upgrade |
| v1.5.6         | v0.2.3            | ✅ Compatible | 2026-10-11 | DuckDB v1.5.6 upgrade |
| v2.0.0         | v0.3.0            | ✅ Compatible | 2026-10-21 | DuckDB v2.0.0 upgrade, on-demand client loading, optimizer callbacks |
