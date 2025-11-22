# DuckDB Secret Manager Analysis

**Date**: 2025-11-22
**Purpose**: Research DuckDB's Secret Manager API and evaluate integration with Oracle extension

## Overview

DuckDB's Secret Manager provides a unified interface for managing credentials across extensions. This document analyzes how PostgreSQL and MySQL extensions implement secret support and evaluates applicability to the Oracle extension.

## Key Findings

### 1. Secret Manager API Patterns

From PostgreSQL and MySQL extension documentation:

```sql
-- Create temporary secret (default)
CREATE SECRET (
    TYPE postgres,
    HOST '127.0.0.1',
    PORT 5432,
    DATABASE postgres,
    USER 'postgres',
    PASSWORD ''
);

-- Create named persistent secret
CREATE SECRET mysql_secret_one (
    TYPE mysql,
    HOST '127.0.0.1',
    PORT 0,
    DATABASE mysql,
    USER 'mysql',
    PASSWORD ''
);

-- Attach using secret (implicit match)
ATTACH '' AS postgres_db (TYPE postgres);

-- Attach using named secret (explicit)
ATTACH '' AS mysql_db_one (TYPE mysql, SECRET mysql_secret_one);

-- Attach with parameter override
ATTACH 'database=my_other_db' AS mysql_db (TYPE mysql);
```

### 2. Secret Types Supported by Extensions

Based on DuckDB documentation research:

- **S3**: `KEY_ID`, `SECRET`, `REGION`, `ENDPOINT`, `SESSION_TOKEN`
- **Azure**: `CONNECTION_STRING`, `ACCOUNT_NAME`, `PROVIDER` (credential_chain, config)
- **GCS**: Service account credentials
- **HTTP/HTTPS**: `BEARER_TOKEN`, `EXTRA_HTTP_HEADERS`
- **PostgreSQL**: `HOST`, `PORT`, `DATABASE`, `USER`, `PASSWORD`
- **MySQL**: `HOST`, `PORT`, `DATABASE`, `USER`, `PASSWORD`
- **R2 (Cloudflare)**: `KEY_ID`, `SECRET`, `ACCOUNT_ID`

### 3. Current Oracle Extension Authentication

**Method 1: Connection String in ATTACH**

```sql
ATTACH 'user/password@host:port/service' AS ora (TYPE oracle);
```

**Method 2: Oracle Wallet (TNS_ADMIN)**

```sql
SELECT oracle_attach_wallet('/path/to/wallet');
ATTACH 'user@connect_identifier' AS ora (TYPE oracle);
```

### 4. Oracle-Specific Authentication Methods

Oracle supports multiple authentication mechanisms:

1. **Username/Password** (current implementation)
2. **Oracle Wallet** (current partial implementation)
3. **External OS Authentication** (`/` as username)
4. **Kerberos/LDAP Authentication**
5. **Client Certificate Authentication**
6. **Token-based Authentication** (OAuth2, Azure AD)

### 5. Gap Analysis

#### What Secret Manager Would Add

- Centralized credential storage
- Reusable credentials across multiple ATTACH operations
- Consistent API with other DuckDB extensions
- Persistent secrets (stored between sessions)
- Secret scoping (database-wide, session-wide)
- Better credential management in scripts

#### What Current Implementation Provides

- Direct connection string support (standard Oracle format)
- Oracle Wallet integration (industry standard for Oracle)
- Environment variable support (TNS_ADMIN)
- Simple API for basic use cases

#### Limitations of Current Approach

- Credentials in plain text in ATTACH statements
- No centralized secret management
- Wallet setup requires external oracle_attach_wallet() call
- Cannot reuse credentials across multiple databases
- Inconsistent with PostgreSQL/MySQL extension patterns

## Implementation Options

### Option A: Full Secret Manager Integration

**Pros:**

- Consistent with other DuckDB database extensions
- Supports all DuckDB secret features (persistence, scoping)
- Better credential management
- Scriptable and automatable

