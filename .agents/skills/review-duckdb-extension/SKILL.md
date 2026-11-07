---
name: review-duckdb-extension
description: "Use when reviewing changes to DuckDB 2.0 extension entrypoints, Catalog/Schema/Table entries, TableFunction optimizer hooks, DataChunk vector population, filter/projection pushdown, PhysicalOracleInsert sink writes, sqllogictest files, or community extension descriptors in src/*, test/*, or description.yml."
---

# DuckDB Extension Review Skill (`review-duckdb-extension`)

<!-- truth: start -->
- Audit every `DataChunk` vector mutation, `Catalog`/`SchemaCatalogEntry` override, `TableFunction` callback, filter pushdown rule, physical sink write, and `sqllogictest` assertion against `references/checklist.md` (`DDB-01` through `DDB-08`).
- Consult `references/false-positive-suppressions.md` (`FP-DDB-01` through `FP-DDB-05`) before reporting findings so intentional patterns are not flagged.
- Require file and symbol citations (`path/to/file:line` or `path/to/file::Symbol`) for every reported finding.
<!-- truth: end -->

## Workflow

1. Inspect the git diff across `src/`, `extension_config.cmake`, `CMakeLists.txt`, `description.yml`, `scripts/`, and `test/`.
2. Evaluate each modified file against [`references/checklist.md`](references/checklist.md).
3. Filter candidate findings against [`references/false-positive-suppressions.md`](references/false-positive-suppressions.md).
4. Emit findings with rule ID (`DDB-XX`), severity (`P0`–`P3`), exact `file:line`, and concrete remediation.

## Guardrails

- Do not flag patterns documented as intentional in [`references/false-positive-suppressions.md`](references/false-positive-suppressions.md).
- Verify the workspace remains clean (`git status --porcelain`) after completing read-only review checks.

## References

- [DuckDB Extension Review Checklist](references/checklist.md)
- [DuckDB Extension False-Positive Suppressions](references/false-positive-suppressions.md)
