# Recovery Guide: DuckDB Version Update

**Slug**: duckdb-version-update
**Last Updated**: 2025-12-24
**Status**: Planning Complete
**Complexity**: Medium

## Intelligence Context (NEW)

**Similar Features Analyzed**:
1. `.github/workflows/main-distribution-pipeline.yml` (Distribution pattern)
2. `.github/workflows/oracle-ci.yml` (Integration pattern)
3. `duckdb/` submodule (Dependency pattern)

**Patterns to Follow**:
- **Split-Pipeline**: Keep distribution and integration tests in separate workflows.
- **Triple-Point Update**: Sync submodules, workflow configs, and CMake.
- **Secret-Dependent Automation**: PATs for workflow updates.

**Tool Strategy Used**:
- Reasoning: `crash` (Redundancy analysis)
- Research: Web Search (Release notes)

## Current Phase

Phase 1 (Planning) - COMPLETE

Checkpoints completed:
- ✓ Checkpoint 0: Intelligence bootstrapped
- ✓ Checkpoint 1: Requirements analyzed (v1.4.3 confirmed)
- ✓ Checkpoint 2: Workspace created (patterns/ added)
- ✓ Checkpoint 3: Intelligent analysis completed
- ✓ Checkpoint 4: Research completed (Plan updated)
- ✓ Checkpoint 5: PRD written (Updated with new strategy)
- ✓ Checkpoint 6: Tasks broken down
- ✓ Checkpoint 7: Recovery guide created

## Next Steps

**Ready for implementation**:

1. Run `/implement duckdb-version-update`
2. Expert agent will:
   - Check for Substrait usage
   - Rename `oracle-ci.yml` -> `integration-tests.yml`
   - Fix `duckdb-update-check.yml` syntax
   - Update submodules and workflow versions
3. Testing agent will verify build and integration tests

## Important Context

**Key components to be modified**:
- `duckdb` submodule
- `.github/workflows/` files

**Pattern compliance checklist**:
- [ ] Rename, don't delete, the integration workflow
- [ ] Use `$GITHUB_OUTPUT` for Python scripts in workflows

**Research findings**: See [research/plan.md](./research/plan.md)
**Pattern analysis**: See [patterns/analysis.md](./patterns/analysis.md)
**Acceptance criteria**: See [prd.md](./prd.md)

## Resumption Instructions

**If session interrupted**:
1. Read `prd.md` for the "Rename not delete" decision.
2. Follow `tasks.md` for the correct order of operations.
3. Ensure `make integration` is run locally before pushing.