> **User Prompt**: The readme mention something about this being read-only currently. If that's the case, that's a mistake. Can you review and determine what is required to implement full support properly? Use context7 to read up on oracle oci C++ and duckdb extensions. Ensure you focus on performance and memory as this will be used to move large data sets and it will need to be extreley will architected. Create a new set of specs based on required changes.

# Feature: Oracle Write Support (INSERT/COPY & Transactions)

## Intelligence Context

**Complexity**: Complex
**Similar Features**:
- Current Read-Only Implementation (`OracleScan`)
- Standard DuckDB Storage Extension Patterns (Postgres/MySQL extensions)

**Patterns to Follow**:
- [Adapter Pattern](../../../specs/guides/patterns/adapter-pattern.md)
- **OCI Array Binding**: The standard high-performance pattern for Oracle DML.
- **RAII Resource Management**: Mandatory for OCI handles.

**Tool Selection**:
- Reasoning: `crash` (analysis of OCI requirements).
- Research: Context7 (OCI docs) + Internal Codebase analysis.
- Testing: Standard SQL tests + Integration benchmarks.

---

## Overview

This feature implements full write capability for the DuckDB Oracle extension, allowing users to `INSERT`, `COPY`, and manage transactions against attached Oracle databases. The implementation prioritizes performance for large dataset movement, leveraging Oracle's OCI Array Binding (Bulk DML) to minimize network round-trips.

Currently, the extension is read-only. This feature transforms it into a read-write storage engine.

## Problem Statement

Users currently cannot export data from DuckDB to Oracle or manage data within Oracle using DuckDB. This limits the extension's utility to "read-only analysis" scenarios. Users needing to move large datasets (ETL/ELT) from DuckDB/Parquet/CSV into Oracle are blocked.

A naive "row-by-row" insert implementation would be prohibitively slow (10-100x slower than bulk). Therefore, a high-performance, memory-efficient bulk loading architecture is required.

## Acceptance Criteria

