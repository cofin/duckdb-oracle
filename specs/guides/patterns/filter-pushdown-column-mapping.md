# Pattern: Filter Pushdown Column Mapping

## Problem

When implementing `pushdown_complex_filter` for a Table Function that acts as a Table Scan (`filter_pushdown = false`), DuckDB's optimizer may prune columns from the scan output (e.g., columns used only for filtering).

However, if the extension pushes down the filter to the SQL query but **also** keeps the filter column in the SQL `SELECT` list (to support the `WHERE` clause or because of simple `SELECT *` construction), there is a mismatch:

- **SQL Result**: 2 columns (e.g., `ID`, `VAL`)
- **DuckDB Expectation**: 1 column (e.g., `VAL`, because `ID` was pruned after filter pushdown)

This leads to column misalignment: the extension reads buffer 0 (`ID`) into output column 0 (`VAL`), resulting in wrong data.

## Solution: Column Mapping

Implement a dynamic mapping between the DuckDB requested columns (`column_ids`) and the actual SQL result columns.

### 1. OracleInitGlobal

In the global initialization, build a `column_mapping` vector.

```cpp
// Input: input.column_ids (indices into table schema requested by DuckDB)
// Input: bind.column_names (columns present in the SQL query result)

if (!input.column_ids.empty()) {
    state->column_mapping.resize(input.column_ids.size());
    for (idx_t i = 0; i < input.column_ids.size(); i++) {
        idx_t col_idx = input.column_ids[i];
        if (col_idx < bind.original_names.size()) {
            string name = bind.original_names[col_idx];
            // Find this name in the actual SQL result columns
            bool found = false;
            for (idx_t j = 0; j < bind.column_names.size(); j++) {
                if (bind.column_names[j] == name) {
                    state->column_mapping[i] = j; // Map Output[i] -> Buffer[j]
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Fallback/Error handling
                state->column_mapping[i] = i;
            }
        }
    }
}
```

### 2. OracleQueryFunction

In the scan function, use the mapping to access the correct buffer.

```cpp
for (idx_t col_idx = 0; col_idx < output.ColumnCount(); col_idx++) {
    idx_t buffer_idx = col_idx;
    // Apply mapping if available
    if (col_idx < gstate.column_mapping.size()) {
        buffer_idx = gstate.column_mapping[col_idx];
    }
    
    // Use buffer_idx to access OCI buffers
    char *ptr = gstate.buffers[buffer_idx].data() + ...;
    // ... write to output.data[col_idx] ...
}
```

## Key Takeaways

1. **Do NOT modify `bind_data` or `returned_types` in `PushdownComplexFilter`** just to prune columns if you are implementing a Table Scan. DuckDB handles pruning via `column_ids` at runtime.
2. **Always respect `column_ids`** passed to `InitGlobal`. This is the source of truth for what DuckDB expects in `output`.
3. **Decouple SQL Projection from Scan Output**: Your SQL query can return *more* columns than DuckDB asks for (e.g., for debugging or filter pushdown implementation simplicity). The mapping layer bridges the gap.
