# Implementation Tasks: Oracle Secret Management

**Feature**: DuckDB Secret Manager Integration
**Status**: Phase 1 Complete ✅ | Phase 2 In Progress
**Last Updated**: 2025-11-22

---

## Phase 1: Planning & Research ✅ (COMPLETE)

**Duration**: 1 day
**Owner**: PRD Agent
**Status**: ✅ Complete

### Tasks

- [x] Research DuckDB Secret Manager API documentation
- [x] Analyze PostgreSQL extension secret implementation patterns
- [x] Analyze MySQL extension secret implementation patterns
- [x] Document current Oracle extension authentication flows
- [x] Create comprehensive PRD document
- [x] Define technical architecture and implementation approach
- [x] Create workspace structure in `specs/active/oracle-secret-management/`
- [x] Identify risks and mitigations
- [x] Define success criteria and metrics
- [x] Create research documentation

### Deliverables

- [x] `specs/active/oracle-secret-management/prd.md`
- [x] `specs/active/oracle-secret-management/research/duckdb-secret-manager-analysis.md`
- [x] `specs/active/oracle-secret-management/tasks.md`
- [x] `specs/active/oracle-secret-management/recovery.md`

---

## Phase 2: Expert Research

**Duration**: 1-2 days
**Owner**: Expert Agent
**Status**: 🔲 Not Started

### Research Tasks

#### 2.1 DuckDB SecretManager C++ API Study
**Priority**: P0 (Critical)

- [ ] Locate SecretManager class definition in DuckDB headers
- [ ] Document `SecretManager::Get()` API signature
- [ ] Document `SecretManager::LookupSecret()` API signature
- [ ] Document `SecretManager::GetSecretByName()` API signature
- [ ] Study `KeyValueSecret` class structure
- [ ] Understand secret deserialization patterns
- [ ] Identify thread safety requirements
- [ ] Document memory management patterns

**Deliverable**: `research/secret-manager-cpp-api.md`

---

#### 2.2 PostgreSQL Extension Analysis
**Priority**: P0 (Critical)

- [ ] Clone/examine DuckDB PostgreSQL extension source
- [ ] Locate secret type registration code
- [ ] Document secret parameter definitions
- [ ] Study ATTACH integration with secrets
- [ ] Analyze error handling patterns
- [ ] Extract connection string building logic
- [ ] Document parameter override implementation
- [ ] Identify reusable patterns

**Deliverable**: `research/postgres-extension-secrets.md`

**Files to examine**:
- PostgreSQL extension secret registration
- ATTACH function modifications
- Secret parameter parsing

---

#### 2.3 Secret Type Registration Prototype
**Priority**: P1 (High)

- [ ] Create minimal secret type registration example
- [ ] Test secret creation in SQL
- [ ] Verify parameter extraction
- [ ] Test validation logic
- [ ] Document registration flow

**Deliverable**: `research/secret-registration-prototype.cpp`

---

#### 2.4 Connection String Builder Design
**Priority**: P1 (High)

- [ ] Design `BuildConnectionStringFromSecret()` function
- [ ] Handle required parameter validation
- [ ] Support DATABASE/SERVICE aliases
- [ ] Design parameter override merge logic
- [ ] Plan error message format
- [ ] Create test cases for edge scenarios

**Deliverable**: `research/connection-string-builder-design.md`

**Test cases to cover**:
- Minimal parameters (user, service)
- All parameters specified
- DATABASE vs SERVICE alias
- Missing required parameters
- Invalid port numbers
- Special characters in passwords
- Empty/NULL parameters

---

#### 2.5 Implementation Plan Refinement
**Priority**: P2 (Medium)

- [ ] Identify exact files to modify
- [ ] Plan CMakeLists.txt changes (if new files)
- [ ] Define public API surface
- [ ] Plan backward compatibility testing
- [ ] Estimate build/test time
- [ ] Create implementation checklist

**Deliverable**: `research/implementation-plan.md`

---

### Phase 2 Exit Criteria

