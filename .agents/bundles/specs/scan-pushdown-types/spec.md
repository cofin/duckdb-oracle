---
type: Spec
flow_id: scan-pushdown-types
title: "Chapter 2: Vectorized Scan, Filter/Projection Pushdown, Type Conversion & Scalar Functions"
state: planned
plan_revision: 1
plan_commit: 4d4fd44274bda21b9de7aa3efad471dd23b1789d
state_revision: 1
current_task: null
last_operation: null
operation_targets: []
last_verified_checkpoint: null
created_at: 2026-10-11T17:11:00Z
updated_at: 2026-10-11T17:11:00Z
description: Remediate VECTOR_SERIALIZE RETURNING CLOB, 64KB scan buffers, positive truncation indicators vs ORA-24347 warnings, projection pushdown in OracleInitGlobal, OraclePushdownComplexFilter column/type preservation, Oracle empty-string/NaN/Inf/LIKE/DATE pushdown bugs, BLOB AddStringOrBlob, TIMESTAMP WITH TIME ZONE parsing, and scalar function row iteration and volatility.
tags: [bugfix, oracle-oci, scan, pushdown, type-conversion]
parent_prd: codebase-remediation-prd
research: []
---

# Chapter 2: Vectorized Scan, Filter/Projection Pushdown, Type Conversion & Scalar Functions

**Flow ID:** `scan-pushdown-types`
**Parent PRD:** [`codebase-remediation-prd`](../codebase-remediation-prd/spec.md)

## Problem & Goal

The vectorized scan, pushdown, type conversion, type registry, and scalar functions layer has 11 High, 9 Medium, and 4 Low findings (`H-07`..`H-17`, `H-23` part, `M-07` part, `M-09`..`M-17`, `L-04`..`L-07`):
- `VECTOR_SERIALIZE(%s)` omits `RETURNING CLOB` (failing with `ORA-51814`/`ORA-40478` on `>= 350`-dimension vectors) and `oracle_scan.cpp` caps `VECTOR`, `SPATIAL`, `XML`, `CLOB`, `BLOB`, `LONG`, and `LONG RAW` buffers at 16,000 bytes instead of `65535`.
- `OracleQueryFunction` misses positive truncation indicators (`sb2 > 0` when original length `<= 32767`) on the final `OCI_NO_DATA` batch, and prematurely throws a generic error on `OCI_SUCCESS_WITH_INFO` before inspecting indicators (crashing on benign `ORA-24347` NULL aggregate warnings and hiding the truncated column name).
- Projection pushdown never executes because `OraclePushdownComplexFilter` runs before `RemoveUnusedColumns` in DuckDB's optimizer, and when `get.projection_ids` IS populated, `OraclePushdownComplexFilter` indexes `bind.original_names` out-of-bounds, destructively shrinks `bind.column_names` out of sync with `column_mapping`, and overwrites `get.returned_types` with `VARCHAR`.
- `ConstantToSQL` pushes empty string `''` (which Oracle treats as `NULL`), unquoted `nan`/`inf`/`-inf` (parsed as column identifiers), and `+/-infinity`/BC dates; `CanPushdownLike` rejects `"OCI CHARACTER"` (`oracle_scan`/`oracle_query`); `TO_CHAR`-wrapped `DATE`/`TIMESTAMP` columns fail outer filter pushdown with `ORA-01843`/`ORA-01861`; and `CHAR(n)` columns are pushed down despite blank-padding semantics.
- `SetStringLikeValue` calls `StringVector::AddString` on `LogicalTypeId::BLOB`, triggering a `D_ASSERT` crash in debug builds; `TIMESTAMP WITH TIME ZONE` / `LOCAL TIME ZONE` drops UTC offsets (`use_offset = false`); `NUMBER(*, 0)` maps inconsistently between `ResolveMetadata` and `ResolveOciDescribe`; and `BINARY_FLOAT` maps to `DOUBLE` instead of `FLOAT`.
- Scalar functions (`oracle_execute`, `oracle_clear_cache`, `oracle_debug_*`) only evaluate row `0` (`args.size()` ignored) and default to `FunctionStability::CONSISTENT` instead of `VOLATILE`.

## Scope & Constraints

- **In Scope**:
  - `src/include/oracle_type_registry.hpp`, `src/oracle_type_registry.cpp`
  - `src/include/oracle_type_conversion.hpp`, `src/oracle_type_conversion.cpp`
  - `src/include/oracle_pushdown.hpp`, `src/oracle_pushdown.cpp`
  - `src/include/oracle_table_function.hpp`, `src/oracle_scan.cpp`
  - `src/include/oracle_functions.hpp`, `src/oracle_functions.cpp`
- **Out of Scope**:
  - OCI connection pool, secrets, and settings (Chapter 1: `oci-connection-lifecycle`).
  - Storage catalog, schema/table entries, and write pipeline (Chapter 3: `storage-catalog-write-pipeline`).

## Implementation Plan

### Phase 1: Type Registry, Type Conversion, Filter/Projection Pushdown, Scan & Scalar Functions

