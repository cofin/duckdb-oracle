# Recovery Guide: Oracle Secret Management Implementation

**Feature**: DuckDB Secret Manager Integration for Oracle Extension
**Workspace**: `specs/active/oracle-secret-management/`
**Last Updated**: 2025-11-22
**Current Phase**: Phase 1 Complete ✅ | Ready for Phase 2

---

## Quick Resume

### Current Status

**Phase 1 (Planning & Research)**: ✅ **COMPLETE**

All planning work has been completed by the PRD Agent. The workspace is fully set up and ready for Expert Agent to begin Phase 2 research.

**What's Done:**
- Comprehensive PRD created with full technical specification
- Research completed on DuckDB Secret Manager patterns
- PostgreSQL/MySQL extension analysis complete
- Task breakdown with detailed implementation checklist
- Decision made: **GO with Hybrid Approach (Option B)**

**Next Action**: Expert Agent to begin Phase 2 research

---

## Workspace Structure

```
specs/active/oracle-secret-management/
├── prd.md                              # Comprehensive Product Requirements Document
├── tasks.md                            # Detailed implementation task breakdown
├── recovery.md                         # This file - recovery instructions
├── research/
│   ├── duckdb-secret-manager-analysis.md  # Research findings and recommendations
│   └── .gitkeep
└── tmp/
    └── .gitkeep
```

---

## Key Documents

### 1. PRD (`prd.md`)

**Purpose**: Complete product requirements and technical specification

**Key Sections:**
- Problem Statement: Inconsistent credential management across extensions
- Proposed Solution: Hybrid approach (connection string + wallet + secret manager)
- Technical Architecture: C++ implementation details
- User Stories: 5 detailed user scenarios
- Implementation Phases: 7-phase breakdown
- Research Questions: For Expert Agent to answer in Phase 2
- Success Metrics: Code quality, performance, adoption targets

**Decision**: **GO** - Implement Option B (Hybrid Approach)

---

### 2. Research Document (`research/duckdb-secret-manager-analysis.md`)

**Purpose**: Analysis of DuckDB Secret Manager and implementation options

**Key Findings:**
- PostgreSQL/MySQL extensions use CREATE SECRET with TYPE parameter
- Oracle extension currently uses connection strings + wallet
- Gap: No secret manager integration
- **Recommendation**: Hybrid approach maintains backward compatibility

**Implementation Options Analyzed:**
- Option A: Full Secret Manager Integration (3-5 days)
- **Option B: Hybrid Approach (4-6 days) ← SELECTED**
- Option C: Documentation Only (0.5 days)

**Rationale for Option B:**
1. Consistency with DuckDB ecosystem
2. Backward compatible (zero breaking changes)
3. Enterprise-ready credential management
4. Future-proof for Oracle Cloud integration

---

### 3. Tasks (`tasks.md`)

**Purpose**: Detailed implementation checklist with 7 phases

**Phase Breakdown:**
- Phase 1: Planning & Research (1 day) ✅ COMPLETE
- Phase 2: Expert Research (1-2 days) ← NEXT
- Phase 3: Core Implementation (2-3 days)
- Phase 4: Integration (1 day)
- Phase 5: Testing (1-2 days - AUTO-INVOKED)
- Phase 6: Documentation (1 day - AUTO-INVOKED)
- Phase 7: Quality Gate & Archive (0.5 day - AUTO-INVOKED)

**Total Estimated Effort**: 5-8 days

---

## How to Resume Work

### For Expert Agent (Phase 2)

**Immediate Next Steps:**

1. **Read the PRD**
   ```bash
   cat specs/active/oracle-secret-management/prd.md
   ```
   - Focus on "Research Questions" section
   - Review "Technical Architecture" section
   - Understand "Proposed Solution"

2. **Read Research Document**
   ```bash
   cat specs/active/oracle-secret-management/research/duckdb-secret-manager-analysis.md
   ```
   - Understand Option B (Hybrid Approach)
   - Review implementation notes

3. **Review Tasks**
   ```bash
   cat specs/active/oracle-secret-management/tasks.md
   ```
   - Focus on Phase 2 tasks
   - Plan research approach

4. **Begin Research Tasks** (see Phase 2 section below)

---

### Phase 2 Research Tasks (Expert Agent)

#### Task 2.1: DuckDB SecretManager C++ API Study
**Priority**: P0 (Critical)
**Estimated**: 0.5 day

