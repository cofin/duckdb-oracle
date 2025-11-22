Invoke the Expert agent to implement features with automated testing, documentation, and archival.

**What this does:**

- Reads PRD and tasks from `specs/active/{slug}/`
- Researches C++ and DuckDB API patterns
- Implements feature with quality standards
- **Automatically invokes Testing agent** (creates comprehensive .test files)
- **Automatically invokes Docs & Vision agent** (docs, quality gate, knowledge capture, archive)
- Returns only when entire workflow complete

**Usage:**

```
/implement oracle-table-scan
```

**Or for most recent spec:**

```
/implement
```

**The Expert agent will:**

1. Read PRD, tasks, and recovery guide
2. Consult AGENTS.md for C++ standards
3. Research similar patterns in codebase
4. Get library docs via Context7 (DuckDB, Oracle OCI)
5. Implement C++ code following quality rules
6. Build and self-test: `make release`
7. **Auto-invoke Testing agent:**
   - Create .test files in test/sql/oracle/
   - Test core functionality
   - Test edge cases (NULL, empty, errors)
   - Test error handling
   - Run: `make test`
   - All tests must pass (100%)
8. **Auto-invoke Docs & Vision agent:**
   - Update README.md and docs/
   - Quality gate validation
   - **Capture new patterns in AGENTS.md**
   - **Update guides in specs/guides/ with new patterns**
   - **Re-validate after updates** (re-run tests, verify build)
   - Clean and archive to specs/archive/
9. Return comprehensive completion summary

**This ONE command now handles:**
✅ C++ Implementation
✅ Testing (automatic via Testing agent)
✅ Documentation (automatic via Docs & Vision agent)
✅ Knowledge capture (automatic - AGENTS.md & specs/guides/)
✅ Quality gate (automatic)
✅ Re-validation (automatic)
✅ Archival (automatic)

**After implementation:**
Feature is complete, tested, documented, patterns captured, and archived! Ready for next feature.

**Example workflow:**

```bash
# 1. Create PRD
/prd Add Oracle connection pooling

# 2. Implement (handles everything automatically)
/implement oracle-connection-pooling

# Done! Feature is complete and archived.
```
