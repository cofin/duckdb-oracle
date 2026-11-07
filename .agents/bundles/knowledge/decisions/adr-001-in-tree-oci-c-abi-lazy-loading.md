---
type: Decision
title: "ADR-001: In-Tree OCI C ABI Declarations & Lazy Runtime Dynamic Loading"
description: Replace compile-time Oracle SDK headers and DT_NEEDED libclntsh linkage with in-tree C ABI declarations and lazy dlopen/LoadLibraryA symbol resolution on first connection.
scope: decision
domain: oracle
status: stable
tags: [adr, oracle, oci, dlopen, community-extensions, packaging]
generated:
  by: flow/0.27.0
  at: 2026-11-07T17:36:00Z
verified:
  - by: agent:flow-setup
    at: 2026-11-07T17:36:00Z
    commit: 36539f3
sources:
  - id: oci-hdr
    resource: src/include/oracle_oci_api.hpp
    title: In-tree OCI C ABI types, constants, and 22 function signatures
  - id: oci-src
    resource: src/oracle_oci_api.cpp::OCIInitClient
    title: Thread-safe lazy dlopen/LoadLibraryA OCI symbol loader
  - id: cmake-lists
    resource: CMakeLists.txt
    title: CMake build linking only against CMAKE_DL_LIBS
---

# ADR-001: In-Tree OCI C ABI Declarations & Lazy Runtime Dynamic Loading

<!-- truth: start -->
- Declare all OCI types, constants, and 22 function signatures in-tree in `src/include/oracle_oci_api.hpp` (`#pragma once`, never `#include <oci.h>`)[^oci-hdr].
- Link `oracle_extension` and `oracle_loadable_extension` solely against `${CMAKE_DL_LIBS}` in `CMakeLists.txt` with zero build-time `DT_NEEDED` dependency on `libclntsh`[^cmake-lists].
- Resolve `libclntsh.so` / `libclntsh.dylib` / `oci.dll` lazily via `std::call_once` in `OCIInitClient()` (`src/oracle_oci_api.cpp`) when the first Oracle connection is opened (`OracleConnectionManager::GetOrCreateEnv()`), so `INSTALL oracle FROM community; LOAD oracle;` and `make test-unit` succeed on hosts without Oracle Instant Client installed[^oci-src].
<!-- truth: end -->

## Context

Official DuckDB community extensions (`duckdb/community-extensions`) are compiled, tested, signed, and documented inside clean upstream GitHub Actions runners (`ubuntu-latest`, `macos-latest`, `windows-latest`) that do not have Oracle Instant Client SDK headers (`<oci.h>`) or shared libraries (`libclntsh.so`, `libclntsh.dylib`, `oci.dll`) installed. Furthermore, the upstream documentation generator (`generate_docs.yml` / `scripts/generate_md.sh`) executes `FORCE INSTALL oracle; LOAD oracle;` on an unmodified runner to introspect extension functions and settings.

Any build-time requirement on `<oci.h>` or load-time dynamic linker dependency (`DT_NEEDED` on `libclntsh`) causes community CI builds and `LOAD oracle;` introspection to fail immediately.

## Decision

1. **In-Tree OCI C ABI Declarations (`src/include/oracle_oci_api.hpp`)**: Maintain minimal, stable C ABI type aliases (`OCIEnv`, `OCIError`, `OCISvcCtx`, `OCIServer`, `OCISession`, `OCIStmt`, `OCIBind`, `OCIDefine`, `OCIDate`, `sword`, `ub4`, `ub2`, `sb4`, `sb2`), status/attribute constants, and function declarations for the 22 OCI entrypoints used by `duckdb-oracle` directly in-tree[^oci-hdr].
2. **Thread-Safe Lazy Runtime Symbol Resolution (`src/oracle_oci_api.cpp`)**: Link only against `${CMAKE_DL_LIBS}` in `CMakeLists.txt`[^cmake-lists]. On first call to `OCIInitClient()` (invoked by `OracleConnectionManager::GetOrCreateEnv()` and `OCIEnvCreate`), use `std::call_once` to search `OCI_LIB_DIR`, `ORACLE_HOME/lib`, `ORACLE_HOME`, and system loader paths via `dlopen`/`dlsym` (POSIX) or `LoadLibraryA`/`GetProcAddress` (Windows), binding all 22 function pointers atomically or throwing a descriptive `duckdb::IOException`[^oci-src].

## Rejected Alternatives

| Alternative | Reason Rejected |
| :--- | :--- |
| Compile-time `<oci.h>` + `DT_NEEDED` linkage against `libclntsh` | Fails `duckdb/community-extensions` clean-runner compilation and crashes `LOAD oracle;` during `generate_docs.yml` when Instant Client is not installed on the host. |
| Bundling Oracle Instant Client inside the extension binary | Oracle Instant Client is proprietary (~80–240 MB per platform) and cannot be statically linked or redistributed inside an MIT-licensed community extension binary. |
| DPI-C (`odpi-c`) wrapper layer | Adds an extra abstraction layer over the 22 OCI functions already encapsulated by `OracleContext`, `OracleConnectionManager`, and `oracle_utils.hpp` without removing the runtime Instant Client requirement. |

## Consequences

- `make release`, `make debug`, `make lint`, `make test-unit`, and `INSTALL oracle FROM community; LOAD oracle;` execute with zero external Oracle SDK or runtime libraries installed.
- Both DuckDB `2.0.0` (`main`, `v0.3.1`) and DuckDB `1.5.6` (`feat/duckdb-v1.5.6`) build cleanly in `duckdb/community-extensions`.
- Adding a new OCI entrypoint requires declaring its C signature in `src/include/oracle_oci_api.hpp` and resolving its symbol in `src/oracle_oci_api.cpp::ResolveSymbols`.

[^oci-hdr]: `src/include/oracle_oci_api.hpp` — In-tree OCI C ABI types, constants, and 22 function signatures.
[^oci-src]: `src/oracle_oci_api.cpp::OCIInitClient` — Thread-safe lazy `dlopen`/`LoadLibraryA` OCI symbol loader.
[^cmake-lists]: `CMakeLists.txt` — CMake build linking only against `CMAKE_DL_LIBS`.