**Objectives:**
- Locate SecretManager class in DuckDB headers
- Document `SecretManager::Get()` API
- Document `LookupSecret()` and `GetSecretByName()` APIs
- Study `KeyValueSecret` structure
- Understand deserialization patterns

**Approach:**
1. Search DuckDB codebase for SecretManager definition
2. Examine PostgreSQL extension for usage examples
3. Create API reference document

**Deliverable**: `research/secret-manager-cpp-api.md`

---

#### Task 2.2: PostgreSQL Extension Analysis
**Priority**: P0 (Critical)
**Estimated**: 0.5 day

**Objectives:**
- Locate secret type registration code in postgres extension
- Document parameter definitions
- Study ATTACH integration
- Extract reusable patterns

**Approach:**
1. Clone/examine DuckDB postgres extension source
2. Find secret registration in extension initialization
3. Trace ATTACH function modifications
4. Document patterns

**Deliverable**: `research/postgres-extension-secrets.md`

---

#### Task 2.3: Secret Type Registration Prototype
**Priority**: P1 (High)
**Estimated**: 0.5 day

**Objectives:**
- Create minimal secret type registration example
- Test parameter extraction
- Verify validation logic

**Approach:**
1. Create prototype C++ code
2. Test in isolation
3. Document registration flow

**Deliverable**: `research/secret-registration-prototype.cpp`

---

#### Task 2.4: Connection String Builder Design
**Priority**: P1 (High)
**Estimated**: 0.5 day

**Objectives:**
- Design `BuildConnectionStringFromSecret()` function
- Handle parameter validation
- Support DATABASE/SERVICE aliases
- Plan error messages

**Approach:**
1. Design function signature
2. Create test cases
3. Document edge cases

**Deliverable**: `research/connection-string-builder-design.md`

---

### For Testing Agent (Phase 5 - AUTO-INVOKED)

**When Expert Agent completes Phase 4 and invokes Testing Agent:**

1. **Read Implementation Summary**
   - Expert should provide summary of what was implemented
   - Review code changes in `src/oracle_secret.cpp` and related files

2. **Review Test Requirements**
   - Check `tasks.md` Phase 5 section
   - Review test file requirements:
     - `test_oracle_secret_basic.test`
     - `test_oracle_secret_named.test`
     - `test_oracle_secret_override.test`
     - `test_oracle_secret_errors.test`
     - `test_oracle_secret_backward_compat.test`

3. **Create Comprehensive Tests**
   - SQL tests for all scenarios
   - Integration tests with Oracle container
   - Edge case tests
   - Performance benchmarks

4. **Verify Backward Compatibility**
   - All existing tests must pass unchanged
   - No regressions

---

### For Docs & Vision Agent (Phase 6 - AUTO-INVOKED)

**When Testing Agent completes Phase 5 and invokes Docs & Vision Agent:**

1. **Read PRD Section: "Documentation Requirements"**
   - README.md updates needed
   - New guides to create
   - Code documentation requirements

2. **Review Implementation and Tests**
   - Understand what was implemented
   - Review test examples for documentation

3. **Create Documentation**
   - Update README.md with secret examples
   - Create `specs/guides/secret-management.md`
   - Create `specs/guides/migration-to-secrets.md`
   - Create `specs/guides/security-best-practices.md`
   - Add Doxygen comments to code

4. **Quality Gate and Archive** (Phase 7)
   - Verify all acceptance criteria met
   - Move workspace to `specs/archive/`
   - Update project guides

---

## Key Implementation Details

### Current Oracle Authentication Flow

```
User calls: ATTACH 'user/pass@host:port/service' AS ora (TYPE oracle);
                ↓
OracleAttach() in oracle_storage_extension.cpp (line 11-35)
                ↓
Parse connection_string = info.path
                ↓
OracleCatalogState created with connection_string
                ↓
OracleConnection::Connect(connection_string)
                ↓
Parse user/pass@host:port/service
                ↓
OCILogon(user, password, db)
```

### Proposed Secret Manager Flow

```
User calls: ATTACH '' AS ora (TYPE oracle);
                ↓
OracleAttach() checks: info.path empty?
                ↓ YES
SecretManager::Get(context).LookupSecret("", "oracle")
                ↓
Retrieved KeyValueSecret
                ↓
BuildConnectionStringFromSecret(secret)
    → Extract HOST, PORT, SERVICE, USER, PASSWORD
    → Build: "user/password@host:port/service"
                ↓
OracleCatalogState created with connection_string
                ↓
OracleConnection::Connect(connection_string)
    (existing code unchanged)
```

