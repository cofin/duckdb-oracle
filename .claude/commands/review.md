Invoke the Docs & Vision agent for documentation, quality gate, knowledge capture, and archival.

**What this does:**
- Validates all acceptance criteria met
- Updates project documentation (README.md, docs/)
- **Captures new patterns in AGENTS.md**
- **Updates agent guides in specs/guides/**
- **Re-validates after updates**
- Cleans workspace and archives requirement

**Usage:**
```
/review oracle-table-scan
```

**Or for most recent spec:**
```
/review
```

**The Docs & Vision agent will:**

### Phase 1: Documentation
1. Read PRD for feature details
2. Read specs/guides/docs-vision-agent.md for workflow
3. Update README.md with new feature section
4. Create/update docs/ with comprehensive guides
5. Add working code examples (SQL usage)
6. Validate examples work

### Phase 2: Quality Gate
1. Verify all PRD acceptance criteria met
2. Verify all tests passing (100%)
3. Check C++ code standards compliance (AGENTS.md)
4. Verify build succeeds: `make release`
5. Check for compiler warnings
6. BLOCK if any criteria not met

### Phase 3: Knowledge Capture (Important!)
1. Analyze implementation for new patterns
2. Extract C++ best practices and conventions
3. **Update AGENTS.md with new patterns:**
   - C++ idioms
   - DuckDB API usage patterns
   - Oracle OCI integration techniques
   - Memory management approaches
   - Error handling strategies
4. **Update relevant guides in specs/guides/:**
   - expert-agent.md (implementation patterns)
   - testing-agent.md (test patterns)
5. Document patterns with working code examples

### Phase 4: Re-validation (Critical!)
1. Re-build after documentation updates: `make clean && make release`
2. Re-run tests: `make test`
3. Check pattern consistency across project
4. Verify no breaking changes introduced
5. Verify documentation accuracy
6. BLOCK if re-validation fails

### Phase 5: Cleanup & Archive
1. Remove all tmp/ files: `find specs/active/{slug}/tmp -type f -delete`
2. Move specs/active/{slug} to specs/archive/
3. Add archive timestamp
4. Generate completion report

**Note:** This command is typically not needed manually because `/implement` automatically invokes Docs & Vision. Use this only if you need to:
- Re-run validation after manual changes
- Regenerate documentation
- Force re-archival
- Update AGENTS.md with patterns after manual implementation

**After review:**
Feature is documented, validated, patterns captured in AGENTS.md and specs/guides/, re-validated, and archived!

**Knowledge Capture Example:**

Before running `/review`, AGENTS.md might not have Oracle-specific patterns. After `/review`, it will contain:

```cpp
## Pattern: Oracle OCI Handle Management

class OCIHandleGuard {
    // RAII wrapper for automatic cleanup
    OCIEnv* env_;
    ~OCIHandleGuard() { /* cleanup */ }
};
```

And specs/guides/expert-agent.md will have detailed integration examples.
