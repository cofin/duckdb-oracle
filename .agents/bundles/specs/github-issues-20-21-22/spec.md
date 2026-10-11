---
kind: spec
flow_id: github-issues-20-21-22
title: "Resolve Open GitHub Issues #20, #21, and #22 (Catalog Introspection, Extension Distribution & Windows Installation)"
type: bugfix
status: completed
created_at: 2026-04-12T22:15:00Z
updated_at: 2026-04-12T22:55:00Z
summary: "Refactor OracleCatalog and OracleSchemaEntry to inherit directly from Catalog and SchemaCatalogEntry with cache-generation synchronization and resilient metadata scanning (#21), and harden unsigned extension release packaging, GitHub Pages deployment, and Windows/local installation workflows (#20, #22)."
verification_strategy: "unit_and_integration"
total_phases: 1
current_phase: 1
total_tasks: 3
completed_tasks: 3
children:
  - tasks/1.1.md
  - tasks/1.2.md
  - tasks/1.3.md
---

# Specification: Resolve Open GitHub Issues #20, #21, and #22

## 1. Feasibility & Root-Cause Assessment of the 3 Open Issues

All three open GitHub issues (`#20`, `#21`, `#22`) **can and should be supported** directly in this repository:

### Issue #21 — `Error encountered while executing duckdb_databases()` (and `duckdb_schemas()`, `duckdb_tables()`, `duckdb_columns()`, `SHOW TABLES`)
- **Reported Symptom**: Running `FROM duckdb_databases();` failed with `INTERNAL Error: Internal system catalog does not have storage` (`DuckCatalog::InMemory()` calling `db.GetStorageManager()`), and users also hit empty results or failures when inspecting schemas/tables via `duckdb_schemas` / `duckdb_tables` / `duckdb_columns`.
- **Root Causes in `src/include/oracle_catalog.hpp`, `src/storage/oracle_schema_entry.cpp`, and `src/storage/oracle_catalog.cpp`**:
  1. **Direct `DuckCatalog` / `DuckSchemaEntry` inheritance violates DuckDB catalog invariants (`D_ASSERT(catalog_p.IsDuckCatalog())`)**:
     - `OracleCatalog` subclasses `DuckCatalog` (`src/include/oracle_catalog.hpp:11`) while overriding `IsDuckCatalog()` to return `false` (`src/storage/oracle_schema_entry.cpp:231-233`), and `OracleSchemaEntry` subclasses `DuckSchemaEntry` (`src/storage/oracle_schema_entry.cpp:33`).
     - In DuckDB (`duckdb/src/catalog/catalog_set.cpp:85-88`), `CatalogSet::CatalogSet(Catalog &catalog_p, ...)` asserts `D_ASSERT(catalog_p.IsDuckCatalog())` and calls `catalog_p.Cast<DuckCatalog>()`. Instantiating `OracleSchemaEntry` in any debug build (`make debug` / `make test_debug`) aborts on `D_ASSERT(catalog_p.IsDuckCatalog())`.
  2. **`info.internal = true` hides all Oracle schemas from DuckDB system views**:
     - In `src/storage/oracle_schema_entry.cpp:201,213`, `OracleSchemaGenerator::CreateDefaultEntry` sets `info.internal = true` on every `OracleSchemaEntry`.
     - In DuckDB (`duckdb/src/catalog/default/default_views.cpp:22-28`), the system views `duckdb_schemas`, `duckdb_tables`, `duckdb_columns`, and `pg_description` filter `WHERE NOT internal`, hiding 100% of attached Oracle schemas when `info.internal = true`.
  3. **`CatalogSet::CreateDefaultEntries` crashes `duckdb_tables()`, `duckdb_columns()`, `information_schema.*`, and `SHOW TABLES` when a schema contains non-table synonyms or tables with unsupported column types**:
     - When `duckdb_tables()` or `duckdb_columns()` scans an `OracleSchemaEntry`, `CatalogSet::CreateDefaultEntries` (`duckdb/src/catalog/catalog_set.cpp:671-691`) calls `CreateDefaultEntry` for every name in `ListObjects(oracle_schema, state->settings.metadata_object_types)`.
     - By default, `metadata_object_types` includes `SYNONYM`, and `OracleCatalogState::ListObjects` (`src/storage/oracle_catalog.cpp:317-323`) returns all synonyms in `all_objects` — including synonyms pointing to sequences, packages, procedures, functions, types, remote `db_link` objects, or missing tables. When `CreateDefaultEntry` returns `nullptr` for any such synonym (or a table with 0 visible columns), `CatalogSet::CreateDefaultEntries` throws `InternalException("Failed to create default entry for %s")`.
     - Similarly, if any single table or view in the schema contains an unsupported column type (e.g., `ROWID`, `LONG`, `BFILE`, `XMLTYPE`, or `SDO_GEOMETRY` when `oracle_enable_spatial_types = false`), `LoadColumns` throws `NotImplementedException`, aborting the entire `duckdb_tables()` / `duckdb_columns()` / `SHOW TABLES` scan across the whole schema.
  4. **N+1 schema metadata round-trips and stale catalog entry caches after `oracle_clear_cache()` / DDL**:
     - When `oracle_lazy_schema_loading = false`, `CatalogSet::CreateDefaultEntries` calls `GetRealSchemaName` once per enumerated schema in `all_users` (40+ redundant Oracle round-trips).
     - `CatalogSet` permanently caches created `OracleSchemaEntry` and `OracleTableEntry` instances (`defaults->created_all_entries = true`), so calling `oracle_clear_cache()` or running `CREATE TABLE` / `DROP TABLE` via `oracle_execute()` clears `OracleCatalogState::schema_cache` and `object_cache` without evicting the catalog's in-memory schema and table entries.