- [ ] **Task 1.1: Fix VECTOR_SERIALIZE CLOB return, CHAR pushdown eligibility, TIMESTAMP_TZ, NUMBER(*,0), BINARY_FLOAT, and DATE write bind type** -> [tasks/1.1.md](tasks/1.1.md)
  - **Objective**: Remediate `H-07` (registry part), `H-13` (registry part), `H-15` (registry part), `H-16` (registry part), `H-23` (registry part), `M-13`, `M-16`, and `L-06` (registry part) in `oracle_type_registry.*`.
  - **Verification Strategy**: `tdd`
  - **Files**: `src/include/oracle_type_registry.hpp`, `src/oracle_type_registry.cpp`
- [ ] **Task 1.2: Fix BLOB AddStringOrBlob, TIMESTAMP_TZ offset parsing, locale-safe number/vector parsing, and shared conversion helpers** -> [tasks/1.2.md](tasks/1.2.md)
  - **Objective**: Remediate `H-14`, `H-15` (conversion part), `M-15`, and `L-06` (conversion part) in `oracle_type_conversion.*`.
  - **Verification Strategy**: `tdd`
  - **Depends On**: `scan-pushdown-types:1.1`
  - **Files**: `src/include/oracle_type_conversion.hpp`, `src/oracle_type_conversion.cpp`
- [ ] **Task 1.3: Fix complex filter pushdown column/type preservation, empty-string/NaN/Inf/Date literal safety, and LIKE pushdown** -> [tasks/1.3.md](tasks/1.3.md)
  - **Objective**: Remediate `H-09` (pushdown part), `H-10` (pushdown part), `H-11`, `H-12`, `H-13` (pushdown part), `M-11` (pushdown part), `M-12`, `M-14`, and `L-05` in `oracle_pushdown.*`.
  - **Verification Strategy**: `tdd`
  - **Depends On**: `scan-pushdown-types:1.1`
  - **Files**: `src/include/oracle_pushdown.hpp`, `src/oracle_pushdown.cpp`
- [ ] **Task 1.4: Implement projection pushdown in OracleInitGlobal, widen 64KB buffers, fix truncation indicators, and harden OracleScanState** -> [tasks/1.4.md](tasks/1.4.md)
  - **Objective**: Remediate `H-07` (scan part), `H-08`, `H-09` (scan part), `H-10` (scan part), `H-13` (scan part), `H-16` (scan part), `M-07` (scan part), `M-09`, `M-10`, `M-11` (scan part), and `L-04` in `oracle_table_function.hpp` and `oracle_scan.cpp`.
  - **Verification Strategy**: `tdd`
  - **Depends On**: `scan-pushdown-types:1.1`, `scan-pushdown-types:1.2`, `scan-pushdown-types:1.3`
  - **Files**: `src/include/oracle_table_function.hpp`, `src/oracle_scan.cpp`
- [ ] **Task 1.5: Fix scalar function multi-row iteration, VOLATILE stability, SELECT rejection, 64-bit row count, and DDL cache invalidation** -> [tasks/1.5.md](tasks/1.5.md)
  - **Objective**: Remediate `H-17`, `M-17`, and `L-07` in `oracle_functions.hpp` and `oracle_functions.cpp`.
  - **Verification Strategy**: `tdd`
  - **Depends On**: `scan-pushdown-types:1.4`
  - **Files**: `src/include/oracle_functions.hpp`, `src/oracle_functions.cpp`

## Continuity Snapshot

- **Active Flow / Lifecycle**: `scan-pushdown-types` / `planned`
- **Current Task & Claimant**: `null`
- **Last Verified Checkpoint**: `null`
- **Decisions**:
  - Perform final SQL column pruning and OCI buffer allocation inside `OracleInitGlobal` using `input.column_ids` and `input.projection_ids`, after DuckDB's `RemoveUnusedColumns` optimizer has completed.
  - Reject empty string `''` and non-finite `FLOAT`/`DOUBLE`/`DATE`/`TIMESTAMP` literals in `ConstantToSQL` so DuckDB's local `TableFilterSet` evaluates them with exact IEEE-754 and SQL standard semantics.
- **Recent Discoveries**:
  - `OCIStmtFetch2` sets indicator `ind > 0` (original length) or `-2` on truncation and returns `OCI_NO_DATA` on the final partial batch even when a column in that batch was truncated.
- **Blockers & Unblock Conditions**: `none`
- **Next Exact Step**: Execute `scan-pushdown-types:1.1`.
- **Plan Identity**: `plan_revision: 1`, `plan_commit: null`
- **State Identity**: `state_revision: 0`, `last_operation: null`, `operation_targets: []`
- **Relevant Knowledge Paths**:
  - `.agents/bundles/knowledge/domains/oracle-oci.md`
  - `.agents/bundles/knowledge/patterns/oci-batch-operations.md`
  - `.agents/bundles/knowledge/patterns/duckdb-pushdown.md`

## Verification Evidence

None yet (`state: planned`).
