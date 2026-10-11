---
type: Standard
title: Project Management & Flow OKF v0.2 Lifecycle
description: Spec-first planning, OKF v0.2 bundle architecture, authoritative task worksheets, state machine transitions, and direct-read continuity.
scope: standard
domain: workflow
tags: [flow, okf, project-management, spec-first, state-machine, continuity]
status: stable
---

# Project Management & Flow OKF v0.2 Lifecycle

Project execution is governed by the **Flow Framework**, backed by beadless **Open Knowledge Format (OKF v0.2)** knowledge bundles stored under `.agents/bundles/`. State tracking, specifications, tasks, and continuous learnings reside directly on the filesystem as typed Markdown documents with YAML frontmatter.

## 1. OKF Bundle Structure

```
.agents/
├── setup-state.json            # Setup status, ignore policy, canonical commands
├── config.json                 # Optional nonstandard layout configuration
├── transactions/               # Untracked crash-recovery transaction journals
├── skills/                     # Project-local operational skills
│   └── flow-memory-keeper/     # Continuous refinement & pattern capture
└── bundles/                    # Version-controlled OKF v0.2 knowledge tree
    ├── index.md                # Bundle root index (okf_version: "0.2")
    ├── log.md                  # Chronological, dated changelog
    ├── product/                # Single sources of truth with <!-- truth --> bounds
    │   ├── product.md          # Product definition & capabilities
    │   ├── product-guidelines.md # Operational & architectural constraints
    │   └── tech-stack.md       # Languages, tools, libraries, test runners
    ├── knowledge/              # Scope-derived, evidence-backed knowledge
    │   ├── index.md            # Knowledge base index & progressive routing
    │   ├── workflow.md         # Canonical commands & continuity contracts
    │   ├── architecture/       # System boundaries & data flows
    │   ├── domains/            # Domain models, protocols & vocabularies
    │   ├── patterns/           # Reusable conventions & implementation patterns
    │   ├── standards/          # Repository standards & governance
    │   ├── testing/            # Verification harness & fixture contracts
    │   └── gotchas/            # Hard-won edge cases & operational traps
    ├── research/               # Pre-PRD technical investigations
    └── specs/<flow_id>/        # Active feature specifications & task worksheets
        ├── spec.md             # Synchronized checklist & architectural overview
        ├── learnings.md        # Discoveries, failures, and recoveries
        └── tasks/              # Authoritative task worksheets
            ├── task-1.md       # Itemized execution contract for Task 1
            └── task-2.md       # Itemized execution contract for Task 2
```

## 2. Spec-First Planning

No implementation code is written without an approved, decision-complete specification:
1. **Discovery & PRD (`/flow:prd`)**: Explores scope, architectural options, dependencies, and risks to produce a high-level roadmap.
2. **Technical Planning (`/flow:plan`)**: Formulates the decision-complete specification in `.agents/bundles/specs/<flow_id>/spec.md` and generates task worksheets under `tasks/*.md`.
3. **Zero-Ambiguity Mandate**: Every task must be an itemized, decision-complete worksheet containing target file paths, exact symbols, and unambiguous acceptance criteria. Stateless or fresh agents must be able to execute the worksheet with zero guessing.

## 3. Authoritative Task Worksheets & State Machine

Task files (`tasks/*.md`) are the **sole authority** for task state. The `spec.md` checklist is a derived view synchronized via `/flow:sync`.

### Task State Lifecycle

```
[open] ---> [in_progress] ---> [closed]
   |              |
   v              v
[skipped]     [blocked]
```

- **`open`**: Task is ready to be claimed when all `depends_on` tasks are `closed` and plan identity matches.
- **`in_progress`**: Claimed by exactly one agent instance. A task must be claimed before file modifications begin.
- **`closed`**: Successfully implemented, verified, and committed with recorded Git commit SHA and test evidence.
- **`blocked`**: Work cannot proceed due to an explicit technical blocker; records the blocker description and next step.
- **`skipped`**: Deliberately omitted with recorded rationale.

### Task Worksheet Contract

Every task file requires complete YAML frontmatter:
```yaml
---
id: task-1
flow_id: native-oracle-write
title: Implement OCI Array Binding in OracleInsert
type: Task
state: open # open | in_progress | closed | blocked | skipped
depends_on: []
plan_revision: 1
plan_commit: null
verification_strategy: behavior_tdd
files:
  - src/storage/oracle_insert.cpp
  - src/include/oracle_insert.hpp
tests:
  - test/integration_tests/test_oracle_insert.test
commit: null
---
```

## 4. Direct-Read Continuity

Flow operates without a background server, daemon, SQLite database, or Dolt tracking layer:
- Agents reorient by directly reading `.agents/setup-state.json`, `.agents/bundles/knowledge/workflow.md`, candidate active specs in `.agents/bundles/specs/`, and task frontmatter.
- Stale or interrupted transactions are resolved via untracked Markdown transaction journals under `.agents/transactions/<tx_id>/journal.md`.
- No historical conversation prompts or hook contexts are treated as state authority—filesystem state is the single source of truth.
