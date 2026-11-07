---
type: Standard
title: Engineering Discipline & Verification Standards
description: Public-seam vertical-slice TDD, hypothesis-driven bug diagnosis, signed conventional commits, and zero-slop code standards.
scope: standard
domain: workflow
status: stable
tags: [flow, discipline, tdd, conventional-commits, code-standards, verification]
generated:
  by: flow/0.27.0
  at: 2026-11-07T17:36:00Z
verified:
  - by: agent:flow-setup
    at: 2026-11-07T17:36:00Z
    commit: 36539f3
sources:
  - id: makefile
    resource: Makefile
    title: Build, lint, unit test, and integration test verification gates
  - id: unit-surface
    resource: test/unit_tests/test_public_surface.test
    title: Public-seam extension surface verification test
---

# Engineering Discipline & Verification Standards

<!-- truth: start -->
- Plan and execute **vertical slices**: each task worksheet combines production C++ changes and canonical `test/unit_tests/` or `test/integration_tests/` sqllogictest assertions in a single `@executor` turn and a single atomic signed commit—never split work into horizontal "write all tests" vs "write all code" tasks[^makefile][^unit-surface].
- Test through confirmed public seams (`ATTACH`, `oracle_scan`, `oracle_query`, `oracle_execute`, `CREATE SECRET`) without mocking the System Under Test or writing tautological assertions[^unit-surface].
- Every commit must be cryptographically signed (`commit.gpgsign = true`) using Conventional Commits (`<type>(<scope>): <summary>`), staging only task-owned paths. Never create or mutate Git tags, and never push without explicit user instruction.
- Write direct, present-tense prose and code with zero historical sediment, zero count-prefixed buzzwords, and zero AI puffery.
<!-- truth: end -->

## 1. Public-Seam Vertical-Slice TDD & Verification Strategies

Every task worksheet selects and justifies one `verification_strategy`[^makefile]:

| Strategy | When to Select | Required Evidence |
| :--- | :--- | :--- |
| `behavior_tdd` | New observable features or behavior | **Red-Green-Refactor in One Vertical Slice**: Add focused sqllogictest assertion in canonical `test/unit_tests/` or `test/integration_tests/` file, observe expected failure, implement minimal production C++ change to make it green, and refactor while green. |
| `regression_tdd` | Defect and bug fixes | **Reproduction Test**: Focused sqllogictest reproduces the defect against baseline code; targeted fix turns it green. |
| `characterization` | Behavior-preserving refactors or dead code removal | Green `make test-unit` / `make integration` baseline prior to changes; identical green test output post-refactor. |
| `static_validation` | Manifests, configurations, tooling, build rules | `make lint` (`clang-format`, `clang-tidy`) and `make release`; isolated representative violation proves the gate catches errors. |
| `documentation_validation` | Markdown docs, OKF chapters, guides | Relative link validation, `description.yml` parity, and structure verification. |
| `integration_acceptance` | Multi-system Oracle container workflows | End-to-end `make integration` against `gvenzl/oracle-free:23-slim` plus negative error-path assertions. |

### Hypothesis-Driven Bug Diagnosis Loop

When diagnosing a failing test or runtime bug:
1. Formulate a falsifiable root-cause hypothesis anchored to `src/<file>.cpp::Symbol`.
2. Add a tight reproduction or single-variable `[DEBUG-<id>]` trace if needed.
3. Verify the hypothesis against fresh execution output, remove all `[DEBUG-<id>]` instrumentation before committing, and lock in a permanent regression test in the canonical `.test` file[^unit-surface].

## 2. Commit & Branch Discipline

1. **Signed Conventional Commits**: Every commit is signed (`commit.gpgsign = true`) and formatted as `<type>(<scope>): <summary>` in present-tense imperative mood.
2. **One Functional Commit Per Task**: Each vertical-slice task produces one atomic commit staging only task-owned paths (`git add <exact-paths>`).
3. **Local by Default & Tag Immutability**: Commits stay local unless the user explicitly asks to push or open a PR. Never create, move, force-update, or delete Git tags.
4. **Branch-Local Archiving**: Completed flows are reviewed (`/flow:review`), finished (`/flow:finish`), and archived (`/flow:archive`) into `.agents/bundles/knowledge/**` and `.agents/bundles/log.md` directly on the working branch.

## 3. Code & Prose Standards (Zero-Slop Policy)

- **Deep Modules & Clear Seams**: Keep public header interfaces (`src/include/*.hpp`) narrow and hide OCI protocol mechanics inside `.cpp` implementation files.
- **Proactive Hygiene**: Resolve adjacent compiler warnings, `clang-tidy` diagnostics, `clang-format` deviations, and dead code within task scope[^makefile].
- **Present-Tense Living Docs**: Keep `README.md`, `docs/**`, and `.agents/bundles/knowledge/**` in present tense describing current verified behavior; record chronological history solely in `.agents/bundles/log.md` and `CHANGELOG.md`.

[^makefile]: `Makefile` — Build, lint, unit test, and integration test verification gates.
[^unit-surface]: `test/unit_tests/test_public_surface.test` — Public-seam extension surface verification test.
