# Flow Registry

This file tracks all flows for the project. Each flow has its own detailed spec in its respective folder.

---

## Active Flows

### Master PRD: DuckDB 1.5.1 Upgrade + Cleanup
*Link: [./specs/duckdb-151-upgrade/](./specs/duckdb-151-upgrade/)*
*Beads: duckdb-oracle-lgv*

## [ ] Flow: duckdb-upgrade
*Link: [./specs/duckdb-upgrade/](./specs/duckdb-upgrade/)*
*Beads: duckdb-oracle-lgv.1*

## [ ] Flow: geometry-crs
*Link: [./specs/geometry-crs/](./specs/geometry-crs/)*
*Beads: duckdb-oracle-lgv.2*

## [ ] Flow: json-type
*Link: [./specs/json-type/](./specs/json-type/)*
*Beads: duckdb-oracle-lgv.3*

## [ ] Flow: dead-code-removal
*Link: [./specs/dead-code-removal/](./specs/dead-code-removal/)*
*Beads: duckdb-oracle-lgv.4*

## [ ] Flow: finalize-pr
*Link: [./specs/finalize-pr/](./specs/finalize-pr/)*
*Beads: duckdb-oracle-lgv.5*

## Completed Flows

### [x] Flow: Legacy PRD Migration — Oracle Write Support & Community Release
*Link: [./specs/legacy-migration/](./specs/legacy-migration/)*
*Completed: 2026-03-02*

Delivered native OCI bind types for write operations, integration tests for write support, `description.yml` for community extension submission, and DuckDB v1.4.4 upgrade.

## Archived Flows (Pre-Flow Framework)

The following work was completed before the Flow framework was adopted. Learnings have been synthesized into `.agents/knowledge/` and `.agents/patterns.md`.

| Archive | Date | Summary |
|---------|------|---------|
| `advanced-oracle-features` | 2025-11-23 | oracle_execute, lazy metadata, schema resolution |
| `ci-fix-2025-11-23` | 2025-11-23 | Test separation, connection pooling fix |
| `cleanup-refactor-2025-11-23` | 2025-11-23 | Code quality, dead code removal |
| `duckdb-version-update-2025-12-28` | 2025-12-28 | DuckDB v1.4.3 upgrade |
| `native-oracle-query-support` | 2025-11-22 | oracle_query table function |
| `oracle-advanced-pushdown` | 2025-11-22 | WHERE clause server-side pushdown |
| `oracle-attach-support` | 2025-11-22 | ATTACH TYPE ORACLE implementation |
| `oracle-long-support` | 2025-11-27 | LONG/LOB type handling |
| `oracle-secret-management` | 2025-11-22 | DuckDB SecretManager integration |
| `oracle-spatial-geometry` | 2025-11-22 | SDO_GEOMETRY → WKT conversion |
| `oracle-write-support` | 2025-11-26 | COPY/INSERT via OCI Array Bind |
| `release-distribution-strategy` | 2025-11-26 | Multi-platform unsigned releases |
| `release-process-fix` | 2025-12-05 | CI artifact and workflow fixes |
| `write-support-fixes` | 2025-11-26 | Column Shift fix, pushdown corrections |