- [ ] SecretManager C++ API fully documented
- [ ] PostgreSQL extension patterns understood
- [ ] Prototype code validates approach
- [ ] Connection string builder designed
- [ ] All research questions from PRD answered
- [ ] Expert agent confirms ready for implementation

---

## Phase 3: Core Implementation

**Duration**: 2-3 days
**Owner**: Expert Agent
**Status**: 🔲 Not Started

### 3.1 Secret Type Registration

**Files to create**:
- `src/oracle_secret.cpp`
- `src/include/oracle_secret.hpp`

**Tasks**:

- [ ] Define `OracleSecretParameters` struct
  - [ ] host (string)
  - [ ] port (idx_t, default 1521)
  - [ ] service (string)
  - [ ] database (string, alias for service)
  - [ ] user (string)
  - [ ] password (string)
  - [ ] wallet_path (string, Phase 2)

- [ ] Implement `ParseOracleSecret()` function
  - [ ] Extract parameters from KeyValueSecret
  - [ ] Handle missing optional parameters
  - [ ] Provide defaults (port=1521, host=localhost)
  - [ ] Validate required parameters exist

- [ ] Implement `ValidateOracleSecret()` function
  - [ ] Check required: USER
  - [ ] Check required: SERVICE or DATABASE
  - [ ] Check optional: HOST (default localhost)
  - [ ] Check optional: PORT (default 1521, range 1-65535)
  - [ ] Return descriptive error messages

- [ ] Implement `DeserializeOracleSecret()` function
  - [ ] Called by DuckDB when loading secrets
  - [ ] Deserialize KeyValueSecret to OracleSecretParameters
  - [ ] Return parsed structure

- [ ] Register secret type in `oracle_extension.cpp::LoadInternal()`
  - [ ] Create SecretType instance
  - [ ] Set name = "oracle"
  - [ ] Set deserializer function
  - [ ] Set default_provider = "config"
  - [ ] Call `SecretType::RegisterSecretType()`

- [ ] Add unit tests for secret parsing
  - [ ] Test minimal parameters
  - [ ] Test all parameters
  - [ ] Test default values
  - [ ] Test validation errors

**Acceptance**:
- [ ] `CREATE SECRET (TYPE oracle, ...)` works in SQL
- [ ] Parameters extracted correctly
- [ ] Validation errors clear and helpful
- [ ] No compiler warnings

---

### 3.2 Connection String Builder

**File**: `src/oracle_secret.cpp`

**Tasks**:

- [ ] Implement `BuildConnectionStringFromSecret(const KeyValueSecret &secret)`
  - [ ] Parse secret to OracleSecretParameters
  - [ ] Validate required parameters
  - [ ] Build EZConnect format: `user/password@host:port/service`
  - [ ] Handle missing optional parameters
  - [ ] Handle empty password (OS auth)
  - [ ] Return valid connection string

- [ ] Add test cases
  - [ ] Full credentials: `user/pass@host:port/service`
  - [ ] No password: `user@host:port/service`
  - [ ] Default port: `user/pass@host/service` (expands to :1521)
  - [ ] Default host: `user/pass@localhost:port/service`
  - [ ] DATABASE alias: SERVICE and DATABASE both work

**Edge Cases**:
- [ ] Special characters in password (URL encoding?)
- [ ] Empty strings vs NULL
- [ ] Whitespace trimming
- [ ] Case sensitivity

**Acceptance**:
- [ ] Generated connection strings work with OracleConnection::Connect()
- [ ] All test cases pass
- [ ] Error messages are descriptive

---

### 3.3 OracleAttach Integration

**File**: `src/storage/oracle_storage_extension.cpp`

**Tasks**:

- [ ] Modify `OracleAttach()` function signature (if needed)

