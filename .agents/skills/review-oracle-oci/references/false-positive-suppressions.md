# Oracle OCI False-Positive Suppressions (`FP-OCI-01`..`FP-OCI-05`)

- **`FP-OCI-01` (DO NOT FLAG)**: Unconstrained `NUMBER` defaulting to `LogicalType::DOUBLE` in `OracleTypeRegistry::ResolveMetadata` and `ResolveOciDescribe` when `oracle_number_as` is `'DOUBLE'`.
  - *Rationale*: Oracle unconstrained `NUMBER` supports up to 38 digits with arbitrary scale that cannot fit a single fixed DuckDB `DECIMAL` type cleanly for all workloads. Mapping defaults to `DOUBLE` for compatibility and is configurable per session or per `ATTACH` via `oracle_number_as` (`'DOUBLE'`, `'VARCHAR'`, or `'DECIMAL(p,s)'`), while bounded `INTEGER`, `NUMBER(*, s)`, `NUMBER(p, -s)`, and `NUMBER(p, s > p)` map to exact `DECIMAL` within 38 digits.
- **`FP-OCI-02` (DO NOT FLAG)**: `65535`-byte (`NumericLimits<ub2>::Maximum()`) buffer cap on `OCIDefineByPos` and `OCIBindByPos` in `src/oracle_scan.cpp` and `src/storage/oracle_write.cpp`.
  - *Rationale*: OCI `OCIDefineByPos` / `OCIBindByPos` `rlenp` and `alenp` arrays use 2-byte `ub2` lengths; values exceeding `65535` bytes are detected deterministically via `ind == -2 || ind > 0` and reported with `IOException`.
- **`FP-OCI-03` (DO NOT FLAG)**: Ignoring `ORA-24315` in `CheckOCIOptionalAttributeORA24315` when setting `OCI_ATTR_CALL_TIMEOUT`.
  - *Rationale*: Oracle Instant Client 23.x rejects `OCI_ATTR_CALL_TIMEOUT` on certain handle states with `ORA-24315`; filtering only error code `24315` preserves compatibility while keeping all other OCI errors fatal.
- **`FP-OCI-04` (DO NOT FLAG)**: In-tree C ABI typedefs and `reinterpret_cast` function pointer assignments in `src/include/oracle_oci_api.hpp` and `src/oracle_oci_api.cpp`.
  - *Rationale*: Required by ADR-001 to load `libclntsh` / `oci.dll` via `dlsym` / `GetProcAddress` with zero compile-time Oracle SDK headers.
- **`FP-OCI-05` (DO NOT FLAG)**: `OCI_COMMIT_ON_SUCCESS` execution mode in `oracle_execute` (`src/oracle_functions.cpp`).
  - *Rationale*: `oracle_execute` is an explicit standalone DDL/DML action that guards against active DuckDB transactions and commits immediately on Oracle.
