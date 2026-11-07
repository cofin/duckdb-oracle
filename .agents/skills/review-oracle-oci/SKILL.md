---
name: review-oracle-oci
description: "Use when reviewing changes to Oracle Call Interface (OCI) C ABI declarations, lazy runtime library loading, handle lifecycles, connection pooling, wallet/SEPS authentication, or OCI fetch/bind buffers in src/oracle_*.cpp, src/storage/oracle_write.cpp, or src/include/oracle_*.hpp."
---

# Oracle OCI Review Skill (`review-oracle-oci`)

<!-- truth: start -->
- Audit every OCI handle allocation, connection checkout, indicator check, and C ABI declaration against `references/checklist.md` (`OCI-01` through `OCI-08`).
- Consult `references/false-positive-suppressions.md` (`FP-OCI-01` through `FP-OCI-05`) before reporting findings so intentional architectural patterns are not flagged.
- Require file and symbol citations (`path/to/file.cpp:line` or `path/to/file.cpp::Symbol`) for every reported finding.
<!-- truth: end -->

## Workflow

1. Inspect the git diff across `src/include/oracle_*.hpp`, `src/oracle_*.cpp`, `src/storage/oracle_*.cpp`, `CMakeLists.txt`, and `scripts/*oci*`.
2. Evaluate each modified file against [`references/checklist.md`](references/checklist.md).
3. Filter candidate findings against [`references/false-positive-suppressions.md`](references/false-positive-suppressions.md).
4. Emit findings with rule ID (`OCI-XX`), severity (`P0`–`P3`), exact `file:line`, and concrete remediation.

## Guardrails

- Do not flag patterns documented as intentional in [`references/false-positive-suppressions.md`](references/false-positive-suppressions.md).
- Verify the workspace remains clean (`git status --porcelain`) after completing read-only review checks.

## References

- [Oracle OCI Review Checklist](references/checklist.md)
- [Oracle OCI False-Positive Suppressions](references/false-positive-suppressions.md)