- [ ] Implement secret retrieval logic
  ```cpp
  // Pseudocode decision tree
  if (!info.path.empty() && info.path != "") {
      // Existing behavior: use connection string
      connection_string = info.path;
  } else {
      // NEW: Retrieve from secret
      auto &secret_manager = SecretManager::Get(context);
      shared_ptr<KeyValueSecret> secret;

      if (options.options.count("secret")) {
          // Named secret
          auto secret_name = options.options["secret"].ToString();
          secret = secret_manager.GetSecretByName(secret_name, "oracle");
      } else {
          // Default secret
          secret = secret_manager.LookupSecret("", "oracle");
      }

      if (!secret) {
          throw BinderException("No Oracle secret found. Create with: CREATE SECRET (TYPE oracle, ...)");
      }

      connection_string = BuildConnectionStringFromSecret(*secret);
  }
  ```

- [ ] Add parameter override support
  - [ ] Parse path for parameter overrides (e.g., `service=NEWDB`)
  - [ ] Merge overrides with secret parameters
  - [ ] Rebuild connection string with overrides

- [ ] Update error messages
  - [ ] Clear message when no secret found
  - [ ] Suggest CREATE SECRET syntax
  - [ ] Include secret name in errors

- [ ] Add debug logging (optional)
  - [ ] Log secret retrieval (name or default)
  - [ ] Log built connection string (redact password)

**Acceptance**:
- [ ] ATTACH '' AS db (TYPE oracle) works with default secret
- [ ] ATTACH '' AS db (TYPE oracle, SECRET name) works with named secret
- [ ] ATTACH 'user/pass@...' AS db (TYPE oracle) unchanged (existing behavior)
- [ ] Error messages helpful and accurate
- [ ] Backward compatibility maintained

---

### 3.4 Parameter Override Logic

**File**: `src/oracle_secret.cpp`

**Tasks**:

- [ ] Implement `ApplyOverrides(const string &base_conn_str, const string &overrides)`
  - [ ] Parse overrides string (format: `key=value` or `key=value,key2=value2`)
  - [ ] Supported keys: host, port, service, database, user, password
  - [ ] Parse base connection string back to components
  - [ ] Merge override values
  - [ ] Rebuild connection string

- [ ] Add test cases
  - [ ] Override service only: `service=TESTDB`
  - [ ] Override multiple: `host=newhost,port=1522`
  - [ ] Override user/password: `user/newpass@...`
  - [ ] Invalid override format (error)

**Edge Cases**:
- [ ] Conflicting overrides (e.g., full connection string + parameters)
- [ ] Partial connection strings
- [ ] URL-encoded values

**Acceptance**:
- [ ] Overrides work as expected
- [ ] Precedence clear and documented
- [ ] Error handling for invalid formats

---

### 3.5 Error Handling & Validation

**Tasks**:

- [ ] Improve error messages across all new code
  - [ ] "No Oracle secret found" → include CREATE SECRET example
  - [ ] "Missing required parameter" → specify which parameter
  - [ ] "Invalid port" → specify valid range
  - [ ] "Secret 'name' not found" → suggest listing secrets

- [ ] Add parameter validation
  - [ ] Port range: 1-65535
  - [ ] Required fields not empty
  - [ ] Service/database name valid characters

- [ ] Handle OCI errors gracefully
  - [ ] Connection failures
  - [ ] Invalid credentials
  - [ ] Network errors

**Acceptance**:
- [ ] All error messages clear and actionable
- [ ] No cryptic errors exposed to users
- [ ] DuckDB exceptions used (IOException, BinderException)

---

### Phase 3 Exit Criteria

- [ ] All code compiles without warnings
- [ ] make release succeeds
- [ ] make debug succeeds
- [ ] Basic smoke test passes (create secret + attach)
- [ ] No memory leaks (valgrind check)
- [ ] Code reviewed for style compliance

---

## Phase 4: Integration

**Duration**: 1 day
**Owner**: Expert Agent
**Status**: 🔲 Not Started

### 4.1 Build System Integration

**Tasks**:

- [ ] Update `CMakeLists.txt` (if new files added)
  - [ ] Add `src/oracle_secret.cpp` to sources
  - [ ] Ensure proper compilation order
  - [ ] Verify DuckDB SecretManager headers available