### Issue #20 — `Failed to install remote extension "oracle" from url` (`TSS3005`, `teddyzhu`, `rfx77`)
- **Reported Symptoms**:
  1. `SET custom_extension_repository = 'https://cofin.github.io/duckdb-oracle'; INSTALL oracle;` fails with `IO Error: Failed to install remote extension "oracle" from url "https://cofin.github.io/duckdb-oracle/v1.5.4/windows_amd64/oracle.duckdb_extension"`.
  2. Downloading the release binary (`oracle-linux_amd64.duckdb_extension` / `oracle-windows_amd64.duckdb_extension`) from GitHub Releases and running `INSTALL '/path/to/oracle-<platform>.duckdb_extension'; LOAD oracle;` also fails.
- **Root Causes**:
  1. **GitHub Pages deployment vs raw `gh-pages` repository URL**:
     - `.github/workflows/release-unsigned.yml` pushes `v<duckdb_version>/<platform>/oracle.duckdb_extension.gz` to the `gh-pages` branch, but `https://cofin.github.io/duckdb-oracle/` returns HTTP 404 unless GitHub Pages is deployed/enabled, and `deploy-gh-pages` does not place a `.nojekyll` marker or trigger an official GitHub Pages deployment (`actions/configure-pages` / `actions/upload-pages-artifact` / `actions/deploy-pages`).
     - Furthermore, `https://raw.githubusercontent.com/cofin/duckdb-oracle/gh-pages` serves the exact `gh-pages` directory tree (`v<duckdb_version>/<platform>/oracle.duckdb_extension.gz`) directly over HTTPS without requiring GitHub Pages settings, and should be documented as a zero-config custom extension repository URL alongside `https://cofin.github.io/duckdb-oracle`.
  2. **Local `INSTALL` / `LOAD` filename stem requirement in DuckDB**:
     - In DuckDB (`duckdb/src/main/extension/extension_install.cpp:557-558` and `extension_load.cpp:507,536-539`), `FileSystem::ExtractBaseName(path)` splits the filename on `.` and takes `vec[0]`.
     - When a user downloads `oracle-linux_amd64.duckdb_extension` or `oracle-windows_amd64.duckdb_extension` from GitHub Releases and runs `INSTALL 'oracle-windows_amd64.duckdb_extension'`, DuckDB extracts the extension name `"oracle-windows_amd64"`, installs it as `oracle-windows_amd64.duckdb_extension`, and looks for the C++ init symbol `oracle_windows_amd64_duckdb_cpp_init` instead of `oracle_duckdb_cpp_init`!
     - Packaging each release platform asset as a platform archive (`oracle-${DUCKDB_PLATFORM}.zip` on Windows, `oracle-${DUCKDB_PLATFORM}.tar.gz` on Linux/macOS) containing `oracle.duckdb_extension` (and also publishing `oracle.duckdb_extension.gz` or documenting renaming to `oracle.duckdb_extension`) allows local `INSTALL '/path/to/oracle.duckdb_extension'; LOAD oracle;` to succeed immediately.

