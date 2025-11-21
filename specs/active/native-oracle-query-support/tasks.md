# Implementation Tasks: Native Oracle Query Support

**Created**: 2025-11-21
**Status**: In Progress

## Phase 1: Research & Planning ✓

- [x] Read internal guides
- [x] Create PRD
- [x] Create tasks list
- [x] Research OCI data types and mappings

## Phase 2: Core Implementation (Data Types)

- [ ] Refactor `OracleQueryBind` to perform detailed type introspection (`OCI_ATTR_DATA_TYPE`, `OCI_ATTR_PRECISION`, `OCI_ATTR_SCALE`).
- [ ] Implement mapping logic for `NUMBER` (to `BIGINT`, `DECIMAL`, or `DOUBLE`).
- [ ] Implement mapping logic for `DATE` and `TIMESTAMP` (to `TIMESTAMP`).
- [ ] Implement mapping logic for `CLOB` and `BLOB` (to `VARCHAR` and `BLOB`).
- [ ] Implement mapping logic for `RAW` (to `BLOB`).
- [ ] Update `OracleQueryFunction` to define output variables using correct OCI types (`SQLT_INT`, `SQLT_FLT`, `SQLT_LBI`, etc.).
- [ ] Handle `NULL` indicators correctly for all types.

## Phase 3: PL/SQL Support

- [ ] Add logic to detect PL/SQL blocks (or allow a flag/separate function).
- [ ] Modify execution path to handle non-SELECT statements (PL/SQL blocks).
- [ ] Ensure transaction commit/rollback is handled (OCI default is usually auto-commit off, check this).

## Phase 4: Testing & Refinement

- [ ] Create `test/sql/oracle_types.test` (placeholder for manual verification or if mock available).
- [ ] Verify error handling (e.g. bad connection string, SQL syntax error).
- [ ] Run `make format` and `make tidy-check`.

## Phase 5: Documentation

- [ ] Update `README.md` with supported types list.
- [ ] Update `specs/guides/` if new patterns emerge.