- [ ] Test build on all platforms
  - [ ] Linux (Ubuntu 22.04, 24.04)
  - [ ] macOS (if available)
  - [ ] Windows (if available)

- [ ] Verify no build warnings
  - [ ] GCC warnings
  - [ ] Clang warnings
  - [ ] MSVC warnings (if applicable)

**Acceptance**:
- [ ] Clean build on all platforms
- [ ] No new warnings introduced
- [ ] Extension loads successfully

---

### 4.2 Integration Smoke Tests

**Tasks**:

- [ ] Manual testing workflow
  ```sql
  -- Test 1: Basic secret creation
  CREATE SECRET (TYPE oracle, HOST 'localhost', PORT 1521,
                 SERVICE 'XEPDB1', USER 'test', PASSWORD 'test');

  -- Test 2: ATTACH with secret (will fail without Oracle, verify error clear)
  ATTACH '' AS ora (TYPE oracle);

  -- Test 3: Named secret
  CREATE SECRET ora_dev (TYPE oracle, HOST 'dev.local', ...);
  ATTACH '' AS ora (TYPE oracle, SECRET ora_dev);

  -- Test 4: Backward compatibility
  ATTACH 'user/pass@host:1521/service' AS ora (TYPE oracle);
  ```

- [ ] Verify error messages quality
- [ ] Check for memory leaks (valgrind)
- [ ] Profile ATTACH operation performance

**Acceptance**:
- [ ] All smoke tests execute (even if connection fails, errors are correct)
- [ ] No segfaults or crashes
- [ ] Error messages helpful
- [ ] Performance acceptable

---

### 4.3 Code Quality Checks

**Tasks**:

- [ ] Run clang-tidy
  ```bash
  make tidy-check
  ```

- [ ] Run clang-format
  ```bash
  clang-format -i src/oracle_secret.cpp src/include/oracle_secret.hpp
  ```

- [ ] Check Doxygen comments
  - [ ] All public functions documented
  - [ ] Parameter descriptions complete
  - [ ] Return value documented

- [ ] Memory leak check
  ```bash
  valgrind --leak-check=full build/debug/duckdb
  ```

**Acceptance**:
- [ ] clang-tidy passes
- [ ] Code formatted correctly
- [ ] All public APIs documented
- [ ] No memory leaks

---

## Phase 5: Testing (AUTO-INVOKED)

**Duration**: 1-2 days
**Owner**: Testing Agent
**Status**: 🔲 Not Started

### 5.1 SQL Test Suite

**Tasks**:

#### Test File 1: `test/sql/test_oracle_secret_basic.test`
- [ ] Test basic secret creation
  ```sql
  CREATE SECRET (TYPE oracle, HOST 'localhost', PORT 1521,
                 SERVICE 'XEPDB1', USER 'test', PASSWORD 'test');
  ```
- [ ] Test minimal secret (defaults)
  ```sql
  CREATE SECRET (TYPE oracle, SERVICE 'TESTDB', USER 'user', PASSWORD 'pass');
  ```
- [ ] Test DATABASE alias
  ```sql
  CREATE SECRET (TYPE oracle, DATABASE 'TESTDB', USER 'user', PASSWORD 'pass');
  ```
- [ ] Test ATTACH with default secret (expect connection error, verify error message)
- [ ] Test secret parameter extraction

#### Test File 2: `test/sql/test_oracle_secret_named.test`
- [ ] Create multiple named secrets
  ```sql
  CREATE SECRET ora_dev (TYPE oracle, ...);
  CREATE SECRET ora_prod (TYPE oracle, ...);
  ```
- [ ] ATTACH with named secret
  ```sql
  ATTACH '' AS ora_dev (TYPE oracle, SECRET ora_dev);
  ```
- [ ] Verify correct secret selected
- [ ] Test secret not found error

#### Test File 3: `test/sql/test_oracle_secret_override.test`
- [ ] Create secret with base parameters
- [ ] ATTACH with service override
  ```sql
  ATTACH 'service=NEWDB' AS ora (TYPE oracle);
  ```
