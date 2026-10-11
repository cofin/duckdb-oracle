---
type: Spec
flow_id: storage-catalog-write-pipeline
title: "Chapter 3: Storage Extension, Catalog Isolation, Metadata Queries, Transactions & Write Pipeline"
state: planned
plan_revision: 1
plan_commit: 4d4fd44274bda21b9de7aa3efad471dd23b1789d
state_revision: 1
current_task: null
last_operation: null
operation_targets: []
last_verified_checkpoint: null
created_at: 2026-10-11T17:13:00Z
updated_at: 2026-10-11T17:13:00Z
description: Remediate shared singleton OracleStorageInfo::state corrupting multi-ATTACH isolation, OracleTransactionManager::active data race, OracleCatalogState::connection self-deadlock on connection_limit = 1, DuckCatalog assertion/DDL overrides, PlanInsert cross-schema and synonym routing, OracleTableEntry redundant partition queries and original_types restoration, and ub2 write truncation.
tags: [bugfix, oracle-oci, storage, catalog, write-pipeline, transactions]
parent_prd: codebase-remediation-prd
research: []
---

# Chapter 3: Storage Extension, Catalog Isolation, Metadata Queries, Transactions & Write Pipeline

**Flow ID:** `storage-catalog-write-pipeline`
**Parent PRD:** [`codebase-remediation-prd`](../codebase-remediation-prd/spec.md)

## Problem & Goal

The storage extension, catalog, schema/table entry, transaction manager, and write/insert layer has 7 High, 5 Medium, and 2 Low findings (`H-18`..`H-24` part, `M-18`..`M-22`, `L-08`..`L-09`):
- `CreateOracleStorageExtension()` allocates a single `OracleStorageInfo` instance shared across all `ATTACH` calls (`storage_info->Cast<OracleStorageInfo>().state`), so a second `ATTACH` overwrites the first database's state, skips alias registration, and retains stale state after `DETACH`.
- `OracleTransactionManager::active` (`vector<unique_ptr<OracleTransaction>>`) is mutated by `StartTransaction` and `EraseTransaction` without a mutex, causing data races under concurrent queries.
- `OracleCatalogState::connection` pins a pooled connection indefinitely when `oracle_connection_cache = true`, causing a guaranteed 10-second self-deadlock when `oracle_connection_limit = 1`.
- `OracleCatalog` (`IsDuckCatalog() == false`) and `OracleSchemaEntry` (`: public DuckSchemaEntry`) do not override `DuckCatalog`-specific virtual methods (`InMemory`, `GetDBPath`, `GetDatabaseSize`, `CreateTable`, `DropEntry`, etc.), triggering `D_ASSERT(catalog_p.IsDuckCatalog())` aborts in debug builds and `InternalException` crashes on `duckdb_databases()`, `PRAGMA database_size`, and DDL.
- `OracleCatalog::PlanInsert` drops the resolved target schema name and synonym target (`op.table.name` only), and `PhysicalOracleInsert` splits `table_name` on `"."`, corrupting cross-schema inserts, synonym inserts, and quoted table names containing dots.
- `OracleTableEntry::Create` executes 5 unused partition metadata queries on every table lookup, and `OracleTableEntry::GetScanFunction` fails to restore `oracle_bind.original_types` after `OracleBindInternal` re-describes rewritten expressions as `VARCHAR`.
- `OracleWriteGlobalState::Sink` and `BindColumn` silently cast string byte lengths to `ub2` (`uint16_t`), wrapping values `> 65,535` bytes modulo 65,536, and convert every string value twice per batch.

## Scope & Constraints

- **In Scope**:
  - `src/include/oracle_storage_extension.hpp`, `src/storage/oracle_storage_extension.cpp`, `src/include/oracle_transaction_manager.hpp`, `src/storage/oracle_transaction_manager.cpp`
  - `src/include/oracle_catalog_state.hpp`, `src/storage/oracle_catalog.cpp`
  - `src/include/oracle_catalog.hpp`, `src/storage/oracle_schema_entry.cpp`
  - `src/include/oracle_table_entry.hpp`, `src/storage/oracle_table_entry.cpp`
  - `src/include/oracle_write.hpp`, `src/storage/oracle_write.cpp`, `src/include/oracle_insert.hpp`, `src/storage/oracle_insert.cpp`
- **Out of Scope**:
  - OCI connection pool, secrets, and settings (Chapter 1: `oci-connection-lifecycle`).
  - Table scan, pushdown, type conversion, and scalar functions (Chapter 2: `scan-pushdown-types`).

## Implementation Plan

### Phase 1: Storage Extension, Catalog State, Schema/Table Entries & Write Pipeline