---

## Files to Modify/Create

### New Files (Phase 3)

1. **`src/oracle_secret.cpp`**
   - `OracleSecretParameters` struct
   - `ParseOracleSecret()` function
   - `ValidateOracleSecret()` function
   - `BuildConnectionStringFromSecret()` function
   - `DeserializeOracleSecret()` function

2. **`src/include/oracle_secret.hpp`**
   - Header declarations
   - Doxygen documentation

### Files to Modify (Phase 3)

1. **`src/oracle_extension.cpp`**
   - `LoadInternal()` function
   - Add secret type registration
   - Register "oracle" secret type

2. **`src/storage/oracle_storage_extension.cpp`**
   - `OracleAttach()` function
   - Add secret retrieval logic
   - Implement decision tree (path vs secret)
   - Add parameter override support

3. **`CMakeLists.txt`** (if needed)
   - Add new source files to build

---

## Research Questions for Expert Agent

These questions from PRD need answers in Phase 2:

### Secret Manager C++ API

1. What is the exact signature for `SecretManager::Get()`?
2. How to retrieve secrets by name vs. default lookup?
3. What is the structure of `KeyValueSecret`?
4. How to deserialize secret parameters?

### Secret Type Registration

1. Where in LoadInternal() to register secret type?
2. What is the SecretType struct definition?
3. Required vs. optional parameters specification?
4. How to define secret_provider types?

### PostgreSQL Extension Implementation

1. How does postgres extension parse secret parameters?
2. What error messages are used for missing secrets?
3. How are parameter overrides handled?
4. What validation is performed on CREATE SECRET?

### Integration Points

1. Does SecretManager need explicit initialization?
2. How to check if secrets feature is available?
3. Thread safety considerations for secret retrieval?
4. Memory management for secret data?

### Edge Cases

1. Behavior when both path and SECRET provided?
2. Empty string vs. NULL for optional parameters?
3. Case sensitivity for parameter names?
4. Special character escaping in passwords?

---

## Acceptance Criteria (Phase 1 Complete)

✅ All Phase 1 criteria met:

- [x] DuckDB Secret Manager API researched
- [x] PostgreSQL/MySQL extension patterns analyzed
- [x] Current Oracle authentication flows documented
- [x] Comprehensive PRD created
- [x] Technical architecture defined
- [x] Implementation approach identified
- [x] Workspace structure created
- [x] Task breakdown complete
- [x] Recovery guide created

---

## Success Metrics

### Development Metrics (for Phase 3-4)

- Code coverage: ≥ 90% for new code
- Zero compiler warnings
- clang-tidy checks pass
- Zero memory leaks (valgrind)
- ATTACH overhead: < 1ms for secret retrieval

### Functional Metrics (for Phase 5)

- CREATE SECRET (TYPE oracle) works
- ATTACH with default secret works
- ATTACH with named secret works
- 100% backward compatibility (all existing tests pass)
- Clear error messages for all failure cases

---

## Important Context

### DuckDB Secret Manager Patterns (from Research)

**PostgreSQL Example:**
```sql
CREATE SECRET (
    TYPE postgres,
    HOST '127.0.0.1',
    PORT 5432,
    DATABASE postgres,
    USER 'postgres',
    PASSWORD ''
);

ATTACH '' AS postgres_db (TYPE postgres);
```

**MySQL Example:**
```sql
CREATE SECRET mysql_secret_one (
    TYPE mysql,
    HOST '127.0.0.1',
    PORT 0,
    DATABASE mysql,
    USER 'mysql',
    PASSWORD ''
);

ATTACH '' AS mysql_db (TYPE mysql, SECRET mysql_secret_one);
```

**Oracle Target (to implement):**
```sql
CREATE SECRET (
    TYPE oracle,
    HOST 'localhost',
    PORT 1521,
    SERVICE 'XEPDB1',
    USER 'scott',
    PASSWORD 'tiger'
);

ATTACH '' AS ora (TYPE oracle);
```

---

### Current Oracle Extension Code Locations

**Connection Parsing**: `src/oracle_connection.cpp:53-84`
```cpp
void OracleConnection::Connect(const std::string &connection_string) {
    // Parse EZConnect: user/password@host:port/service
    auto slash_pos = connection_string.find('/');
    auto at_pos = connection_string.find('@', slash_pos);
    // ... parsing logic ...
    auto user = connection_string.substr(0, slash_pos);
    auto password = connection_string.substr(slash_pos + 1, at_pos - slash_pos - 1);
    auto db = connection_string.substr(at_pos + 1);
    // ... OCILogon() call ...
}
```

