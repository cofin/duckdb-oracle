---
type: Guide
title: Project Workflow
---

# Project Workflow

<!-- truth: start -->
- Task files under `.agents/bundles/specs/<flow_id>/tasks/` are authoritative; `spec.md` is the synchronized checklist and continuity view.
- Canonical Commands:
  - Setup: `make configure_ci`
  - Focused tests: `make test-unit`
  - Integration tests: `make integration`
  - Lint & Hygiene: `make lint`
  - Build: `make release` / `make debug`
  - Versioning: `make bump-version bump=patch`
- Every task declares and justifies one `verification_strategy`; only observable behavior and defect correction require an initial failing test.
- Consumer operational skills install only under `.agents/skills/`. Product, knowledge, research, and specs remain under `.agents/bundles/`.
- Commits are signed (`commit.gpgsign = true`) using Conventional Commits (`<type>(<scope>): <summary>`), with one functional commit per task.
- Flow never creates, moves, force-updates, or deletes Git tags, and never pushes automatically.
<!-- truth: end -->

## Canonical Commands

```bash
# Setup OCI SDK & prerequisites
make configure_ci

# Run unit / smoke tests (no Oracle container needed)
make test-unit

# Run full integration tests (with containerized Oracle)
make integration

# Run formatting and tidy checks
make lint

# Build extension
make release
make debug

# Clean build artifacts
make clean-all

# Bump extension version
make bump-version bump=patch
```

## Direct-Read Continuity

1. Resolve `.agents/setup-state.json:root_directory`, defaulting to `.agents/`.
2. Before selecting a flow or doing normal work, scan `<configured-root>/transactions/*/journal.md`. Jointly arbitrate every nonterminal journal; recover the selected transaction from its recorded fragments, or stop on unresolved conflict.
3. Resolve the configured bundle root, then read its index and candidate spec frontmatter under `specs/<flow_id>/`.
4. Read authoritative task frontmatter in `tasks/*.md`.
5. Verify plan/state identity, task dependencies, claims, and checklist agreement.
6. Select an explicit task, the sole in-progress task, or the first ready task.
7. Read the complete worksheet, direct dependencies, newest discoveries, and relevant knowledge chapters.

Hooks and prior conversation are routing hints, not authority. Task files are the single authority for task state.

## Verification Strategies

| Strategy | Select for | Required evidence |
| --- | --- | --- |
| `behavior_tdd` | New observable behavior | Focused behavior fails because it is absent; minimal implementation makes it green. |
| `regression_tdd` | Defect correction | Focused reproduction demonstrates the defect; the narrow fix makes it green. |
| `characterization` | Behavior-preserving refactor/deletion | Green focused baseline before and unchanged behavior after. |
| `static_validation` | Manifest, config, generated surface, tooling | Native parser/lint/type/build; isolated representative violation proves a new/replacement gate fails with the expected diagnostic. |
| `documentation_validation` | Links, examples, docs structure | Docs-native baseline and final link/example/build/structure checks. |
| `integration_acceptance` | Composition of existing contracts | Green focused baseline; end-to-end scenario plus injected negative states proving refusal paths. |