### Issue #22 — `Windows support?` (`ericemc3`)
- **Reported Symptom**: Running `INSTALL oracle FROM community;` on Windows (`v1.5.5`, `windows_amd64`) fails with `HTTP Error: Failed to download extension "oracle" at URL "https://community-extensions.duckdb.org/v1.5.5/windows_amd64/oracle.duckdb_extension.gz" (HTTP 404)`.
- **Root Cause**:
  - `duckdb-oracle` is an unsigned thick-client OCI extension (`oci.dll` on Windows, `libclntsh.so` on Linux, `libclntsh.dylib` on macOS) distributed via this repository's custom extension repository and GitHub Releases — not `community-extensions.duckdb.org`.
  - `windows_amd64` binaries are already built in CI (`main-distribution-pipeline.yml` and `release-unsigned.yml`), but Windows users hit the 404 from `FROM community`, then hit Issue #20 (404 on `cofin.github.io` and filename-stem mismatch on `oracle-windows_amd64.duckdb_extension`), and lack Windows-specific setup instructions (`duckdb -unsigned`, Oracle Instant Client Basic `oci.dll` in `PATH`, custom repository URL, and local archive install).

---

## 2. Architectural Design

### 2.1 Native `OracleCatalog` & `OracleSchemaEntry` Hierarchy (Issue #21)
1. **`OracleCatalogState` Cache Generation Counter (`src/include/oracle_catalog_state.hpp`, `src/storage/oracle_catalog.cpp`)**:
   - Add `std::atomic<uint64_t> cache_generation {1};` and `uint64_t GetCacheGeneration() const noexcept { return cache_generation.load(std::memory_order_acquire); }` to `OracleCatalogState`.
   - Increment `cache_generation.fetch_add(1, std::memory_order_acq_rel);` inside `OracleCatalogState::ClearCaches()` so any attached `OracleCatalog` and `OracleSchemaEntry` can detect cache invalidation in $O(1)$ time without holding nested locks.
   - In `OracleCatalogState::ListObjects`, filter `SYNONYM` rows in `all_objects` so only synonyms with `db_link IS NULL` targeting `TABLE`, `VIEW`, or `MATERIALIZED VIEW` in `all_objects` are enumerated.
   - In `OracleCatalogState::GetRealSchemaName`, fall back to `GetCurrentSchema()` if `StringUtil::CIEquals(name, GetCurrentSchema())` when `all_users` does not return a match.
2. **`OracleCatalog : public Catalog` (`src/include/oracle_catalog.hpp`, `src/storage/oracle_schema_entry.cpp`)**:
   - Inherit directly from `Catalog` (removing `DuckCatalog` inheritance and `duckdb/catalog/duck_catalog.hpp`).
   - Implement `LookupSchema(CatalogTransaction transaction, const EntryLookupInfo &schema_lookup, OnEntryNotFound if_not_found)` and `ScanSchemas(ClientContext &context, std::function<void(SchemaCatalogEntry &)> callback)` backed by a mutex-guarded `case_insensitive_map_t<unique_ptr<OracleSchemaEntry>> schemas;`, a `vector<unique_ptr<OracleSchemaEntry>> retired_schemas;` (preserving pointer stability for in-flight queries across `ClearCaches()`), `bool schemas_enumerated = false;`, and `uint64_t cached_generation = 0;`.
   - When `ScanSchemas` enumerates `state->ListSchemas()`, instantiate `OracleSchemaEntry` objects directly from the canonical schema names returned by `ListSchemas()` without issuing N+1 `GetRealSchemaName` queries.
   - Set `info.internal = false` on all created `OracleSchemaEntry` instances so `duckdb_schemas`, `duckdb_tables`, and `duckdb_columns` system views include attached Oracle schemas and tables.
