# Implementation Tasks: Oracle Pushdown & Session Tuning (Phase 1)

**Created**: 2025-11-22  
**Status**: Not Started

## Phase 1: Planning ✓
- [x] PRD & research completed; recovery scaffold present.

## Phase 2: Core Implementation
- [x] Add DuckDB config options: `oracle_enable_pushdown`, `oracle_prefetch_rows`, `oracle_prefetch_memory`, `oracle_array_size`, `oracle_connection_cache`, `oracle_connection_limit`, `oracle_debug_show_queries`.
- [x] Implement pushdown serialization (projection, simple conjunctive filters) gated by `oracle_enable_pushdown` and integrate into `OracleBindInternal`.
- [x] Apply OCI tuning (`OCI_ATTR_PREFETCH_ROWS`, `OCI_ATTR_PREFETCH_MEMORY`) in bind/execute (array-size tuning kept via prefetch).
- [x] Add lightweight connection/schema cache with `oracle_clear_cache()` scalar.
- [x] Extend attach handling to accept options and map them to settings.
- [x] Add debug logging hook for generated SQL / pushdown decisions.

## Phase 3: Testing
- [x] Create `test/sql/oracle_pushdown.test` covering settings and cache helper (non-OCI smoke coverage).
- [ ] (Optional) Integration test against local Oracle container for server-side filtering correctness.

## Phase 4: Documentation
- [x] Update `specs/guides/architecture.md` and `development-workflow.md` with new settings and attach options.
- [x] Add README snippet showing pushdown usage and settings.

## Phase 5: QA & Archive
- [ ] `make format` and `make test` pass.
- [ ] Capture learnings; archive workspace per process.
