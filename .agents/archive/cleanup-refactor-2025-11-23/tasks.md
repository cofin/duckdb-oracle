# Tasks: Cleanup and Refactor

## Phase 1: Integration Script
- [x] Modify `scripts/test_integration.sh` to implement log suppression.
- [x] Verify script works locally with and without `--show-logs`.

## Phase 2: Test Renaming
- [x] Rename files in `test/integration/oracle/`.
- [x] Verify `make integration` still finds and runs them.

## Phase 3: Code Cleanup
- [x] Scan `src/oracle_extension.cpp` for unused code.
- [x] Scan `src/oracle_connection_manager.cpp`.
- [x] Scan `src/storage/` files.
- [x] Remove identified dead code.
- [x] Run `make format`.

## Phase 4: Verification
- [x] Run `make test` (Unit).
- [x] Run `make integration` (Integration).
