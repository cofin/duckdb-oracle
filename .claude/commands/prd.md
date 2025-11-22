Invoke the PRD agent to create a comprehensive requirement workspace.

**What this does:**

- Creates `specs/active/{feature-slug}/` directory structure
- Writes detailed PRD with technical considerations
- Creates actionable task list
- Generates recovery guide for resuming work
- Identifies research questions for Expert

**Usage:**

```
/prd Add Oracle table scan function
```

**The PRD agent will:**

1. Analyze the requirement and existing codebase patterns
2. Research DuckDB extension APIs and Oracle OCI
3. Create workspace: `specs/active/{slug}/`
4. Write comprehensive PRD covering:
   - Technical scope (C++ implementation, DuckDB APIs, Oracle OCI)
   - Testing strategy (.test files)
   - Build changes (CMakeLists.txt, vcpkg.json)
   - Documentation requirements
5. Create task breakdown by phase
6. Write recovery guide for any agent to resume

**Output Structure:**

```
specs/active/{slug}/
├── prd.md          # Product Requirements Document
├── tasks.md        # Phase-by-phase task checklist
├── recovery.md     # Recovery guide for resuming work
├── research/       # Expert research findings
│   └── .gitkeep
└── tmp/            # Temporary files (cleaned by Docs & Vision)
    └── .gitkeep
```

**After planning, run:**

- `/implement {slug}` to build the feature (auto-runs testing, docs, archive)
