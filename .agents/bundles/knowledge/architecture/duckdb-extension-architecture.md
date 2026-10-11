---
type: Architecture
title: DuckDB Extension Architecture & Subsystems
description: Extension loading lifecycle, catalog integration, storage extension registration, and secret management in DuckDB.
scope: architecture
domain: duckdb
tags: [duckdb, extension-api, catalog, storage-extension, secret-manager]
status: stable
---

# DuckDB Extension Architecture & Subsystems

DuckDB extensions dynamically extend the core database engine with custom storage backends, table functions, scalar functions, type systems, and authentication mechanisms without modifying upstream DuckDB binaries.

```
+-----------------------------------------------------------------------------------+
| DuckDB Core Engine (v1.5.6)                                                        |
+-----------------------------------------------------------------------------------+
       |                                       |
       v (Load & Initialize)                   v (Resolve Credentials)
+-------------------------------+      +--------------------------------------------+
| Extension Entry Point         |      | SecretManager (`TYPE oracle`)              |
| - oracle_init(DatabaseInstance)|      | - Key: secret name / scope                 |
| - oracle_version()            |      | - Fields: user, password, host, port, etc. |
+-------------------------------+      +--------------------------------------------+
       |
       +---------------------------------------+
       |                                       |
       v                                       v
+-------------------------------+      +--------------------------------------------+
| StorageExtension Hook         |      | Table & Scalar Functions                   |
| - attach: Create OracleCatalog|      | - oracle_scan: Table scan function         |
| - create_transaction_manager  |      | - oracle_query: Raw SQL query function     |
+-------------------------------+      | - oracle_execute: DDL/DML function         |
       |                               | - oracle_clear_cache: Cache invalidation   |
       v                               +--------------------------------------------+
+-------------------------------+
| Catalog Subsystem             |
| - OracleCatalog               |
|   +---> OracleCatalogState    |
|   +---> OracleSchemaEntry     |
|           +---> OracleTable   |
|                   +-> GetScan |
|   +---> PlanInsert            |
+-------------------------------+
```

## 1. Extension Loading Lifecycle

When DuckDB runs `LOAD oracle;`, the runtime looks up two exported C-linkage symbols:

```cpp
extern "C" {
DUCKDB_EXTENSION_API void oracle_init(duckdb::DatabaseInstance &db) {
    duckdb::DuckDB db_wrapper(db);
    db_wrapper.LoadExtension<duckdb::OracleExtension>();
}

DUCKDB_EXTENSION_API const char *oracle_version() {
    return duckdb::DuckDB::LibraryVersion();
}
}
```

- **Version Lockstep**: `oracle_version()` returns the DuckDB library version string compiled into the extension (e.g. `v1.5.6`). DuckDB verifies this matches the hosting process version; mismatches refuse to load.
- **`LoadExtension` Callback**: Instantiates `OracleExtension::Load(db)` (`src/oracle_extension.cpp`), registering functions, settings, secrets, and storage hooks.

## 2. Storage Extension Interface (`StorageExtension`)

DuckDB allows custom engines to act as attached databases via `ATTACH '' AS ora (TYPE oracle, ...)`:

```cpp
auto &config = DBConfig::GetConfig(db);
auto storage_extension = make_uniq<OracleStorageExtension>();
config.storage_extensions["oracle"] = std::move(storage_extension);
```

- **`OracleAttach` (`src/storage/oracle_storage_extension.cpp`)**: Allocates a dedicated `make_shared_ptr<OracleCatalogState>(connection_string, wallet_path)` for every `ATTACH` invocation, applies per-attachment options via `state->ApplyOptions(info.options)`, registers the state under the attached database alias (`OracleCatalogState::Register(state, name)`), and returns `CreateOracleCatalog(&db, state)`.
- **`OracleCreateTransactionManager`**: Looks up the per-catalog state via `OracleCatalogState::LookupByAlias(db.GetName())` and constructs `OracleTransactionManager(db, state)` with a mutex-guarded active transaction map.

## 3. Catalog & Schema Hierarchy (Per-ATTACH State Isolation)

