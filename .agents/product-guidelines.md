# Product Guidelines: DuckDB Oracle Extension

## Core Principles

1.  **Elite Performance**: Maximize performance and throughput. The extension must leverage Oracle's capabilities (like OCI Array Fetch/Bind, Direct Path Loads, and filter/projection pushdown) and DuckDB's vectorized execution to achieve the highest possible data transfer rates.
2.  **Stability & Correctness**: Performance must not come at the cost of stability or data corruption (e.g., handling OCI "Column Shift" issues robustly). Error handling must be rigorous, wrapping OCI status codes and raising clear, informative exceptions.
3.  **Deliberate Trade-offs**: When a compromise between raw performance and stability/complexity is required, options must always be evaluated and presented explicitly before a decision is made.

## Development & Implementation Rules

-   **Defensive OCI Interaction**: Always account for OCI driver quirks (e.g., binding numbers and dates as `SQLT_STR` when necessary to prevent buffer aliasing).
-   **Resource Management**: Strictly manage OCI environment and connection handles using RAII patterns (e.g., `OracleConnectionManager` singleton) to prevent memory leaks and connection hangs.
-   **Security First**: Never concatenate user input directly into SQL strings for execution (e.g., in `oracle_execute`). Require the use of DuckDB SecretManager for credential handling.
-   **Scalability**: Ensure metadata operations are bounded. Use lazy schema loading (`oracle_lazy_schema_loading`) and configurable metadata result limits to support enterprise-scale Oracle databases (>100k tables) without overwhelming memory or attach times.

## User Experience

-   **Sensible Defaults**: Default settings must prioritize stability and a smooth out-of-the-box experience, while providing clear configuration knobs (e.g., `oracle_array_size`, `oracle_prefetch_rows`) for advanced users to tune performance.
-   **Actionable Errors**: When operations fail, error messages should directly bubble up the underlying Oracle OCI error text so the user knows exactly what to fix.