3. **`OracleSchemaEntry : public SchemaCatalogEntry` (`src/storage/oracle_schema_entry.cpp`, `src/storage/oracle_table_entry.cpp`)**:
   - Inherit directly from `SchemaCatalogEntry` (removing `DuckSchemaEntry` and `DefaultGenerator` / `CatalogSet` usage).
   - Implement `LookupEntry(CatalogTransaction transaction, const EntryLookupInfo &lookup_info)`, `GetSimilarEntry(CatalogTransaction transaction, const EntryLookupInfo &lookup_info)`, and both `Scan` overloads (`Scan(ClientContext &context, CatalogType type, ...)` and `Scan(CatalogType type, ...)`):
     - Return `nullptr` / `{}` / no-op immediately when `type != CatalogType::TABLE_ENTRY`.
     - Synchronize with `state->GetCacheGeneration()`, moving evicted `OracleTableEntry` instances into `retired_tables` to preserve pointer lifetime.
     - In `LookupEntry`: resolve on-demand via `GetObjectName` and `ResolveSynonym`, allowing `NotImplementedException` from `OracleTypeRegistry::ValidateSupported` to propagate when a user explicitly queries a table with unsupported column types.
     - In `GetSimilarEntry`: score `lookup_info.GetEntryName()` against the object names returned by `state->ListObjects(oracle_schema, state->settings.metadata_object_types)` (wrapped in `try { ... } catch (...) {}`) and any cached table keys without calling `LoadTableEntryLocked`, preventing missing-table lookups from triggering $N$ `all_tab_columns` queries via `SchemaCatalogEntry::GetSimilarEntry`'s default `Scan` implementation.
     - In `Scan`: enumerate `state->ListObjects(oracle_schema, state->settings.metadata_object_types)`, resolve each object not yet cached (skipping names cached in `unresolvable_entries`), and catch `const Exception &` during `Scan` so tables with unsupported column types or dangling synonyms are recorded in `unresolvable_entries` and skipped without aborting `duckdb_tables()`, `duckdb_columns()`, `information_schema.*`, or `SHOW TABLES`.
   - In `OracleTableEntry::Create` / `LoadColumns` (`src/storage/oracle_table_entry.cpp`), populate `NotNullConstraint` on `CreateTableInfo::constraints` when `all_tab_columns.nullable == "N"` so `duckdb_columns()` and `information_schema.columns` report `is_nullable = false` accurately.

### 2.2 Extension Distribution, Release Packaging & Windows Documentation (Issues #20 & #22)
1. **`.github/workflows/release-unsigned.yml`**:
   - In `Run Smoke Tests`, test local `INSTALL 'build/release/extension/oracle/oracle.duckdb_extension'; LOAD oracle;` on all matrix platforms (including Windows, stripping `\r` from `duckdb.exe` output).
   - In `Prepare Artifacts`, package each platform's `oracle.duckdb_extension` (with exact inner filename `oracle.duckdb_extension`) into `deploy/release/oracle-${DUCKDB_PLATFORM}.zip` (for `windows_amd64`) and `deploy/release/oracle-${DUCKDB_PLATFORM}.tar.gz` (for Linux/macOS) alongside `oracle-${DUCKDB_PLATFORM}.duckdb_extension`, and include both explicit filenames in `oracle-${DUCKDB_PLATFORM}.sha256`, `SHA256SUMS.txt`, and GitHub Release assets.
   - In `deploy-gh-pages`, create `deploy_content/.nojekyll` before publishing to `gh-pages` via `peaceiris/actions-gh-pages@v4` (`keep_files: true`) so all `v<duckdb_version>/<platform>/oracle.duckdb_extension.gz` directories are served without Jekyll filtering on both `https://cofin.github.io/duckdb-oracle` and `https://raw.githubusercontent.com/cofin/duckdb-oracle/gh-pages`.
   - Update generated release notes in `release-unsigned.yml` to show both remote repository URLs (`https://cofin.github.io/duckdb-oracle` and `https://raw.githubusercontent.com/cofin/duckdb-oracle/gh-pages`), local archive extraction (`oracle.duckdb_extension`), and runtime Oracle Instant Client requirements.
2. **`README.md` & `docs/SETUP.md`**:
   - Document remote installation with both `https://cofin.github.io/duckdb-oracle` and `https://raw.githubusercontent.com/cofin/duckdb-oracle/gh-pages`.
   - Explain why `INSTALL oracle FROM community;` returns HTTP 404 (thick-client OCI dependency requires Oracle Instant Client at build and runtime).
   - Add platform-specific runtime prerequisite and local installation instructions for Windows (`oci.dll` in `PATH`, extracting `oracle.duckdb_extension` from `oracle-windows_amd64.zip` or renaming `oracle-windows_amd64.duckdb_extension` to `oracle.duckdb_extension`), Linux (`libclntsh.so` in `LD_LIBRARY_PATH`), and macOS (`libclntsh.dylib` in `DYLD_LIBRARY_PATH`).

---

## 3. Task Breakdown & Dependency Order

| Task ID | Title | Depends On | Verification Strategy |
| :--- | :--- | :--- | :--- |
| `1.1` | Refactor `OracleCatalog` and `OracleSchemaEntry` to Native DuckDB Catalog Hierarchy with Cache Generation Sync and Scan Resilience | — | `unit_and_integration` |
| `1.2` | Add Catalog Introspection Unit and Integration Tests (`duckdb_databases`, `duckdb_schemas`, `duckdb_tables`, `duckdb_columns`, `information_schema`, `SHOW TABLES`) | `1.1` | `unit_and_integration` |
| `1.3` | Harden Release Packaging, GitHub Pages Deployment, and Windows/Local Installation Documentation | — | `unit_and_integration` |
