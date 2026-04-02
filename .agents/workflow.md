# Project Workflow

## Guiding Principles

1. **Beads is the Source of Truth:** Task status lives in Beads (`br ready`, `br close`). Use `/flow-sync` to export Beads state to spec.md when needed.
2. **The Tech Stack is Deliberate:** Changes to the tech stack must be documented in `tech-stack.md` *before* implementation
3. **Test-Driven Development:** Write tests before implementing functionality
4. **High Code Coverage:** Aim for >80% code coverage for all modules
5. **Performance First:** Every decision should prioritize throughput and correctness
6. **Non-Interactive & CI-Aware:** Prefer non-interactive commands. Use `CI=true` for watch-mode tools (tests, linters) to ensure single execution.

## Beads Integration

Beads provides persistent cross-session memory. Configured for local-only use during setup.

### Session Protocol

**Session Start:**

```bash
br status                          # Workspace overview
br ready                           # List unblocked tasks (dependency-aware)
br list --status in_progress       # Resume active work
```

**Session End:**

```bash
br sync --flush-only         # Sync notes locally
git add .beads/
```

> If `br` is unavailable, workflow degrades gracefully to git-only tracking.

### When to Track in Beads

**Rule: If work takes >5 minutes, track it in Beads.**

| Duration | Action | Example |
|----------|--------|---------|
| <5 min | Just do it | Fix typo, update config |
| 5-30 min | Create task | Add validation, write test |
| 30+ min | Create task with subtasks | Implement feature |

**Why this matters:**

- Notes survive context compaction - critical for multi-session work
- `br ready` finds unblocked work automatically
- If resuming in 2 weeks would be hard without context, use Beads

### Creating Issues with Full Context

**CRITICAL: Always include `--description` with `br create`, then add notes via `br update`:**

```bash
br create "Task name" --parent {epic_id} -p 2 \
  --description="WHY this issue exists and WHAT needs to be done"

# Then add context notes (br create does NOT support --notes):
br update {id} --notes "CONTEXT: files affected, dependencies, origin command, timestamp"
```

- `--description`: Purpose and goal (set at creation with `br create`)
- `--notes`: Context for future agents (set via `br update` — survives compaction!)
- Priority levels: P0=critical, P1=high, P2=medium, P3=low, P4=backlog

## Task Workflow

All tasks follow a strict lifecycle:

### Standard Task Workflow (Beads-First)

**CRITICAL:** Beads is the source of truth. Never write `[x]`, `[~]`, `[!]`, or `[-]` markers to spec.md. After ANY Beads state change, agents MUST run `/flow-sync` to update spec.md.

1. **Select Task:** Use `br ready` for dependency-aware selection, or fall back to parsing spec.md

2. **Mark In Progress:**
   - Sync to Beads: `br update <id> --status in_progress`
   - **Do NOT edit spec.md** - Beads is source of truth

3. **Write Failing Tests (Red Phase):**
   - Create a new `.test` file in `test/unit_tests/` or `test/integration_tests/`
   - Use DuckDB `sqllogictest` format with `require oracle` directive
   - **CRITICAL:** Run `make test` and confirm tests fail as expected

4. **Implement to Pass Tests (Green Phase):**
   - Write minimum C++ code to make failing tests pass
   - Run `make test` and confirm all tests pass

5. **Refactor (Optional but Recommended):**
   - Clean up implementation with passing tests as safety net
   - Run `make format` to ensure DuckDB code style compliance
   - Run `make tidy-check` for static analysis
   - Rerun `make test` after refactoring

6. **Verify Quality:**
   ```bash
   make format          # Auto-format code
   make tidy-check      # Static analysis (clang-tidy)
   make test            # Unit tests
   make integration     # Integration tests (if Oracle changes)
   ```

7. **Document Deviations:** If implementation differs from tech stack:
   - **STOP** implementation
   - Update `tech-stack.md` with new design
   - Add dated note explaining the change
   - Resume implementation

