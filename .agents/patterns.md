# Project Patterns

> Consolidated learnings and patterns from all flows.
> This file is the single source of truth for project conventions.
> For detailed implementation knowledge, see `.agents/knowledge/`.

## Code Conventions

- C++17 in practice; DuckDB minimum is C++11 (no `std::optional<>` — use `pair<T,bool>` or `Value` instead)
- Follow DuckDB format rules: `make format` and `make tidy-check` before every commit
- `PascalCase` for classes/structs/functions, `snake_case` for variables, `UPPER_SNAKE_CASE` for constants
- Doxygen-style comments for public C++ API functions
- All code in `duckdb::` namespace with `#pragma once` headers

## Architecture Patterns

- **Connection Pooling**: `OracleConnectionManager` (singleton) → `OracleConnectionPool` (per-connection-string) → `OracleConnectionHandle` (RAII). Use `OCI_THREADED` global environment + per-connection session pools with hard timeouts. Never use raw `OCILogon` (hangs indefinitely under load).
- **Thread Safety**: Never share `OCISvcCtx` across threads even though OCI claims thread-safety. Each thread gets its own pooled connection handle.
- **Pushdown**: Filter and projection pushdown enabled by default (`oracle_enable_pushdown = true`). Pushdown adds columns that DuckDB may prune — use explicit column mapping to prevent data corruption.
- **Lazy Schema Loading**: `oracle_lazy_schema_loading` enumerates current schema only + on-demand fallback. Capped at `oracle_metadata_result_limit` (default 10K). Reduced 100K-table attach from 8+ min to <5 sec.
- **Cache Invalidation**: `OracleCatalogState` maintains schema, table, and object caches. Clear ALL caches when state changes (`ClearCaches()`). Test cache behavior explicitly — cache misses cause silent stale data bugs.
- **Write Pipeline**: `CopyFunction` interface: `OracleWriteBind` → `OracleWriteInitGlobal` → `OracleWriteInitLocal` → `OracleWriteSink` → `OracleWriteFinalize`. Per-thread bind buffers allocated at `STANDARD_VECTOR_SIZE`.
- **Version Detection**: `OracleVersionInfo` detects at connect time — `supports_json_type` (21c+), `supports_vector` (23ai+), `supports_vector_serialize` (23.4+). Stored in `OracleCatalogState`, drives query rewriting decisions.

## OCI Gotchas & Warnings

- **Column Shift**: Use `SQLT_STR` (string binding) for NUMBER, DATE, TIMESTAMP during array fetch to avoid memory corruption. Only use native types (`SQLT_INT`, `SQLT_BDOUBLE`, `SQLT_ODT`) in write paths where buffer layouts are fully controlled.
- **Spatial Truncation**: SDO_GEOMETRY WKT conversion via `SDO_UTIL.TO_WKTGEOMETRY` might exceed 4KB `VARCHAR` fetch size. Handle oversized geometries gracefully.
- **LOB Handling**: Use `SQLT_LBI` for LOBs. CLOB/NCLOB falls back to `TO_CHAR`; BLOB/BFILE falls back to `RAWTOHEX`.
- **NULL Handling**: Always use indicator arrays (`sb2`) with array binds. Missing indicators cause silent data corruption.
- **Buffer Alignment**: Validate buffer sizes for type conversions to prevent overruns. DuckDB `DataChunk` vectors may not match OCI expected alignment.

## Security Rules

- **Never** concatenate user input into `oracle_execute` SQL strings — SQL injection risk
- All credentials via DuckDB `SecretManager` (`CREATE SECRET ... TYPE oracle`)
- Limit Oracle account privileges in documentation and examples
- `oracle_execute()` doesn't use prepared statements — document this limitation clearly

## Testing Patterns

- Unit tests in `test/unit_tests/` using `sqllogictest` with `require oracle` — no Oracle container needed
- Integration tests in `test/integration_tests/` require `gvenzl/oracle-free:23-slim` container
- Connection string placeholder: `${ORACLE_CONNECTION_STRING}`
- Setup data: `test/integration_tests/init_sql/01_setup.sql`
- Separate unit from integration tests in CI for speed (unit runs on every PR, integration on merge)

## CI/CD Patterns

- DuckDB version and CI tools version must match exactly (currently v1.5.1)
- `GITHUB_TOKEN` cannot modify workflow files — need PAT with `workflows` permission for CI changes
- Multi-platform builds: Linux x86_64/aarch64, macOS arm64, Windows x86_64
- Excluded: WebAssembly (no OCI), macOS Intel (no Instant Client)
- Release tags must be semver format: `v1.0.0`
- `description.yml` defines community extension metadata

## Context for AI Assistants

- When generating C++ code, use `duckdb::unique_ptr`, DuckDB `Value` / `DataChunk` / `Vector` types
- The extension is a DuckDB "storage extension" — it implements `StorageExtension`, `Catalog`, `TransactionManager`
- Settings are registered in `oracle_extension.cpp` via `ExtensionUtil::RegisterSetting`
- The `oracle_execute` function is a table function, not a prepared statement API
