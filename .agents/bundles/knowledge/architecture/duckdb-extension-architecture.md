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
| DuckDB Core Engine (v1.5.5)                                                        |
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
|   +---> OracleSchemaEntry     |
|           +---> OracleTable   |
|                   +-> BindScan|
|                   +-> BindIns |
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

- **Version Lockstep**: `oracle_version()` returns the DuckDB library version string compiled into the extension (e.g. `v1.5.5`). DuckDB verifies this matches the hosting process version; mismatches refuse to load.
- **`LoadExtension` Callback**: Instantiates `OracleExtension::Load(db)` (`src/oracle_extension.cpp`), registering functions, settings, secrets, and storage hooks.

## 2. Storage Extension Interface (`StorageExtension`)

DuckDB allows custom engines to act as attached databases via `ATTACH '' AS ora (TYPE oracle, ...)`:

```cpp
auto &config = DBConfig::GetConfig(db);
auto storage_extension = make_uniq<StorageExtension>();

// Attach callback invoked on ATTACH database statement
storage_extension->attach = [](StorageExtensionInfo *info, AttachedDatabase &db,
                               const string &name, AttachInfo &attach_info,
                               AccessMode access_mode) -> unique_ptr<Catalog> {
    return make_uniq<OracleCatalog>(db, attach_info.path, access_mode, attach_info.options);
};

// Transaction manager callback for multi-statement transaction tracking
storage_extension->create_transaction_manager = [](StorageExtensionInfo *info,
                                                   AttachedDatabase &db) -> unique_ptr<TransactionManager> {
    return make_uniq<OracleTransactionManager>(db);
};

config.storage_extensions["oracle"] = std::move(storage_extension);
```

## 3. Catalog & Schema Hierarchy

DuckDB organizes attached databases into a tree of catalog objects:

1. **`OracleCatalog` (`src/storage/oracle_catalog.cpp`)**:
   - Subclasses `duckdb::Catalog`.
   - Manages connection configurations, lazy schema discovery (`oracle_lazy_schema_loading`), and synonym resolution.
   - Overrides `GetSchema` to lazily probe Oracle data dictionary views (`ALL_TABLES`, `ALL_VIEWS`, `ALL_SYNONYMS`).

2. **`OracleSchemaEntry` (`src/storage/oracle_schema_entry.cpp`)**:
   - Subclasses `duckdb::SchemaCatalogEntry`.
   - Represents an Oracle schema (database user / namespace).
   - Lazily loads and caches table metadata into `OracleTableEntry` instances upon lookup.

3. **`OracleTableEntry` (`src/storage/oracle_table_entry.cpp`)**:
   - Subclasses `duckdb::TableCatalogEntry`.
   - Overrides `GetScanFunction` to bind `OracleScanFunction` with projected column IDs.
   - Overrides `BindInsert` to return `PhysicalOracleInsert`, enabling `INSERT INTO ...` syntax.

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