DuckDB organizes attached databases into a tree of catalog objects with strict per-`ATTACH` catalog state isolation:

1. **`OracleCatalogState` (`src/storage/oracle_catalog.cpp`)**:
   - Encapsulates metadata discovery queries (`ALL_USERS`, `ALL_OBJECTS`, `ALL_SYNONYMS`, `ALL_TAB_COLUMNS`, `ALL_VEC_COLUMNS`, `ALL_PART_TABLES`, `ALL_TAB_PARTITIONS`) using parameterized bind variables (`ExecuteMetadataQueryWithStringBinds`), lazy schema discovery (`oracle_lazy_schema_loading`), `CURRENT_SCHEMA` resolution, and per-catalog metadata caches.
   - Maintains an atomic `cache_generation` counter incremented by `ClearCaches()` so `OracleCatalog` and `OracleSchemaEntry` synchronize their in-memory entry caches in $O(1)$ time while moving evicted entries to `retired_schemas` / `retired_tables` to preserve pointer stability for in-flight queries.
   - Filters `SYNONYM` rows in `ListObjects` so only local (`db_link IS NULL`) synonyms targeting `TABLE`, `VIEW`, or `MATERIALIZED VIEW` objects in `ALL_OBJECTS` are enumerated.
   - Acquires a short-lived `OracleConnection` inside `ExecuteMetadataQuery` / `ExecuteMetadataQueryWithStringBinds` that immediately returns its pooled handle to `OracleConnectionManager` after each metadata query, preventing self-deadlock when `oracle_connection_limit = 1`.
   - Records per-`ATTACH` options in `settings.attach_overridden_keys` so `GetOracleSettings` preserves `ATTACH`-level overrides unless a session explicitly sets a `SettingScope::LOCAL` override.

2. **`OracleCatalog` & `OracleSchemaEntry` (`src/storage/oracle_schema_entry.cpp`)**:
   - `OracleCatalog` subclasses `duckdb::Catalog` directly (never `DuckCatalog`), overriding `LookupSchema`, `ScanSchemas`, `GetDefaultSchema`, `InMemory()`, `GetDBPath()` (redacting passwords via `RedactOracleConnectionString`), `GetDatabaseSize()`, and `PlanInsert` (passing resolved `oracle_table.GetSchemaName()` and `oracle_table.GetTableName()` to `PhysicalOracleInsert` for cross-schema and synonym-backed targets).
   - `OracleSchemaEntry` subclasses `duckdb::SchemaCatalogEntry` directly with `info.internal = false` (so DuckDB system views `duckdb_schemas`, `duckdb_tables`, `duckdb_columns`, and `information_schema.*` include attached Oracle schemas and tables), overriding `LookupEntry`, `GetSimilarEntry` (scoring against `ListObjects` without triggering $N$ `all_tab_columns` queries), and `Scan` (skipping tables with unsupported column types via `unresolvable_entries` while keeping direct `LookupEntry` strict), and throwing clean `BinderException` diagnostics for unsupported DDL operations.

3. **`OracleTableEntry` (`src/storage/oracle_table_entry.cpp`)**:
   - Subclasses `duckdb::TableCatalogEntry`, loading column metadata and `NotNullConstraint` (`NULLABLE = 'N'`) from `ALL_TAB_COLUMNS` (and enriching Oracle 23ai `VECTOR` columns from `ALL_VEC_COLUMNS` and spatial SRIDs from `ALL_SDO_GEOM_METADATA`).
   - Overrides `GetScanFunction` to construct an `OracleBindData` with full column metadata, `original_types`, and 65,535-byte buffer sizing for special/rewritten categories.

## 4. SecretManager Integration (`src/oracle_secret.cpp`)

DuckDB includes a unified `SecretManager` for persistent and temporary credential storage.
The extension registers an `oracle` secret type:

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

The extension resolves secrets during `ATTACH` or table function invocation via `ResolveOracleConnection` (`src/oracle_connection_resolver.cpp`), automatically parsing wallet paths, credentials, and host descriptors.
