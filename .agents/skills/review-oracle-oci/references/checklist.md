# Oracle OCI Review Checklist (`OCI-01`..`OCI-08`)

| Rule ID | Severity | Check |
| :--- | :--- | :--- |
| `OCI-01` | `P0` | **Zero Compile/Load-Time OCI SDK Dependency**: `src/` and `CMakeLists.txt` must never `#include <oci.h>` or link `libclntsh` at build time; all OCI symbols must be declared in `src/include/oracle_oci_api.hpp` and resolved lazily in `src/oracle_oci_api.cpp`. |
| `OCI-02` | `P0` | **RAII Handle & Environment Lifetime**: Every `OCIHandleAlloc` / `OCIDescriptorAlloc` must be owned by `OCIHandlePtr` / `OCIDescriptorPtr`, and `OracleContext` must retain `std::shared_ptr<OCIEnv> env_owner` as its first member so child handles are freed before `OCIEnv`. |
| `OCI-03` | `P1` | **Single-Connection Pool Deadlock Prevention**: Code holding an `OracleConnectionHandle` must never invoke a helper (`GetVersionInfo()`, metadata queries) that acquires a second connection from the same pool when `oracle_connection_limit = 1`. |
| `OCI-04` | `P1` | **Connection Return Hygiene**: Returning a connection to `OracleConnectionManager` must roll back uncommitted transactions (`OCITransRollback`), verify `IsOracleContextHealthy` (`OCIPing`), and unlock `pool->lock` before calling `pool->cv.notify_one()`. |
| `OCI-05` | `P1` | **Indicator Truncation Precedence & `ORA-24347`**: `OCIStmtFetch2` loops must inspect `ind == -2 || ind > 0` before checking `OCI_SUCCESS_WITH_INFO`, and `OCI_SUCCESS_WITH_INFO` must only fail on `ORA-01406`. |
| `OCI-06` | `P1` | **`ORA-24315` Timeout Attribute Compatibility**: Setting `OCI_ATTR_CALL_TIMEOUT` must use `CheckOCIOptionalAttributeORA24315` so OCI 23.x Instant Client does not fail when rejecting the attribute. |
| `OCI-07` | `P2` | **Per-`ATTACH` Setting Precedence**: `GetOracleSettings` must preserve `state->settings` for keys in `attach_overridden_keys` unless `lookup.GetScope() == SettingScope::LOCAL`. |
| `OCI-08` | `P2` | **Credential Redaction & Scrubbing**: Plaintext password buffers must be scrubbed after `OCISessionBegin`, and `GetDBPath()` / error strings must redact passwords via `RedactOracleConnectionString`. |