**ATTACH Entry Point**: `src/storage/oracle_storage_extension.cpp:11-35`
```cpp
static unique_ptr<Catalog> OracleAttach(...) {
    string connection_string = info.path;  // ← Need to modify this
    // ... state creation ...
    auto state = make_shared_ptr<OracleCatalogState>(connection_string);
    state->ApplyOptions(options.options);
    return CreateOracleCatalog(db, state);
}
```

**Settings Application**: `src/storage/oracle_catalog.cpp:32-55`
```cpp
void OracleCatalogState::ApplyOptions(const unordered_map<string, Value> &options) {
    for (auto &entry : options) {
        auto key = StringUtil::Lower(entry.first);
        if (key == "enable_pushdown") { ... }
        else if (key == "prefetch_rows") { ... }
        // ... more settings ...
    }
}
```

---

## Troubleshooting

### If Workspace Structure Missing

```bash
# Recreate workspace
mkdir -p specs/active/oracle-secret-management/research
mkdir -p specs/active/oracle-secret-management/tmp

# Verify files exist
ls -la specs/active/oracle-secret-management/
```

### If Documentation Missing

All key documents should exist:
- `prd.md` - Product Requirements Document
- `tasks.md` - Task breakdown
- `recovery.md` - This file
- `research/duckdb-secret-manager-analysis.md` - Research findings

If missing, check git status or recreate from task list.

### If Unclear on Next Steps

1. **For Expert Agent**: Start with Phase 2, Task 2.1 (SecretManager C++ API study)
2. **For Testing Agent**: Wait for Expert to complete Phase 4 and invoke you
3. **For Docs & Vision Agent**: Wait for Testing to complete Phase 5 and invoke you

---

## Communication Between Agents

### Expert → Testing Handoff

When Expert completes Phase 4, provide:
1. Summary of implementation
2. List of modified/created files
3. Any gotchas or edge cases discovered
4. Suggested test scenarios beyond standard list

### Testing → Docs & Vision Handoff

When Testing completes Phase 5, provide:
1. Test results summary
2. Code examples from tests for documentation
3. Any behavior clarifications needed in docs
4. List of error messages to document

---

## Timeline

| Phase | Duration | Cumulative | Status |
|-------|----------|------------|--------|
| Phase 1: Planning | 1 day | 1 day | ✅ Complete |
| Phase 2: Research | 1-2 days | 2-3 days | 🔲 Next |
| Phase 3: Implementation | 2-3 days | 4-6 days | 🔲 Pending |
| Phase 4: Integration | 1 day | 5-7 days | 🔲 Pending |
| Phase 5: Testing | 1-2 days | 6-9 days | 🔲 Pending |
| Phase 6: Documentation | 1 day | 7-10 days | 🔲 Pending |
| Phase 7: Archive | 0.5 day | 7.5-10.5 days | 🔲 Pending |

**Estimated Total**: 7.5-10.5 days (conservative estimate)
**Fast Track Estimate**: 5-8 days (from PRD)

---

## References

### DuckDB Documentation
- Secret Manager: https://duckdb.org/docs/1.1/configuration/secrets_manager
- CREATE SECRET: https://duckdb.org/docs/0.10/sql/statements/create_secret
- PostgreSQL Extension: https://duckdb.org/docs/stable/core_extensions/postgres
- MySQL Extension: https://duckdb.org/docs/stable/core_extensions/mysql

### Internal Documentation
- `/home/cody/code/other/duckdb-oracle/CLAUDE.md` - Agent system
- `/home/cody/code/other/duckdb-oracle/specs/guides/architecture.md`
- `/home/cody/code/other/duckdb-oracle/specs/guides/code-style.md`

---

## Decision Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2025-11-22 | GO with Hybrid Approach (Option B) | Backward compatible, consistent with DuckDB ecosystem, enterprise-ready |
| 2025-11-22 | Phase 1 MVP: Basic secrets only | Defer wallet integration to Phase 2 for simplicity |
| 2025-11-22 | Target DuckDB 1.0+ | SecretManager API stable in 1.0+ |

---

**Last Updated**: 2025-11-22
**Current Owner**: Expert Agent (Phase 2)
**Status**: Ready for Expert Research Phase
**Next Action**: Begin Task 2.1 (DuckDB SecretManager C++ API Study)
