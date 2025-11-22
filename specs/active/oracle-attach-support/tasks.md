# Tasks: Oracle Attach Support

## Phase 1: OCI Wrappers (The Foundation)

- [ ] **Create `OracleLib`**: Abstract OCI library loading (if dynamic) or just standard includes.
- [ ] **Create `OracleResult`**: Class to hold/fetch query results (`src/include/oracle_result.hpp`).
  - [ ] Implement `GetString(row, col)`, `GetInt64`, `GetDouble`.
  - [ ] Implement `FetchAll()` for metadata queries.
- [ ] **Create `OracleConnection`**: Class to manage OCI Session (`src/include/oracle_connection.hpp`).
  - [ ] Implement `Connect(string connection_string)`.
  - [ ] Implement `Query(string query) -> unique_ptr<OracleResult>`.
  - [ ] Destructor cleans up `OCIEnv`, `OCISvcCtx`.

## Phase 2: Catalog & Storage Extension

- [ ] **Create `OracleCatalog`**: (`src/storage/oracle_catalog.cpp`)
  - [ ] Inherit `Catalog`.
  - [ ] Implement `Initialize`, `GetCatalogType`.
  - [ ] Implement `ScanSchemas`.
- [ ] **Create `OracleTransaction`**: (`src/storage/oracle_transaction.cpp`)
  - [ ] Implement `GetConnection()`.
- [ ] **Create `OracleStorageExtension`**: (`src/storage/oracle_storage_extension.cpp`)
  - [ ] Register in `oracle_extension.cpp`.

## Phase 3: Schema & Table Entries

- [ ] **Create `OracleSchemaEntry`**: (`src/storage/oracle_schema_entry.cpp`)
  - [ ] Inherit `SchemaCatalogEntry`.
  - [ ] Implement `GetEntry` (Lazy load tables).
- [ ] **Create `OracleTableEntry`**: (`src/storage/oracle_table_entry.cpp`)
  - [ ] Inherit `TableCatalogEntry`.
  - [ ] Implement `GetScanFunction`.
  - [ ] Implement `Bind` (Fetch columns from `ALL_TAB_COLUMNS`).

## Phase 4: Scan Function & Pushdown

- [ ] **Update `OracleScanFunction`**:
  - [ ] Accept `OracleBindData` from `OracleTableEntry`.
  - [ ] Use `OracleConnection` from the Transaction.
- [ ] **Implement `OracleFilterPushdown`**: (`src/storage/oracle_filter_pushdown.cpp`)
  - [ ] Transform `TableFilter` to SQL String.

## Phase 5: Integration & Testing

- [ ] **Verify Attach**: Run `ATTACH` command.
- [ ] **Verify Queries**: Run `SELECT` on attached table.
- [ ] **Verify Memory**: Check for OCI handle leaks.
