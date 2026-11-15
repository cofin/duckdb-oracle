---
type: Task
id: unconstrained-number:ch01:t01
chapter_id: ch01-numeric-fidelity
title: "NUMBER mapping rules and oracle_number_as setting"
description: "Add has_precision metadata tracking, exact DECIMAL mappings for INTEGER/NUMBER(*,s)/negative-scale/overscale columns, and the oracle_number_as setting with ATTACH override support."
worksheet_format: full
status: stable
state: completed
priority: P1
verification_strategy: behavior_tdd
depends_on: []
read_set:
  - src/include/oracle_type_registry.hpp
  - src/oracle_type_registry.cpp
  - src/include/oracle_settings.hpp
  - src/oracle_settings.cpp
  - src/oracle_connection_resolver.cpp
  - src/storage/oracle_catalog.cpp
  - src/storage/oracle_storage_extension.cpp
  - src/storage/oracle_table_entry.cpp
  - src/oracle_scan.cpp
  - src/storage/oracle_write.cpp
  - test/unit_tests/oracle/test_tuning_settings.test
  - test/integration_tests/oracle/type_registry.test
  - test/integration_tests/oracle/type_conversion.test
  - test/integration_tests/oracle/pushdown.test
files:
  - src/include/oracle_type_registry.hpp
  - src/oracle_type_registry.cpp
  - src/include/oracle_settings.hpp
  - src/oracle_settings.cpp
  - src/oracle_connection_resolver.cpp
  - src/storage/oracle_catalog.cpp
  - src/storage/oracle_storage_extension.cpp
  - src/storage/oracle_table_entry.cpp
  - src/oracle_scan.cpp
  - src/storage/oracle_write.cpp
tests:
  - test/unit_tests/oracle/test_tuning_settings.test
  - test/integration_tests/oracle/type_registry.test
  - test/integration_tests/oracle/type_conversion.test
  - test/integration_tests/oracle/pushdown.test
estimated_loc: 190
plan_revision: 1
plan_commit: 83efe19
state_revision: 2
claimed_by: "agy:0562b821-d24d-4860-a83a-6839add195c1:coordinator"
claimed_at: 2026-11-15T17:05:30Z
commit: 997a5d3
sources:
  - id: type-registry
    resource: src/oracle_type_registry.cpp::NumberLogicalType
    title: "Oracle NUMBER to DuckDB LogicalType mapping"
  - id: settings
    resource: src/oracle_settings.cpp::RegisterOracleExtensionOptions
    title: "Extension option registration and ATTACH parsing"
tags: [oracle, numbers, settings, pushdown]
---

# NUMBER mapping rules and oracle_number_as setting

## Objective & Scope Boundary

Add `oracle_number_as` (plus `ATTACH ... (number_as ...)`), map exact `NUMBER(*, s)`, `NUMBER(p, -s)`, and `NUMBER(p, s > p)` variants to `DECIMAL` when width fits in 38 digits, and disable filter pushdown when a `NUMBER` column is mapped to `VARCHAR`.

## Bounded Read Manifest

- `src/include/oracle_type_registry.hpp`, `src/oracle_type_registry.cpp`
- `src/include/oracle_settings.hpp`, `src/oracle_settings.cpp`
- `src/oracle_connection_resolver.cpp`, `src/storage/oracle_catalog.cpp`
- `src/storage/oracle_table_entry.cpp`, `src/oracle_scan.cpp`, `src/storage/oracle_write.cpp`
- `test/unit_tests/oracle/test_tuning_settings.test`
- `test/integration_tests/oracle/type_registry.test`, `type_conversion.test`, `pushdown.test`

## Symbol & Signature Contracts

- `src/include/oracle_type_registry.hpp::OracleTypeMetadata` (`bool has_precision = false`).
- `src/oracle_settings.cpp::ParseOracleNumberAs` (`LogicalType ParseOracleNumberAs(const string &raw)`) and `FormatOracleNumberAs(const LogicalType &type)`.
- `src/include/oracle_settings.hpp::OracleSettings` (`LogicalType number_as = LogicalType::DOUBLE`).
- `src/oracle_type_registry.cpp::NumberLogicalType` (`NumberLogicalType(const OracleTypeMetadata &metadata, const OracleSettings &settings)`).

## Edge Cases & Error Semantics

- `ParseOracleNumberAs` accepts `'DOUBLE'`, `'VARCHAR'`, or `'DECIMAL(p,s)'` case-insensitively with `1 <= p <= 38` and `0 <= s <= p`; any other input raises `InvalidInputException`.
- `OracleBindInternal` clears `has_scale = false` when `OCI_ATTR_PRECISION == 0` so `oracle_query` expression columns follow `oracle_number_as` rather than truncating via `DECIMAL(38, s)`.
- When `NUMBER` resolves to `VARCHAR`, `decision.pushdown_eligible = false` prevents numeric vs lexicographical filter mismatches.

## Public-Seam Test Blueprint

- Unit (`test/unit_tests/oracle/test_tuning_settings.test`): verify default `'DOUBLE'`, accepted `'VARCHAR'` / `'DECIMAL(38, 10)'` / lowercase variants, and rejected strings (`'BIGINT'`, `'DECIMAL(39,0)'`, `'DECIMAL(10,11)'`, `''`).
- Integration (`type_registry.test`, `type_conversion.test`, `pushdown.test`): verify `typeof()`, precision > 2^53 fidelity, overflow conversion errors, `ATTACH ... (number_as ...)`, and `VARCHAR` filter parity (`'9' < '10'`).

## Verification

- Fast gate: `make release && make lint`
- Chapter gate: `make test-unit && SKIP_BUILD=1 make integration`

## Acceptance Criteria

- [x] All unit and integration tests for `oracle_number_as`, exact `NUMBER` mappings, and `VARCHAR` pushdown pass.

## Notes & Discoveries

- Verified `ALL_TAB_COLUMNS` `data_precision`/`data_scale` and `OracleAttach` precedence (`GetOracleSettings(context, nullptr)` before `state->ApplyOptions(attach_options)`) against Oracle 23ai (`17/17` unit tests, `22/22` integration suites).