- [ ] **Task 1.1: Isolate per-ATTACH OracleCatalogState, synchronize OracleTransactionManager, and validate ATTACH options** -> [tasks/1.1.md](tasks/1.1.md)
  - **Objective**: Remediate `H-18`, `H-20`, and `L-08` (storage extension part) in `oracle_storage_extension.*` and `oracle_transaction_manager.*`.
  - **Verification Strategy**: `tdd`
  - **Files**: `src/include/oracle_storage_extension.hpp`, `src/storage/oracle_storage_extension.cpp`, `src/include/oracle_transaction_manager.hpp`, `src/storage/oracle_transaction_manager.cpp`
- [ ] **Task 1.2: Release catalog metadata connections after query, synchronize state getters, and parameterize catalog metadata SQL** -> [tasks/1.2.md](tasks/1.2.md)
  - **Objective**: Remediate `H-24` (catalog connection pin part), `M-18` (catalog part), `M-19` (catalog part), `M-20`, and `L-09` (catalog part) in `oracle_catalog_state.hpp` and `oracle_catalog.cpp`.
  - **Verification Strategy**: `tdd`
  - **Depends On**: `storage-catalog-write-pipeline:1.1`
  - **Files**: `src/include/oracle_catalog_state.hpp`, `src/storage/oracle_catalog.cpp`
- [ ] **Task 1.3: Override DuckCatalog/DuckSchemaEntry virtuals and preserve target schema/table in PlanInsert** -> [tasks/1.3.md](tasks/1.3.md)
  - **Objective**: Remediate `H-19` (schema entry part), `H-21`, and `L-08` (schema entry part) in `oracle_catalog.hpp` and `oracle_schema_entry.cpp`.
  - **Verification Strategy**: `tdd`
  - **Depends On**: `storage-catalog-write-pipeline:1.2`, `storage-catalog-write-pipeline:1.4`, `storage-catalog-write-pipeline:1.5`
  - **Files**: `src/include/oracle_catalog.hpp`, `src/storage/oracle_schema_entry.cpp`
- [ ] **Task 1.4: Restore original_types in OracleTableEntry::GetScanFunction, remove eager partition queries, and enrich VECTOR column metadata** -> [tasks/1.4.md](tasks/1.4.md)
  - **Objective**: Remediate `H-22`, `M-18` (table entry part), `M-19` (table entry part), and `M-22` (table entry part) in `oracle_table_entry.hpp` and `oracle_table_entry.cpp`.
  - **Verification Strategy**: `tdd`
  - **Depends On**: `storage-catalog-write-pipeline:1.2`
  - **Files**: `src/include/oracle_table_entry.hpp`, `src/storage/oracle_table_entry.cpp`
- [ ] **Task 1.5: Pass explicit schema/table to PhysicalOracleInsert, guard >64KB ub2 write lengths, bind DATE via SQLT_ODT, and optimize batch writes** -> [tasks/1.5.md](tasks/1.5.md)
  - **Objective**: Remediate `H-19` (insert/write part), `H-23` (write part), `M-21`, `M-22` (write part), and `L-09` (write part) in `oracle_write.*` and `oracle_insert.*`.
  - **Verification Strategy**: `tdd`
  - **Depends On**: `storage-catalog-write-pipeline:1.2`, `storage-catalog-write-pipeline:1.4`
  - **Files**: `src/include/oracle_write.hpp`, `src/storage/oracle_write.cpp`, `src/include/oracle_insert.hpp`, `src/storage/oracle_insert.cpp`

## Continuity Snapshot

- **Active Flow / Lifecycle**: `storage-catalog-write-pipeline` / `planned`
- **Current Task & Claimant**: `null`
- **Last Verified Checkpoint**: `null`
- **Decisions**:
  - Allocate a fresh `shared_ptr<OracleCatalogState>` inside `OracleAttach` for every attached database, register it under `name`, and retrieve it in `OracleCreateTransactionManager` via `OracleCatalogState::LookupByAlias(db.GetName())`, eliminating `OracleStorageInfo`.
  - Never hold an idle `OracleConnectionHandle` across metadata calls inside `OracleCatalogState`; let `OracleConnectionManager` pool connections across both metadata and scan/write operations.
- **Recent Discoveries**:
  - `DuckCatalog` assertions in `DuckSchemaEntry::AddEntry` and `OracleCatalog::InMemory()` fail whenever `duckdb_databases()` or DDL statements are executed against an attached Oracle catalog.
- **Blockers & Unblock Conditions**: `none`
- **Next Exact Step**: Execute `storage-catalog-write-pipeline:1.1`.
- **Plan Identity**: `plan_revision: 1`, `plan_commit: null`
- **State Identity**: `state_revision: 0`, `last_operation: null`, `operation_targets: []`
- **Relevant Knowledge Paths**:
  - `.agents/bundles/knowledge/architecture/duckdb-extension-architecture.md`
  - `.agents/bundles/knowledge/patterns/duckdb-write-pipeline.md`

## Verification Evidence

None yet (`state: planned`).