**Cons:**

- Significant C++ implementation effort
- Need to integrate with DuckDB's SecretManager C++ API
- Backward compatibility concerns
- Oracle Wallet integration becomes complex

**Estimated Effort:** 3-5 days

### Option B: Hybrid Approach

Keep existing connection string + wallet support, add optional secret integration:

```sql
-- Option 1: Existing (no change)
ATTACH 'user/pass@host:port/service' AS ora (TYPE oracle);

-- Option 2: Wallet (existing)
SELECT oracle_attach_wallet('/path/to/wallet');
ATTACH 'user@service' AS ora (TYPE oracle);

-- Option 3: NEW - Secret-based
CREATE SECRET (
    TYPE oracle,
    HOST 'localhost',
    PORT 1521,
    SERVICE 'XEPDB1',
    USER 'scott',
    PASSWORD 'tiger'
);
ATTACH '' AS ora (TYPE oracle);

-- Option 4: NEW - Named secret
CREATE SECRET ora_prod (TYPE oracle, HOST 'prod.example.com', ...);
ATTACH '' AS ora (TYPE oracle, SECRET ora_prod);
```

**Pros:**

- Backward compatible
- Follows PostgreSQL/MySQL pattern
- Flexible authentication methods
- Progressive enhancement

**Cons:**

- More code paths to maintain
- Complex wallet + secret interaction
- Documentation complexity

**Estimated Effort:** 4-6 days

### Option C: Minimal Enhancement (Documentation + Guidance)

Document current patterns better and provide helper utilities:

**Pros:**

- No code changes required
- Existing Oracle patterns are well-understood
- Oracle Wallet is industry standard
- Simpler maintenance

**Cons:**

- Inconsistent with other DuckDB extensions
- Limited credential management
- Plain text passwords in ATTACH

**Estimated Effort:** 0.5 days (documentation only)

## Recommended Approach

**RECOMMENDATION: Option B (Hybrid Approach)**

### Rationale

1. **Consistency with DuckDB Ecosystem**: PostgreSQL and MySQL extensions support CREATE SECRET, users expect the same pattern
2. **Backward Compatibility**: Existing users continue working without changes
3. **Enterprise Use Cases**: Secret Manager enables better DevOps/GitOps workflows
4. **Security**: Reduces plain-text credentials in scripts
5. **Future-Proof**: Enables future enhancements (credential rotation, vault integration)

### Implementation Priority

**Phase 1 (MVP):**

- Basic secret type registration (`TYPE oracle`)
- Parameters: `HOST`, `PORT`, `SERVICE`, `USER`, `PASSWORD`
- Secret retrieval in OracleAttach
- ATTACH with empty connection string uses default secret
- ATTACH with SECRET parameter uses named secret

**Phase 2 (Enhanced):**

- `DATABASE` parameter as alias for `SERVICE`
- `WALLET_PATH` parameter in secret for wallet integration
- Connection string parsing for partial overrides
- `PROVIDER` support (credential_chain for Oracle Cloud)

**Phase 3 (Advanced):**

- External authentication support (`AUTHENTICATION_TYPE` parameter)
- Token-based authentication
- Multiple authentication methods in one secret
- Secret validation on creation

## Technical Implementation Notes

### C++ API Integration Points

Based on PostgreSQL extension patterns, implementation requires:

1. **Secret Type Registration**
   - Register "oracle" secret type in `LoadInternal()`
   - Define secret parameters and validation

2. **Secret Retrieval in OracleAttach**
   - Query SecretManager for matching secrets
   - Parse secret parameters
   - Build connection string from secret
   - Merge with ATTACH options if provided

3. **Connection String Construction**
   - Convert secret parameters to Oracle EZConnect format
   - Handle TNS_ADMIN for wallet-based secrets
   - Support partial overrides from ATTACH path

