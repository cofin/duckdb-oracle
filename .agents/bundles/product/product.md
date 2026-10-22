---
type: Guide
title: Product Definition
description: Native C++ DuckDB extension providing direct read and write access to Oracle databases via lazy runtime OCI loading.
tags: [duckdb, oracle, oci, extension, product]
status: stable
---

# Product Definition

<!-- truth: start -->
- `duckdb-oracle` (`v0.3.0`, targeting DuckDB `v2.0.0` LTS) is a native C++17 DuckDB extension providing direct read and write access to Oracle databases via the Oracle Call Interface (OCI).
- Uses in-tree OCI C ABI declarations and lazy runtime dynamic loading (`dlopen`/`LoadLibraryA`) so `INSTALL oracle FROM community; LOAD oracle;` requires zero build-time or load-time Oracle SDK dependencies, while supporting Oracle Instant Client 19c, 21c, and 23ai at connection time.
- Supports DuckDB SecretManager (`TYPE oracle`) and `ATTACH '' AS ora (TYPE oracle, SECRET ...)` workflow for native catalog integration.
- Implements DuckDB 2.0 `TableFunction` optimizer callbacks (`get_bind_info`, `get_table_entry`, `cardinality` via single-round-trip `ALL_TABLES.NUM_ROWS`, and `to_string`), OCI Array Fetch and `UnifiedVectorFormat` Array Bind for batch reads/inserts, spatial data mapping (`SDO_GEOMETRY` -> DuckDB `GEOMETRY` with CRS), Oracle 23ai `VECTOR` support (`ARRAY`/`LIST`), Oracle 21c/23ai `JSON`, flashback queries (`AS OF SCN` / `AS OF TIMESTAMP`), schema auto-detection, synonym resolution, and filter/projection pushdown.
- Built for Linux (`x86_64`, `aarch64`), macOS (`arm64`), and Windows (`x86_64`).
<!-- truth: end -->

## Key Capabilities
- **Direct Queries & Attached Databases**: Attach Oracle instances as DuckDB databases; scan tables directly or use `oracle_query`, `oracle_scan`, and `oracle_execute`.
- **Zero-SDK Build & Lazy Runtime OCI Loading**: Loads `libclntsh.so` / `libclntsh.dylib` / `oci.dll` on first connection attempt (`OCI_LIB_DIR`, `ORACLE_HOME`, or system loader path).
- **Optimizer & Pushdown Integration**: Pushes `WHERE` clauses, column projections, flashback clauses, and `ALL_TABLES.NUM_ROWS` cardinality estimates to Oracle.
- **Data Types**: Comprehensive type mapping including numeric, temporal, binary, JSON, spatial (`SDO_GEOMETRY`), and vector embeddings (`VECTOR`).
