---
type: Architecture
title: Oracle OCI Subsystem Architecture
description: Runtime topology, handle ownership hierarchy, connection caching, and thick-driver integration for the DuckDB Oracle extension.
scope: architecture
domain: oracle
tags: [oracle, oci, architecture, handles, connection-pooling]
status: stable
---

# Oracle OCI Subsystem Architecture

The Oracle Call Interface (OCI) subsystem provides low-level, high-throughput communication between DuckDB and Oracle Database instances. It operates as a C-based thick client requiring the Oracle Instant Client shared libraries.

```
+-----------------------------------------------------------------------------------+
| DuckDB Engine (ClientContext / Pipeline / StorageExtension)                       |
+-----------------------------------------------------------------------------------+
       |                                       |
       v                                       v
+-------------------------------+      +--------------------------------------------+
| OracleCatalogState            |      | TableFunction / PhysicalOracleInsert       |
+-------------------------------+      +--------------------------------------------+
       |                                       |
       +-------------------+-------------------+
                           |
                           v
       +-------------------------------------------------------+
       | OracleConnectionManager (Singleton Connection Pool)   |
       | - Key: normalized conn_string + wallet_path           |
       | - Pool: list<shared_ptr<OracleConnectionHandle>>      |
       +-------------------------------------------------------+
                           |
                           v
       +-------------------------------------------------------+
       | OracleContext (Handle Tree)                           |
       |                                                       |
       |  std::shared_ptr<OCIEnv> (OCI_THREADED | OCI_OBJECT)  |
       |    +---> OCIError (Thread-local error diagnostic)     |
       |    +---> OCISvcCtx (Service Context)                  |
       |            +---> OCIServer (Physical connection)      |
       |            +---> OCISession (Authenticated user)      |
       |                                                       |
       |  Per-Execution Statement Handles:                     |
       |    +---> OCIStmt (AllocateOCIStatement / Shared)      |
       +-------------------------------------------------------+
                           |
                           v  (OCI Array Fetch / Array Bind via TCP)
       +-------------------------------------------------------+
       | Oracle Database Instance (11g / 19c / 21c / 23ai)     |
       +-------------------------------------------------------+
```

## Handle Ownership & Lifetime Hierarchy

OCI enforces strict parent-child relationships between handles. Freeing a parent handle implicitly invalidates or corrupts child handles if not orchestrated correctly:

1. **Environment Handle (`std::shared_ptr<OCIEnv>`)**:
   - Root handle allocated via `OCIEnvCreate` with modes `OCI_THREADED | OCI_OBJECT`.
   - Shared across threads; all subordinate handles inherit memory allocation and concurrency settings from this environment.
   - Co-owned via `std::shared_ptr<OCIEnv> env_owner` as the first member of `OracleContext` and inside `OracleConnectionManager` so that active or checked-out connections and child statement handles never outlive `OCIEnv` during static destruction, and `OracleContext::~OracleContext` frees `authp`, `svchp`, `srvhp`, and `errhp` before `env_owner.reset()`.

2. **Error Handle (`OCIError`)**:
   - Child of `OCIEnv` with handle type `OCI_HTYPE_ERROR`.
   - Used to retrieve error text, error codes, and server diagnostic records via `OCIErrorGet`.
   - Must be passed to virtually all OCI API calls.

3. **Server Handle (`OCIServer`)**:
   - Represents the physical transport connection to the database listener.
   - Attached using `OCIServerAttach` with an Oracle Net connect descriptor, TNS alias, or Easy Connect string (e.g., `host:port/service_name`).

4. **Session Handle (`OCISession`)**:
   - Represents the user authentication and session context.
   - Authenticated via `OCISessionBegin` using `OCI_CRED_RDBMS` (with `OCI_ATTR_USERNAME` and `OCI_ATTR_PASSWORD`, scrubbing the plaintext password buffer immediately afterward) or `OCI_CRED_EXT` for External Password Store (SEPS `/@connect_identifier`) wallets.
   - Initializes deterministic session state (`ALTER SESSION SET NLS_DATE_FORMAT = 'YYYY-MM-DD HH24:MI:SS' NLS_TIMESTAMP_FORMAT = 'YYYY-MM-DD HH24:MI:SS.FF' NLS_TIMESTAMP_TZ_FORMAT = 'YYYY-MM-DD HH24:MI:SS.FF TZH:TZM' NLS_NUMERIC_CHARACTERS = '.,'`) and configures `OCI_ATTR_STMTCACHESIZE` on `OCISvcCtx` from `oracle_stmt_cache_size`.
   - For Oracle Autonomous Database (ADB) or mutual TLS (mTLS), `ScopedTnsAdmin` sets `TNS_ADMIN` only when `wallet_path` is non-empty and differs from the active `TNS_ADMIN`, and releases the guard immediately after `OCISessionBegin`.

5. **Service Context Handle (`OCISvcCtx`)**:
   - The operational composite handle containing references to both the active `OCIServer` and `OCISession`.
   - All statement execution, transactions, and commits are dispatched through the service context.

6. **Statement Handle (`OCIStmt`)**:
   - Allocated via `AllocateOCIStatement` or `AllocateSharedOCIStatement` (`OCIHandleAlloc` with `OCI_HTYPE_STMT`), prepared via `OCIStmtPrepare`, and freed via `OCIHandleFree(stmt, OCI_HTYPE_STMT)` before connection handles are destroyed.
   - Represents a prepared SQL statement, its bind variables (`OCIBind`), and its output column defines (`OCIDefine`).

## Connection Pooling & Session Caching

`OracleConnectionManager` (`src/oracle_connection_manager.cpp`) maintains a connection cache:

- **Cache Key**: A deterministic key derived from the normalized connection string, wallet path, and effective connection settings.
- **Session Reuse & Health Checks**: When enabled (`oracle_connection_cache = true`), `OracleConnectionHandle::~OracleConnectionHandle` rolls back any uncommitted transaction (`OCITransRollback`), verifies session health via `IsOracleContextHealthy` (`OCI_ATTR_SERVER_STATUS == OCI_SERVER_NORMAL` and `OCIPing`), and returns healthy handles to `pool->idle` (unlocking `pool->lock` before `pool->cv.notify_one()`). `Acquire()` also validates idle handles before checkout.
- **Concurrency Guard**: Access to the pool is protected by a per-pool mutex (`std::mutex`) and condition variable (`std::condition_variable`) with deadline enforcement even across `pool->stale` invalidations.
- **Connection Ceiling & Call Timeout**: Bounded by `oracle_connection_limit` (default 8, validated up to 1024), `oracle_call_timeout_ms` (default 30,000 ms, up to 3,600,000 ms), and `oracle_stmt_cache_size` (default 20, up to 1024).

## Thick Driver Runtime Integration

DuckDB Oracle links dynamically to the Oracle Instant Client SDK:
- **Core Shared Object**: `libclntsh.so` (Linux), `libclntsh.dylib` (macOS), `oci.dll` (Windows).
- **Asynchronous I/O Requirement (`libaio`)**: On Linux, Instant Client requires `libaio.so.1`. On newer distributions such as Ubuntu 24.04 (Noble), this package is named `libaio1t64`, requiring a symlink to `libaio.so.1`.
- **Header Files**: `oci.h`, `oratypes.h`, `ocidfn.h`, `ociapr.h` located under `oracle_sdk/instantclient_*/sdk/include`.