- [ ] ATTACH with multiple overrides
- [ ] Verify override precedence

#### Test File 4: `test/sql/test_oracle_secret_errors.test`
- [ ] Missing required parameter (USER)
  ```sql
  CREATE SECRET (TYPE oracle, HOST 'localhost', SERVICE 'DB', PASSWORD 'pass');
  ---- Error: Missing required parameter USER
  ```
- [ ] Missing required parameter (SERVICE/DATABASE)
  ```sql
  CREATE SECRET (TYPE oracle, HOST 'localhost', USER 'user', PASSWORD 'pass');
  ---- Error: Missing required parameter SERVICE or DATABASE
  ```
- [ ] Invalid port number
  ```sql
  CREATE SECRET (TYPE oracle, PORT 99999, ...);
  ---- Error: Invalid port
  ```
- [ ] ATTACH with no secret found
  ```sql
  ATTACH '' AS ora (TYPE oracle);
  ---- Error: No Oracle secret found
  ```

#### Test File 5: `test/sql/test_oracle_secret_backward_compat.test`
- [ ] All existing ATTACH patterns work unchanged
  ```sql
  ATTACH 'user/pass@host:1521/service' AS ora (TYPE oracle);
  ```
- [ ] oracle_attach_wallet() still works
  ```sql
  SELECT oracle_attach_wallet('/path/to/wallet');
  ATTACH 'user@service' AS ora (TYPE oracle);
  ```
- [ ] Verify no behavior changes

**Acceptance**:
- [ ] All SQL tests pass
- [ ] Edge cases covered
- [ ] Error messages validated
- [ ] Backward compatibility verified

---

### 5.2 Integration Tests

**File**: `scripts/test_integration_secrets.sh` (new)

**Tasks**:

- [ ] Test secret + real Oracle connection
  ```bash
  # Start Oracle container
  docker run -d --name oracle-test gvenzl/oracle-free:23-slim

  # Create secret
  CREATE SECRET (TYPE oracle, HOST 'localhost', PORT 1521,
                 SERVICE 'FREEPDB1', USER 'system', PASSWORD 'oracle');

  # ATTACH and query
  ATTACH '' AS ora (TYPE oracle);
  SELECT COUNT(*) FROM ora.sys.dba_tables;
  ```

- [ ] Test named secrets with different credentials
- [ ] Test secret + wallet interaction (if applicable)
- [ ] Test persistent secrets across sessions

**Acceptance**:
- [ ] Integration tests pass with real Oracle
- [ ] Secrets work end-to-end
- [ ] No connection issues

---

### 5.3 Edge Case Tests

**Tasks**:

- [ ] Empty password (OS authentication)
  ```sql
  CREATE SECRET (TYPE oracle, SERVICE 'DB', USER '/');
  ```

- [ ] Special characters in password
  ```sql
  CREATE SECRET (TYPE oracle, ..., PASSWORD 'p@ss!w0rd#123');
  ```

- [ ] Very long parameters (buffer overflow check)

- [ ] Concurrent secret creation/deletion

- [ ] Secret name collisions

**Acceptance**:
- [ ] All edge cases handled gracefully
- [ ] No crashes or undefined behavior
- [ ] Error messages helpful

---

### 5.4 Performance Tests

**Tasks**:

- [ ] Benchmark ATTACH operation
  - [ ] Without secret (baseline)
  - [ ] With secret (measure overhead)
  - [ ] Target: < 1ms overhead

- [ ] Memory usage check
  - [ ] Measure memory per secret
  - [ ] Target: < 10KB per secret

- [ ] Query performance
  - [ ] Ensure no degradation in query execution
  - [ ] Secret retrieval not in hot path

**Acceptance**:
- [ ] Performance targets met
- [ ] No regressions
- [ ] Benchmarks documented

---

## Phase 6: Documentation (AUTO-INVOKED)

**Duration**: 1 day
**Owner**: Docs & Vision Agent
**Status**: 🔲 Not Started