8. **Commit Code Changes:**
   - Stage all code changes related to the task
   - Use conventional commit format: `feat(scope): description`

9. **Record Task Completion (Beads-First):**
   - **Step 9.1: Get Commit Hash:** `git log -1 --format="%h"`
   - **Step 9.2: Close in Beads:** `br close <id> --reason "commit: <sha>"`
   - **Step 9.3 (MANDATORY):** Run `/flow-sync` to export Beads state to spec.md
   - **Do NOT manually edit spec.md markers**

10. **Log Learnings:**
    - Append discoveries to flow's `learnings.md`
    - Sync to Beads: `br update <id> --notes "pattern: ..."`
    - Elevate reusable patterns to `.agents/patterns.md` at phase completion

### Knowledge Flywheel

1. **Capture** - After each task, append learnings to flow's `learnings.md`
2. **Elevate** - At phase/flow completion, move reusable patterns to `.agents/patterns.md`
3. **Synthesize** - During sync and archive, integrate learnings into cohesive knowledge base chapters in `.agents/knowledge/` (e.g., `architecture.md`, `oci-patterns.md`). Update the current state, do NOT outline history.
4. **Inherit** - New flows read `patterns.md` + scan `.agents/knowledge/` chapters.

**Knowledge Base:**

| Tier | File | Loaded | Purpose |
|------|------|--------|---------|
| **Patterns** | `.agents/patterns.md` | Always | Elevated actionable rules for priming |
| **Knowledge Chapters** | `.agents/knowledge/*.md` | On demand | Synthesized implementation details and current state |

**Important:** `.agents/patterns.md` is NOT archived with flows. It remains at the top level as persistent project knowledge. Knowledge chapters in `.agents/knowledge/` also persist independently of archives and describe the active codebase state.

**Learnings Entry Format:**

```markdown
## [YYYY-MM-DD HH:MM] - Phase N Task M: Task Description

- **Implemented:** Brief description
- **Files changed:** path/to/files
- **Commit:** abc1234
- **Learnings:**
  - Patterns: Codebase uses X for Y
  - Gotchas: Must do Z before W
  - Context: Module A owns B
```

### Phase Completion Verification and Checkpointing Protocol

**Trigger:** This protocol is executed immediately after a task is completed that also concludes a phase in `spec.md`.

1.  **Announce Protocol Start:** Inform the user that the phase is complete and the verification and checkpointing protocol has begun.

2.  **Ensure Test Coverage for Phase Changes:**
    -   **Step 2.1: Determine Phase Scope:** Read `spec.md` to find the Git commit SHA of the *previous* phase's checkpoint.
    -   **Step 2.2: List Changed Files:** Execute `git diff --name-only <previous_checkpoint_sha> HEAD`
    -   **Step 2.3: Verify and Create Tests:** For each `.cpp`/`.hpp` file, verify a corresponding `.test` file exists. Create missing tests using `sqllogictest` format.

3.  **Execute Automated Tests:**
    -   Announce the exact command before running.
    -   **Unit tests:** `make test`
    -   **Integration tests:** `make integration` (if Oracle-facing changes)
    -   If tests fail, attempt fix up to **2 times**, then ask user for guidance.

4.  **Propose Manual Verification Plan:**
    ```
    The automated tests have passed. For manual verification:

    **Manual Verification Steps:**
    1.  **Build the extension:** `make release`
    2.  **Start DuckDB with extension:** `./build/release/duckdb -unsigned`
    3.  **Load and test:** `LOAD 'build/release/extension/oracle/oracle.duckdb_extension';`
    4.  **Confirm expected behavior:** [specific queries to run]
    ```

5.  **Await Explicit User Feedback:** Ask for confirmation before proceeding.

6.  **Create Checkpoint Commit:** `flow(checkpoint): Checkpoint end of Phase X`

