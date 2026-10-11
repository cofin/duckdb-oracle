---
type: Spec
flow_id: codebase-remediation-prd
title: "Master PRD: DuckDB-Oracle v1.5.6 Full Codebase & CI Remediation"
state: completed
plan_revision: 1
plan_commit: 4d4fd44274bda21b9de7aa3efad471dd23b1789d
state_revision: 2
current_task: null
last_operation: finish
operation_targets: []
last_verified_checkpoint: 2026-10-11T18:35:00Z
created_at: 2026-10-11T17:05:00Z
updated_at: 2026-10-11T18:35:00Z
description: Master roadmap remediating all 63 High, Medium, and Low findings identified during the comprehensive C++, OCI, storage, pushdown, CI/CD, and test suite audit on DuckDB v1.5.6.
tags: [bugfix, oracle-oci, duckdb-extension, ci-cd, storage]
parent_prd: null
research: []
---

# Master PRD: DuckDB-Oracle v1.5.6 Full Codebase & CI Remediation

**Flow ID:** `codebase-remediation-prd`

## Context & North Star Goal

Following the upgrade to DuckDB `v1.5.6` (`069cc9f9b5be802405797faecc284961b07c70ef`, extension version `v0.2.3`, PR #35), a six-agent whole-codebase correctness, quality, security, and CI/CD review audited every C++ translation unit, header, storage/catalog integration, OCI call site, GitHub Actions workflow, build/setup script, documentation page, and sqllogictest file in `cofin/duckdb-oracle`.

This Master PRD groups all **63 verified findings** (**24 High**, **28 Medium**, **11 Low**) into **5 granular, non-overlapping Flows (Chapters)** and **23 implementation-ready Task Worksheets**. Every finding is traced to a specific chapter, task ID, file:line range, concrete remediation, and verification strategy.

## Global Constraints

1. **Zero In-Line Comments & Clean C++ Organization**: Adhere strictly to repository C++17 and DuckDB extension conventions; document invariants and non-obvious OCI contracts accurately without restatement comments.
2. **Disjoint File Ownership**: Every task across all 5 child flows owns a strictly disjoint set of repository files and fits within a single executor invocation and a single signed conventional commit (`<type>(<scope>): <summary>`).
3. **OCI Resource Safety**: All OCI handles (`OCIEnv`, `OCIServer`, `OCISvcCtx`, `OCISession`, `OCIError`, `OCIStmt`) and descriptors (`OCIParam`) must be managed by RAII wrappers (`std::shared_ptr` / `std::unique_ptr` with custom deleters) in strict reverse-allocation destruction order.
4. **Inner-Loop vs Milestone Verification**: Individual task worksheets run fast, targeted checks (`make lint`, `make test-unit`, or `bash -n`), reserving full containerized Oracle verification (`make integration`) for chapter milestone gates and flow completion.

## Roadmap (Ordered Chapters)

1. **[`oci-connection-lifecycle`](../oci-connection-lifecycle/spec.md)** — *Chapter 1: OCI Connection Pool, Environment Lifetime, Authentication & Settings Resolution* (`5 tasks`: `1.1`–`1.5`)
   - Remediates `OCIEnv` static destruction use-after-free, `ScopedTnsAdmin` global mutex/env thread-safety hazards, pooled session health checks (`OCIPing`) and transaction rollback, `@` in passwords, SEPS wallet (`/@alias`) & TNS alias secrets, per-`ATTACH` option preservation in `GetOracleSettings`, and `OracleConnection::Query` double execution & silent 4095-byte truncation.
2. **[`scan-pushdown-types`](../scan-pushdown-types/spec.md)** — *Chapter 2: Vectorized Scan, Filter/Projection Pushdown, Type Conversion & Scalar Functions* (`5 tasks`: `1.1`–`1.5`)
   - Remediates `VECTOR_SERIALIZE(... RETURNING CLOB)`, 16KB/64KB scan buffer sizing, positive indicator (`> 0`) truncation detection vs `ORA-24347` warnings, projection pushdown in `OracleInitGlobal`, `OraclePushdownComplexFilter` column mapping & type preservation, Oracle `''` NULL pushdown semantics, `NaN`/`Inf` & `infinity`/BC literals, `LIKE` pushdown for `oracle_scan`/`oracle_query`, `BLOB` `AddStringOrBlob` debug crash, `TIMESTAMP WITH TIME ZONE` session format & UTC conversion, and scalar function multi-row `args.size()` & `FunctionStability::VOLATILE`.
3. **[`storage-catalog-write-pipeline`](../storage-catalog-write-pipeline/spec.md)** — *Chapter 3: Storage Extension, Catalog Isolation, Metadata Queries, Transactions & Write Pipeline* (`5 tasks`: `1.1`–`1.5`)
   - Remediates the shared singleton `OracleStorageInfo::state` corrupting multi-`ATTACH` isolation, `OracleTransactionManager::active` data race, `OracleCatalogState::connection` self-deadlock when `oracle_connection_limit = 1`, `OracleCatalog`/`OracleSchemaEntry` `DuckCatalog` assertion/DDL overrides, `OracleCatalog::PlanInsert` cross-schema & synonym routing, `OracleTableEntry` redundant partition queries & `original_types` restoration, `ub2` (`> 65,535` bytes) silent write truncation, and `DATE` `SQLT_ODT` write binding (`ORA-01830`).
4. **[`ci-build-release-hardening`](../ci-build-release-hardening/spec.md)** — *Chapter 4: GitHub Actions Workflows, Build System, OCI Setup Scripts & Licensing* (`4 tasks`: `1.1`–`1.4`)
   - Remediates `duckdb-update-check.yml` closing its own open PR daily (PRs #31–#35), `release-unsigned.yml` publishing partial releases and masking smoke-test crashes, `integration-tests.yml` stale `instantclient_23_6` env & double `make release`, GitHub Actions `${{ ... }}` script injection, `$GITHUB_ENV` literal `\$LD_LIBRARY_PATH` bug, macOS OCI version/mount cleanup, `extension_config.cmake` `TEST_DIR`, `Makefile` `ensure_libaio` `sudo`/debug targets, and `LICENSE` vs `description.yml` alignment.
5. **[`test-suite-docs-alignment`](../test-suite-docs-alignment/spec.md)** — *Chapter 5: Test Suite De-duplication, Spatial/Integration Coverage & Documentation/OKF Alignment* (`4 tasks`: `1.1`–`1.4`)
   - Remediates duplicate unit and integration `.test` files, stale `advanced_*.test` references, `manual_verification.md` inside the unittest glob, hardcoded credentials and missing cleanup in integration tests, disabled `SDO_GEOMETRY` integration test (`test_write_spatial.test.disabled`), and documentation/OKF knowledge drift across `README.md`, `CHANGELOG.md`, `CONTRIBUTING.md`, `SECURITY.md`, `docs/RELEASE.md`, and `.agents/bundles/`.

---

## Complete Finding-to-Chapter/Task Traceability Matrix

### High Severity Findings (`H-01` – `H-24`)

| ID | Severity | Location | Summary | Chapter | Task |
| --- | --- | --- | --- | --- | --- |
| `H-01` | High | `src/oracle_connection_manager.cpp:58-85,110-127`, `src/include/oracle_connection_manager.hpp:18-28,95` | `OCIEnv` use-after-free on shutdown when checked-out `OracleContext` outlives `OracleConnectionManager`; missing `OCI_OBJECT` flag | `oci-connection-lifecycle` | `1.4` |
| `H-02` | High | `src/oracle_connection_manager.cpp:11-41,266-334` | `ScopedTnsAdmin` holds global `WalletEnvLock()` across network handshakes even when `wallet_path` is empty, mutates `environ` unnecessarily, and unsets empty `TNS_ADMIN=""` | `oci-connection-lifecycle` | `1.4` |
| `H-03` | High | `src/oracle_connection_manager.cpp:92-108,214-218`, `src/include/oracle_connection_manager.hpp:67-69` | Pooled connections lack health checks (`OCI_ATTR_SERVER_STATUS` / `OCIPing`) on checkout, never roll back uncommitted transactions on return, and `MarkUnusable()` is dead code | `oci-connection-lifecycle` | `1.4` |
| `H-04` | High | `src/oracle_connection_manager.cpp:45-56,297-306`, `src/oracle_connection_resolver.cpp:11-16`, `src/oracle_secret.cpp:82-148` | Connection string parsing breaks on `@` in passwords (`find('@')` vs `rfind('@')`), rejects SEPS wallet auth (`/@alias` with `OCI_CRED_EXT`), and rewrites wallet TNS aliases to `@localhost:1521/<alias>` | `oci-connection-lifecycle` | `1.2`, `1.3`, `1.4` |
| `H-05` | High | `src/oracle_connection_resolver.cpp:20-89`, `src/oracle_settings.cpp:51-81`, `src/storage/oracle_storage_extension.cpp:87-90` | `GetOracleSettings` unconditionally overwrites all per-`ATTACH` options with global defaults because `AddExtensionOption` populates `db_config.user_settings` at load time | `oci-connection-lifecycle` | `1.2` |
| `H-06` | High | `src/oracle_connection.cpp:28-184` | `OracleConnection::Query` and `QueryWithStringBinds` silently truncate columns `> 4095` bytes, execute every query twice (`OCI_DESCRIBE_ONLY` + `OCI_DEFAULT`), leave `col_name` uninitialized, and duplicate 80 lines | `oci-connection-lifecycle` | `1.5` |
| `H-07` | High | `src/oracle_type_registry.cpp:138-139`, `src/oracle_scan.cpp:155-177,341-350` | `VECTOR_SERIALIZE` omits `RETURNING CLOB` (failing with `ORA-51814`/`ORA-40478` on `>=350`-dim vectors) and `oracle_scan.cpp` caps `VECTOR`/`SPATIAL`/`XML`/`CLOB`/`BLOB` buffers at 16,000 bytes | `scan-pushdown-types` | `1.1`, `1.4` |
| `H-08` | High | `src/oracle_scan.cpp:427-468` | `OracleQueryFunction` misses positive truncation indicators (`sb2 > 0` when length `<= 32767`) on final batch and prematurely throws on `OCI_SUCCESS_WITH_INFO` (crashing `ORA-24347` aggregate warnings and hiding column names) | `scan-pushdown-types` | `1.4` |
| `H-09` | High | `src/oracle_pushdown.cpp:483-523`, `src/oracle_scan.cpp:244-395` | Projection pushdown never runs in standard DuckDB execution because `FILTER_PUSHDOWN` precedes `RemoveUnusedColumns` and `OracleInitGlobal` ignores `input.column_ids` when preparing SQL and buffers | `scan-pushdown-types` | `1.3`, `1.4` |
| `H-10` | High | `src/oracle_pushdown.cpp:483-522`, `src/oracle_scan.cpp:244-273,449-459` | Non-empty `get.projection_ids` indexes `bind.original_names` with `column_ids` offsets, shrinks `bind.column_names` out of sync with `column_mapping` (leaving output vectors uninitialized), and mutates `get.returned_types` | `scan-pushdown-types` | `1.3`, `1.4` |
| `H-11` | High | `src/oracle_pushdown.cpp:36-39,240-248,294-299,357-360` | Pushing empty string literal `''` to Oracle silently changes semantics because `''` is `NULL` in Oracle SQL (`WHERE col <> ''` returns 0 rows) | `scan-pushdown-types` | `1.3` |
| `H-12` | High | `src/oracle_pushdown.cpp:55-59` | `ConstantToSQL` emits unquoted `nan`, `inf`, `-inf` for non-finite `FLOAT`/`DOUBLE`, which Oracle parses as column identifiers (`ORA-00904` or silent column comparison) | `scan-pushdown-types` | `1.3` |
| `H-13` | High | `src/oracle_pushdown.cpp:138-144,180`, `src/oracle_scan.cpp:171-200,223` | `CanPushdownLike` rejects `"OCI CHARACTER"` (disabling `LIKE` pushdown on `oracle_scan`/`oracle_query`), and `TO_CHAR` wrapper in `OracleBindInternal` breaks outer `DATE`/`TIMESTAMP` filter pushdown (`ORA-01843`/`ORA-01861`) | `scan-pushdown-types` | `1.1`, `1.3`, `1.4` |
| `H-14` | High | `src/oracle_type_conversion.cpp:19-22,155-156` | `SetStringLikeValue` calls `StringVector::AddString` instead of `AddStringOrBlob` for `LogicalTypeId::BLOB`, triggering `D_ASSERT` crash and UTF-8 check failure in debug builds on `RAW`/`BLOB` | `scan-pushdown-types` | `1.2` |
| `H-15` | High | `src/oracle_type_conversion.cpp:39-47`, `src/oracle_type_registry.cpp:241-243`, `src/oracle_connection_manager.cpp:314-315` | `TIMESTAMP WITH TIME ZONE` / `LOCAL TIME ZONE` fails at runtime (`NLS_TIMESTAMP_TZ_FORMAT` unset) or silently drops UTC offsets (`use_offset = false` and mapped to naive `TIMESTAMP`) | `scan-pushdown-types` | `1.1`, `1.2` |
| `H-16` | High | `src/oracle_type_registry.cpp:25-39,233-238,265-296` | Unconstrained `NUMBER(*, 0)` maps to `DOUBLE` in `ResolveMetadata` vs `BIGINT` in `ResolveOciDescribe` (overflowing `stoll` on `>19` digits), and `ResolveOciDescribe` ignores `OCI_ATTR_VECTOR_DATA_FORMAT` | `scan-pushdown-types` | `1.1`, `1.4` |
| `H-17` | High | `src/oracle_functions.cpp:19-120,156-200` | Scalar functions (`oracle_execute`, `oracle_clear_cache`, `oracle_debug_*`) only process row `0` (`args.size()` ignored) and default to `FunctionStability::CONSISTENT` instead of `VOLATILE` | `scan-pushdown-types` | `1.5` |
| `H-18` | High | `src/storage/oracle_storage_extension.cpp:77-122`, `src/include/oracle_storage_extension.hpp:8-10` | Shared `OracleStorageInfo::state` across all `ATTACH` calls overwrites previous catalog state, skips alias registration on subsequent attaches, and leaks state across `DETACH` | `storage-catalog-write-pipeline` | `1.1` |
| `H-19` | High | `src/storage/oracle_schema_entry.cpp:165-193`, `src/storage/oracle_insert.cpp:41-66`, `src/storage/oracle_write.cpp:30-75,366-389` | `OracleCatalog::PlanInsert` drops target schema name and synonym target (`INSERT INTO ora.other_schema.tbl` writes to `CURRENT_SCHEMA` or fails with `ORA-00942`), and `PhysicalOracleInsert` splits `table_name` on `.` | `storage-catalog-write-pipeline` | `1.3`, `1.5` |
| `H-20` | High | `src/include/oracle_transaction_manager.hpp:20-23`, `src/storage/oracle_transaction_manager.cpp:11-47` | Unsynchronized `OracleTransactionManager::active` vector causes data races and memory corruption under concurrent queries | `storage-catalog-write-pipeline` | `1.1` |
| `H-21` | High | `src/storage/oracle_schema_entry.cpp:30-36,75-83,132-207`, `src/storage/oracle_storage_extension.cpp:109-112` | `OracleCatalog` (`IsDuckCatalog() == false`) + `DuckSchemaEntry` triggers `D_ASSERT(catalog_p.IsDuckCatalog())` abort in debug builds and throws `InternalException` on `duckdb_databases()`, `PRAGMA database_size`, and DDL | `storage-catalog-write-pipeline` | `1.3` |
| `H-22` | High | `src/storage/oracle_table_entry.cpp:147-236`, `src/include/oracle_table_entry.hpp:19-30` | `OracleTableEntry::GetScanFunction` fails to restore `oracle_bind.original_types` after `OracleBindInternal` re-describes rewritten expressions, and `OracleTableEntry::Create` runs 5 unused partition queries per table lookup | `storage-catalog-write-pipeline` | `1.4` |
| `H-23` | High | `src/storage/oracle_write.cpp:480-501,538-550,630-639`, `src/include/oracle_write.hpp:65`, `src/oracle_type_registry.cpp:328-336` | Silent `ub2` (`uint16_t`) wrap-around truncation when writing values `> 65,535` bytes, and `LogicalType::TIMESTAMP` bound via `TO_DATE(:1, 'YYYY-MM-DD HH24:MI:SS')` for Oracle `DATE` columns failing with `ORA-01830` on fractional seconds | `storage-catalog-write-pipeline` | `1.5`, `scan-pushdown-types:1.1` |
| `H-24` | High | `src/storage/oracle_catalog.cpp:64-72`, `src/include/oracle_catalog_state.hpp:90-92`, `.github/workflows/duckdb-update-check.yml:114-129`, `.github/workflows/release-unsigned.yml:177-185,287-312`, `.github/workflows/integration-tests.yml:18-21`, `LICENSE`, `description.yml:7` | `OracleCatalogState::connection` pins pool slot causing 10s self-deadlock when `oracle_connection_limit = 1`; CI `duckdb-update-check.yml` closes active PR daily; `release-unsigned.yml` allows partial releases & false-positive smoke tests; `integration-tests.yml` hardcodes `instantclient_23_6`; `LICENSE` (`MIT`) contradicts `description.yml` (`Apache-2.0`) | `storage-catalog-write-pipeline`, `ci-build-release-hardening` | `storage-catalog-write-pipeline:1.2`, `ci-build-release-hardening:1.1`–`1.4` |

### Medium Severity Findings (`M-01` – `M-28`)

| ID | Severity | Location | Summary | Chapter | Task |
| --- | --- | --- | --- | --- | --- |
| `M-01` | Medium | `src/include/oracle_utils.hpp:62-85` | `CheckOCIError` and `OCIErrorCodeMatches` leave `text errbuf[512]` uninitialized on stack and ignore `OCIErrorGet` return status / `OCI_INVALID_HANDLE` | `oci-connection-lifecycle` | `1.1` |
| `M-02` | Medium | `src/oracle_connection_manager.cpp:276-285,311-331`, `src/include/oracle_settings.hpp:19-39`, `src/oracle_scan.cpp:84-87,304-307` | Hardcoded 30s `OCI_ATTR_CALL_TIMEOUT` (no setting), hardcoded `stmt_cache_size = 0`, missing `CheckOCIOptionalAttributeORA24315` on `svchp`, and missing `NLS_NUMERIC_CHARACTERS = '.,'` + `NLS_TIMESTAMP_TZ_FORMAT` session init | `oci-connection-lifecycle` | `1.2`, `1.4` |
| `M-03` | Medium | `src/oracle_settings.cpp:8-15,51-81`, `src/oracle_connection_resolver.cpp:30-63` | `UBIGINT` extension options lack range validation before casting to `int64_t` (`UINT64 -> INT64` overflow when `> INT64_MAX`) | `oci-connection-lifecycle` | `1.2` |
| `M-04` | Medium | `src/oracle_connection_manager.cpp:106,168-254` | `OracleConnectionManager::Acquire` outer loop omits deadline check on `pool->stale` retry and `~OracleConnectionHandle` calls `notify_one()` while holding `pool->lock` | `oci-connection-lifecycle` | `1.4` |
| `M-05` | Medium | `src/oracle_connection.cpp:117-125` | `QueryWithStringBinds` binds empty strings with `bind_indicator = 0` and `size = 0` instead of `OCI_IND_NULL` (`-1`) | `oci-connection-lifecycle` | `1.5` |
| `M-06` | Medium | `src/oracle_secret.cpp:21-148` | `ValidateOracleSecret` / `BuildConnectionStringFromSecret` do not reject empty `host`, `'/'` in `user`, or negative deserialized `port` before `idx_t` cast | `oci-connection-lifecycle` | `1.3` |
| `M-07` | Medium | `src/oracle_connection_resolver.cpp:73-91`, `src/oracle_scan.cpp:61,120` | `ResolveOracleConnection` leaves `resolved.catalog_state` null when `state_hint` is passed, and `OracleBindInternal` ignores `resolved.catalog_state` on attached alias resolution | `oci-connection-lifecycle`, `scan-pushdown-types` | `oci-connection-lifecycle:1.2`, `scan-pushdown-types:1.4` |
| `M-08` | Medium | `src/oracle_connection_manager.cpp:264,297-306`, `src/include/oracle_utils.hpp` | Plaintext password remains in local `std::string password` buffer after `OCISessionBegin` and lacks a connection-string credential redaction helper | `oci-connection-lifecycle` | `1.1`, `1.4` |
| `M-09` | Medium | `src/include/oracle_table_function.hpp:30-53` | `OracleScanState` declares `buffers`, `indicators`, and `return_lens` after `stmt`, so `~OracleScanState` frees define buffers before `OCIHandleFree(stmt, OCI_HTYPE_STMT)` | `scan-pushdown-types` | `1.4` |
| `M-10` | Medium | `src/oracle_scan.cpp:148-158,433` | `OCI_ATTR_CHAR_SIZE` (`ub2`) is read into `ub4`, `OCI_ATTR_DATA_SIZE` is ignored for `RAW(n)` columns, and `OCIAttrGet(OCI_ATTR_ROWS_FETCHED)` is unchecked | `scan-pushdown-types` | `1.4` |
| `M-11` | Medium | `src/ oracle_scan.cpp:196-200,212-242`, `src/oracle_pushdown.cpp:173-181` | `OracleScanBind` / `OracleQueryBind` lack NULL/empty input checks, and wrapping queries with trailing `;` or `--` single-line comments in `FROM (...)` without a newline causes `ORA-00911` / `ORA-00907` | `scan-pushdown-types` | `1.3`, `1.4` |
| `M-12` | Medium | `src/oracle_pushdown.cpp:40-46,63-68` | `ConstantToSQL` throws or emits invalid Oracle `DATE`/`TIMESTAMP` literals for `+/-infinity` and BC (`year <= 0` or `year > 9999`) | `scan-pushdown-types` | `1.3` |
| `M-13` | Medium | `src/oracle_type_registry.cpp:84-87,245-247,260-264` | Fixed-length `CHAR(n)` / `NCHAR(n)` (`SQLT_AFC`) columns are marked `pushdown_eligible = true`, causing blank-padded comparison mismatches between Oracle and DuckDB `VARCHAR` | `scan-pushdown-types` | `1.1` |
| `M-14` | Medium | `src/oracle_pushdown.cpp:63-68,215-310` | Pushing `TIMESTAMP '...'` literals against Oracle `DATE` columns forces `INTERNAL_FUNCTION("COL")` in Oracle and disables B-tree index range scans | `scan-pushdown-types` | `1.3` |
| `M-15` | Medium | `src/oracle_type_conversion.cpp:13-16,49-59,97-145` | `ParseVectorElement` validates `std::isfinite(double)` before `float` cast (allowing overflow to `+Inf`/`-Inf`), `std::stod` is locale-dependent, and `ReadOracleString` truncates at embedded NUL bytes (`\0`) | `scan-pushdown-types` | `1.2` |
| `M-16` | Medium | `src/oracle_type_registry.cpp:230-232,298-303` | `ResolveMetadata` and `ResolveOciDescribe` map `BINARY_FLOAT` (`SQLT_BFLOAT`/`SQLT_IBFLOAT`) to `LogicalType::DOUBLE` instead of `LogicalType::FLOAT` | `scan-pushdown-types` | `1.1` |
| `M-17` | Medium | `src/oracle_functions.cpp:19-97` | `OracleExecuteFunction` does not guard against `SELECT` statements (`ORA-24374`), uses 32-bit `OCI_ATTR_ROW_COUNT` instead of 64-bit `OCI_ATTR_UB8_ROW_COUNT`, allows writes inside explicit DuckDB transactions, and skips `ClearCaches()` after DDL | `scan-pushdown-types` | `1.5` |
| `M-18` | Medium | `src/storage/oracle_catalog.cpp:286-337,405-424`, `src/storage/oracle_table_entry.cpp:48-171` | `ListObjects` duplicates `MATERIALIZED VIEW` rows (missing `DISTINCT`) and fails on empty/lowercase `oracle_metadata_object_types`; `ResolveSynonym` ignores `db_link IS NULL` and non-table targets; `OracleTableEntry::Create` builds 0-column entries or aborts `SHOW TABLES` on unsupported columns | `storage-catalog-write-pipeline` | `1.2`, `1.4` |
| `M-19` | Medium | `src/storage/oracle_catalog.cpp:307-310,345-395,409-459`, `src/storage/oracle_table_entry.cpp:19-55` | Inconsistent identifier casing and SQL string concatenation vs bind variables across `GetObjectName`, `GetRealSchemaName`, `LoadPartitionMetadata`, `ListObjects`, `ResolveSynonym`, `LoadColumns`, and `LoadSpatialSRIDs` | `storage-catalog-write-pipeline` | `1.2`, `1.4` |
| `M-20` | Medium | `src/include/oracle_catalog_state.hpp:60-73`, `src/storage/oracle_catalog.cpp:119-129,170-248` | `OracleCatalogState::GetCurrentSchema()` and `GetVersionInfo()` read `current_schema` and `version_info` without locking `this->lock`, racing with `ClearCaches()` | `storage-catalog-write-pipeline` | `1.2` |
| `M-21` | Medium | `src/storage/oracle_write.cpp:473-501,571-642`, `src/storage/oracle_insert.cpp:72-81` | `OracleWriteGlobalState::Sink` and `BindColumn` run `OracleWriteValueToString` twice per row, call `GetValue` on flattened numeric vectors, and increment `OracleInsertGlobalState::insert_count` non-atomically | `storage-catalog-write-pipeline` | `1.5` |
| `M-22` | Medium | `src/storage/oracle_table_entry.cpp:48-115`, `src/storage/oracle_write.cpp:30-157` | `LoadColumns` and `ResolveOracleWriteTargetMetadata` read `ALL_TAB_COLUMNS.DATA_TYPE` (`"VECTOR"`) without enriching dimension and format (`FLOAT32`/`FLOAT64`/`INT8`/`BINARY`) from `ALL_VEC_COLUMNS` | `storage-catalog-write-pipeline` | `1.4`, `1.5` |
| `M-23` | Medium | `.github/workflows/release-unsigned.yml:52-76,130-134,189,268,316-362`, `.github/workflows/auto-tag.yml:37-48`, `.github/workflows/duckdb-update-check.yml:45-98,160-181` | GitHub Actions `${{ ... }}` script injection in `run:` blocks, unquoted `"$GITHUB_OUTPUT"`, missing `INPUT_CHECK_PRERELEASE` env binding, `feat:` vs `chore(deps):` commit prefix, unanchored tag grep in `auto-tag.yml`, and missing target tag checkout / version verification in `release-unsigned.yml` | `ci-build-release-hardening` | `1.1`, `1.2` |
| `M-24` | Medium | `.github/workflows/release-unsigned.yml:160-170,260,365`, `.github/workflows/integration-tests.yml:1-15`, `.github/workflows/main-distribution-pipeline.yml:1-18` | Deprecated Node 16 actions (`peaceiris/actions-gh-pages@v3`, `softprops/action-gh-release@v1`), missing `permissions: contents: read`, and fragile Windows `link.exe` rename / VS 2022 Enterprise path | `ci-build-release-hardening` | `1.2` |
| `M-25` | Medium | `scripts/setup_oci_linux.sh:34-66,160-169`, `scripts/setup_oci_macos.sh:6,35-72,98-107`, `scripts/setup_oci_windows.ps1:31-48` | Literal unexpanded `\$LD_LIBRARY_PATH` / `\$DYLD_LIBRARY_PATH` written to `$GITHUB_ENV`, `set -u` unbound variable failure, macOS `23.26.1.0.0` drift, missing `curl -f` & DMG unmount `trap`, and unversioned Linux fallback | `ci-build-release-hardening` | `1.3` |
| `M-26` | Medium | `extension_config.cmake:11-15`, `Makefile:33-53,70-80`, `scripts/test_integration.sh:115-128,265-288`, `scripts/bump_duckdb_version.sh:114-123,146` | `extension_config.cmake` omits `TEST_DIR` (defaulting to non-existent `test/sql`); `Makefile` `ensure_libaio` lacks `sudo` and debug test prerequisites; `test_integration.sh` runs tests in unsorted order and requires `nc`; `bump_duckdb_version.sh` uses non-portable `sed -i -E` | `ci-build-release-hardening` | `1.3`, `1.4` |
| `M-27` | Medium | `test/unit_tests/**`, `test/integration_tests/**` | Duplicate unit and integration `.test` files, references to 6 non-existent `advanced_*.test` files, `manual_verification.md` inside `test/unit_tests/*` glob, hardcoded credentials/missing cleanup in integration tests, and disabled 8-line stub `test_write_spatial.test.disabled` | `test-suite-docs-alignment` | `1.1`, `1.2` |
| `M-28` | Medium | `CHANGELOG.md`, `README.md`, `CONTRIBUTING.md`, `docs/RELEASE.md`, `test/unit_tests/manual_verification.md` | Documentation drift: fabricated `[1.0.0]` and removed `oracle_attach_wallet()` in `CHANGELOG.md`, false `ALL_TABLES` view claim in `README.md:135`, wrong repo URL and `instantclient_23_6` in `CONTRIBUTING.md`, and stale `v1.5.5`/`v0.2.2` references in `docs/RELEASE.md` | `test-suite-docs-alignment` | `1.3` |

### Low Severity Findings (`L-01` – `L-11`)

| ID | Severity | Location | Summary | Chapter | Task |
| --- | --- | --- | --- | --- | --- |
| `L-01` | Low | `src/include/oracle_utils.hpp:8-20` | Global namespace Windows `setenv`/`unsetenv` shims and `_putenv_s` empty-string unsetting behavior | `oci-connection-lifecycle` | `1.1` |
| `L-02` | Low | `src/oracle_debug_stats.cpp:68-110` | `OracleDebugGetCounter` copies full `OracleDebugStatsSnapshot` (including `std::string last_query`) under lock just to read one integer counter | `oci-connection-lifecycle` | `1.1` |
| `L-03` | Low | `src/oracle_secret.cpp:2,4`, `src/oracle_connection.cpp:3-5,186-200`, `src/oracle_connection_manager.cpp:3,5,333`, `src/include/oracle_connection_manager.hpp:24-25,88-90` | Unused header `#include` directives and dead `OracleConnection::Commit`/`Rollback`, `OracleContext::connected`/`owns_env`, `OracleConnectionManager::Env`, and `OracleSecretParameters::database` | `oci-connection-lifecycle` | `1.3`, `1.4`, `1.5` |
| `L-04` | Low | `src/oracle_scan.cpp:29-48,80,199-207,283-326,339-386`, `src/include/oracle_table_function.hpp:30-34` | Dead `OracleBindData::stmt` / `conn_handle` fields and `!need_reprepare` branch in `OracleInitGlobal`, duplicate `buffers[col_idx].resize(...)`, loop-invariant `fetch_size` assignment, and optional `as_of_scn` / `as_of_timestamp` Flashback parameter support | `scan-pushdown-types` | `1.4` |
| `L-05` | Low | `src/oracle_pushdown.cpp:387-426` | `TryExtractLike` pushes `col LIKE ''` when pattern is empty string, which evaluates to `NULL` (`UNKNOWN`) in Oracle | `scan-pushdown-types` | `1.3` |
| `L-06` | Low | `src/oracle_type_registry.cpp:64,79,92-160`, `src/include/oracle_type_registry.hpp:63`, `src/oracle_type_conversion.cpp:97-145`, `src/storage/oracle_write.cpp:78-97,232-306` | Dead `RAWTOHEX`/`TO_CHAR` LOB branches, unused `OracleTypeDecision::fetch_type`, unparenthesized `&&`/`||` in `IsTimestampTZType`/`IsTimestampLTZType`, and duplicated `VECTOR`/metadata integer parsing helpers | `scan-pushdown-types` | `1.1`, `1.2` |
| `L-07` | Low | `src/oracle_functions.cpp:47-84` | Unreachable `else` branch checking `if (stmthp)` after dereferencing `stmthp.get()` in `OracleExecuteFunction` | `scan-pushdown-types` | `1.5` |
| `L-08` | Low | `src/storage/oracle_storage_extension.cpp:17-18,34-41,68-75`, `src/storage/oracle_schema_entry.cpp:209-213` | Case-sensitive `secret`/`wallet_path` lookup in `OracleAttach`, silent ignore when both `info.path` and `SECRET` are given, dead `access_mode`/`info.path` stores, and redundant double `catalog->Initialize(false)` | `storage-catalog-write-pipeline` | `1.1`, `1.3` |
| `L-09` | Low | `src/storage/oracle_catalog.cpp:122,250-284`, `src/include/oracle_catalog_state.hpp:97`, `src/include/oracle_write.hpp:73-77`, `src/storage/oracle_write.cpp:443-454` | `OracleCatalogState::ListSchemas` omits `"main"` when `lazy_schema_loading = false` and `use_current_schema = true`; dead `OracleCatalogState::table_cache` and empty `OracleWriteLocalState` | `storage-catalog-write-pipeline` | `1.2`, `1.5` |
| `L-10` | Low | `CMakeLists.txt:1,21` | `cmake_minimum_required(VERSION 3.10)` triggers deprecation warnings on CMake 3.27+, and fallback `/usr/share/oracle/instantclient_23_26` is hardcoded to a single version | `ci-build-release-hardening` | `1.4` |
| `L-11` | Low | `test/README.md:1-12`, `SECURITY.md:16`, `.agents/bundles/knowledge/**`, `.agents/bundles/product/**` | `test/README.md` omits `make test-unit` and `make integration`; `SECURITY.md` omits reporting URL; OKF knowledge and product docs reference `v1.5.5`, non-existent test files, swapped `oracle_catalog.cpp`/`oracle_schema_entry.cpp` roles, and unimplemented `OCI_BATCH_ERRORS` | `test-suite-docs-alignment` | `1.1`, `1.3`, `1.4` |

## Continuity Snapshot

- **Active Flow / Lifecycle**: `codebase-remediation-prd` / `planned`
- **Current Task & Claimant**: `null`
- **Last Verified Checkpoint**: `null`
- **Decisions**:
  - Partitioned all 63 findings across 5 non-overlapping flows (`oci-connection-lifecycle`, `scan-pushdown-types`, `storage-catalog-write-pipeline`, `ci-build-release-hardening`, `test-suite-docs-alignment`) with strictly disjoint file ownership across all 23 tasks.
  - Preserved DuckDB `v1.5.6` (`069cc9f9b5be802405797faecc284961b07c70ef`) and extension `v0.2.3` on branch `feat/duckdb-v1.5.6`.
- **Recent Discoveries**:
  1. `GetOracleSettings` unconditionally overwrites per-`ATTACH` options because `DBConfig::AddExtensionOption` inserts default values into `db_config.user_settings` at extension load time.
  2. `OracleStorageInfo::state` caches the first attached database's `OracleCatalogState` globally on `StorageExtension`, corrupting subsequent `ATTACH` calls and leaking state across `DETACH`.
  3. `OraclePushdownComplexFilter` runs before `RemoveUnusedColumns`, and `OracleInitGlobal` ignores `input.column_ids`, causing every table scan to fetch all columns from Oracle.
  4. `VECTOR_SERIALIZE` without `RETURNING CLOB` defaults to `VARCHAR2(4000)` in Oracle 23ai, failing on `>= 350`-dimension vectors.
  5. `.github/workflows/duckdb-update-check.yml` closes all open `duckdb-update` PRs before checking if the open PR is already for `NEW_VERSION`, causing PRs #31–#35 to be recreated daily.
- **Blockers & Unblock Conditions**: `none`
- **Next Exact Step**: Approve the roadmap and execute Chapter 1 via `flow-implement oci-connection-lifecycle`.
- **Plan Identity**: `plan_revision: 1`, `plan_commit: null`
- **State Identity**: `state_revision: 0`, `last_operation: null`, `operation_targets: []`
- **Relevant Knowledge Paths**:
  - `.agents/bundles/knowledge/architecture/oracle-oci-subsystem.md`
  - `.agents/bundles/knowledge/architecture/duckdb-extension-architecture.md`
  - `.agents/bundles/knowledge/domains/oracle-oci.md`
  - `.agents/bundles/knowledge/domains/duckdb-extension.md`
  - `.agents/bundles/knowledge/patterns/oci-raii-handles.md`
  - `.agents/bundles/knowledge/patterns/oci-batch-operations.md`
  - `.agents/bundles/knowledge/patterns/duckdb-pushdown.md`
  - `.agents/bundles/knowledge/patterns/duckdb-write-pipeline.md`
  - `.agents/bundles/knowledge/standards/development-discipline.md`
  - `.agents/bundles/knowledge/standards/project-management.md`

## Verification Evidence

None yet (`state: planned`).