### 6.1 README.md Updates

**Tasks**:

- [ ] Add secret management section
  ```markdown
  ## Authentication Methods

  ### 1. Connection String (Simple)
  ```sql
  ATTACH 'user/password@host:port/service' AS ora (TYPE oracle);
  ```

  ### 2. Secret Manager (Recommended)
  ```sql
  CREATE SECRET (TYPE oracle, HOST 'localhost', PORT 1521,
                 SERVICE 'XEPDB1', USER 'scott', PASSWORD 'tiger');
  ATTACH '' AS ora (TYPE oracle);
  ```

  ### 3. Oracle Wallet (Production)
  ```sql
  SELECT oracle_attach_wallet('/path/to/wallet');
  ATTACH 'user@service' AS ora (TYPE oracle);
  ```
  ```

- [ ] Add quick start examples with secrets
- [ ] Update existing examples to show secret alternative
- [ ] Add comparison table (connection string vs wallet vs secret)

**Acceptance**:
- [ ] README.md includes comprehensive secret examples
- [ ] All authentication methods documented
- [ ] Quick start updated

---

### 6.2 New Guide: Secret Management

**File**: `specs/guides/secret-management.md`

**Sections**:

- [ ] Overview of secret management
- [ ] When to use each authentication method
- [ ] Secret parameter reference
  - [ ] All parameters documented
  - [ ] Examples for each parameter
  - [ ] Default values listed

- [ ] Advanced usage
  - [ ] Named secrets
  - [ ] Persistent secrets
  - [ ] Parameter overrides
  - [ ] Multiple environments

- [ ] Best practices
  - [ ] Development: temporary secrets
  - [ ] CI/CD: environment variables + secrets
  - [ ] Production: Oracle Wallet preferred
  - [ ] Security considerations

- [ ] Troubleshooting
  - [ ] Common errors
  - [ ] Debug procedures
  - [ ] FAQ

**Acceptance**:
- [ ] Guide complete and comprehensive
- [ ] All use cases covered
- [ ] Examples tested and verified

---

### 6.3 Migration Guide

**File**: `specs/guides/migration-to-secrets.md`

**Sections**:

- [ ] Introduction
  - [ ] Why migrate to secrets?
  - [ ] Backward compatibility guarantee

- [ ] Migration strategies
  - [ ] Strategy 1: No changes required (connection strings continue working)
  - [ ] Strategy 2: Gradual migration (add secrets, keep old code as fallback)
  - [ ] Strategy 3: Full migration (replace all connection strings)

- [ ] Step-by-step migration examples
  ```sql
  -- BEFORE
  ATTACH 'user/pass@host:port/service' AS ora (TYPE oracle);

  -- AFTER
  CREATE SECRET (TYPE oracle, HOST 'host', PORT port,
                 SERVICE 'service', USER 'user', PASSWORD 'pass');
  ATTACH '' AS ora (TYPE oracle);
  ```

- [ ] Environment-specific patterns
  - [ ] Development
  - [ ] Staging
  - [ ] Production
  - [ ] CI/CD pipelines

- [ ] Rollback procedure (in case of issues)

**Acceptance**:
- [ ] Migration guide clear and complete
- [ ] Examples cover common scenarios
- [ ] Rollback procedure documented

---

### 6.4 Security Best Practices Guide

**File**: `specs/guides/security-best-practices.md`

**Sections**:

- [ ] Secret storage considerations
  - [ ] DuckDB stores persistent secrets unencrypted (documented behavior)
  - [ ] Use temporary secrets for sensitive data
  - [ ] Oracle Wallet provides encrypted storage

- [ ] Recommended patterns by environment
  | Environment | Recommended Method | Rationale |
  |-------------|-------------------|-----------|
  | Development | Temporary secrets | Easy to use, no persistence needed |
  | CI/CD | Secrets + env vars | Scriptable, no plain text in code |
  | Production | Oracle Wallet | Encrypted, enterprise-grade |

