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

The extension translates supported DuckDB filter trees into Oracle SQL:

```cpp
void OraclePushdownComplexFilter(ClientContext &context, LogicalGet &get,
                                 FunctionData *bind_data_p,
                                 vector<unique_ptr<Expression>> &expressions) {
    auto &bind_data = bind_data_p->Cast<OracleBindData>();
    if (!bind_data.settings.enable_pushdown || !bind_data.pushdown_eligible) {
        return;
    }

    vector<string> where_clauses;
    vector<idx_t> expressions_to_remove;

    for (idx_t i = 0; i < expressions.size(); i++) {
        string oracle_expr;
        if (TransformExpressionToOracleSQL(*expressions[i], bind_data, oracle_expr)) {
            where_clauses.push_back(oracle_expr);
            // Mark expression so DuckDB knows it was pushed down
            expressions_to_remove.push_back(i);
        }
    }

    if (!where_clauses.empty()) {
        bind_data.pushdown_clauses = where_clauses;
        bind_data.query = ReconstructOracleQuery(bind_data);
    }
}
```

### Supported Pushdown Predicates

| DuckDB Expression | Generated Oracle SQL | Safe Types |
| :--- | :--- | :--- |
| `col = 'ABC'` | `"COL" = 'ABC'` | Numeric, Varchar, Date |
| `col >= 100 AND col < 200` | `"COL" >= 100 AND "COL" < 200` | Numeric, Date |
| `col IN (1, 2, 3)` | `"COL" IN (1, 2, 3)` | Integer, Varchar |
| `col IS NOT NULL` | `"COL" IS NOT NULL` | All types |
| `col LIKE 'PREFIX%'` | `"COL" LIKE 'PREFIX%'` | Varchar |

### Fallback Invariant

If an expression contains functions not guaranteed to have byte-identical semantics in Oracle (e.g., regex matching, complex collations, custom UDFs), the transformer returns `false`. DuckDB retains the expression in `expressions` and evaluates it in memory on the returned chunk, guaranteeing semantic correctness.
