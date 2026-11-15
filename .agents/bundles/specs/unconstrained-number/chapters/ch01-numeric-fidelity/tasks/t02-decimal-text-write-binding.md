---
type: Task
id: unconstrained-number:ch01:t02
chapter_id: ch01-numeric-fidelity
title: "Decimal text binding for FLOAT and DOUBLE writes"
description: "Bind FLOAT and DOUBLE write values as shortest round-trip decimal text via SQLT_CHR for non-binary Oracle targets and reject NaN/Infinity with column-named errors."
worksheet_format: full
status: stable
state: completed
priority: P1
verification_strategy: regression_tdd
depends_on:
  - unconstrained-number:ch01:t01
read_set:
  - src/oracle_type_registry.cpp
  - src/storage/oracle_write.cpp
  - test/integration_tests/oracle/write.test
files:
  - src/oracle_type_registry.cpp
  - src/storage/oracle_write.cpp
tests:
  - test/integration_tests/oracle/write.test
estimated_loc: 110
plan_revision: 1
plan_commit: 83efe19
state_revision: 2
claimed_by: "agy:0562b821-d24d-4860-a83a-6839add195c1:coordinator"
claimed_at: 2026-11-15T17:21:55Z
commit: ceeb56a
sources:
  - id: resolve-write
    resource: src/oracle_type_registry.cpp::OracleTypeRegistry::ResolveWrite
    title: "Write bind type resolution"
  - id: write-sink
    resource: src/storage/oracle_write.cpp::OracleWriteGlobalState::Sink
    title: "Vector flattening and OCI bind buffer staging"
tags: [oracle, write-pipeline, decimal, regression]
---

# Decimal text binding for FLOAT and DOUBLE writes

## Objective & Scope Boundary

Fix [Issue #44](https://github.com/cofin/duckdb-oracle/issues/44) on the write path so `FLOAT` and `DOUBLE` inputs bind as shortest round-trip decimal text (`SQLT_CHR`) unless targeting `BINARY_FLOAT` or `BINARY_DOUBLE`, preserving exact decimals such as `13.8`, `0.1`, and `0.15`.

## Bounded Read Manifest

- `src/oracle_type_registry.cpp`
- `src/storage/oracle_write.cpp`
- `test/integration_tests/oracle/write.test`

## Symbol & Signature Contracts

- `src/oracle_type_registry.cpp::OracleTypeRegistry` (`ResolveWrite` sets `decision.write_bind_type = SQLT_CHR` for `FLOAT`/`DOUBLE` unless `decision.normalized_type` is `BINARY_FLOAT` or `BINARY_DOUBLE`).
- `src/storage/oracle_write.cpp::OracleWriteGlobalState` (`Sink` formats `FLOAT` (`float`) and `DOUBLE` (`double`) directly via `duckdb_fmt::format("{}", val)` from `UnifiedVectorFormat` into `converted_strings` when `bind_type == SQLT_CHR`).

## Edge Cases & Error Semantics

- Non-finite `FLOAT`/`DOUBLE` values targeting non-binary Oracle columns raise `InvalidInputException("Oracle write value for column '%s' cannot bind NaN or Infinity to %s", ...)`, while `BINARY_FLOAT`/`BINARY_DOUBLE` targets continue to accept them.
- `FLOAT` values format as `float` (`13.8f` -> `'13.8'`) rather than widening to `double` (`13.800000190734863`).
- `1e+20` / `1e-07` exponent notation is verified against live Oracle first; fall back to fixed decimal formatting if rejected by Oracle.

## Public-Seam Test Blueprint

- Integration (`test/integration_tests/oracle/write.test`): assert stored Oracle text via `oracle_query(..., 'SELECT TO_CHAR(n, ''TM9'') ...')` for `13.8`, `0.1`, `0.15`, `0`, negatives, `NULL`, `1e20`, `1e-7`, `FLOAT` targets, `INSERT INTO dst SELECT * FROM src`, `NaN`/`Infinity` rejection on `NUMBER`, `BINARY_DOUBLE` continuity, `1e300` rollback, and 30-digit `DECIMAL(38,10)`/`VARCHAR` round-trips.

## Verification

- Fast gate: `make release && make lint`
- Chapter gate: `make test-unit && SKIP_BUILD=1 make integration`

## Acceptance Criteria

- [x] Oracle `TO_CHAR(col, 'TM9')` round-trip assertions pass for all `NUMBER`, `FLOAT`, and `BINARY_DOUBLE` write test cases.

## Notes & Discoveries

- Always inspect `TO_CHAR(..., 'TM9')` via `oracle_query` so DuckDB's binary float parser cannot mask `13.800000000000001`.
- Confirmed `1e+20` and `1e-07` formatted by `duckdb_fmt::format` bind cleanly via `SQLT_CHR` to Oracle 23ai `NUMBER`/`FLOAT` (`100000000000000000000` and `.0000001`), and `1e300` rolls back atomically with `ORA-01426`. All 22 Oracle 23ai integration suites passed (`write.test` 90/90).
