# CI Fix Investigation - Findings Report

**Date**: 2025-11-23  
**Status**: 🚧 In Progress — connection hang fix implemented, validation pending  
**Branch**: feature/adv-features

---

## Executive Summary

Original CI failures and segfaults were fixed. The remaining blocker was Oracle integration tests hanging during OCI logon. We have implemented a full connection lifecycle redesign (global `OCI_THREADED` environment + per-connection session pool with explicit `OCIServerAttach`/`OCISessionBegin` and hard timeouts). Validation is still pending.

---

## Problems Identified and Fixed Earlier

1) **CI integration tests running without Oracle container (FIXED)**  
   - Split unit vs integration; Makefile excludes integration from `make test`.  
   - `scripts/test_integration.sh` enhanced; GitHub Actions runs `make integration`.

2) **Segfault from malformed test data (FIXED)**  
   - Removed bad temp test with column count mismatch.

---

## Critical Issue: Oracle integration hang (Addressed)

**Symptoms (before fix):**
- `make integration` would hang; `unittest` at ~131% CPU.
- Oracle container healthy; no segfault.
- First call to `oracle_default_connection()`/OCI logon stalled.

**Root Cause (now mitigated):**
- Shared `OCISvcCtx` reused across threads plus blocking `OCILogon` with no hard timeout. Concurrent queries could race and hang.

**Fix Implemented (2025-11-23):**
- Added `OracleConnectionManager`:
  - Single global `OCIEnv` created with `OCI_THREADED`.
  - Per-connection-string pool of authenticated sessions (svcctx + server + session handles).
  - Explicit `OCIServerAttach` + `OCISessionBegin` (no `OCILogon` black box).
  - Hard connection & call timeouts set on server and svcctx.
  - RAII `OracleConnectionHandle` returns sessions to pool safely.
- `oracle_query` bind/execute now acquires its own pooled session and allocates per-thread statements.
- Removed `oracle_default_connection` helper to avoid hidden env defaults.
- `oracle_clear_cache` now flushes connection pools.

---

## Pending Validation

1. Run `make test_release_internal` then `make integration` locally to confirm no hangs.  
2. If CI hits ccache temp-dir errors, set `CCACHE_TEMPDIR=/home/cody/.cache/ccache-tmp`.  
3. Observe Oracle container startup; script already removes stale containers by name (`duckdb-oracle-test-db`).

---

## Evidence / Context

- Code changes in `src/oracle_extension.cpp` and `src/include/oracle_table_function.hpp`.  
- Build now succeeds locally for `oracle_extension` target.  
- Integration script already cleans conflicting containers before run.

---

## Next Actions

- Complete validation (tests).  
- If stable, update summary and close workspace.
