# Recovery Plan

If the session is interrupted, resume by:

1. Check `specs/active/oracle-attach-support/tasks.md` for current status (Phase 5 pending live Oracle checks).
2. Code is implemented in `src/` (`oracle_catalog.cpp`, `oracle_storage_extension.cpp`,
   `oracle_connection.cpp`, `oracle_transaction*.cpp`, etc.).
3. Build now succeeds. `make format` and `make test` pass locally (tests cover OCI-missing/negative paths).
4. Next steps: run attach/query against a real Oracle instance and watch for handle leaks; update tasks once verified.
