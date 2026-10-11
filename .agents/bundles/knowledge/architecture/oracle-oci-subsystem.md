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
       | OracleConnectionContext (Handle Tree)                 |
       |                                                       |
       |  OCIEnv (OCI_THREADED | OCI_OBJECT)                   |
       |    +---> OCIError (Thread-local error diagnostic)     |
       |    +---> OCISvcCtx (Service Context)                  |
       |            +---> OCIServer (Physical connection)      |
       |            +---> OCISession (Authenticated user)      |
       |                                                       |
       |  Per-Execution Statement Handles:                     |
       |    +---> OCIStmt (Allocated via AllocateSharedOCIStmt)|
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
   - Co-owned via `std::shared_ptr<OCIEnv>` inside both `OracleConnectionManager` and every `OracleConnectionContext` so that active or checked-out connections and child statement handles never outlive `OCIEnv` during shutdown.

2. **Error Handle (`OCIError`)**:
   - Child of `OCIEnv` with handle type `OCI_HTYPE_ERROR`.
   - Used to retrieve error text, error codes, and server diagnostic records via `OCIErrorGet`.
   - Must be passed to virtually all OCI API calls.

3. **Server Handle (`OCIServer`)**:
   - Represents the physical transport connection to the database listener.
   - Attached using `OCIServerAttach` with an Oracle Net connect descriptor or Easy Connect string (e.g., `host:port/service_name`).

4. **Session Handle (`OCISession`)**:
   - Represents the user authentication and session context.
   - Initialized with `OCI_ATTR_USERNAME` and `OCI_ATTR_PASSWORD`, then authenticated via `OCISessionBegin` using `OCI_CRED_RDBMS` (and optional `OCI_STMT_CACHE` sizing via `oracle_stmt_cache_size`).
   - For Oracle Autonomous Database (ADB) or mutual TLS (mTLS), `TNS_ADMIN` points to an unpacked Oracle Wallet directory containing `tnsnames.ora`, `sqlnet.ora`, and `cwallet.sso`.

5. **Service Context Handle (`OCISvcCtx`)**:
   - The operational composite handle containing references to both the active `OCIServer` and `OCISession`.
   - All statement execution, transactions, and commits are dispatched through the service context.

6. **Statement Handle (`OCIStmt`)**:
   - Prepared via `OCIStmtPrepare2` (supporting OCI session statement caching when `oracle_stmt_cache_size > 0`) and released with `OCIStmtRelease`, or allocated via `OCIHandleAlloc` (`OCI_HTYPE_STMT`).
   - Represents a prepared SQL statement, its bind variables (`OCIBind`), and its output column defines (`OCIDefine`).

## Connection Pooling & Session Caching

`OracleConnectionManager` (`src/oracle_connection_manager.cpp`) maintains a connection cache:

- **Cache Key**: A deterministic key derived from the normalized connection string, wallet path, and effective connection settings.
- **Session Reuse**: When enabled (`oracle_connection_cache = true`), connections are returned to the pool after query execution or metadata discovery and reused by subsequent queries, avoiding the high latency of physical connect and `OCISessionBegin`.
- **Concurrency Guard**: Access to the pool is protected by a standard mutex (`std::mutex`). Connections checked out of the pool are wrapped in `std::shared_ptr<OracleConnectionHandle>`, which returns the connection to the pool when all references drop while retaining `std::shared_ptr<OCIEnv>` ownership.
- **Connection Ceiling & Call Timeout**: Bounded by `oracle_connection_limit` (default 8, validated up to 1024) and `oracle_call_timeout_ms` (default 30,000 ms, up to 3,600,000 ms).

## Thick Driver Runtime Integration

DuckDB Oracle links dynamically to the Oracle Instant Client SDK:
- **Core Shared Object**: `libclntsh.so` (Linux), `libclntsh.dylib` (macOS), `oci.dll` (Windows).
- **Asynchronous I/O Requirement (`libaio`)**: On Linux, Instant Client requires `libaio.so.1`. On newer distributions such as Ubuntu 24.04 (Noble), this package is named `libaio1t64`, requiring a symlink to `libaio.so.1`.
- **Header Files**: `oci.h`, `oratypes.h`, `ocidfn.h`, `ociapr.h` located under `oracle_sdk/instantclient_*/sdk/include`.