4. **Backward Compatibility**
   - Detect if connection string is provided
   - Fall back to existing parsing logic
   - No behavior change for existing code

### Example Implementation Flow

```cpp
// In OracleAttach (oracle_storage_extension.cpp)
static unique_ptr<Catalog> OracleAttach(...) {
    string connection_string;

    // Check if SECRET parameter provided
    if (options.options.count("secret")) {
        auto secret_name = options.options["secret"].GetValue<string>();
        auto secret = SecretManager::Get(context).GetSecretByName(secret_name, "oracle");
        connection_string = BuildConnectionString(secret);
    } else if (info.path.empty() || info.path == "") {
        // Use default secret
        auto secret = SecretManager::Get(context).LookupSecret("oracle");
        connection_string = BuildConnectionString(secret);
    } else {
        // Existing behavior: use connection string from path
        connection_string = info.path;
    }

    // Continue with existing logic
    auto state = make_shared_ptr<OracleCatalogState>(connection_string);
    ...
}
```

## Security Considerations

1. **Secret Storage**: DuckDB stores persistent secrets in unencrypted binary format (documented behavior)
2. **Wallet Priority**: Oracle Wallet provides encrypted storage, should remain preferred for production
3. **Secret Scoping**: Temporary secrets preferred for sensitive environments
4. **Audit Trail**: Secret usage should be logged (DuckDB handles this)

## Migration Path for Existing Users

### Current Users (No Changes Required)

```sql
-- Continues to work
ATTACH 'user/pass@host:port/service' AS ora (TYPE oracle);
```

### New Users (Recommended Pattern)

```sql
-- Development: temporary secret
CREATE SECRET (TYPE oracle, HOST 'localhost', PORT 1521, ...);
ATTACH '' AS ora (TYPE oracle);

-- Production: wallet-based
SELECT oracle_attach_wallet('/secure/wallet/path');
ATTACH 'user@prod_service' AS ora (TYPE oracle);
```

### Enterprise Users (Secret Manager)

```sql
-- Persistent named secrets per environment
CREATE PERSISTENT SECRET ora_dev (TYPE oracle, ...);
CREATE PERSISTENT SECRET ora_prod (TYPE oracle, ...);

-- Attach by environment
ATTACH '' AS ora (TYPE oracle, SECRET ora_dev);
```

## Testing Requirements

1. **Secret Creation**: Validate all parameter combinations
2. **Secret Retrieval**: Named vs. default secret matching
3. **ATTACH Integration**: Empty path + secret
4. **Parameter Override**: Connection string overrides secret
5. **Backward Compatibility**: All existing tests pass
6. **Wallet Integration**: SECRET with WALLET_PATH parameter
7. **Error Handling**: Missing secret, invalid parameters
8. **Persistence**: CREATE PERSISTENT SECRET survival across sessions

## Documentation Requirements

1. Update README with secret examples
2. Add migration guide for existing users
3. Document secret parameters
4. Provide production deployment patterns
5. Security best practices guide
6. Comparison table: connection string vs. wallet vs. secret

## Conclusion

**GO Decision: YES - Implement Option B (Hybrid Approach)**

### Value Proposition

- Aligns Oracle extension with DuckDB ecosystem standards
- Enables enterprise credential management workflows
- Maintains backward compatibility (zero breaking changes)
- Enhances security posture for development and CI/CD
- Positions extension for future Oracle Cloud integration

### Success Criteria

1. All existing tests pass without modification
2. CREATE SECRET with TYPE oracle works
3. ATTACH '' with secret matches PostgreSQL/MySQL behavior
4. Documentation provides clear migration paths
5. Integration tests cover all authentication methods

### Next Steps

1. Expert agent: Research DuckDB SecretManager C++ API
2. Expert agent: Implement secret type registration
3. Expert agent: Integrate secret retrieval in OracleAttach
4. Testing agent: Create comprehensive secret test suite
5. Docs & Vision agent: Update documentation and guides
