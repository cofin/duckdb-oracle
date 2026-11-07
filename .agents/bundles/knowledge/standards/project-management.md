---
type: Standard
title: Project Management & Flow OKF v0.2 Lifecycle
description: Spec-first chaptered planning, OKF v0.2 bundle architecture, authoritative task worksheets, and direct Markdown + Git continuity.
scope: standard
domain: workflow
status: stable
tags: [flow, okf, project-management, spec-first, chaptered-planning, continuity]
generated:
  by: flow/0.27.0
  at: 2026-11-07T17:36:00Z
verified:
  - by: agent:flow-setup
    at: 2026-11-07T17:36:00Z
    commit: 36539f3
sources:
  - id: setup-state
    resource: .agents/setup-state.json
    title: Active Flow bundle root, OKF version, and shared Git tracking policy
  - id: bundle-index
    resource: .agents/bundles/index.md
    title: Root OKF v0.2 bundle manifest
---

# Project Management & Flow OKF v0.2 Lifecycle

<!-- truth: start -->
- `.agents/bundles/` is the OKF v0.2 bundle root (`okf_version: "0.2"` in `.agents/bundles/index.md`), tracked in Git under `shared` policy (`setup-state.json`)[^setup-state][^bundle-index].
- Feature specifications use chaptered decomposition (`specs/<flow_id>/spec.md`, frontmatter-free `chapters/index.md`, and `chapters/ch01-<slug>/..chNN-<slug>/{spec.md,tasks/*.md}`) with `1–4` disjoint vertical-slice tasks per chapter (`read_set <= 6` files, `files <= 3`, `estimated_loc <= 250`, zero upper cap on chapter count).
- Task worksheets (`tasks/<id>-<slug>.md`), chapter specs (`chapters/<ch_id>/spec.md`), and root `spec.md` checklists (`[ ]`, `[~]`, `[x] [<sha>]`, `[!]`, `[-]`) plus Git commits are the sole state authority—no external databases or transaction journals.
- `/flow:implement` executes strictly **1 chapter** per invocation, synchronizes living Markdown (`spec.md`, `chapters/index.md`, `knowledge/**`, `docs/**`) in present tense, passes zero-context chapter review (`/flow:review`), and stamps `last_reviewed_commit: <head_sha>`.
<!-- truth: end -->

Project execution is governed by **Flow (`v0.27.0`)**, backed by **Open Knowledge Format (OKF v0.2)** knowledge bundles stored under `.agents/bundles/`[^setup-state][^bundle-index].

## 1. Consumer `.agents/` & OKF v0.2 Bundle Layout

```text
.agents/
├── setup-state.json                              # Active bundle root, setup status & ignore_policy
├── scratch/                                      # Untracked throwaway prototypes & spikes
├── skills/                                       # Project-local domain & review skills
│   ├── index.md
│   ├── <domain>/{SKILL.md,references/*.md}
│   └── review-<domain>/{SKILL.md,references/{checklist,false-positive-suppressions}.md}
├── agents/                                       # Paired project-local subagents
│   ├── <domain>-reviewer.md
│   └── <domain>-specialist.md
└── bundles/                                      # Version-controlled OKF v0.2 knowledge tree
    ├── index.md                                  # Root manifest (okf_version: "0.2")
    ├── log.md                                    # Frontmatter-free append-only ISO-dated changelog
    ├── product/                                  # Core product context chapters (<= 40-line truth blocks)
    │   ├── index.md
    │   ├── product.md
    │   ├── product-guidelines.md
    │   └── tech-stack.md
    ├── knowledge/                                # Evidence-backed institutional knowledge
    │   ├── index.md
    │   ├── workflow.md
    │   ├── architecture/
    │   ├── domains/
    │   ├── patterns/
    │   ├── standards/
    │   ├── testing/
    │   ├── gotchas/
    │   └── decisions/
    ├── research/
    │   └── index.md
    └── specs/
        ├── index.md
        └── <flow_id>/
            ├── spec.md                           # Full-plan roadmap & chapter checklist
            ├── learnings.md                      # Mid-flow discoveries
            └── chapters/
                ├── index.md                      # Frontmatter-free chapter catalog
                └── ch01-<slug>/
                    ├── spec.md                   # Chapter spec & last_reviewed_commit
                    └── tasks/<id>-<slug>.md      # 8-section vertical-slice task blueprints
```

## 2. Spec-First Chaptered Planning

1. **Discovery & PRD (`/flow:prd`)**: Explores scope, deep-module seams, and multi-flow decomposition when needed.
2. **Chaptered Feature Planning (`/flow:plan`)**: Formulates `specs/<flow_id>/spec.md`, `chapters/index.md`, and `chapters/ch01-<slug>/..chNN-<slug>/{spec.md,tasks/*.md}` upfront in parallel Chapter Planner subagents without truncating engineering or documentation detail.
3. **Zero-Decision Readiness Standard**: Every task worksheet contains 8 complete sections (`Objective & Scope`, `Target File & Symbol Map`, `Public Interface & Type Signatures`, `Behavioral Invariants & Edge Cases`, `Verification Strategy & Test Matrix`, `Implementation Steps`, `Acceptance Criteria`, `Notes & Discoveries`) so an isolated `@executor` subagent implements the slice with zero guesswork.

## 3. Direct Markdown & Git State Contract

- **Task States (`state:` in `tasks/<id>-<slug>.md`)**: `pending`, `in_progress`, `completed`, `blocked`, `deferred`.
- **Spec & Chapter States (`state:` in `spec.md`)**: `draft`, `planned`, `active`, `completed`, `archived`.
- **Document Maturity (`status:` in `knowledge/**` and `product/**`)**: `draft`, `stable`, `deprecated`.
- **Checklists**: Projected directly in chapter and root `spec.md` (`[ ]` pending, `[~]` in_progress, `[x] [<sha>]` completed, `[!]` blocked, `[-]` deferred).

[^setup-state]: `.agents/setup-state.json` — Active Flow bundle root, OKF version, and shared Git tracking policy.
[^bundle-index]: `.agents/bundles/index.md` — Root OKF v0.2 bundle manifest.
