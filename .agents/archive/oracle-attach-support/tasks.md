# Tasks: Oracle Attach Support

## Phase 1: OCI Wrappers (The Foundation)

- [ ] **Create `OracleLib`**: Abstract OCI library loading (if dynamic) or just standard includes.
- [x] **Create `OracleResult`**: Class to hold/fetch query results (`src/include/oracle_result.hpp`).
  - [x] Implement `GetString(row, col)`, `GetInt64`, `GetDouble`.
  - [x] Implement `FetchAll()` for metadata queries.
- [x] **Create `OracleConnection`**: Class to manage OCI Session (`src/include/oracle_connection.hpp`).
  - [x] Implement `Connect(string connection_string)`.
  - [x] Implement `Query(string query) -> unique_ptr<OracleResult>`.
  - [x] Destructor cleans up `OCIEnv`, `OCISvcCtx`.

## Phase 2: Catalog & Storage Extension

- [x] **Create `OracleCatalog`**: (`src/storage/oracle_catalog.cpp`)
  - [x] Inherit `Catalog`.
  - [x] Implement `Initialize`, `GetCatalogType`.
  - [x] Implement `ScanSchemas`.
- [x] **Create `OracleTransaction`**: (`src/storage/oracle_transaction.cpp`)
  - [x] Implement `GetConnection()`.
- [x] **Create `OracleStorageExtension`**: (`src/storage/oracle_storage_extension.cpp`)
  - [x] Register in `oracle_extension.cpp`.

## Phase 3: Schema & Table Entries

- [x] **Create `OracleSchemaEntry`**: (`src/storage/oracle_schema_entry.cpp`)
  - [x] Inherit `SchemaCatalogEntry`.
  - [x] Implement `GetEntry` (Lazy load tables).
- [x] **Create `OracleTableEntry`**: (`src/storage/oracle_table_entry.cpp`)
  - [x] Inherit `TableCatalogEntry`.
  - [x] Implement `GetScanFunction`.
  - [x] Implement `Bind` (Fetch columns from `ALL_TAB_COLUMNS`).

## Phase 4: Scan Function & Pushdown

- [x] **Update `OracleScanFunction`**:
  - [x] Accept `OracleBindData` from `OracleTableEntry` (currently via table function bind).
  - [x] Use `OracleConnection` from the Transaction.
- [x] **Implement `OracleFilterPushdown`**: (`src/storage/oracle_filter_pushdown.cpp`)
  - [x] Transform `TableFilter` to SQL String for basic comparisons and IS NULL.

## Phase 5: Integration & Testing

- [ ] **Verify Attach**: Run `ATTACH` command against a real Oracle instance (pending OCI creds).
- [ ] **Verify Queries**: Run `SELECT` on attached table with live data (pending OCI creds).
- [ ] **Verify Memory**: Check for OCI handle leaks in long-running session (pending live env).
- [x] **Format/Tests**: `make format`, `make test` (pass with negative/OCI-missing scenarios).

## Phase 6: Hardening (Post-OCI Access)

- [ ] Switch pushdown SQL generation to OCI bind variables to avoid injection/NLS issues.
- [ ] Confirm connection reuse/ pooling strategy and transaction isolation (SERIALIZABLE/READ ONLY per query).
- [ ] Address Oracle empty-string-is-NULL quirk in filter pushdown (block or rewrite).
- [ ] Improve NUMBER mapping to DECIMAL/HUGEINT to avoid precision loss.
- [ ] Verify array fetch size matches DuckDB vector size; add interrupt-safe cleanup of OCI handles.
- [ ] Decide LOB strategy (prefetch small LOBs or exclude for MVP).
