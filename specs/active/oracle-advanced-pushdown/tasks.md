# Implementation Tasks: Oracle Pushdown & Session Tuning (Phase 1)

**Created**: 2025-11-22  
**Status**: Not Started

## Phase 1: Planning ✓
- [x] PRD & research completed; recovery scaffold present.

## Phase 2: Core Implementation
- [ ] Add DuckDB config options: `oracle_enable_pushdown`, `oracle_prefetch_rows`, `oracle_prefetch_memory`, `oracle_array_size`, `oracle_connection_cache`, `oracle_connection_limit`, `oracle_debug_show_queries`.
- [ ] Implement pushdown serialization (projection, simple conjunctive filters, limit) and integrate into `OracleBindInternal`.
- [ ] Apply OCI tuning (`OCI_ATTR_PREFETCH_ROWS`, `OCI_ATTR_PREFETCH_MEMORY`, array fetch) in bind/execute.
- [ ] Add lightweight connection/schema cache with limit; expose `oracle_clear_cache()` scalar.
- [ ] Extend attach handling to accept options and map them to settings.
- [ ] Add debug logging hook for generated SQL / pushdown decisions.

## Phase 3: Testing
- [ ] Create `test/sql/oracle_pushdown.test` covering pushdown on/off, predicates, projection, limit, fallback behavior, setting validation.
- [ ] (Optional) Integration test against local Oracle container for server-side filtering correctness.

## Phase 4: Documentation
- [ ] Update `specs/guides/architecture.md` and `development-workflow.md` with new settings and attach options.
- [ ] Add README snippet showing pushdown usage and settings.

## Phase 5: QA & Archive
- [ ] `make format` and `make test` pass.
- [ ] Capture learnings; archive workspace per process.