- [ ] Credential rotation
  - [ ] How to update secrets
  - [ ] Impact on active connections
  - [ ] Testing after rotation

- [ ] Audit and compliance
  - [ ] DuckDB logs secret usage
  - [ ] Monitoring secret access
  - [ ] Compliance considerations

- [ ] Common security mistakes
  - [ ] Persistent secrets in development
  - [ ] Hardcoded passwords in SQL files
  - [ ] Credentials in version control

**Acceptance**:
- [ ] Security guide complete
- [ ] Best practices clearly stated
- [ ] Risks and mitigations documented

---

### 6.5 Code Documentation

**Tasks**:

- [ ] Add Doxygen comments to all new files
  - [ ] `src/oracle_secret.cpp`
  - [ ] `src/include/oracle_secret.hpp`

- [ ] Document public functions
  ```cpp
  //! Builds an Oracle connection string from secret parameters
  //! \param secret KeyValueSecret containing Oracle connection parameters
  //! \return Connection string in EZConnect format (user/pass@host:port/service)
  //! \throws BinderException if required parameters missing
  string BuildConnectionStringFromSecret(const KeyValueSecret &secret);
  ```

- [ ] Add inline comments for complex logic
  - [ ] Decision trees
  - [ ] Parameter parsing
  - [ ] Override merging

- [ ] Document struct members
  ```cpp
  struct OracleSecretParameters {
      string host;        //! Oracle server hostname (default: localhost)
      idx_t port = 1521;  //! Oracle listener port (default: 1521)
      string service;     //! Oracle service name (required)
      string user;        //! Oracle username (required)
      string password;    //! Oracle password (required)
  };
  ```

**Acceptance**:
- [ ] All public APIs documented
- [ ] Inline comments explain complex logic
- [ ] Doxygen generates clean documentation

---

### 6.6 Update Architecture Guide

**File**: `specs/guides/architecture.md`

**Tasks**:

- [ ] Add section on secret management
- [ ] Update authentication flow diagram
- [ ] Document OracleAttach decision tree
- [ ] Add secret-related components to architecture overview

**Acceptance**:
- [ ] Architecture guide reflects new components
- [ ] Diagrams updated
- [ ] Flow documented

---

## Phase 7: Quality Gate & Archive (AUTO-INVOKED)

**Duration**: 0.5 day
**Owner**: Docs & Vision Agent
**Status**: 🔲 Not Started

### 7.1 Quality Gate Checklist

**Code Quality**:
- [ ] All tests passing (SQL + integration)
- [ ] Code coverage ≥ 90% for new code
- [ ] No compiler warnings
- [ ] clang-tidy checks pass
- [ ] clang-format applied
- [ ] No memory leaks (valgrind)

**Documentation**:
- [ ] README.md updated with secret examples
- [ ] Secret management guide complete
- [ ] Migration guide complete
- [ ] Security best practices guide complete
- [ ] All code documented (Doxygen)
- [ ] Architecture guide updated

**Functionality**:
- [ ] CREATE SECRET (TYPE oracle) works
- [ ] ATTACH with default secret works
- [ ] ATTACH with named secret works
- [ ] Parameter overrides work
- [ ] Backward compatibility verified (all existing tests pass)
- [ ] Error messages clear and helpful

**Performance**:
- [ ] ATTACH overhead < 1ms
- [ ] No query performance degradation
- [ ] Memory usage acceptable

**Security**:
- [ ] Security review complete
- [ ] Best practices documented
- [ ] No credential leaks in logs

**Integration**:
- [ ] Works with real Oracle database
- [ ] Compatible with all Oracle versions (19c, 21c, 23c)
- [ ] Cross-platform (Linux, macOS, Windows if applicable)

---

### 7.2 Final Review

**Tasks**:

- [ ] Expert agent code review
- [ ] Testing agent test review
- [ ] Docs agent documentation review
- [ ] PRD agent final acceptance

**Review Criteria**:
- [ ] All acceptance criteria met (from PRD)
- [ ] All tasks completed
- [ ] No critical issues outstanding
- [ ] Release notes drafted

