# Changelog

## [1.0.0] - 2025-11-24

### Added
- **Array Fetch**: Implemented `OCIDefineArrayOfStruct` streaming for high-performance data retrieval. Tunable via `oracle_array_size`.
- **Connection Pooling**: Added `OracleConnectionManager` with `OCI_THREADED` support, session pooling, and automatic timeouts.
- **Platform Support**: Verified support for Linux (AMD64/ARM64), macOS (ARM64), and Windows (AMD64).
- **Schema Resolution**: Automatic detection of `CURRENT_SCHEMA` context and smart resolution of unqualified table names.
- **Lazy Loading**: Scalable metadata enumeration for databases with 100K+ tables.
- **Secret Management**: Integration with DuckDB Secret Manager (`CREATE SECRET ... TYPE oracle`).
- **Wallet Support**: `oracle_attach_wallet()` for TNS/Wallet-based connections (e.g., Autonomous DB).
- **Integration Testing**: Robust container-based integration test suite (`scripts/test_integration.sh`) with CI/CD support.

### Fixed
- **Cross-Platform Builds**: Fixed Makefile and CMake logic for macOS (`libaio` exclusion) and Windows (`oci.lib` linking).
- **Stability**: Resolved memory corruption issues ("invalid next size") in buffer allocation.
- **Correctness**: Fixed `ORA-01007` errors caused by duplicate column definitions in pushdown scenarios.
- **CI Noise**: Suppressed verbose container logs in integration tests by default.

### Changed
- **Architecture**: Replaced shared `OCILogon` with explicit `OCIServerAttach`/`OCISessionBegin` lifecycle management.
