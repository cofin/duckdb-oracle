---
type: Task
id: unconstrained-number:ch01:t03
chapter_id: ch01-numeric-fidelity
title: "Documentation, OKF knowledge alignment, and v0.3.2 version bump"
description: "Document type mapping and oracle_number_as in README/CHANGELOG/COMPATIBILITY, bump version to 0.3.2, and update OKF knowledge, ADR-002, limitations, and review skills."
worksheet_format: compact
status: stable
state: pending
priority: P2
verification_strategy: documentation_validation
depends_on:
  - unconstrained-number:ch01:t01
  - unconstrained-number:ch01:t02
read_set:
  - README.md
  - CHANGELOG.md
  - docs/COMPATIBILITY.md
  - .bumpversion.toml
  - description.yml
  - test/unit_tests/test_extension_metadata.test
  - .agents/bundles/knowledge/index.md
  - .agents/bundles/knowledge/domains/oracle-oci.md
  - .agents/bundles/knowledge/gotchas/oracle-oci.md
  - .agents/skills/oracle-oci/references/types-and-batches.md
  - .agents/skills/review-oracle-oci/references/checklist.md
  - .agents/skills/review-oracle-oci/references/false-positive-suppressions.md
files:
  - README.md
  - CHANGELOG.md
  - docs/COMPATIBILITY.md
  - .bumpversion.toml
  - description.yml
  - .agents/bundles/index.md
  - .agents/bundles/knowledge/index.md
  - .agents/bundles/knowledge/domains/oracle-oci.md
  - .agents/bundles/knowledge/gotchas/oracle-oci.md
  - .agents/bundles/knowledge/decisions/adr-002-oracle-number-mapping.md
  - .agents/bundles/knowledge/gotchas/type-fidelity-limitations.md
  - .agents/skills/oracle-oci/references/types-and-batches.md
  - .agents/skills/review-oracle-oci/references/checklist.md
  - .agents/skills/review-oracle-oci/references/false-positive-suppressions.md
tests:
  - test/unit_tests/test_extension_metadata.test
estimated_loc: 220
plan_revision: 1
plan_commit: 83efe19
state_revision: 1
claimed_by: null
claimed_at: null
commit: null
sources:
  - id: bumpversion
    resource: .bumpversion.toml
    title: "Extension semantic version bump configuration"
  - id: readme
    resource: README.md
    title: "User-facing configuration and type mapping guide"
tags: [docs, knowledge, release, versioning]
---

# Documentation, OKF knowledge alignment, and v0.3.2 version bump

## Objective & Scope Boundary

Document numeric mapping behavior and `oracle_number_as`, bump `duckdb-oracle` from `0.3.1` to `0.3.2`, and update OKF knowledge and review skills to match the shipped implementation.

## Bounded Read Manifest

- `README.md`, `CHANGELOG.md`, `docs/COMPATIBILITY.md`, `.bumpversion.toml`, `description.yml`, `test/unit_tests/test_extension_metadata.test`
- `.agents/bundles/knowledge/{index.md,domains/oracle-oci.md,gotchas/oracle-oci.md}`
- `.agents/skills/{oracle-oci/references/types-and-batches.md,review-oracle-oci/references/{checklist.md,false-positive-suppressions.md}}`

## Contracts & Edge Cases

- Run `make bump-version bump=patch` so `.bumpversion.toml`, `description.yml`, and `test/unit_tests/test_extension_metadata.test` advance in lockstep to `0.3.2`.
- Keep `FP-OCI-01` accurate to the configurable `oracle_number_as` default (`DOUBLE`) and record all remaining caveats (pushdown asymmetry in `DOUBLE` mode, `TIMESTAMP(7..9)` sub-microsecond truncation, doubles exceeding Oracle `NUMBER` magnitude, and any unverified paths) in `.agents/bundles/knowledge/gotchas/type-fidelity-limitations.md`.

## Test Matrix

| Case | Input | Expected |
| --- | --- | --- |
| Version lockstep | `SELECT extension_version FROM duckdb_extensions()` | `0.3.2` |
| Bundle validity | `bb validate --workspace . --json` | `"valid": true` |

## Verification

- Fast gate: `make release && make lint`
- Chapter gate: `make test-unit && bb validate --workspace . --json`

## Acceptance Criteria

- [ ] `README.md`, `CHANGELOG.md`, `docs/COMPATIBILITY.md`, OKF knowledge chapters, `ADR-002`, skills, and `0.3.2` version metadata are synchronized and pass `make test-unit` and `bb validate`.
