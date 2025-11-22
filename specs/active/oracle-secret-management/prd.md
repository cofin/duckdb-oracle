# Product Requirements Document: Oracle Secret Management Integration

**Feature**: DuckDB Secret Manager Integration for Oracle Extension
**Status**: Phase 1 - Requirements Definition
**Priority**: High
**Target Release**: v1.1.0
**Created**: 2025-11-22
**Last Updated**: 2025-11-22

---

## Executive Summary

Integrate the Oracle extension with DuckDB's Secret Manager to provide secure, centralized credential management that aligns with PostgreSQL and MySQL extension patterns. This enhancement enables enterprise users to manage Oracle credentials using DuckDB's standard `CREATE SECRET` syntax while maintaining full backward compatibility with existing connection string and wallet-based authentication.

**Decision**: **GO - Recommended for Implementation**

---

## Table of Contents

1. [Problem Statement](#problem-statement)
2. [Goals and Non-Goals](#goals-and-non-goals)
3. [Target Users](#target-users)
4. [Current State Analysis](#current-state-analysis)
5. [Proposed Solution](#proposed-solution)
6. [Technical Architecture](#technical-architecture)
7. [User Stories](#user-stories)
8. [Acceptance Criteria](#acceptance-criteria)
9. [Implementation Phases](#implementation-phases)
10. [Dependencies](#dependencies)
11. [Risks and Mitigations](#risks-and-mitigations)
12. [Research Questions](#research-questions)
13. [Success Metrics](#success-metrics)
14. [References](#references)

---

## Problem Statement

### Pain Points

1. **Inconsistent Credential Management**: Oracle extension uses connection strings, while PostgreSQL/MySQL extensions support `CREATE SECRET`
2. **Security Concerns**: Credentials appear in plain text in `ATTACH` statements and query logs
3. **Limited Reusability**: Cannot reuse credentials across multiple ATTACH operations without re-entering
4. **Enterprise Adoption Barrier**: No centralized secret management for DevOps/GitOps workflows
5. **Documentation Fragmentation**: Wallet setup requires separate `oracle_attach_wallet()` function call

### Impact

- **Developer Experience**: Inconsistent API across DuckDB database extensions
- **Security Posture**: Plain-text credentials in scripts and logs
- **Enterprise Readiness**: Limited credential rotation and management capabilities
- **Adoption**: Users migrating from PostgreSQL expect similar patterns

---

## Goals and Non-Goals

### Primary Goals

1. Enable `CREATE SECRET` with `TYPE oracle` matching PostgreSQL/MySQL patterns
2. Support secret-based `ATTACH` with empty connection string
3. Maintain 100% backward compatibility (no breaking changes)
4. Provide seamless integration with existing Oracle Wallet support

### Secondary Goals

1. Support both named and default secrets
2. Enable parameter overrides in ATTACH with secrets
3. Provide clear migration documentation
4. Implement proper error handling and validation

### Non-Goals

1. Replace Oracle Wallet (remains the preferred production method)
2. Implement credential rotation or vault integration (future enhancement)
3. Support all Oracle authentication methods in Phase 1 (external auth, Kerberos)
4. Change existing connection string parsing behavior

---

## Target Users

### Primary Users

1. **Data Engineers**: Building ETL pipelines with multiple Oracle connections
2. **Data Analysts**: Connecting to Oracle from DuckDB for ad-hoc analysis
3. **DevOps Engineers**: Managing Oracle credentials in CI/CD environments
4. **Enterprise DBAs**: Standardizing credential management across teams

### User Personas

#### Persona 1: Sarah - Data Engineer

- Uses DuckDB to federate queries across PostgreSQL, MySQL, and Oracle
- Expects consistent credential management across all database types
- Needs to manage 5+ Oracle database connections in production scripts
- Values reusability and scriptability

#### Persona 2: Mike - Enterprise DBA

- Manages Oracle credentials for 20+ development teams
- Requires centralized credential distribution
- Uses DuckDB for Oracle → cloud data warehouse ETL
- Needs audit trails and security compliance

#### Persona 3: Lisa - Data Analyst

- Connects to Oracle for ad-hoc reporting
- Familiar with PostgreSQL extension patterns in DuckDB
- Prefers simple, consistent SQL syntax
- Needs credentials stored securely for repeated queries

---

## Current State Analysis

### Authentication Methods Supported

#### Method 1: Connection String (Current)

```sql
ATTACH 'user/password@host:port/service' AS ora (TYPE oracle);
```

**Pros:**

- Simple and direct
- Standard Oracle EZConnect format
- No additional setup

**Cons:**

- Plain-text passwords in SQL
- No credential reuse
- Credentials in query logs

#### Method 2: Oracle Wallet (Current)

```sql
SELECT oracle_attach_wallet('/path/to/wallet');
ATTACH 'user@service' AS ora (TYPE oracle);
```

**Pros:**

- Encrypted credential storage (industry standard)
- Oracle-native authentication
- Supports certificate-based auth

**Cons:**

- Requires separate function call
- Wallet setup complexity
- Not integrated with DuckDB patterns

### Technical Implementation

**Current Flow:**

```
OracleAttach() → Parse connection_string → OracleConnection::Connect()
                 (user/pass@host:port/service)
```

**Connection Parsing** (from `oracle_connection.cpp:53-74`):

- Expects format: `user/password@connect_identifier`
- Uses `OCILogon()` with parsed credentials
- No secret manager integration

**Settings Application** (from `oracle_storage_extension.cpp:33`):

- `ApplyOptions()` processes ATTACH parameters
- Currently handles: `enable_pushdown`, `prefetch_rows`, `array_size`, etc.
- No secret retrieval logic

---

## Proposed Solution

### Overview

Implement a **Hybrid Approach** that adds Secret Manager support while preserving all existing functionality.

### Solution Architecture

```sql
-- EXISTING: Connection string (continues to work)
ATTACH 'user/pass@host:port/service' AS ora (TYPE oracle);

-- EXISTING: Wallet (continues to work)
SELECT oracle_attach_wallet('/wallet/path');
ATTACH 'user@service' AS ora (TYPE oracle);

-- NEW: Default secret
CREATE SECRET (
    TYPE oracle,
    HOST 'localhost',
    PORT 1521,
    SERVICE 'XEPDB1',
    USER 'scott',
    PASSWORD 'tiger'
);
ATTACH '' AS ora (TYPE oracle);

-- NEW: Named secret
CREATE SECRET ora_prod (
    TYPE oracle,
    HOST 'prod.example.com',
    PORT 1521,
    SERVICE 'PRODDB',
    USER 'app_user',
    PASSWORD 'secure_pass'
);
ATTACH '' AS ora (TYPE oracle, SECRET ora_prod);

-- NEW: Secret with parameter override
ATTACH 'service=TESTDB' AS ora (TYPE oracle, SECRET ora_prod);
```

### Secret Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `TYPE` | string | Yes | - | Must be "oracle" |
| `HOST` | string | Yes* | localhost | Oracle server hostname |
| `PORT` | integer | No | 1521 | Oracle listener port |
| `SERVICE` | string | Yes* | - | Oracle service name |
| `DATABASE` | string | No | - | Alias for SERVICE |
| `USER` | string | Yes | - | Oracle username |
| `PASSWORD` | string | Yes | - | Oracle password |
| `WALLET_PATH` | string | No | - | Path to Oracle Wallet (Phase 2) |

*Either `SERVICE` or `DATABASE` required; `HOST` required unless using `WALLET_PATH`

### Decision Logic

```
┌─────────────────────────────────────────┐
│  ATTACH 'path' AS db (TYPE oracle, ...) │
└──────────────────┬──────────────────────┘
                   │
                   ▼
          ┌────────────────────┐
          │ Is path non-empty? │
          └────────┬───────────┘
                   │
        ┌──────────┴──────────┐
        │ YES                 │ NO
        ▼                     ▼
┌───────────────┐    ┌───────────────────┐
│ Use path as   │    │ Check SECRET opt  │
│ connection    │    └────────┬──────────┘
│ string        │             │
│ (existing)    │    ┌────────┴─────────┐
└───────────────┘    │ YES              │ NO
                     ▼                  ▼
            ┌────────────────┐  ┌──────────────┐
            │ Get named      │  │ Get default  │
            │ secret         │  │ oracle secret│
            └────────┬───────┘  └──────┬───────┘
                     │                 │
                     └────────┬────────┘
                              ▼
                     ┌─────────────────┐
                     │ Build connection│
                     │ string from     │
                     │ secret params   │
                     └────────┬────────┘
                              ▼
                     ┌─────────────────┐
                     │ Apply path      │
                     │ overrides       │
                     │ (if any)        │
                     └────────┬────────┘
                              ▼
                     ┌─────────────────┐
                     │ Connect via OCI │
                     └─────────────────┘
```

---

## Technical Architecture

### C++ Implementation Components

#### 1. Secret Type Registration

**File**: `src/oracle_extension.cpp`
**Function**: `LoadInternal(DatabaseInstance &instance)`

```cpp
// Register oracle secret type
auto secret_type = make_uniq<SecretType>();
secret_type->name = "oracle";
secret_type->deserializer = DeserializeOracleSecret;
secret_type->default_provider = "config";

SecretType::RegisterSecretType(*secret_type);
```

#### 2. Secret Parameter Definition

**New File**: `src/oracle_secret.cpp`

```cpp
struct OracleSecretParameters {
    string host;
    idx_t port = 1521;
    string service;  // or database
    string user;
    string password;
    string wallet_path;  // Phase 2
};

static OracleSecretParameters ParseOracleSecret(const KeyValueSecret &secret) {
    OracleSecretParameters params;
    params.host = secret.TryGetValue("host", "localhost");
    params.port = secret.TryGetValue("port", 1521);
    // ... parse all parameters
    return params;
}
```

#### 3. OracleAttach Integration

**File**: `src/storage/oracle_storage_extension.cpp`
**Function**: `OracleAttach()`

```cpp
static unique_ptr<Catalog> OracleAttach(...) {
    string connection_string;

    if (!info.path.empty() && info.path != "") {
        // Existing behavior: use provided connection string
        connection_string = info.path;
    } else {
        // NEW: Retrieve from secret manager
        auto &secret_manager = SecretManager::Get(context);

        // Check for named secret in options
        shared_ptr<KeyValueSecret> secret;
        if (options.options.count("secret")) {
            auto secret_name = options.options["secret"].ToString();
            secret = secret_manager.GetSecretByName(secret_name, "oracle");
        } else {
            // Use default secret
            secret = secret_manager.LookupSecret("", "oracle");
        }

        if (!secret) {
            throw BinderException(
                "No Oracle secret found. Create one with: "
                "CREATE SECRET (TYPE oracle, HOST '...', ...)");
        }

        connection_string = BuildConnectionStringFromSecret(*secret);

        // Apply path-based overrides if provided
        if (!info.path.empty()) {
            connection_string = ApplyOverrides(connection_string, info.path);
        }
    }

    // Continue with existing logic
    auto state = make_shared_ptr<OracleCatalogState>(connection_string);
    OracleCatalogState::Register(state);
    state->ApplyOptions(options.options);
    return CreateOracleCatalog(db, state);
}
```

#### 4. Connection String Builder

**New File**: `src/oracle_secret.cpp`

```cpp
static string BuildConnectionStringFromSecret(const KeyValueSecret &secret) {
    auto params = ParseOracleSecret(secret);

    // Validate required parameters
    if (params.user.empty()) {
        throw BinderException("Oracle secret missing required parameter: USER");
    }
    if (params.service.empty()) {
        throw BinderException("Oracle secret missing required parameter: SERVICE");
    }

    // Build EZConnect format: user/password@host:port/service
    string conn_str = params.user;
    if (!params.password.empty()) {
        conn_str += "/" + params.password;
    }
    conn_str += "@" + params.host + ":" + to_string(params.port) + "/" + params.service;

    return conn_str;
}
```

### File Structure Changes

```
src/
├── oracle_extension.cpp           # Add secret type registration
├── oracle_secret.cpp              # NEW - Secret parsing and validation
├── include/
│   └── oracle_secret.hpp          # NEW - Secret parameter structures
└── storage/
    └── oracle_storage_extension.cpp  # Modify OracleAttach for secret retrieval
```

---

## User Stories

### Story 1: Data Engineer - Multiple Oracle Connections

**As** a data engineer,
**I want** to manage multiple Oracle database credentials using CREATE SECRET,
**So that** I can easily switch between development, staging, and production environments.

**Acceptance Criteria:**

- Can create multiple named secrets for different Oracle databases
- Can attach to specific databases using SECRET parameter
- No need to re-enter credentials for each ATTACH

### Story 2: Security-Conscious Developer - Hide Credentials

**As** a developer concerned about security,
**I want** my Oracle passwords stored in DuckDB's secret manager,
**So that** they don't appear in plain text in my SQL scripts or query logs.

**Acceptance Criteria:**

- CREATE SECRET accepts Oracle credentials
- ATTACH works with empty path when secret exists
- Credentials not visible in EXPLAIN output

### Story 3: Migration User - Backward Compatibility

**As** an existing Oracle extension user,
**I want** my current ATTACH statements to continue working,
**So that** I don't have to rewrite my existing scripts.

**Acceptance Criteria:**

- All existing ATTACH 'user/pass@host:port/service' statements work unchanged
- oracle_attach_wallet() function continues to work
- No breaking changes to current behavior

### Story 4: Enterprise Admin - Centralized Management

**As** an enterprise database administrator,
**I want** to create persistent Oracle secrets,
**So that** my team can reuse shared credentials without managing files.

**Acceptance Criteria:**

- CREATE PERSISTENT SECRET stores credentials across sessions
- Secrets can be listed and managed centrally
- Multiple users can reference same secret

### Story 5: Analyst - Consistent API Experience

**As** a data analyst familiar with PostgreSQL extension,
**I want** Oracle to support the same CREATE SECRET syntax,
**So that** I have a consistent experience across all database extensions.

**Acceptance Criteria:**

- CREATE SECRET (TYPE oracle, ...) matches PostgreSQL pattern
- ATTACH '' AS db (TYPE oracle) matches PostgreSQL behavior
- Documentation uses familiar examples

---

## Acceptance Criteria

### Functional Requirements

#### Must Have (Phase 1 MVP)

1. **Secret Creation**
   - [ ] CREATE SECRET with TYPE oracle registers successfully
   - [ ] Required parameters: HOST, SERVICE, USER, PASSWORD
   - [ ] Optional parameter: PORT (default 1521)
   - [ ] DATABASE accepted as alias for SERVICE
   - [ ] Validation errors for missing required parameters

2. **Secret-Based ATTACH**
   - [ ] ATTACH '' AS db (TYPE oracle) uses default secret
   - [ ] ATTACH '' AS db (TYPE oracle, SECRET name) uses named secret
   - [ ] Error message when no secret found
   - [ ] Connection establishes successfully with secret credentials

3. **Backward Compatibility**
   - [ ] All existing tests pass without modification
   - [ ] Connection string ATTACH works unchanged
   - [ ] oracle_attach_wallet() function works unchanged
   - [ ] No performance degradation for existing code paths

4. **Parameter Overrides**
   - [ ] ATTACH 'service=NEWDB' with SECRET overrides service name
   - [ ] ATTACH 'user/pass@host:port/service' ignores secrets (existing behavior)

5. **Error Handling**
   - [ ] Clear error when secret not found
   - [ ] Validation errors for invalid parameters
   - [ ] OCI connection errors propagate correctly

#### Should Have (Phase 2)

1. **Wallet Integration**
   - [ ] WALLET_PATH parameter in secret
   - [ ] Sets TNS_ADMIN when present
   - [ ] Works with wallet-based authentication

2. **Advanced Parameters**
   - [ ] PROVIDER support (credential_chain for OCI)
   - [ ] AUTHENTICATION_TYPE parameter
   - [ ] Multiple authentication methods

#### Nice to Have (Future)

1. **Secret Validation**
   - [ ] Test connection on CREATE SECRET
   - [ ] Warning for weak passwords
   - [ ] Parameter validation (port range, etc.)

2. **Cloud Integration**
   - [ ] Oracle Cloud credential_chain provider
   - [ ] Azure AD token authentication
   - [ ] IAM-based authentication

### Non-Functional Requirements

1. **Performance**
   - Secret retrieval adds < 1ms to ATTACH operation
   - No impact on query execution performance
   - Connection pooling unaffected

2. **Security**
   - Secrets follow DuckDB's standard storage (unencrypted binary)
   - Credentials not logged in query plans
   - Memory cleanup on connection close

3. **Compatibility**
   - Works with DuckDB 1.0+
   - Compatible with all Oracle Instant Client versions (19c, 21c, 23c)
   - Cross-platform (Linux, macOS, Windows)

4. **Maintainability**
   - Code follows existing DuckDB extension patterns
   - Uses DuckDB's smart pointers and string types
   - Proper error handling with DuckDB exceptions
   - Doxygen comments on all public APIs

---

## Implementation Phases

### Phase 1: Planning & Research ✅ (COMPLETE)

**Duration**: 1 day
**Owner**: PRD Agent

**Tasks:**

- [x] Research DuckDB Secret Manager API
- [x] Analyze PostgreSQL/MySQL extension patterns
- [x] Document current Oracle authentication flows
- [x] Create comprehensive PRD
- [x] Define technical architecture
- [x] Identify implementation approach

**Deliverables:**

- `specs/active/oracle-secret-management/prd.md`
- `specs/active/oracle-secret-management/research/`
- `specs/active/oracle-secret-management/tasks.md`

---

### Phase 2: Expert Research

**Duration**: 1-2 days
**Owner**: Expert Agent

**Research Tasks:**

1. Study DuckDB SecretManager C++ API in detail
   - Secret type registration patterns
   - KeyValueSecret deserialization
   - SecretManager::Get() and LookupSecret() APIs
2. Examine PostgreSQL extension implementation
   - Secret parameter definitions
   - ATTACH integration points
   - Error handling patterns
3. Prototype secret parameter parsing
   - Test parameter extraction
   - Validation logic
   - Error message format
4. Design connection string builder
   - EZConnect format generation
   - Parameter override merging
   - Edge case handling

**Deliverables:**

- Research document with code examples
- API reference for DuckDB SecretManager
- Prototype code snippets
- Implementation plan

---

### Phase 3: Core Implementation

**Duration**: 2-3 days
**Owner**: Expert Agent

**Implementation Tasks:**

#### 3.1 Secret Type Registration (0.5 day)

- Create `src/oracle_secret.cpp` and `src/include/oracle_secret.hpp`
- Define OracleSecretParameters struct
- Implement DeserializeOracleSecret function
- Register "oracle" secret type in LoadInternal()
- Add unit tests for secret parsing

#### 3.2 Connection String Builder (0.5 day)

- Implement BuildConnectionStringFromSecret()
- Handle parameter validation
- Support DATABASE/SERVICE aliases
- Add unit tests for various parameter combinations

#### 3.3 OracleAttach Integration (1 day)

- Modify OracleAttach() in oracle_storage_extension.cpp
- Add secret retrieval logic
- Implement decision tree (path vs secret)
- Handle SECRET option parameter
- Add parameter override support
- Maintain backward compatibility

#### 3.4 Parameter Override Logic (0.5 day)

- Implement ApplyOverrides() function
- Parse connection string fragments
- Merge with secret parameters
- Handle edge cases

#### 3.5 Error Handling (0.5 day)

- Add descriptive error messages
- Validate required parameters
- Handle missing secrets gracefully
- Improve OCI connection errors

**Code Quality Checks:**

- All functions documented with Doxygen comments
- Use DuckDB smart pointers and types
- Follow existing code style
- RAII for resource management
- Proper exception handling

---

### Phase 4: Integration

**Duration**: 1 day
**Owner**: Expert Agent

**Integration Tasks:**

1. Update CMakeLists.txt if new files added
2. Ensure oracle_secret.cpp compiled
3. Link with DuckDB SecretManager
4. Integration smoke tests
5. Memory leak checks (valgrind)
6. Build on all platforms (Linux, macOS, Windows)

**Verification:**

- make release succeeds
- make debug succeeds
- No compiler warnings
- Extension loads correctly

---

### Phase 5: Testing (AUTO-INVOKED)

**Duration**: 1-2 days
**Owner**: Testing Agent

**Test Creation:**

#### 5.1 SQL Tests (test/sql/)

- `test_oracle_secret_basic.test` - Basic secret creation and ATTACH
- `test_oracle_secret_named.test` - Named secrets
- `test_oracle_secret_override.test` - Parameter overrides
- `test_oracle_secret_errors.test` - Error conditions
- `test_oracle_secret_backward_compat.test` - Existing functionality

#### 5.2 Integration Tests

- Secret + real Oracle container connection
- Wallet + secret interaction
- Multiple secrets with different credentials
- Persistent secret across sessions

#### 5.3 Edge Cases

- Empty secret parameters
- Invalid port numbers
- Missing required parameters
- Conflicting parameter overrides
- Special characters in passwords

**Test Coverage Target**: 90%+ for new code

---

### Phase 6: Documentation (AUTO-INVOKED)

**Duration**: 1 day
**Owner**: Docs & Vision Agent

**Documentation Updates:**

#### 6.1 README.md

- Add CREATE SECRET examples
- Update quick start guide
- Show migration from connection strings
- Document all secret parameters

#### 6.2 New Guides

- `specs/guides/secret-management.md` - Comprehensive secret guide
- `specs/guides/migration-to-secrets.md` - Migration guide
- `specs/guides/security-best-practices.md` - Security recommendations

#### 6.3 Code Documentation

- Doxygen comments on all new functions
- Inline comments for complex logic
- Example usage in header files

#### 6.4 Comparison Table

| Feature | Connection String | Wallet | Secret Manager |
|---------|-------------------|--------|----------------|
| Security | Plain text | Encrypted | DuckDB managed |
| Reusability | No | Limited | Yes |
| Setup | None | External | CREATE SECRET |
| Persistence | No | Yes | Optional |
| Recommended For | Development | Production | CI/CD, Scripts |

---

### Phase 7: Quality Gate & Archive (AUTO-INVOKED)

**Duration**: 0.5 day
**Owner**: Docs & Vision Agent

**Quality Checks:**

- [ ] All tests passing
- [ ] Documentation complete
- [ ] Code review checklist satisfied
- [ ] Performance benchmarks acceptable
- [ ] Security review complete
- [ ] Backward compatibility verified

**Archive:**

- Move workspace to `specs/archive/oracle-secret-management/`
- Update project guides with new patterns
- Create release notes

---

## Dependencies

### Internal Dependencies

1. **DuckDB Core** (v1.0+)
   - SecretManager API
   - KeyValueSecret class
   - Secret type registration
   - Storage extension framework

2. **Oracle Extension Components**
   - OracleConnection class (existing)
   - OracleAttach function (modify)
   - OracleCatalogState (no changes)

### External Dependencies

1. **Oracle Instant Client** (19c/21c/23c)
   - OCI library (libclntsh.so)
   - No changes required
   - Existing integration works

2. **Build System**
   - CMake 3.10+
   - C++17 compiler
   - vcpkg (OpenSSL)

### Documentation Dependencies

1. DuckDB documentation style guide
2. Existing extension examples (PostgreSQL, MySQL)
3. Oracle authentication documentation

---

## Risks and Mitigations

### Technical Risks

#### Risk 1: Secret Manager API Changes

**Probability**: Low
**Impact**: High
**Mitigation**:

- Study stable API patterns from other extensions
- Follow PostgreSQL extension implementation closely
- Version-check DuckDB core dependencies
- Maintain fallback to connection string

#### Risk 2: Wallet + Secret Interaction Complexity

**Probability**: Medium
**Impact**: Medium
**Mitigation**:

- Phase 1: Basic secrets only (no wallet integration)
- Phase 2: Add WALLET_PATH parameter
- Clear precedence rules documented
- Extensive integration testing

#### Risk 3: Backward Compatibility Breakage

**Probability**: Low
**Impact**: Critical
**Mitigation**:

- Decision logic prioritizes existing path-based auth
- Comprehensive regression test suite
- Beta testing with existing users
- Clear migration guide

#### Risk 4: Performance Degradation

**Probability**: Low
**Impact**: Medium
**Mitigation**:

- Secret lookup cached by DuckDB
- Fast-path for non-secret code
- Benchmark ATTACH operation before/after
- Profile with valgrind/perf

### Adoption Risks

#### Risk 1: User Confusion (Multiple Auth Methods)

**Probability**: Medium
**Impact**: Medium
**Mitigation**:

- Clear documentation with decision tree
- Comparison table in README
- Recommended patterns for each use case
- Migration examples

#### Risk 2: Security Misconfiguration

**Probability**: Medium
**Impact**: High
**Mitigation**:

- Security best practices guide
- Warning about persistent secret storage
- Recommend wallets for production
- Document DuckDB secret storage format

### Operational Risks

#### Risk 1: Increased Support Burden

**Probability**: Medium
**Impact**: Low
**Mitigation**:

- Comprehensive error messages
- Troubleshooting guide
- Common issues FAQ
- Example configurations

---

## Research Questions

### For Expert Agent (Phase 2)

1. **Secret Manager C++ API:**
   - What is the exact signature for `SecretManager::Get()`?
   - How to retrieve secrets by name vs. default lookup?
   - What is the structure of `KeyValueSecret`?
   - How to deserialize secret parameters?

2. **Secret Type Registration:**
   - Where in LoadInternal() to register secret type?
   - What is the SecretType struct definition?
   - Required vs. optional parameters specification?
   - How to define secret_provider types?

3. **PostgreSQL Extension Implementation:**
   - How does postgres extension parse secret parameters?
   - What error messages are used for missing secrets?
   - How are parameter overrides handled?
   - What validation is performed on CREATE SECRET?

4. **Integration Points:**
   - Does SecretManager need explicit initialization?
   - How to check if secrets feature is available?
   - Thread safety considerations for secret retrieval?
   - Memory management for secret data?

5. **Testing:**
   - How to create secrets in .test files?
   - Can integration tests access SecretManager?
   - How to test persistent secrets?
   - Secret cleanup between tests?

6. **Wallet Integration:**
   - How to combine secret parameters with TNS_ADMIN?
   - Should WALLET_PATH set environment variable?
   - Precedence rules for wallet vs. password in secret?
   - Security implications of storing wallet paths?

7. **Edge Cases:**
   - Behavior when both path and SECRET provided?
   - Empty string vs. NULL for optional parameters?
   - Case sensitivity for parameter names?
   - Special character escaping in passwords?

---

## Success Metrics

### Development Metrics

1. **Code Quality**
   - Test coverage: ≥ 90% for new code
   - Zero compiler warnings
   - clang-tidy checks pass
   - Zero memory leaks (valgrind)

2. **Performance**
   - ATTACH operation: < 1ms overhead for secret retrieval
   - Query execution: 0% degradation
   - Memory usage: < 10KB per secret

3. **Compatibility**
   - 100% backward compatibility (all existing tests pass)
   - Works on Linux, macOS, Windows
   - Compatible with Oracle 19c, 21c, 23c

### User Metrics

1. **Adoption**
   - 30% of users using secrets within 6 months
   - Positive feedback on API consistency
   - Reduced security-related issues

2. **Documentation**
   - README includes secret examples
   - Migration guide complete
   - Zero "how to use secrets?" issues filed

3. **Support**
   - < 5 secret-related issues per month
   - Average resolution time < 24 hours
   - Self-service success rate > 80%

### Business Metrics

1. **Enterprise Adoption**
   - Enables 5+ enterprise deployments
   - Reduces credential management overhead
   - Improves security compliance posture

2. **Community**
   - Positive sentiment on feature
   - Contributions to secret documentation
   - Secret patterns shared in examples

---

## References

### DuckDB Documentation

- [Secret Manager Overview](https://duckdb.org/docs/1.1/configuration/secrets_manager)
- [CREATE SECRET Statement](https://duckdb.org/docs/0.10/sql/statements/create_secret)
- [PostgreSQL Extension Secrets](https://duckdb.org/docs/stable/core_extensions/postgres)
- [MySQL Extension Secrets](https://duckdb.org/docs/stable/core_extensions/mysql)

### Oracle Documentation

- Oracle Call Interface (OCI) Authentication
- Oracle Wallet Configuration
- Oracle EZConnect Connection String Format

### Internal Documentation

- `/home/cody/code/other/duckdb-oracle/CLAUDE.md` - Agent system guide
- `/home/cody/code/other/duckdb-oracle/specs/guides/architecture.md`
- `/home/cody/code/other/duckdb-oracle/specs/guides/code-style.md`

### Research Documents

- `specs/active/oracle-secret-management/research/duckdb-secret-manager-analysis.md`

---

## Appendix

### Example Usage Scenarios

#### Scenario 1: Development Workflow

```sql
-- One-time setup
CREATE SECRET (
    TYPE oracle,
    HOST 'localhost',
    PORT 1521,
    SERVICE 'XEPDB1',
    USER 'dev_user',
    PASSWORD 'dev_pass'
);

-- Reuse in multiple sessions
ATTACH '' AS ora (TYPE oracle);
SELECT * FROM ora.hr.employees LIMIT 10;
```

#### Scenario 2: Multi-Environment Script

```sql
-- Define environment secrets
CREATE SECRET ora_dev (
    TYPE oracle, HOST 'dev-db.internal', SERVICE 'DEV',
    USER 'app', PASSWORD 'dev123'
);
CREATE SECRET ora_prod (
    TYPE oracle, HOST 'prod-db.internal', SERVICE 'PROD',
    USER 'app', PASSWORD 'prod$ecure'
);

-- Switch environments
ATTACH '' AS ora_dev (TYPE oracle, SECRET ora_dev);
ATTACH '' AS ora_prod (TYPE oracle, SECRET ora_prod);

-- Federate data
CREATE TABLE merged AS
SELECT *, 'dev' as env FROM ora_dev.app.users
UNION ALL
SELECT *, 'prod' as env FROM ora_prod.app.users;
```

#### Scenario 3: CI/CD Pipeline

```sql
-- In deployment script (credentials from vault)
CREATE PERSISTENT SECRET ci_oracle (
    TYPE oracle,
    HOST '${ORACLE_HOST}',
    PORT ${ORACLE_PORT},
    SERVICE '${ORACLE_SERVICE}',
    USER '${ORACLE_USER}',
    PASSWORD '${ORACLE_PASSWORD}'
);

-- In test queries
ATTACH '' AS test_db (TYPE oracle);
-- No credentials in query logs
```

#### Scenario 4: Migration from Connection String

```sql
-- BEFORE (credentials exposed)
ATTACH 'myuser/mypass@prod.example.com:1521/PRODDB' AS ora (TYPE oracle);

-- AFTER (credentials managed)
CREATE PERSISTENT SECRET ora_prod (
    TYPE oracle,
    HOST 'prod.example.com',
    PORT 1521,
    SERVICE 'PRODDB',
    USER 'myuser',
    PASSWORD 'mypass'
);
ATTACH '' AS ora (TYPE oracle, SECRET ora_prod);
```

---

## Change Log

| Date | Version | Changes | Author |
|------|---------|---------|--------|
| 2025-11-22 | 1.0 | Initial PRD creation | PRD Agent |

---

**Status**: Ready for Phase 2 (Expert Research)
**Next Action**: Expert agent to research DuckDB SecretManager C++ API
**Estimated Total Effort**: 5-8 days (design + implementation + testing + docs)
