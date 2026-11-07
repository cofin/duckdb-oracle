---
type: Guide
title: Product Definition
description: Native C++17 DuckDB extension providing direct read and write access to Oracle databases via lazy runtime OCI loading.
status: stable
tags: [duckdb, oracle, oci, extension, product]
generated:
  by: flow/0.27.0
  at: 2026-11-07T17:36:00Z
verified:
  - by: agent:flow-setup
    at: 2026-11-07T17:36:00Z
    commit: 36539f3
sources:
  - id: desc-yml
    resource: description.yml
    title: Community extension descriptor and version metadata
  - id: ext-entry
    resource: src/oracle_extension.cpp::LoadInternal
    title: DuckDB Oracle extension entrypoint and registration
  - id: oci-api
    resource: src/oracle_oci_api.cpp::OCIInitClient
    title: Lazy runtime OCI dynamic loader
---

# Product Definition

<!-- truth: start -->
- `duckdb-oracle` (`v0.3.1`, targeting DuckDB `v2.0.0` LTS on `main` and DuckDB `v1.5.6` on `feat/duckdb-v1.5.6`) is a native C++17 DuckDB extension providing direct read and write access to Oracle databases via the Oracle Call Interface (OCI)[^desc-yml][^ext-entry].
- Uses in-tree OCI C ABI declarations and lazy runtime dynamic loading (`dlopen`/`LoadLibraryA`) so `INSTALL oracle FROM community; LOAD oracle;` requires zero build-time or load-time Oracle SDK dependencies, while supporting Oracle Instant Client 19c, 21c, and 23ai at connection time[^oci-api].
- Supports DuckDB SecretManager (`TYPE oracle`) and `ATTACH '' AS ora (TYPE oracle, SECRET ...)` workflow for native catalog integration[^ext-entry].
- Implements DuckDB 2.0 `TableFunction` optimizer callbacks (`get_bind_info`, `get_table_entry`, `cardinality` via single-round-trip `ALL_TABLES.NUM_ROWS`, and `to_string`), OCI Array Fetch and `UnifiedVectorFormat` Array Bind for batch reads/inserts, spatial data mapping (`SDO_GEOMETRY` -> DuckDB `GEOMETRY` with CRS), Oracle 23ai `VECTOR` support (`ARRAY`/`LIST`), Oracle 21c/23ai `JSON`, flashback queries (`AS OF SCN` / `AS OF TIMESTAMP`), schema auto-detection, synonym resolution, and filter/projection pushdown.
- Built for Linux (`x86_64`, `aarch64`), macOS (`arm64`), and Windows (`x86_64`)[^desc-yml].
<!-- truth: end -->

## Key Capabilities
- **Direct Queries & Attached Databases**: Attach Oracle instances as DuckDB databases; scan tables directly or use `oracle_query`, `oracle_scan`, and `oracle_execute`[^ext-entry].
- **Zero-SDK Build & Lazy Runtime OCI Loading**: Loads `libclntsh.so` / `libclntsh.dylib` / `oci.dll` on first connection attempt (`OCI_LIB_DIR`, `ORACLE_HOME`, or system loader path)[^oci-api].
- **Optimizer & Pushdown Integration**: Pushes `WHERE` clauses, column projections, flashback clauses, and `ALL_TABLES.NUM_ROWS` cardinality estimates to Oracle.
- **Data Types**: Comprehensive type mapping including numeric, temporal, binary, JSON, spatial (`SDO_GEOMETRY`), and vector embeddings (`VECTOR`)[^desc-yml].

[^desc-yml]: `description.yml` — Community extension descriptor and version metadata.
[^ext-entry]: `src/oracle_extension.cpp::LoadInternal` — DuckDB Oracle extension entrypoint and registration.
[^oci-api]: `src/oracle_oci_api.cpp::OCIInitClient` — Lazy runtime OCI dynamic loader.
