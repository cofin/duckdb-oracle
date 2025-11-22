# Feature: Oracle Pushdown & Session Tuning (Phase 1)

**Created**: 2025-11-22  
**Status**: Planning

## Overview
Add production-ready predicate/projection/limit pushdown for Oracle sources plus session tuning knobs (prefetch/array size), connection caching, debug logging, and attach options. This brings the Oracle extension to parity with MySQL/Postgres DuckDB extensions and aligns with community patterns (BigQuery/Snowflake) while remaining feasible atop the current `oracle_query`/`oracle_scan` functions.

## Problem Statement
Today `oracle_scan` and `oracle_query` always pull full result sets and evaluate predicates client-side. This wastes network bandwidth and increases latency, especially for selective workloads. There are no tuning controls for OCI prefetch or array fetch size, so throughput is suboptimal. Users also lack attach-time switches to enable/disable pushdown or inspect generated SQL, limiting debuggability and operational control. Finally, there is no schema/connection cache lifecycle hook, making long-lived sessions harder to manage.

## Goals
**Primary**
- Push down projection, selection, and limit into Oracle when enabled.
- Expose OCI tuning settings (`prefetch_rows`, `prefetch_memory`, `array_size`) and cache/debug switches.
- Provide attach options that map to these settings for convenience.

**Secondary**
- Add a cache clear function for metadata/schema caches.
- Keep backward compatibility: default behavior equals current (pushdown off).

## Acceptance Criteria
- [ ] Setting `SET oracle_enable_pushdown=true` enables server-side projection/selection/limit for both `oracle_scan` and `oracle_query`.
- [ ] Settings `oracle_prefetch_rows`, `oracle_prefetch_memory`, `oracle_array_size` apply to OCI statements and are validated (positive integers, sane upper bounds).
- [ ] Attach supports options: `enable_pushdown`, `prefetch_rows`, `array_size`, `read_only`, `SECRET <alias>`; options are reflected in the session.
- [ ] `oracle_clear_cache()` clears connection/schema cache without crashing active sessions.
- [ ] When pushdown disabled, behavior matches current results bit-for-bit.
- [ ] SQL tests cover pushdown on/off, limits, filter correctness, and setting validation.

## Technical Design
### Architecture
- **Planner hook**: extend `oracle_scan`/`oracle_query` bind to accept optional pushdown context (projection list, filters, limit). For phase 1, serialize simple conjunctive predicates (`=`, `<`, `<=`, `>`, `>=`, `<>`, `IN`, `IS NULL/NOT NULL`) and limits.
- **SQL generator**: construct `SELECT <proj> FROM <table_or_subquery> WHERE <filters> LIMIT <n>`; if serialization fails, fall back to no pushdown.
- **Settings** (DuckDB config options):
  - `oracle_enable_pushdown` (bool, default false)
  - `oracle_prefetch_rows` (uint, default 200)
  - `oracle_prefetch_memory` (uint bytes, default 0 meaning auto)
  - `oracle_array_size` (uint rows, default 256)
  - `oracle_connection_cache` (bool, default true)
  - `oracle_connection_limit` (uint, default 8)
  - `oracle_debug_show_queries` (bool, default false)
- **OCI tuning**: before `OCIStmtExecute`, set `OCI_ATTR_PREFETCH_ROWS` / `OCI_ATTR_PREFETCH_MEMORY`; set `OCI_ATTR_ROW_PREFETCH` (array fetch) and use `OCIStmtFetch2` with `itrs=array_size`.
- **Connection cache**: LRU or simple map keyed by connection string; limit by `oracle_connection_limit`. Provide `oracle_clear_cache()` scalar to drop entries.
- **Attach integration**: map attach options to settings (using `Load` to register an attach handler or documented ATTACH syntax) so users can run `ATTACH 'user/pw@//host:1521/db' AS ora (TYPE oracle, enable_pushdown=true, prefetch_rows=200, array_size=256)`.
- **Debug**: when enabled, log generated SQL and pushdown decisions to DuckDB logger.

### Affected Components
- `src/oracle_extension.cpp` (bind logic, settings, OCI tuning, cache)
- `test/sql/` (new pushdown/setting tests)
- Potential helpers: new `oracle_cache.cpp` if cache extracted.

### Error Handling
- Invalid setting values → `CatalogException` with clear message.
- OCI tuning failures → non-fatal warning, continue with defaults.
- Serialization failures → disable pushdown for that query only.

### C++ Signatures (proposed)
```cpp
struct OraclePushdownState {
    optional<string> projection_sql;
    optional<string> filter_sql;
    optional<int64_t> limit;
};

unique_ptr<FunctionData> OracleBindInternal(..., const PushdownInfo *pushdown);

void ApplyOciTuning(OCIStmt *stmt, OCIError *errhp, idx_t prefetch_rows,
                    idx_t prefetch_mem, idx_t array_size);
```

## Testing Strategy
- **SQL tests** (`test/sql/oracle_pushdown.test`):
  - pushdown off (default) vs on: row counts identical.
  - selection predicate pushdown (`WHERE id = 1`), projection (`SELECT col1`), limit (`LIMIT 5`).
  - complex predicate fallback (e.g., regex) still succeeds without pushdown.
  - setting validation (negative values error).
  - prefetch/array size sanity (does not crash, respects limits).
- **Integration** (optional, guarded) against local Oracle container to validate server-side filtering.

## Risks & Constraints
- **Predicate coverage**: limited expression support may surprise users; document fallback.
  * Mitigation: emit debug logs and use capability flag.
- **OCI memory pressure** from large prefetch: mitigate with caps and defaults.
- **Thread safety** for cache: start single-threaded; later introduce mutexes if parallel scans needed.

## References
- MySQL extension settings/pushdown: citeturn0search1turn0search3  
- Postgres extension pushdown/cache/debug: citeturn0search2  
- BigQuery extension pushdown/auth patterns: citeturn0search8  
- Snowflake extension pushdown flag: citeturn0search4  
- OCI prefetch/array best practices: citeturn1search0turn1search1turn1search4
