# Recovery Guide: CI Fix

**Workspace**: `specs/active/ci-fix`
**Phase**: Implementation (Phase 2/3)

## Context
We are fixing CI hangs and optimizing the Oracle extension for performance and concurrency.
Previous work focused on organizing tests and initial OCI connection refactoring.
Current focus is on implementing streaming fetch and ensuring thread safety.

## State
- `tasks.md`: Tracks granular progress.
- `findings.md`: detailed investigation notes.
- Codebase: `src/oracle_extension.cpp` has been partially refactored.

## Next Steps
1.  Research OCI best practices for threading and streaming.
2.  Implement streaming fetch in `oracle_scan`.
3.  Verify fix locally.
4.  Push to CI.
