# Product Requirements Document: Oracle Attach Support

## 1. Overview

Enables first-class Oracle database integration in DuckDB via the `ATTACH` command. This allows users to query Oracle tables directly as if they were local tables, with support for schema discovery and basic query optimization.

## 2. User Experience

```sql
-- Attach Oracle Database
ATTACH 'user/password@//host:1521/service' AS ora (TYPE ORACLE);

-- List Schemas
SHOW DATABASES; -- includes 'ora'
SHOW SCHEMAS IN ora;

-- Query Tables
SELECT * FROM ora.schema.table WHERE id = 5;

-- Detach
DETACH ora;
```

## 3. Functional Requirements

* **Read-Only Access**: V1 supports reading data only.
* **Automatic Schema Discovery**: All Oracle users/schemas are visible.
* **Lazy Loading**: Tables are loaded only when accessed to minimize startup time.
* **Filter Pushdown**: Basic filters (`=`, `<`, `>`, `IS NULL`, `IN`) are pushed to Oracle.
* **Type Fidelity**:
  * `NUMBER(p,s)` -> `DECIMAL(p,s)` or `DOUBLE`
  * `DATE` -> `TIMESTAMP`
  * `VARCHAR2` -> `VARCHAR`
* **Connection Management**: Robust handling of OCI handles (no leaks).

## 4. Technical Specifications

* **Language**: C++17 (DuckDB Standard)
* **Dependencies**: OCI (Oracle Call Interface) - provided by system or vcpkg.
* **Architecture**: `StorageExtension` based.

## 5. Limitations

* No Write Support (INSERT/UPDATE/DELETE).
* No View Support (Oracle Views treated as tables if they appear in `ALL_TABLES`, otherwise ignored in V1).
* No Transaction Management (Auto-commit/ReadOnly).

## 6. Future Work (V2)

* Write Support (`COPY TO`).
* Connection Pooling.
* Async I/O.
