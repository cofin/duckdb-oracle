---
type: Task
id: unconstrained-number:ch02:t01
chapter_id: ch02-timezone-roundtrip
title: "Reproduce zoned timestamp round-trip and apply session UTC fix if reproduced"
description: "Test TIMESTAMP WITH [LOCAL] TIME ZONE round-trips via SYS_EXTRACT_UTC, set session TIME_ZONE = '+00:00' if reproduced, and record behavior in type-fidelity-limitations.md."
worksheet_format: compact
status: stable
state: in_progress
priority: P2
verification_strategy: integration_acceptance
depends_on: []
read_set:
  - src/oracle_connection_manager.cpp
  - src/oracle_type_conversion.cpp
  - src/storage/oracle_write.cpp
  - test/integration_tests/oracle/type_conversion.test
  - .agents/bundles/knowledge/gotchas/type-fidelity-limitations.md
files:
  - src/oracle_connection_manager.cpp
  - .agents/bundles/knowledge/gotchas/type-fidelity-limitations.md
tests:
  - test/integration_tests/oracle/type_conversion.test
estimated_loc: 45
plan_revision: 1
plan_commit: 83efe19
state_revision: 2
claimed_by: "agy:0562b821-d24d-4860-a83a-6839add195c1:coordinator"
claimed_at: 2026-11-15T17:32:00Z
commit: null
sources:
  - id: conn-mgr
    resource: src/oracle_connection_manager.cpp::OracleConnectionManager::CreateConnection
    title: "Session ALTER SESSION setup"
tags: [oracle, timestamps, timezone, integration]
---

# Reproduce zoned timestamp round-trip and apply session UTC fix if reproduced

## Objective & Scope Boundary

Verify whether `TIMESTAMP WITH TIME ZONE` and `TIMESTAMP WITH LOCAL TIME ZONE` values shift instants across an Oracle-to-DuckDB-to-Oracle round-trip and resolve with `TIME_ZONE = '+00:00'` if reproduced.

## Bounded Read Manifest

- `src/oracle_connection_manager.cpp`
- `src/oracle_type_conversion.cpp`
- `src/storage/oracle_write.cpp`
- `test/integration_tests/oracle/type_conversion.test`
- `.agents/bundles/knowledge/gotchas/type-fidelity-limitations.md`

## Contracts & Edge Cases

- Reproduction writes `TIMESTAMP '2024-01-01 10:00:00 +05:00'` into `TIMESTAMP WITH TIME ZONE` and `TIMESTAMP WITH LOCAL TIME ZONE` columns, copies through DuckDB, and compares `SYS_EXTRACT_UTC(col)`.
- If reproduced, add `TIME_ZONE = '+00:00'` to the `ALTER SESSION SET` statement in `src/oracle_connection_manager.cpp::OracleConnectionManager` and document the `CURRENT_DATE` / `CURRENT_TIMESTAMP` UTC effect in `gotchas/type-fidelity-limitations.md`.
- If not reproduced, retain the round-trip test as characterization and record the observed Oracle behavior in `gotchas/type-fidelity-limitations.md`.

## Test Matrix

| Case | Input | Expected |
| --- | --- | --- |
| `TIMESTAMP WITH TIME ZONE` | `2024-01-01 10:00:00 +05:00` | `SYS_EXTRACT_UTC` matches source after write |
| `TIMESTAMP WITH LOCAL TIME ZONE` | `2024-01-01 10:00:00 +05:00` | `SYS_EXTRACT_UTC` matches source after write |

## Verification

- Fast gate: `make release && make lint`
- Chapter gate: `make test-unit && SKIP_BUILD=1 make integration`

## Acceptance Criteria

- [ ] Zoned timestamp characterization or fix passes `make integration` and `gotchas/type-fidelity-limitations.md` records the verified behavior.
