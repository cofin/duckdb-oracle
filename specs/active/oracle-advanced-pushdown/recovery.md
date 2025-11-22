# Recovery Instructions: Oracle Pushdown & Session Tuning (Phase 1)

**Last Updated**: 2025-11-22

## Current Status
- Phase: Planning (code not started)
- Completion: 0%

## What's Been Done
- ✓ PRD drafted with scoped features and acceptance criteria.
- ✓ Research captured (pushdown patterns, OCI tuning).
- ✓ Tasks list created.

## Current Blockers
- None.

## Next Steps
1. Add DuckDB config options and wiring in `src/oracle_extension.cpp`.
2. Implement pushdown serialization + OCI tuning helpers.
3. Write SQL tests (`test/sql/oracle_pushdown.test`) for on/off, filters, projection, limit, validation.

## Files Modified
- `specs/active/oracle-advanced-pushdown/prd.md`
- `specs/active/oracle-advanced-pushdown/research/plan.md`
- `specs/active/oracle-advanced-pushdown/tasks.md`
- `specs/active/oracle-advanced-pushdown/recovery.md`

## Commands to Resume Work
```bash
git status
make
make test
```