---

### 7.3 Knowledge Capture

**Tasks**:

- [ ] Update `specs/guides/patterns/secret-management-pattern.md`
  - [ ] Document reusable patterns
  - [ ] Code examples
  - [ ] Best practices

- [ ] Update `CLAUDE.md` if workflow improved
  - [ ] New agent coordination patterns
  - [ ] Research process improvements

- [ ] Create `specs/guides/secret-manager-integration.md`
  - [ ] Technical deep-dive
  - [ ] API reference
  - [ ] Future enhancement ideas

**Acceptance**:
- [ ] Knowledge captured for future features
- [ ] Patterns documented
- [ ] Process improvements recorded

---

### 7.4 Archive Workspace

**Tasks**:

- [ ] Move workspace to `specs/archive/`
  ```bash
  mv specs/active/oracle-secret-management/ \
     specs/archive/oracle-secret-management-$(date +%Y%m%d)/
  ```

- [ ] Create archive summary
  - [ ] Implementation summary
  - [ ] Lessons learned
  - [ ] Metrics achieved
  - [ ] Future work ideas

- [ ] Update project guides with new patterns
  - [ ] Add secret examples to guides
  - [ ] Update quick reference

**Acceptance**:
- [ ] Workspace archived
- [ ] Project guides updated
- [ ] Ready for next feature

---

### 7.5 Release Notes

**Tasks**:

- [ ] Draft release notes for v1.1.0
  ```markdown
  ## Oracle Extension v1.1.0

  ### Features
  - ✨ **Secret Manager Integration**: Create and manage Oracle credentials using DuckDB's `CREATE SECRET` syntax
  - 🔒 **Improved Security**: Reduce plain-text credentials in scripts
  - 🔄 **Backward Compatible**: All existing authentication methods continue to work

  ### Examples
  ```sql
  CREATE SECRET (TYPE oracle, HOST 'localhost', PORT 1521,
                 SERVICE 'XEPDB1', USER 'scott', PASSWORD 'tiger');
  ATTACH '' AS ora (TYPE oracle);
  ```

  ### Migration Guide
  See `specs/guides/migration-to-secrets.md` for upgrade instructions.
  ```

- [ ] Update CHANGELOG.md
- [ ] Tag release (if applicable)

**Acceptance**:
- [ ] Release notes complete
- [ ] CHANGELOG updated
- [ ] Ready for release

---

## Summary

### Total Estimated Effort: 5-8 days

| Phase | Duration | Owner | Status |
|-------|----------|-------|--------|
| Phase 1: Planning & Research | 1 day | PRD Agent | ✅ Complete |
| Phase 2: Expert Research | 1-2 days | Expert Agent | 🔲 Not Started |
| Phase 3: Core Implementation | 2-3 days | Expert Agent | 🔲 Not Started |
| Phase 4: Integration | 1 day | Expert Agent | 🔲 Not Started |
| Phase 5: Testing | 1-2 days | Testing Agent | 🔲 Not Started |
| Phase 6: Documentation | 1 day | Docs & Vision Agent | 🔲 Not Started |
| Phase 7: Quality Gate & Archive | 0.5 day | Docs & Vision Agent | 🔲 Not Started |

---

## Next Steps

1. **Expert Agent**: Begin Phase 2 research
   - Study DuckDB SecretManager C++ API
   - Analyze PostgreSQL extension implementation
   - Create prototypes and design documents

2. **PRD Agent**: Monitor progress
   - Review research findings
   - Update PRD if needed
   - Coordinate agent handoffs

3. **Testing Agent**: Prepare test environment
   - Set up Oracle container for integration tests
   - Plan test scenarios
   - Ready for Phase 5 invocation

4. **Docs & Vision Agent**: Prepare documentation templates
   - Draft guide outlines
   - Prepare for Phase 6 invocation

---

**Last Updated**: 2025-11-22
**Status**: Phase 1 Complete | Ready for Phase 2
**Next Owner**: Expert Agent
