---
type: Spec
flow_id: oci-connection-lifecycle
title: "Chapter 1: OCI Connection Pool, Environment Lifetime, Authentication & Settings Resolution"
state: planned
plan_revision: 1
plan_commit: 4d4fd44274bda21b9de7aa3efad471dd23b1789d
state_revision: 1
current_task: null
last_operation: null
operation_targets: []
last_verified_checkpoint: null
created_at: 2026-10-11T17:10:00Z
updated_at: 2026-10-11T17:10:00Z
description: Remediate OCIEnv static destruction use-after-free, ScopedTnsAdmin global mutex/env thread-safety hazards, pooled session health checks and rollback, password @ parsing, SEPS wallet and TNS alias secrets, per-ATTACH option preservation, and OracleConnection::Query double execution and 4095-byte truncation.
tags: [bugfix, oracle-oci, connection-pool, security, settings]
parent_prd: codebase-remediation-prd
research: []
---

# Chapter 1: OCI Connection Pool, Environment Lifetime, Authentication & Settings Resolution

**Flow ID:** `oci-connection-lifecycle`
**Parent PRD:** [`codebase-remediation-prd`](../codebase-remediation-prd/spec.md)

## Problem & Goal

The OCI connection, secret, settings, and utility layer has 6 High, 8 Medium, and 3 Low findings (`H-01`..`H-06`, `M-01`..`M-08`, `L-01`..`L-03`):
- `OracleContext` holds a raw non-owning `OCIEnv *env` while `OracleConnectionManager::Instance()` owns the `std::unique_ptr<OCIEnv>`, risking use-after-free during static destruction if a catalog or connection handle outlives `OracleConnectionManager`, and `OCIEnvCreate` omits `OCI_OBJECT`.
- `ScopedTnsAdmin` acquires a global `WalletEnvLock()` across network handshakes and post-login SQL even when `wallet_path` is empty, calls `setenv` even when `TNS_ADMIN` already matches, and unsets `TNS_ADMIN=""`.
- Pooled connections are never health-checked (`OCI_ATTR_SERVER_STATUS` / `OCIPing`) on checkout or return, uncommitted transactions are never rolled back on handle release, and `MarkUnusable()` is dead code.
- Passwords containing `@` fail to parse because `find('@')` matches the first `@`; SEPS wallet logins (`/@service` with `OCI_CRED_EXT`) and wallet TNS aliases without `HOST` are rejected or rewritten to `@localhost:1521/<alias>`.
- `GetOracleSettings` unconditionally overwrites all per-`ATTACH` options (`ENABLE_PUSHDOWN`, `PREFETCH_ROWS`, `LAZY_SCHEMA_LOADING`, etc.) because `DBConfig::AddExtensionOption` populates `db_config.user_settings` at extension load time.
- `OracleConnection::Query` and `QueryWithStringBinds` execute every metadata query twice (`OCI_DESCRIBE_ONLY` + `OCI_DEFAULT`), silently truncate values `> 4095` bytes, bind empty strings with `indicator = 0` (`size = 0`) instead of `OCI_IND_NULL` (`-1`), and duplicate ~80 lines of code.

## Scope & Constraints

- **In Scope**:
  - `src/include/oracle_utils.hpp`, `src/include/oracle_debug_stats.hpp`, `src/oracle_debug_stats.cpp`
  - `src/include/oracle_settings.hpp`, `src/oracle_settings.cpp`, `src/include/oracle_connection_resolver.hpp`, `src/oracle_connection_resolver.cpp`
  - `src/include/oracle_secret.hpp`, `src/oracle_secret.cpp`
  - `src/include/oracle_connection_manager.hpp`, `src/oracle_connection_manager.cpp`
  - `src/include/oracle_connection.hpp`, `src/oracle_connection.cpp`
- **Out of Scope**:
  - Table scan, pushdown, type conversion, and scalar functions (Chapter 2: `scan-pushdown-types`).
  - Storage extension, catalog entries, transactions, and write pipeline (Chapter 3: `storage-catalog-write-pipeline`).

## Implementation Plan

### Phase 1: OCI Utilities, Settings, Secrets, Connection Pool & Metadata Query Execution

