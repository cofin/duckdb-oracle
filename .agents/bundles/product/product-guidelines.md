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
- Compatibility: Lockstep pin to target DuckDB release (currently v1.5.6, extension v0.2.3) across submodules and build configurations.
- Performance: Maximize throughput using OCI Array Fetch and Array Bind batching, configurable prefetch rows/memory, statement caching (`oracle_stmt_cache_size`), and connection caching.
- Security: Prefer DuckDB SecretManager for credentials; treat raw SQL executed in `oracle_query`/`oracle_execute` as trusted input.
- Platforms: Linux (x86_64/arm64), macOS (arm64), and Windows (x86_64) requiring Oracle Instant Client runtime; WASM excluded.
<!-- truth: end -->

## Guiding Principles
- **Robust Handle Safety**: Never leak OCI handles; always clean up handles on disconnect or exception.
- **DuckDB Integration Standards**: Follow DuckDB core extension patterns, including `Sink` logic for write batches and `Bind` logic for catalog queries.
- **No Silent Truncation**: Respect buffer caps and throw descriptive errors when conversions fail.
