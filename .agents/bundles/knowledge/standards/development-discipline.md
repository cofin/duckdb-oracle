---
type: Standard
title: Engineering Discipline & Verification Standards
description: TDD verification strategies, red-green-refactor cycles, signed conventional commits, branch-local archiving, and zero-slop code standards.
scope: standard
domain: workflow
tags: [flow, discipline, tdd, conventional-commits, code-standards, verification]
status: stable
---

# Engineering Discipline & Verification Standards

High-quality software delivery in this repository requires strict adherence to engineering standards across testing, version control, code aesthetics, and spec lifecycle management.

## 1. TDD Verification Strategies

Every task worksheet must select and justify exactly one `verification_strategy`:

| Strategy | When to Select | Required Evidence |
| :--- | :--- | :--- |
| `behavior_tdd` | New observable features or behavior | **Red-Green-Refactor**: Focused test fails because implementation is absent; minimal implementation makes it green; refactor preserves green. |
| `regression_tdd` | Defect and bug fixes | **Reproduction Test**: Isolated test reproduces the failure against baseline code; targeted fix turns it green. |
| `characterization` | Behavior-preserving refactors or dead code removal | Green baseline test evidence prior to changes; byte-for-byte or behaviorally identical green test output post-refactor. |
| `static_validation` | Manifests, configurations, tooling, build rules | Native tool/compiler invocation (`clang-tidy`, `clang-format`, CMake syntax check); isolated representative failure proves the gate catches violations. |
| `documentation_validation`| Markdown docs, OKF chapters, guides | Clean link validation, formatting check, and documentation structure verification. |
| `integration_acceptance` | Complex multi-system contract composition | End-to-end integration test (`make integration`) plus injected negative tests proving refusal paths. |

### The Two-Task TDD Spec Split
When planning non-trivial features in `spec.md`, tasks must be split into two discrete tasks:
1. **"Write Tests" Task**: Adds failing unit or integration tests (e.g. in `test/unit_tests/` or `test/integration_tests/`) and runs them freshly to verify they fail for the intended reason (Red).
2. **"Implement Feature" Task**: Implements the minimal production code to turn the tests green (Green), followed by refactoring (Refactor).

## 2. Commit Discipline

1. **Signed Commits**:
   - Every commit must be cryptographically signed (`commit.gpgsign = true`).
2. **Conventional Commits Format**:
   - Commits follow `<type>(<scope>): <summary>` (e.g., `feat(oracle): add OCI array bind support for INSERT`, `fix(pushdown): quote reserved keywords in column pushdown`).
   - Summary is written in present-tense imperative mood ("add", not "added" or "adds").
3. **One Functional Commit Per Task**:
   - Each task produces exactly one functional commit containing only task-owned paths.
   - Never stage broadly (`git add .` or `git add -A` in a dirty checkout is prohibited).
   - Never force-add files ignored by `.gitignore` or `.agents/.gitignore`.
4. **Local Commits by Default**:
   - Commits remain strictly local unless the user explicitly requests a push.
   - Pushing happens incrementally after lint checks pass (`make lint`).
5. **Git Tag Immutability**:
   - Agents are strictly forbidden from creating, moving, force-updating, or deleting Git tags. Tags are immutable release marks governed by CI automation.

## 3. Branch-Local Spec Archiving

- Specifications are completed and archived directly on the active working branch without waiting for PR merge into `main`.
- When `/flow:finish` confirms all tasks in `.agents/bundles/specs/<flow_id>/tasks/` are `closed` or `skipped`, the spec status transitions to `completed`.
- When `/flow:archive` runs, discoveries and architectural patterns from `learnings.md` are consolidated into current-state chapters under `.agents/bundles/knowledge/`, a summary is appended to `.agents/bundles/log.md`, and the completed spec folder is contracted. Git history remains the permanent archive.
- If scope expands beyond the active spec, finish the current spec and open a follow-up spec via `/flow:plan`.

## 4. Code & Prose Standards (Zero-Slop Policy)

- **Clarity Over Cleverness**: Code must be direct, self-describing, and maintainable.
- **Documentation Integrity**: In C++, use Doxygen/Javadoc headers (`//!`) for public APIs and classes. In Python, use Google-style docstrings exclusively and PEP 585/604 modern types. Never use inline `#` comments in Python.
- **Proactive Hygiene**: When touching a file within task scope, proactively resolve adjacent compiler warnings, clang-format deviations, and dead code.
- **Prose Tone**: Avoid AI conversational fluff, formulaic transitions ("In summary", "Let's dive in", "It is worth noting"), and negative parallelisms ("Not only X, but also Y"). Write factual, dense, precise prose.
