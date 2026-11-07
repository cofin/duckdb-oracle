---
type: Guide
title: Project Workflow
description: Canonical build, lint, typecheck, test, and Flow execution rules for duckdb-oracle.
status: stable
tags: [flow, workflow, commands, verification, continuity]
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
    title: Canonical build, lint, unit test, integration test, and version targets
  - id: test-integration
    resource: scripts/test_integration.sh
    title: Containerized Oracle Free 23ai integration test runner
---

# Project Workflow

<!-- truth: start -->
- Task worksheets (`specs/<flow_id>/tasks/<id>-<slug>.md` or `specs/<flow_id>/chapters/<ch_id>/tasks/<id>-<slug>.md`), chapter specs (`chapters/<ch_id>/spec.md`), and root `spec.md` checklists are the sole authority for task state, `last_reviewed_commit`, and commits.
- Canonical Commands:
  - Setup OCI runtime (integration tests only): `make configure_oci`
  - Focused unit tests (zero OCI SDK required): `make test-unit`
  - Single sqllogictest file: `./build/release/test/unittest "test/unit_tests/test_public_surface.test"`
  - Full integration tests (containerized Oracle): `make integration`
  - Lint & formatting (`clang-format` + `clang-tidy`): `make lint`
  - Build: `make release` / `make debug`
  - Versioning: `make bump-version bump=patch`
- Every task worksheet declares one `verification_strategy` (`behavior_tdd`, `regression_tdd`, `characterization`, `static_validation`, `documentation_validation`, `integration_acceptance`).
- Test through confirmed public seams (`ATTACH`, `oracle_scan`, `oracle_query`, `oracle_execute`, `CREATE SECRET`) in canonical `test/unit_tests/` and `test/integration_tests/` files; never mock the System Under Test or write tautological assertions.
- Project-local domain and review skills live under `.agents/skills/` and paired subagents under `.agents/agents/`.
- Commits are signed (`commit.gpgsign = true`) using Conventional Commits (`<type>(<scope>): <summary>`), with one atomic functional commit per task. Never create or mutate Git tags, and never push or open PRs without explicit user instruction.
<!-- truth: end -->

## Canonical Commands

Prefer repository-native `Makefile` targets over ad hoc tool invocations[^makefile]:

```bash
# Install Oracle Instant Client runtime for local integration tests (Linux)
make configure_oci

# Build release or debug extension (requires zero Oracle SDK installed)
make release
make debug

# Run fast offline unit / smoke tests (zero Oracle SDK or container required)
make test-unit

# Run a single sqllogictest file during tight TDD loops
./build/release/test/unittest "test/unit_tests/test_public_surface.test"

# Run clang-format and clang-tidy verification
make lint

# Run full containerized Oracle 23ai integration test suite
make integration
SKIP_BUILD=1 make integration

# Clean all build directories
make clean-all

# Bump extension version in description.yml
make bump-version bump=patch
```

## Direct Markdown Continuity & Chaptered Execution Loop

1. Resolve `.agents/setup-state.json` (default `.agents/bundles/`), read `index.md`, and inspect active `specs/<flow_id>/spec.md`, `chapters/index.md`, `chapters/<ch_id>/spec.md`, and `tasks/<id>-<slug>.md`.
2. Run preflight (`dependencies_closed`, `chapter_ready`, `worksheet_complete`, `verification_strategy_declared`, `plan_identity_matches`, `target_symbols_exist`) before claiming the ready `pending` task.
3. Execute one vertical-slice worksheet per isolated `@executor` subagent turn (`model: inherit`), verify exit code `0` on `make test-unit` and `make lint`[^makefile] (plus `make integration` when touching live OCI paths[^test-integration]), stage only task-owned paths, create one atomic signed commit, and update task frontmatter (`state: completed`, `commit: <sha>`) and `spec.md` (`- [x] [<sha>] ...`).
4. Stop cleanly after **1 completed chapter** per `/flow:implement` invocation: synchronize `specs/<flow_id>/spec.md`, `chapters/index.md`, `chapters/<ch_id>/spec.md`, `.agents/bundles/knowledge/**`, and `docs/**` in place in present tense, run zero-context chapter review (`/flow:review`), and record `last_reviewed_commit: <head_sha>`.
5. Run `/flow:learn` at any time to reconcile cross-machine/cross-harness drift, harvest discoveries, resynthesize `.agents/bundles/knowledge/**`, and synchronize `.agents/skills/` and `.agents/agents/`.

## Verification Strategies

| Strategy | Select for | Required evidence |
| --- | --- | --- |
| `behavior_tdd` | New observable behavior | Focused `.test` assertion fails because behavior is absent; minimal implementation makes it green. |
| `regression_tdd` | Defect correction | Focused reproduction in `test/unit_tests/` or `test/integration_tests/` demonstrates the defect; narrow fix makes it green. |
| `characterization` | Behavior-preserving refactor/deletion | Green `make test-unit` / `make integration` baseline before and unchanged behavior after. |
| `static_validation` | Manifest, config, CMake, CI workflow | `make lint` (`clang-format`, `clang-tidy`) and `make release`; isolated representative violation proves gate failure. |
| `documentation_validation` | Links, examples, OKF chapters, docs | Relative link resolution, `description.yml` parity, and structure verification. |
| `integration_acceptance` | End-to-end Oracle container workflows | Green `make integration` against `gvenzl/oracle-free:23-slim` plus negative error-path assertions[^test-integration]. |

[^makefile]: `Makefile` — Canonical build, lint, unit test, integration test, and version targets.
[^test-integration]: `scripts/test_integration.sh` — Containerized Oracle Free 23ai integration test runner.
