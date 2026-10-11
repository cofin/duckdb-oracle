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

## 1. Projection Pushdown (`OracleInitGlobal`)

When a query requests a subset of columns (`SELECT colA, colB FROM ora.table`), DuckDB provides the projection list via `input.column_ids` in `OracleInitGlobal` (`src/oracle_scan.cpp`):

```cpp
// In OracleInitGlobal: build projection list from input.column_ids
for (auto col_idx : input.column_ids) {
    if (col_idx == COLUMN_IDENTIFIER_ROW_ID) {
        projected_sql_columns.push_back("NULL");
    } else {
        auto &col_name = bind_data.original_names[col_idx];
        // Wrap VECTOR columns with VECTOR_SERIALIZE(... RETURNING CLOB)
        // and SDO_GEOMETRY columns with SDO_UTIL.TO_WKTGEOMETRY(...)
        projected_sql_columns.push_back(BuildProjectedColumnSQL(col_name, bind_data, col_idx));
    }
}
```

- SQL query construction and `OCIStmtPrepare` are deferred to `OracleInitGlobal` after the optimizer finishes column projection and filter pushdown (`TableFilterSet` and `OraclePushdownComplexFilter`).
- Unprojected columns are completely omitted from the generated Oracle `SELECT` statement, and OCI only defines buffers (`OCIDefineByPos`) for projected columns.

## 2. Filter Pushdown (`TableFilterSet`)

DuckDB's optimizer passes filter constraints to the table function via `TableFilterSet`:

```
TableFilterSet (Map of column_index -> unique_ptr<TableFilter>)
       |
       +---> ConstantFilter (Equality, Inequality, Comparisons: >, >=, <, <=)
       +---> InFilter (Set membership: col IN (val1, val2, val3))
       +---> IsNullFilter / IsNotNullFilter (col IS NULL / col IS NOT NULL)
       +---> ConjunctionAndFilter / ConjunctionOrFilter
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
