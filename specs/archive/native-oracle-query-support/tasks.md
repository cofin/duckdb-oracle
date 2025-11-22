# Implementation Tasks: Native Oracle Query Support

**Created**: 2025-11-21
**Status**: Complete

## Phase 1: Research & Planning ✓

- [x] Read internal guides
- [x] Create PRD
- [x] Create tasks list
- [x] Research OCI data types and mappings
- [x] Research JSON and Wallet support
- [x] Enhance CMake OCI detection (handle `ORACLE_HOME`, `LD_LIBRARY_PATH`, pkg-config if applicable).

## Phase 2: Core Implementation (Scan & Query) ✓

- [x] Rename `oracle_query` internal implementation to support both modes (scan vs query).
- [x] Implement `oracle_scan(conn_str, schema, table)`:
    - [x] Construct `SELECT * FROM schema.table` query.
    - [x] Reuse the `OracleQueryBind` logic.
- [x] Implement `oracle_query(conn_str, query)`:
    - [x] Existing logic, refined for types.

## Phase 3: Data Type Support ✓

- [x] **JSON**: Detect `SQLT_JSON` (or check constraint) and map to DuckDB `JSON` type (via VARCHAR fallback).
- [x] **Numeric**: Handle `SQLT_NUM` with precision/scale -> `BIGINT`/`DECIMAL`/`DOUBLE`.
- [x] **LOBs**: Handle `CLOB`/`BLOB` using `OCILobLocator` reading.
- [x] **Date/Time**: Handle `SQLT_TIMESTAMP_TZ` and `SQLT_DAT`.
- [x] **Vector**: Handle `SQLT_VEC` (Oracle 23ai) by mapping to `VARCHAR` (DuckDB can cast string repr).

## Phase 4: PL/SQL & Connectivity ✓

- [x] Detect PL/SQL blocks (starts with `BEGIN`, `DECLARE`).
- [x] Execute PL/SQL with `iters=1` and return status row.
- [x] **Wallet**: Add `oracle_attach_wallet(path)` scalar function to set `TNS_ADMIN`.

## Phase 5: Testing & Refinement ✓

- [x] Manual verification test plan.
- [x] Mocking OCI (if feasible) or create robust error handling tests.
- [x] `make format` and `make tidy-check`.
- [x] Implement GitHub Actions CI with Oracle SDK support.
- [x] Implement Integration Tests with Docker/Podman.

## Phase 6: Documentation ✓

- [x] Update `README.md` with `oracle_scan`, `oracle_query`, and JSON/Wallet usage.
- [x] Update `specs/guides/` to reflect final API.