7.  **Record Verification in Beads:**
    `br comments add <epic_id> "Phase N verified: tests passed, manual verification confirmed, checkpoint: <sha>"`

8.  **Sync to spec.md (MANDATORY):** Run `/flow-sync` — do NOT manually edit spec.md.

9.  **Announce Completion.**

### Quality Gates

Before marking any task complete, verify:

- [ ] All tests pass (`make test`)
- [ ] Static analysis clean (`make tidy-check`)
- [ ] Code formatted (`make format`)
- [ ] Doxygen comments on public C++ API functions
- [ ] No security vulnerabilities (no user input in SQL strings)
- [ ] Documentation updated if needed

## Development Commands

### Setup
```bash
# Install Oracle Instant Client SDK
export ORACLE_HOME=/path/to/instantclient_23_6

# Build dependencies
make configure_ci       # Install OCI prerequisites (CI environments)
```

### Daily Development
```bash
make release            # Build extension (release mode)
make debug              # Build extension (debug mode)
make test               # Run unit tests (no Oracle needed)
make integration        # Run full test suite with Oracle container
```

### Before Committing
```bash
make format             # Auto-format with clang-format
make tidy-check         # Static analysis with clang-tidy
make test               # Verify unit tests pass
```

## Testing Requirements

### Unit Testing (`test/unit_tests/`)
- Every module must have corresponding `.test` files
- Use `sqllogictest` format with `require oracle` directive
- Test both success and error cases
- No Oracle container needed — tests use mock/stub responses

### Integration Testing (`test/integration_tests/`)
- Require live Oracle container: `gvenzl/oracle-free:23-slim`
- Test complete data flows (ATTACH → scan → write)
- Use `${ORACLE_CONNECTION_STRING}` placeholder
- Setup SQL in `test/integration_tests/init_sql/01_setup.sql`
- Cover: basic types, vectors (23ai), spatial, LOBs, edge cases

## Commit Guidelines

### Message Format
```
<type>(<scope>): <description>

[optional body]

[optional footer]
```

### Types
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation only
- `style`: Formatting changes
- `refactor`: Code change that neither fixes a bug nor adds a feature
- `test`: Adding or updating tests
- `chore`: Maintenance tasks (deps, CI, build)

### Scopes
- `read`: Read/scan operations
- `write`: Write/COPY operations
- `oci`: OCI connection/pooling
- `spatial`: SDO_GEOMETRY support
- `vector`: Oracle 23ai VECTOR support
- `secret`: Secret management
- `ci`: CI/CD workflows
- `deps`: Dependency updates

### Examples
```bash
git commit -m "feat(write): Add native OCI bind types for integer columns"
git commit -m "fix(oci): Prevent Column Shift in array fetch with SQLT_STR"
git commit -m "test(integration): Add write tests for VECTOR type"
git commit -m "chore(deps): Upgrade DuckDB to v1.4.4"
```

## Definition of Done

A task is complete when:

1. All code implemented to specification
2. Unit tests written and passing
3. Static analysis clean (`make tidy-check`)
4. Code formatted (`make format`)
5. Doxygen comments on public APIs
6. Implementation notes added to `spec.md`
7. Changes committed with proper message
8. Task closed in Beads with commit reference: `br close <id> --reason "commit: <sha>"`
9. Markdown synced via `/flow-sync` (MANDATORY after any Beads state change)

## Release Workflow

### Pre-Release Checklist
- [ ] All tests passing (`make test && make integration`)
- [ ] No clang-tidy warnings
- [ ] Code formatted
- [ ] `ORACLE_HOME` documented
- [ ] CI green on all platforms

### Release Steps
1. Merge feature branch to main
2. Tag release with semver: `git tag v1.x.x`
3. Push tag — triggers `release-unsigned.yml` for multi-platform builds
4. Verify artifacts for Linux x86_64/aarch64, macOS arm64, Windows x86_64
5. Submit to DuckDB community extensions (update `description.yml` if needed)