### Functional Requirements
- [ ] **INSERT Support**: `INSERT INTO oracle_table SELECT ...` works.
- [ ] **COPY Support**: `COPY oracle_table FROM 'file.parquet'` works (via the Insert path).
- [ ] **Transaction Support**: `BEGIN`, `COMMIT`, and `ROLLBACK` correctly control Oracle transactions.
- [ ] **Auto-Commit**: Single statements outside explicit transactions auto-commit (or follow DuckDB's mode).
- [ ] **Data Types**: Support writing:
    - `INTEGER`, `BIGINT` -> `NUMBER`
    - `DOUBLE` -> `BINARY_DOUBLE`/`FLOAT`
    - `VARCHAR` -> `VARCHAR2`/`CLOB`
    - `DATE`, `TIMESTAMP` -> `DATE`/`TIMESTAMP`
    - `BLOB` -> `BLOB`
- [ ] **NULL Handling**: Correctly insert `NULL` values using indicator variables.

### Performance Requirements
- [ ] **Array Binding**: Implementation MUST use `OCIStmtExecute` with `iters > 1`.
- [ ] **Throughput**: Capable of inserting > 50,000 rows/second (network dependent, but overhead should be minimal).
- [ ] **Memory Stability**: Memory usage must not grow unbounded during large `COPY` operations (fixed buffer size per batch).

### Pattern Compliance
- [ ] **RAII**: All OCI handles (`OCIStmt`, `OCIBind`) must be managed via RAII classes.
- [ ] **Error Handling**: OCI errors during write must throw `IOException` and abort the batch.
- [ ] **Clean Architecture**: Write logic separated into `OracleInsert` classes, keeping `OracleExtension.cpp` clean.

## Technical Design

### Affected Components

**Backend (C++)**:
- **Modules**: `src/storage/`, `src/include/`
- **Classes**:
    - `OracleTransactionManager` (Modify): Implement `Commit`/`Rollback`.
    - `OracleTableEntry` (Modify): Implement `BindInsert`.
    - `OracleInsertGlobalState` (New): Manage OCI handles for the insert session.
    - `OracleInsertLocalState` (New): Manage bind buffers for a thread/batch.

### Implementation Approach

The implementation will follow the **Push-Based Sink** model used by DuckDB storage extensions.

**1. Catalog & Transaction Layer**:
- Update `OracleTransactionManager` to call `OCITransCommit` and `OCITransRollback`.
- Ensure the connection used for writing is locked/reserved for the transaction duration.

**2. Bind Phase (`BindInsert`)**:
- Input: Target table schema.
- Action:
    - Generate SQL: `INSERT INTO schema.table (c1, c2) VALUES (:1, :2)`.
    - Prepare `OCIStmt` on the active transaction's connection.
    - Create `OracleInsertGlobalState` holding the statement handle.

**3. Execution Phase (`Sink`)**:
- **Buffering**:
    - Allocate C-style arrays for each column (size = `STANDARD_VECTOR_SIZE`, typically 2048).
    - Allocate Indicator arrays (for NULLs) and Length arrays (for VARCHARs).
- **Data Conversion**:
    - Iterate input `DataChunk`.
    - Copy/Convert values from DuckDB Vectors to OCI arrays.
    - Handle Type Impedance (e.g., `Decimal` -> String -> `OCINumber` if native conversion is too complex initially).
- **Flushing**:
    - When buffer is full, call `OCIStmtExecute(..., iters=count, ...)`
    - Reset batch count.

**4. Type Mapping Detail**:
- `LogicalType::VARCHAR` -> `SQLT_CHR` (bound as pointer to string data).
- `LogicalType::BIGINT` -> `SQLT_INT`.
- `LogicalType::DOUBLE` -> `SQLT_BDOUBLE`.
- `LogicalType::TIMESTAMP` -> `SQLT_ODT` (OCIDate struct) or `SQLT_CHR` (String with `TO_TIMESTAMP`). *Decision: Try direct OCIDate struct for performance if feasible, else formatted string.*

### Code Samples

**Insert Binding Logic**:

```cpp
unique_ptr<TableFunctionData> OracleTableEntry::BindInsert(ClientContext &context, TableFunctionBindInput &input) {
    auto &transaction = OracleTransaction::Get(context, *catalog);
    auto insert_data = make_uniq<OracleInsertData>(transaction);
    
    // 1. Construct SQL
    string sql = "INSERT INTO " + KeywordHelper::WriteQuoted(table_name, '"') + " (";
    // ... build column list and placeholders ...
    
    // 2. Prepare Statement
    insert_data->statement = CreateOCIStatement(transaction.GetConnection(), sql);
    
    return insert_data;
}
```

**Sink Execution Logic**:

```cpp
SinkResultType OracleInsert::Sink(ExecutionContext &context, DataChunk &chunk, OperatorSinkInput &input) {
    auto &state = input.local_state.Cast<OracleInsertLocalState>();
    
    // 1. Convert DataChunk to OCI Arrays
    for(idx_t col = 0; col < chunk.ColumnCount(); col++) {
        state.BindColumn(chunk.data[col], col, chunk.size());
    }
    
    // 2. Execute Batch
    ub4 iters = chunk.size();
    OCIStmtExecute(..., iters, ...);
    
    return SinkResultType::NEED_MORE_INPUT;
}
```

## Testing Strategy

### Unit Tests
- `test/sql/insert/test_insert_basic.test`: Insert primitive types.
- `test/sql/insert/test_insert_nulls.test`: Validate NULL handling.
- `test/sql/insert/test_insert_types.test`: Validate all mapped types.
- `test/sql/transaction/test_transaction_commit.test`: Insert + Commit -> Verify data exists.
- `test/sql/transaction/test_transaction_rollback.test`: Insert + Rollback -> Verify data is gone.

### Integration Tests
- **Bulk Load**: Copy 1GB Parquet file into Oracle. Measure time.
- **Concurrency**: Multiple connections inserting simultaneously (if supported).

### Edge Cases
- **Empty Chunk**: Sink called with 0 rows.
- **String Overflow**: Insert string > 4000 bytes (handling CLOBs or truncation errors).
- **Constraint Violation**: Insert duplicate PK. Verify robust error reporting (not crash).
- **Connection Loss**: Kill OCI session during insert. Verify clean error.

### Pattern Test Coverage
- [ ] `oracle_enable_pushdown` should not interfere with writes.
- [ ] Memory sanitizer checks (ASAN) to ensure no OCI handle leaks.

## Security Considerations

- **Bind Parameters**: STRICTLY use bind parameters (`:1`, `:2`) for all values. NEVER interpolate data into SQL strings.
- **Privileges**: Document that the user needs `INSERT` / `CREATE SESSION` privileges.

## Risks & Mitigations

- **Risk**: OCI Memory Management is manual and error-prone.
    - **Mitigation**: Wrap every OCI handle in a C++ class with a destructor calling `OCIHandleFree`.
- **Risk**: Array Bind size limits.
    - **Mitigation**: Use `STANDARD_VECTOR_SIZE` (2048) which is safe for most OCI drivers.
- **Risk**: Encoding issues (UTF-8).
    - **Mitigation**: Explicitly set `OCI_UTF16ID` or `OCI_UTF8ID` in bind calls if needed, but standard `SQLT_CHR` usually respects client charset (AL32UTF8).

## Dependencies

- **OCI SDK**: Existing dependency.
- **DuckDB Internal API**: `TableCatalogEntry`, `TableFunctionData`.

## References

- [Oracle Call Interface Programmer's Guide](https://docs.oracle.com/en/database/oracle/oracle-database/19/lnoci/index.html)
- [DuckDB Storage Extension Implementation Guide](https://duckdb.org/docs/extensions/storage)
