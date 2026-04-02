# Research Plan: Oracle Advanced Pushdown

**Created**: 2025-11-22

## Research Questions
1. Which pushdown capabilities are supported by existing DuckDB remote extensions (filter/projection/limit/order/aggregate)?
2. How are pushdown toggles, debug flags, cache controls, and attach options exposed?
3. What auth/secret patterns are used (wallets, GCP secrets, DSN strings)?
4. What OCI-specific performance best practices should inform our implementation (prefetch, array fetch, LOB considerations)?
5. Given current code (table/query + wallet helper), which advanced features are realistic for a first pushdown iteration?

## External Findings
- **DuckDB MySQL extension**: `mysql_experimental_filter_pushdown` flag, schema cache + `mysql_clear_cache`, debug logging, time zone setting, attach via `ATTACH ... (TYPE mysql)`. citeturn0search1turn0search3
- **DuckDB Postgres extension**: `pg_experimental_filter_pushdown`, `pg_connection_cache`, `pg_connection_limit`, binary copy toggle, debug flag, projection/selection/limit pushdown; `postgres_execute` for arbitrary SQL. citeturn0search2
- **Community BigQuery extension**: supports projection/limit/filter pushdown, secret-based auth, attach options (execution project override), DDL/DML support. citeturn0search8
- **Snowflake extension (community)**: filter/projection pushdown gated by `enable_pushdown true` attach option. citeturn0search4
- **OCI performance guidance**: set `OCI_ATTR_PREFETCH_ROWS` / `OCI_ATTR_PREFETCH_MEMORY` on statements to reduce round-trips; defaults to 1; LOB prefetch allowed; disable by zeroing attrs. citeturn1search0turn1search4
- **OCI array fetch**: leverage array interface and `OCI_ATTR_ROWS_FETCHED` to fetch multiple rows per call for throughput. citeturn1search1

## Feasible Feature Set (phase 1)
- Filter + projection + limit pushdown for `oracle_scan`/`oracle_query`, controlled by setting `oracle_enable_pushdown` (default off).
- Statement tuning knobs: `oracle_prefetch_rows`, `oracle_prefetch_memory`, `oracle_array_size`.
- Connection/session knobs: `oracle_connection_cache` (bool), `oracle_connection_limit` (int).
- Debug logging: `oracle_debug_show_queries` to emit generated SQL.
- Cache maintenance: `oracle_clear_cache()` to drop metadata/schema cache.
- Attach helper: `ATTACH 'connstr' AS ora (TYPE oracle, enable_pushdown=true, prefetch_rows=200, array_size=256, read_only, SECRET wallet_alias)`.
- Keep writes/DDL out-of-scope for now; reconsider after pushdown baseline.

## Risks / Constraints
- Predicate serialization must cover basic comparisons and `IN` lists; complex expressions may need fallback to client evaluation.
- OCI prefetch/array settings can affect memory use; need sane defaults (e.g., prefetch_rows=200, array_size=256) and validation.
- Connection cache needs thread safety; initial single-connection cache keyed by connection string is acceptable.

## Decision
Pursue MySQL/Postgres-level pushdown (filters, projection, limit) with OCI prefetch tuning and debug/caching controls; defer aggregates and write-path features.
