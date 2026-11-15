---
type: Chapter
flow_id: unconstrained-number
chapter_id: ch02-timezone-roundtrip
title: "Reproduce and fix zoned timestamp round-trip"
description: "Empirically test TIMESTAMP WITH TIME ZONE and TIMESTAMP WITH LOCAL TIME ZONE round-trips against Oracle and align session TIME_ZONE if shifted."
status: stable
state: completed
planning_status: empirical_dependent
planning_quality: standard
depends_on_chapters:
  - ch01-numeric-fidelity
replan_trigger: "Reproduction shows named region or offset mismatch that ALTER SESSION TIME_ZONE = '+00:00' cannot resolve."
plan_revision: 1
plan_commit: 83efe19
state_revision: 2
current_task: null
last_reviewed_commit: 1309446
---

# Reproduce and fix zoned timestamp round-trip

## 5. Implementation Plan

- [x] [e4eca72] t01 — Reproduce zoned timestamp round-trip and apply session UTC fix if reproduced (tasks/t01-reproduce-and-fix-tz-roundtrip.md)