- [x] **Task 1.1: Harden OCI error inspection, env helpers, credential redaction, and debug counter reads** -> [tasks/1.1.md](tasks/1.1.md)
  - **Objective**: Remediate `M-01`, `M-08` (redaction helper), `L-01`, and `L-02` in `oracle_utils.hpp` and `oracle_debug_stats.cpp`.
  - **Verification Strategy**: `tdd`
  - **Files**: `src/include/oracle_utils.hpp`, `src/include/oracle_debug_stats.hpp`, `src/oracle_debug_stats.cpp`
- [x] **Task 1.2: Preserve per-ATTACH settings, add call timeout & statement cache settings, and harden connection string resolution** -> [tasks/1.2.md](tasks/1.2.md)
  - **Objective**: Remediate `H-04` (resolver part), `H-05`, `M-02` (settings part), `M-03`, and `M-07` (resolver part) across `oracle_settings.*` and `oracle_connection_resolver.*`.
  - **Verification Strategy**: `tdd`
  - **Depends On**: `oci-connection-lifecycle:1.1`
  - **Files**: `src/include/oracle_settings.hpp`, `src/oracle_settings.cpp`, `src/include/oracle_connection_resolver.hpp`, `src/oracle_connection_resolver.cpp`
- [x] **Task 1.3: Support SEPS wallet credentials, TNS aliases without host, and strict secret parameter validation** -> [tasks/1.3.md](tasks/1.3.md)
  - **Objective**: Remediate `H-04` (secret part), `M-06`, and `L-03` (secret part) in `oracle_secret.hpp` and `oracle_secret.cpp`.
  - **Verification Strategy**: `tdd`
  - **Depends On**: `oci-connection-lifecycle:1.2`
  - **Files**: `src/include/oracle_secret.hpp`, `src/oracle_secret.cpp`
- [x] **Task 1.4: Fix OCIEnv lifetime, ScopedTnsAdmin concurrency, pooled session health checks, and session initialization** -> [tasks/1.4.md](tasks/1.4.md)
  - **Objective**: Remediate `H-01`, `H-02`, `H-03`, `H-04` (manager part), `M-02` (manager part), `M-04`, `M-08` (password scrubbing), and `L-03` (manager part) in `oracle_connection_manager.*`.
  - **Verification Strategy**: `tdd`
  - **Depends On**: `oci-connection-lifecycle:1.1`, `oci-connection-lifecycle:1.2`, `oci-connection-lifecycle:1.3`
  - **Files**: `src/include/oracle_connection_manager.hpp`, `src/oracle_connection_manager.cpp`
- [x] **Task 1.5: Deduplicate OracleConnection::Query, remove double execution, detect truncation, and fix empty string binds** -> [tasks/1.5.md](tasks/1.5.md)
  - **Objective**: Remediate `H-06`, `M-05`, and `L-03` (connection part) in `oracle_connection.hpp` and `oracle_connection.cpp`.
  - **Verification Strategy**: `tdd`
  - **Depends On**: `oci-connection-lifecycle:1.4`
  - **Files**: `src/include/oracle_connection.hpp`, `src/oracle_connection.cpp`

## Continuity Snapshot

- **Active Flow / Lifecycle**: `oci-connection-lifecycle` / `planned`
- **Current Task & Claimant**: `null`
- **Last Verified Checkpoint**: `null`
- **Decisions**:
  - Use `std::shared_ptr<OCIEnv>` shared between `OracleConnectionManager` and every `OracleContext` so `OCIEnv` is guaranteed to outlive all child OCI handles even during static destruction.
  - Track explicitly configured per-`ATTACH` setting overrides on `OracleCatalogState` and only overwrite `state->settings` with `SET oracle_*` values when `SettingScope::LOCAL` is explicitly set in the session or when the option was not overridden at `ATTACH` time.
- **Recent Discoveries**:
  - `DBConfig::AddExtensionOption` populates `db_config.user_settings` with `default_value`, causing `context.TryGetCurrentSetting` to always return a truthy `SettingScope::GLOBAL` result.
- **Blockers & Unblock Conditions**: `none`
- **Next Exact Step**: Execute `oci-connection-lifecycle:1.1`.
- **Plan Identity**: `plan_revision: 1`, `plan_commit: null`
- **State Identity**: `state_revision: 0`, `last_operation: null`, `operation_targets: []`
- **Relevant Knowledge Paths**:
  - `.agents/bundles/knowledge/architecture/oracle-oci-subsystem.md`
  - `.agents/bundles/knowledge/patterns/oci-raii-handles.md`
  - `.agents/bundles/knowledge/gotchas/oracle-oci.md`

## Verification Evidence

None yet (`state: planned`).
