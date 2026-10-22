---
type: Guide
title: Product Guidelines
description: Architectural, compatibility, performance, and security invariants for the DuckDB Oracle extension.
tags: [duckdb, oracle, oci, guidelines, architecture]
status: stable
---

# Product Guidelines

<!-- truth: start -->
- Architecture: Manage all OCI handles (`OCIEnv`, `OCISvcCtx`, `OCIError`, `OCIStmt`, `OCIBind`) via RAII patterns and smart pointers (`std::shared_ptr<OCIEnv>`); convert OCI errors to DuckDB `IOException`.
- Compatibility: Lockstep pin to target DuckDB release (currently `v2.0.0` LTS, extension `v0.3.0`) across submodules, `extension-ci-tools` (`v2.0-cyanoptera`), and `description.yml`.
- Performance: Maximize throughput using OCI Array Fetch and `UnifiedVectorFormat` Array Bind batching, DuckDB 2.0 `TableFunction` optimizer callbacks (`get_bind_info`, `get_table_entry`, `cardinality`, `to_string`, `filter_prune`), zero-copy `OracleSlice` numeric parsing, configurable prefetch rows/memory (`oracle_prefetch_rows = 1024`, `oracle_array_size = 256`), statement caching (`oracle_stmt_cache_size`), and connection caching.
- Security: Prefer DuckDB SecretManager (`TYPE oracle`) and SEPS wallet authentication (`/@alias`) for credentials; treat raw SQL executed in `oracle_query`/`oracle_execute` as trusted input.
- Platforms: Linux (`x86_64`, `aarch64`), macOS (`arm64`), and Windows (`x86_64`) with zero build-time or `LOAD`-time Oracle SDK dependency and lazy runtime `dlopen`/`LoadLibraryA` resolution of Oracle Instant Client (19c, 21c, 23ai) on first connection; WASM excluded.
<!-- truth: end -->

## Guiding Principles
- **Handle Lifecycle Safety**: Never leak OCI handles; free child statement, session, service context, server, and error handles before releasing `std::shared_ptr<OCIEnv>`.
- **DuckDB 2.0 Extension Standards**: Follow DuckDB `v2.0.0` extension contracts, including `DUCKDB_CPP_EXTENSION_ENTRY(oracle, loader)`, `DataChunk::SetChildCardinality` vector size synchronization, `UnifiedVectorFormat` in physical sinks, and direct `Catalog` / `SchemaCatalogEntry` inheritance.
- **No Silent Truncation**: Inspect indicator arrays (`ind == -2 || ind > 0`) before `OCI_SUCCESS_WITH_INFO` checks and throw descriptive `IOException` errors when conversions or buffer bounds fail.
