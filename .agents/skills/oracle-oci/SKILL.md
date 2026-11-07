---
name: oracle-oci
description: "Use when modifying Oracle Call Interface (OCI) C ABI declarations, lazy runtime dlopen/LoadLibraryA loading, RAII handle management, connection pooling, wallet/SEPS authentication, or OCI type conversion and array fetch/bind buffers in src/oracle_oci_api.*, src/oracle_connection*.*, src/oracle_secret.cpp, src/oracle_settings.cpp, src/oracle_type_*.cpp, or src/include/oracle_utils.hpp."
---

# Oracle OCI Engineering Skill (`oracle-oci`)

<!-- truth: start -->
- Declare all OCI types, constants, and 22 function signatures in-tree in `src/include/oracle_oci_api.hpp` (`#pragma once`, never `#include <oci.h>`) and resolve symbols lazily via `OCIInitClient()` in `src/oracle_oci_api.cpp`.
- Wrap all OCI handles and descriptors in RAII smart pointers (`OCIHandlePtr<T>`, `OCIDescriptorPtr<T>`, `AllocateOCIStatement`, `AllocateSharedOCIStatement` in `src/include/oracle_utils.hpp`) and keep `std::shared_ptr<OCIEnv> env_owner` as the first member of `OracleContext`.
- Translate all OCI status codes via `CheckOCIError` (or `CheckOCIOptionalAttributeORA24315` for `OCI_ATTR_CALL_TIMEOUT` on OCI 23.x) into `duckdb::IOException`.
- Inspect `sb2` indicator arrays (`ind == -2 || ind > 0`) across all fetched rows and columns before checking `status == OCI_SUCCESS_WITH_INFO`, and only fail `OCI_SUCCESS_WITH_INFO` on `ORA-01406` (`OCIErrorCodeMatches(errhp, 1406)`).
- Preserve per-`ATTACH` setting overrides via `OracleSettings::attach_overridden_keys` unless `lookup.GetScope() == SettingScope::LOCAL`.
<!-- truth: end -->

## Workflow

1. **In-Tree C ABI & Lazy Runtime Loading (`src/include/oracle_oci_api.hpp`, `src/oracle_oci_api.cpp`)**:
   - When adding or modifying OCI function bindings, declare the C signature in `src/include/oracle_oci_api.hpp` and resolve the symbol inside `ResolveSymbols` in `src/oracle_oci_api.cpp`.
   - Keep `CMakeLists.txt` linked strictly to `${CMAKE_DL_LIBS}` with zero compile-time or `LOAD`-time dependency on `libclntsh`.
2. **Handle Lifecycle, Connection Pooling & Wallet Auth (`src/oracle_connection*.cpp`, `src/oracle_secret.cpp`)**:
   - Follow [`references/handles-and-pooling.md`](references/handles-and-pooling.md) when touching `OracleContext`, `OracleConnectionManager`, `ScopedTnsAdmin`, or `ResolveOracleConnection`.
   - Ensure `OracleConnectionHandle::~OracleConnectionHandle` rolls back uncommitted transactions (`OCITransRollback`), validates health via `IsOracleContextHealthy` (`OCIPing`), and unlocks `pool->lock` before `pool->cv.notify_one()`.
3. **Type Mapping, Array Fetch & Array Bind (`src/oracle_type_*.cpp`, `src/oracle_scan.cpp`, `src/storage/oracle_write.cpp`)**:
   - Follow [`references/types-and-batches.md`](references/types-and-batches.md) when modifying `OracleTypeRegistry`, `SetOracleOutputValue`, or batch write transposition.
4. **Verify**:
   - Run `make lint` (`clang-format` + `clang-tidy`) and `make test-unit` (plus `make integration` when touching live OCI session/query paths).

## Guardrails

- Never free `OCIEnv` before child `OCISession`, `OCISvcCtx`, `OCIServer`, `OCIStmt`, or `OCIError` handles are freed.
- Never hold a pooled `OracleConnectionHandle` across nested metadata queries (`GetVersionInfo()`) when `oracle_connection_limit = 1`.
- Never log or expose plaintext passwords in `GetDBPath()` or exception messages; always pass connection strings through `RedactOracleConnectionString`.

## References

- [Handles, Connection Pooling & Wallet Auth](references/handles-and-pooling.md)
- [OCI Type Conversion, Indicators & Array Batching](references/types-and-batches.md)
- [Oracle OCI Subsystem Architecture](../../bundles/knowledge/architecture/oracle-oci-subsystem.md)
- [Oracle OCI Domain Concepts](../../bundles/knowledge/domains/oracle-oci.md)
- [Oracle OCI Edge Cases & Gotchas](../../bundles/knowledge/gotchas/oracle-oci.md)
