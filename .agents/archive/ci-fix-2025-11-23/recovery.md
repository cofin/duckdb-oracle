# Recovery Guide: CI Fix

**Workspace**: `specs/active/ci-fix`
**Phase**: Verification

## Context
We are fixing CI hangs and optimizing the Oracle extension for performance and concurrency.
We implemented streaming fetch (array fetch), fixed memory corruption, fixed thread safety, and resolved internal errors.

## State
- `tasks.md`: Tracks granular progress.
- `findings.md`: detailed investigation notes.
- Codebase: `src/oracle_extension.cpp` and `storage` have been refactored.

## Next Steps
1.  Consider investigating `ORA-01007` in `advanced_schema_resolution_integration.test`.
2.  Run Testing Agent to expand coverage if needed.
3.  Push to CI.