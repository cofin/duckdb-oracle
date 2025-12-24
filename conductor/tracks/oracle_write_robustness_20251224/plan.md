# Plan: Enhance stability and robustness of Oracle Write support (INSERT/COPY)

## Phase 1: Research and Infrastructure for Testing
- [ ] Task: Research OCI batch error reporting (`OCI_BATCH_ERRORS`) and how to simulate failures.
- [ ] Task: Create a base integration test suite specifically for write operations.
    - [ ] Sub-task: Write Tests: Create a `.test` file that attempts to write various types to an Oracle container.
    - [ ] Sub-task: Implement Feature: Ensure the test infrastructure can spin up an Oracle container and execute the tests.
- [ ] Task: Conductor - User Manual Verification 'Phase 1: Research and Infrastructure for Testing' (Protocol in workflow.md)

## Phase 2: Enhanced Error Handling for Batch Operations
- [ ] Task: Implement granular error reporting for `OCIStmtExecute` during batch writes.
    - [ ] Sub-task: Write Tests: Create tests that trigger specific Oracle constraints (e.g., unique key violation, null constraint) during a batch insert.
    - [ ] Sub-task: Implement Feature: Update `oracle_write.cpp` to use `OCIErrorGet` and iterate through batch errors if `iters > 1`.
- [ ] Task: Implement RAII wrappers for write-specific OCI handles and buffers.
    - [ ] Sub-task: Write Tests: Add leak detection tests (e.g., valgrind or address sanitizer) during write loops.
    - [ ] Sub-task: Implement Feature: Refactor buffer management in `OracleWrite` to use smart pointers or RAII structs.
- [ ] Task: Conductor - User Manual Verification 'Phase 2: Enhanced Error Handling for Batch Operations' (Protocol in workflow.md)

## Phase 3: Robustness and Performance for Large Writes
- [ ] Task: Optimize buffer allocation and array sizes for large migrations.
    - [ ] Sub-task: Write Tests: Create a test that migrates 100k+ rows from a Parquet file to Oracle.
    - [ ] Sub-task: Implement Feature: Implement adaptive array sizing based on row width and `oracle_array_size` setting.
- [ ] Task: Ensure correct handling of `NULL` values in batch binds.
    - [ ] Sub-task: Write Tests: Add test cases with alternating NULL and non-NULL values across all supported types.
    - [ ] Sub-task: Implement Feature: Verify and fix indicator variable handling in `OCIBindArrayOfStruct`.
- [ ] Task: Conductor - User Manual Verification 'Phase 3: Robustness and Performance for Large Writes' (Protocol in workflow.md)

## Phase 4: Final Validation and Quality Gate
- [ ] Task: Run full integration suite and verify >80% coverage for `src/storage/oracle_write.cpp`.
- [ ] Task: Perform manual verification of large data migration from local CSV to Oracle.
- [ ] Task: Conductor - User Manual Verification 'Phase 4: Final Validation and Quality Gate' (Protocol in workflow.md)
