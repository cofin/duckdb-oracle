---
type: Pattern
title: DuckDB Filter & Projection Pushdown Optimization
description: Translating DuckDB TableFilterSet expressions and column projections into Oracle SQL WHERE clauses and projection lists.
scope: pattern
domain: duckdb
tags: [duckdb, pushdown, table-filter-set, query-optimization, sql-generation]
status: stable
---

# DuckDB Filter & Projection Pushdown Optimization

Without pushdown optimization, DuckDB table scans over external databases fetch all columns and all rows across the network, executing filters and projections locally. This wastes network bandwidth and database I/O.

DuckDB Oracle implements both **Projection Pushdown** and **Filter Pushdown** in `src/oracle_pushdown.cpp`.

## 1. Projection Pushdown & Filter Column Pruning (`OracleInitGlobal`)

When a query requests a subset of columns (`SELECT colA FROM ora.table WHERE colB = 10`), DuckDB provides `input.column_ids` and `input.projection_ids` (`filter_prune = true`) in `OracleInitGlobal` (`src/oracle_scan.cpp`):

```cpp
// In OracleInitGlobal: map projected output columns back to active bind columns
state->column_mapping = BuildOutputColumnMapping(input.column_ids, input.projection_ids, bind.column_names.size());
```

- When `projection_ids` is populated (`filter_prune = true`), `BuildOutputColumnMapping` maps each output vector column (`0..projection_ids.size()-1`) to `input.column_ids[input.projection_ids[i]]`.
- Columns used exclusively in pushed-down `WHERE` filters are excluded from `state->active_bind_cols`, omitting them from both the generated Oracle `SELECT` list and OCI `OCIDefineByPos` buffer allocations.
- SQL query construction and `OCIStmtPrepare` are deferred to `OracleInitGlobal` after the optimizer finishes column projection and filter pushdown (`TableFilterSet` and `OraclePushdownComplexFilter`).

## 2. Filter Pushdown (`TableFilterSet` & `TableFilterType`)

DuckDB `v2.0.0` passes filter constraints to the table function via `TableFilterSet` and `OraclePushdownComplexFilter`:

```
TableFilterSet (Iterative Entries() -> FilterEntry{idx_t col_idx, TableFilter &filter})
       |
       +---> CONSTANT_COMPARISON (Equality, Inequality, Comparisons: =, !=, >, >=, <, <=)
       +---> IN_FILTER (Set membership: col IN (val1, val2, val3))
       +---> IS_NULL / IS_NOT_NULL (col IS NULL / col IS NOT NULL)
       +---> CONJUNCTION_AND / CONJUNCTION_OR
       +---> OPTIONAL_FILTER (unwrapped recursively when child filter is supported)
       +---> EXPRESSION_FILTER (evaluated via ExpressionToSQL when child expression is supported)
       +---> DYNAMIC_FILTER / BLOOM_FILTER (safely skipped for local DuckDB execution without disabling pushdown)
```

### Translating to Oracle SQL (`src/oracle_pushdown.cpp`)

`OraclePushdownComplexFilter` and `OraclePushdownFilterSet` translate supported DuckDB filter expressions into Oracle `WHERE` clauses while checking `bind_data.pushdown_eligible[col_idx]`:

```cpp
void OraclePushdownComplexFilter(ClientContext &, LogicalGet &get,
                                 FunctionData *bind_data_p,
                                 vector<unique_ptr<Expression>> &expressions) {
    auto &bind_data = bind_data_p->Cast<OracleBindData>();
    if (!bind_data.settings.enable_pushdown) {
        return;
    }

    const auto &column_ids = get.GetColumnIds();
    for (idx_t i = 0; i < expressions.size();) {
        string clause;
        if (TryExtractPushdownExpression(*expressions[i], column_ids, bind_data.original_names,
                                         bind_data.pushdown_eligible, clause) &&
            !clause.empty()) {
            bind_data.pushdown_clauses.push_back(clause);
            expressions.erase(expressions.begin() + static_cast<ptrdiff_t>(i));
        } else {
            i++;
        }
    }
}
```

### Supported Pushdown Predicates

| DuckDB Expression / Filter | Generated Oracle SQL | Safe Types |
| :--- | :--- | :--- |
| `col = 'ABC'` (`CONSTANT_COMPARISON`) | `"COL" = 'ABC'` | Numeric, `VARCHAR2` (non-empty), `DATE`, `TIMESTAMP` |
| `col >= 100 AND col < 200` (`CONJUNCTION_AND`) | `"COL" >= 100 AND "COL" < 200` | Numeric, `DATE`, `TIMESTAMP` |
| `col IN (1, 2, 3)` (`IN_FILTER` / `COMPARE_IN`) | `"COL" IN (1, 2, 3)` | Numeric, `VARCHAR2` (non-empty) |
| `col IS NULL` / `col IS NOT NULL` | `"COL" IS NULL` / `"COL" IS NOT NULL` | All pushdown-eligible columns |
| `col LIKE 'PREFIX%'` / `prefix` / `suffix` / `contains` | `"COL" LIKE 'PREFIX%' ESCAPE '\'` | `VARCHAR2` (non-empty) |
| `DYNAMIC_FILTER` / `BLOOM_FILTER` | Skipped (`return true` with empty `out_sql`) | Evaluated locally by DuckDB without aborting static filter pushdown |

### Fallback Invariant

If an expression contains functions not guaranteed to have byte-identical semantics in Oracle (e.g., regex matching, complex collations, custom UDFs), the transformer returns `false`. DuckDB retains the expression in `expressions` and evaluates it in memory on the returned chunk, guaranteeing semantic correctness.
