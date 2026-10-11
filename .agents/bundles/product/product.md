---
type: Guide
title: Product Definition
---

# Product Definition

<!-- truth: start -->
- `duckdb-oracle` is a native C++ DuckDB extension providing high-performance direct read and write access to Oracle databases via the Oracle Call Interface (OCI).
- Supports DuckDB SecretManager (`TYPE oracle`) and `ATTACH '' AS ora (TYPE oracle, SECRET ...)` workflow for intuitive querying.
- Features OCI Array Fetch and Array Bind for batch reads/inserts, spatial data mapping (`SDO_GEOMETRY` -> DuckDB `GEOMETRY`), Oracle 23ai `VECTOR` support (`LIST(FLOAT)`), schema auto-detection, synonym resolution, and filter/projection pushdown.
- Built for Linux (x86_64, aarch64), macOS (arm64), and Windows (x86_64), requiring Oracle Instant Client runtime.
<!-- truth: end -->

## Key Capabilities
- **Direct Queries & Attached Databases**: Attach Oracle instances as DuckDB databases; scan tables directly or use `oracle_query`, `oracle_scan`, and `oracle_execute`.
- **Batch Processing**: OCI Array Fetch and Bind tuned for high throughput.
- **Pushdown Optimization**: Pushes `WHERE` clauses and column projections to Oracle.
- **Data Types**: Comprehensive type mapping including numeric, temporal, binary, spatial (`SDO_GEOMETRY`), and vector embeddings.
