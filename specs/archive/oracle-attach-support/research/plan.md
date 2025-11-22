# Research Plan: Oracle Attach Support (Expanded)

## Goal

Implement `ATTACH 'connection_string' AS myserver (TYPE ORACLE)` in DuckDB, utilizing the `StorageExtension` API and mirroring the robust architecture of `duckdb_postgres`.

## Architecture: The "Oracle-Postgres" Clone

We will mimic the `duckdb_postgres` architecture, replacing `libpq` components with `OCI` equivalents.

### 1. Connection Layer (`OracleConnection`, `OracleResult`)

* **`OracleConnection`**: RAII wrapper around `OCIEnv`, `OCISvcCtx`, `OCIServer`, `OCISession`.
  * **Responsibility**: Connect, Disconnect, Execute Queries.
  * **Method**: `Query(string sql) -> unique_ptr<OracleResult>`
* **`OracleResult`**: RAII wrapper around `OCIStmt`.
  * **Responsibility**: Execute statement, Fetch rows, Provide data accessors.
  * **Difference**: Unlike `PGresult` which holds all data, `OCIStmt` is a cursor. For Catalog queries (metadata), we will fetch all rows into memory (vector of vectors) to mimic `PGresult`. For Data queries (Scan), we will stream.

### 2. Catalog Layer (`OracleCatalog`)

* **Inherits**: `Catalog`
* **State**: `attach_path` (connection string), `access_mode`, `default_schema`.
* **Key Methods**:
  * `Initialize`: Connects using `OracleConnection`.
  * `CreateSchema`, `DropSchema`: Delegate to `OracleTransaction`.
  * `ScanSchemas`: Iterates `ALL_USERS`.
  * `LookupSchema`: Checks `ALL_USERS`.

### 3. Schema Layer (`OracleSchemaEntry`)

* **Inherits**: `SchemaCatalogEntry`
* **Responsibility**: Manage `tables`, `indexes`, `types` (CatalogSets).
* **Lazy Loading**: When `GetEntry` is called, it queries `ALL_TABLES` (or `ALL_OBJECTS`) if not cached.

### 4. Table Layer (`OracleTableEntry`)

* **Inherits**: `TableCatalogEntry`
* **Responsibility**: Store column definitions (`ALL_TAB_COLUMNS`).
* **`GetScanFunction`**: Returns `OracleScanFunction`.
* **`GetStorageInfo`**: Returns cardinality/index info.

### 5. Transaction Layer (`OracleTransactionManager`, `OracleTransaction`)

* **`OracleTransactionManager`**: Creates `OracleTransaction`.
* **`OracleTransaction`**: Wraps a transaction context (even if implicit in OCI). Provides the `OracleConnection` to the Scan/Catalog.

### 6. Storage Extension (`OracleStorageExtension`)

* Registers `Attach` and `CreateTransactionManager`.

## Metadata Mapping

* **Schemas**: `SELECT username FROM all_users`
* **Tables**: `SELECT table_name FROM all_tables WHERE owner = :schema`
* **Columns**: `SELECT column_name, data_type, data_length, data_precision, data_scale FROM all_tab_columns WHERE owner = :schema AND table_name = :table ORDER BY column_id`

## Filter Pushdown Strategy

* Implement `OracleFilterPushdown` class.
* Translate DuckDB `TableFilter` to SQL `WHERE` clauses.
* Append to the generated `SELECT` statement in `OracleScanFunction`.

## Development Phases (Refined)

1. **Core OCI Wrappers**: `OracleConnection`, `OracleResult`.
2. **Catalog Shell**: `OracleStorageExtension`, `OracleCatalog` (stubbed).
3. **Schema/Table Logic**: `OracleSchemaEntry`, `OracleTableEntry` (populating from OCI).
4. **Scan Integration**: Update `oracle_extension.cpp` to support the new flow.
5. **Filter Pushdown**: Add `WHERE` clause generation.
