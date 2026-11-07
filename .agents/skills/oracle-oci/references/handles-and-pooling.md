# Handles, Connection Pooling & Wallet Authentication

## 1. `OracleContext` Handle Teardown Order (`src/oracle_connection.cpp`)

`OracleContext` declares `std::shared_ptr<OCIEnv> env_owner` as its first struct member and tears down child handles in strict reverse dependency order inside `OracleContext::~OracleContext`:
1. `OCISessionEnd(svchp, errhp, authp, OCI_DEFAULT)` + `OCIHandleFree(authp, OCI_HTYPE_SESSION)`
2. `OCIServerDetach(srvhp, errhp, OCI_DEFAULT)`
3. `OCIHandleFree(svchp, OCI_HTYPE_SVCCTX)`
4. `OCIHandleFree(srvhp, OCI_HTYPE_SERVER)`
5. `OCIHandleFree(errhp, OCI_HTYPE_ERROR)`
6. `env_owner.reset()`

## 2. `OracleConnectionManager` Pooling Invariants (`src/oracle_connection_manager.cpp`)

- **Pool Key**: Normalized connection string + wallet path (`BuildPoolKey`).
- **Checkout (`Acquire`)**: Waits on `pool->cv` bounded by `acquire_timeout`, pops idle contexts, and verifies `IsOracleContextHealthy` (`OCI_ATTR_SERVER_STATUS == OCI_SERVER_NORMAL` and `OCIPing`) outside the lock before returning `OracleConnectionHandle`.
- **Return (`~OracleConnectionHandle`)**: Issues `OCITransRollback` to clear any uncommitted transaction state, checks `IsOracleContextHealthy`, pushes healthy contexts back to `pool->idle` when `settings.connection_cache` is enabled and `!pool->stale`, and unlocks `pool->lock` before calling `pool->cv.notify_one()`.
- **Single-Connection Deadlock Prevention**: Resolve `state->GetVersionInfo()` before acquiring a scan connection in `OracleBindInternal`, and return metadata connections immediately after each query in `OracleCatalogState::ExecuteMetadataQueryWithStringBinds`.

## 3. Wallet, SEPS (`/@alias`), & Per-`ATTACH` Settings

- **SEPS External Password Store**: When `user` and `password` are empty (e.g., `/@alias`), `CreateConnection` authenticates via `OCISessionBegin(..., OCI_CRED_EXT, OCI_DEFAULT)` without setting `OCI_ATTR_USERNAME` or `OCI_ATTR_PASSWORD`.
- **Wallet `TNS_ADMIN` Guard (`ScopedTnsAdmin`)**: Acquires a process mutex and sets `TNS_ADMIN` only when `wallet_path` is non-empty and differs from the current `TNS_ADMIN`, restoring the prior environment immediately after `OCISessionBegin`.
- **Per-`ATTACH` Settings (`src/oracle_settings.cpp`)**: `ApplyAttachOptions` records explicitly supplied option keys in `settings.attach_overridden_keys`, and `GetOracleSettings` preserves `state->settings` unless `lookup.GetScope() == SettingScope::LOCAL`.
