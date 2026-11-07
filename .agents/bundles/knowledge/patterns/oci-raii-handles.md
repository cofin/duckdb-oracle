---
type: Pattern
title: OCI RAII Handle & Descriptor Management
description: Smart pointer deleters and RAII wrappers for OCI handles and descriptors preventing resource leaks in C++.
scope: pattern
domain: oracle
status: stable
tags: [oracle, oci, raii, memory-management, smart-pointers]
generated:
  by: flow/0.27.0
  at: 2026-11-07T17:36:00Z
verified:
  - by: agent:flow-setup
    at: 2026-11-07T17:36:00Z
    commit: 36539f3
sources:
  - id: oci-utils
    resource: src/include/oracle_utils.hpp::OCIHandleFreeDeleter
    title: RAII OCI handle and descriptor deleters and statement allocation helpers
  - id: conn-ctx
    resource: src/oracle_connection.cpp::OracleContext::~OracleContext
    title: Ordered OCI handle teardown with co-owned std::shared_ptr<OCIEnv>
  - id: oracle-scan
    resource: src/oracle_scan.cpp::OracleInitGlobal
    title: Statement lifetime across bind, global init, and scan pipeline phases
---

# OCI RAII Handle & Descriptor Management

<!-- truth: start -->
- Allocate all OCI statements via `AllocateOCIStatement` or `AllocateSharedOCIStatement` (`src/include/oracle_utils.hpp`) using `OCIHandlePtr<OCIStmt>` (`std::unique_ptr<T, OCIHandleFreeDeleter>`)[^oci-utils].
- Store `std::shared_ptr<OCIEnv> env_owner` as the first member of `OracleContext` so child handles (`authp`, `svchp`, `srvhp`, `errhp`) are freed before `OCIEnv` during destruction[^conn-ctx].
- Anti-Pattern: Calling raw `OCIHandleAlloc` / `OCIDescriptorAlloc` without wrapping the handle in `OCIHandlePtr` / `OCIDescriptorPtr`.
  - Reason: Any intermediate `CheckOCIError` throwing `duckdb::IOException` leaks the handle and server cursor state in the Oracle client process[^oci-utils].
<!-- truth: end -->

In native C OCI development, every handle allocated via `OCIHandleAlloc` and every descriptor allocated via `OCIDescriptorAlloc` must be explicitly freed with `OCIHandleFree` and `OCIDescriptorFree`[^oci-utils]. Manual cleanup in C++ is fragile when exceptions (`duckdb::IOException`, `std::runtime_error`) are thrown.

## RAII Deleters (`src/include/oracle_utils.hpp`)

DuckDB Oracle implements custom deleters paired with `std::unique_ptr` and `std::shared_ptr`[^oci-utils]:

```cpp
namespace duckdb {

struct OCIHandleFreeDeleter {
    OCIHandleFreeDeleter() : handle_type(0) {}
    explicit OCIHandleFreeDeleter(ub4 handle_type) : handle_type(handle_type) {}

    ub4 handle_type;

    void operator()(void *handle) const {
        if (handle) {
            OCIHandleFree(handle, handle_type);
        }
    }
};

template <class T>
using OCIHandlePtr = std::unique_ptr<T, OCIHandleFreeDeleter>;

struct OCIDescriptorFreeDeleter {
    OCIDescriptorFreeDeleter() : descriptor_type(0) {}
    explicit OCIDescriptorFreeDeleter(ub4 descriptor_type) : descriptor_type(descriptor_type) {}

    ub4 descriptor_type;

    void operator()(void *descriptor) const {
        if (descriptor) {
            OCIDescriptorFree(descriptor, descriptor_type);
        }
    }
};

template <class T>
using OCIDescriptorPtr = std::unique_ptr<T, OCIDescriptorFreeDeleter>;

} // namespace duckdb
```

## Statement Allocation Helpers

Statement handles are allocated dynamically per query execution or bind phase. Allocation helpers encapsulate creation and RAII ownership[^oci-utils]:

```cpp
inline OCIHandlePtr<OCIStmt> AllocateOCIStatement(OCIEnv *envhp, OCIError *errhp, const std::string &msg) {
    OCIStmt *stmt = nullptr;
    CheckOCIError(OCIHandleAlloc(envhp, reinterpret_cast<dvoid **>(&stmt), OCI_HTYPE_STMT, 0, nullptr), errhp, msg);
    return OCIHandlePtr<OCIStmt>(stmt, OCIHandleFreeDeleter {OCI_HTYPE_STMT});
}

inline std::shared_ptr<OCIStmt> AllocateSharedOCIStatement(OCIEnv *envhp, OCIError *errhp, const std::string &msg) {
    auto stmt = AllocateOCIStatement(envhp, errhp, msg);
    return std::shared_ptr<OCIStmt>(stmt.release(), [](OCIStmt *ptr) {
        if (ptr) {
            OCIHandleFree(ptr, OCI_HTYPE_STMT);
        }
    });
}
```

## Statement & Environment Lifetime Across Pipeline Phases

DuckDB splits table function execution into distinct stages[^oracle-scan]:
1. **Bind Phase (`OracleBindInternal`)**: Connects to Oracle, inspects metadata (or runs `OCIStmtExecute` with `OCI_DESCRIBE_ONLY` for arbitrary `oracle_query` SQL) to populate output column names and types.
2. **Global Init Phase (`OracleInitGlobal`)**: Reconstructs the final Oracle SQL statement incorporating projected `column_ids` and pushed-down `TableFilterSet` / complex filter predicates, prepares the cursor via `OCIStmtPrepare`, configures `OCI_ATTR_PREFETCH_ROWS` and `OCI_ATTR_CALL_TIMEOUT` (`oracle_call_timeout_ms`), and defines output buffers (`OCIDefineByPos`) only for projected columns[^oracle-scan].
3. **Scan Phase (`OracleQueryFunction`)**: Executes and fetches row batches from the prepared cursor in `OracleScanState` and converts OCI buffers into DuckDB vectors.

Every `OracleContext` co-owns a `std::shared_ptr<OCIEnv>`, and prepared statements release cleanly via `OCIHandleFree` (`OCI_HTYPE_STMT`) before the connection and environment handles are destroyed[^conn-ctx].

[^oci-utils]: `src/include/oracle_utils.hpp::OCIHandleFreeDeleter` — RAII OCI handle and descriptor deleters and statement allocation helpers.
[^conn-ctx]: `src/oracle_connection.cpp::OracleContext::~OracleContext` — Ordered OCI handle teardown with co-owned `std::shared_ptr<OCIEnv>`.
[^oracle-scan]: `src/oracle_scan.cpp::OracleInitGlobal` — Statement lifetime across bind, global init, and scan pipeline phases.
