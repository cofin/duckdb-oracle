# Knowledge Base

> Synthesized implementation knowledge from all completed flows.
> For actionable patterns, see [patterns.md](../patterns.md).
> Drill into specific chapters when relevant to current work.

## Chapters

| Chapter | Topics | Summary |
|---------|--------|---------|
| [architecture.md](architecture.md) | Extension structure, catalog, connection, read/write paths | How the DuckDB Oracle extension is organized as a storage extension |
| [oci-patterns.md](oci-patterns.md) | Array fetch, array bind, pooling, spatial, versioning | OCI-specific implementation patterns and critical gotchas |
| [ci-cd.md](ci-cd.md) | Build system, testing, GitHub Actions, releases | Build commands, test infrastructure, and release workflow |
| [write-support.md](write-support.md) | COPY pipeline, type mapping, buffer management | Write path implementation with native OCI bind types |

## Provenance

Knowledge synthesized from 14 archived flows:
- `advanced-oracle-features` — oracle_execute, lazy metadata, schema resolution
- `ci-fix-2025-11-23` — Test separation, connection pooling fixes
- `cleanup-refactor-2025-11-23` — Code quality, dead code removal
- `duckdb-version-update-2025-12-28` — DuckDB v1.4.3→v1.4.4 upgrade strategy
- `oracle-write-support` — COPY/INSERT via OCI Array Bind
- `write-support-fixes` — Column Shift fix, filter pushdown corrections
- `oracle-spatial-geometry` — SDO_GEOMETRY → WKT conversion
- `oracle-secret-management` — DuckDB SecretManager integration
- `oracle-attach-support` — ATTACH TYPE ORACLE implementation
- `oracle-advanced-pushdown` — WHERE clause server-side pushdown
- `native-oracle-query-support` — oracle_query table function
- `oracle-long-support` — LONG/LOB type handling
- `release-distribution-strategy` — Multi-platform unsigned releases
- `release-process-fix` — CI artifact and workflow fixes

## Topic Index

- **OCI**: oci-patterns.md
- **Connection Pooling**: oci-patterns.md, architecture.md
- **Type System**: architecture.md, write-support.md, oci-patterns.md
- **Testing**: ci-cd.md
- **Build**: ci-cd.md
- **Release**: ci-cd.md
- **Write/COPY**: write-support.md
- **Read/Scan**: architecture.md
- **Security**: patterns.md (Security Rules section)
