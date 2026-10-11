---
type: Pattern
title: OCI RAII Handle & Descriptor Management
description: Smart pointer deleters and RAII wrappers for OCI handles and descriptors preventing resource leaks in C++.
scope: pattern
domain: oracle
tags: [oracle, oci, raii, memory-management, smart-pointers]
status: stable
---

# OCI RAII Handle & Descriptor Management

In native C OCI development, every handle allocated via `OCIHandleAlloc` and every descriptor allocated via `OCIDescriptorAlloc` must be explicitly freed with `OCIHandleFree` and `OCIDescriptorFree`. Failing to free them leaks memory and file descriptors within the Oracle client process. Manual cleanup in C++ is fragile when exceptions (`duckdb::IOException`, `std::runtime_error`) are thrown.

## RAII Deleters (`src/include/oracle_utils.hpp`)

DuckDB Oracle implements custom deleters paired with `std::unique_ptr` and `std::shared_ptr`:

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

Statement handles are allocated dynamically per query execution or bind phase. To prevent boilerplate error checking and manual cleanup on failure, allocation helpers encapsulate creation:

```cpp
inline OCIHandlePtr<OCIStmt> AllocateOCIStatement(OCIEnv *envhp, OCIError *errhp, const std::string &msg) {
    OCIStmt *stmt = nullptr;
    auto status = OCIHandleAlloc(envhp, reinterpret_cast<void **>(&stmt), OCI_HTYPE_STMT, 0, nullptr);
    CheckOCIError(status, errhp, msg);
    return OCIHandlePtr<OCIStmt>(stmt, OCIHandleFreeDeleter(OCI_HTYPE_STMT));
}

inline std::shared_ptr<OCIStmt> AllocateSharedOCIStatement(OCIEnv *envhp, OCIError *errhp, const std::string &msg) {
    OCIStmt *stmt = nullptr;
    auto status = OCIHandleAlloc(envhp, reinterpret_cast<void **>(&stmt), OCI_HTYPE_STMT, 0, nullptr);
    CheckOCIError(status, errhp, msg);
    return std::shared_ptr<OCIStmt>(stmt, [=](OCIStmt *s) {
        if (s) {
            OCIHandleFree(s, OCI_HTYPE_STMT);
        }
    });
}
```

## Shared Statement Handles Across Pipeline Phases

DuckDB splits table function execution into two distinct stages:
1. **Bind Phase (`OracleBindInternal`)**: Runs on the main planning thread. Connects to Oracle, allocates the statement, sets attributes (timeouts, prefetch), and runs `OCIStmtExecute` with `OCI_DESCRIBE_ONLY` to populate output column names and types.
2. **Scan Phase (`OracleScanFunction`)**: Runs on worker execution threads. Fetches row batches from the prepared cursor.

To prevent re-preparing and re-describing queries across phases, `OracleBindData` holds a `std::shared_ptr<OCIStmt>` created via `AllocateSharedOCIStatement`. When the query completes or an error aborts the pipeline, the last reference drops and safely releases the statement handle.
