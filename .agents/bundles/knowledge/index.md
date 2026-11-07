# Knowledge Base

Persistent, evidence-backed engineering knowledge for `duckdb-oracle`.

## Topic Index

### Workflow
* [Project Workflow](workflow.md) - Canonical repository commands, direct Markdown + Git continuity, and verification strategies.

### Architecture
* [Oracle OCI Subsystem Architecture](architecture/oracle-oci-subsystem.md) - Runtime topology, handle ownership hierarchy, connection caching, and lazy Instant Client loading.
* [DuckDB Extension Architecture & Subsystems](architecture/duckdb-extension-architecture.md) - Extension lifecycle, catalog integration, storage extension hooks, and secret management.

### Domains
* [Oracle OCI Domain Concepts & Execution Lifecycle](domains/oracle-oci.md) - OCI statement execution lifecycle, parameter binding, indicator variables, SQLT external data types, and error handling.
* [DuckDB 2.0 Vectorized Engine, Type System & Extension APIs](domains/duckdb-extension.md) - DuckDB v2.0.0 vectorized execution primitives, VectorBuffer size synchronization, const/mutable Vector access, Identifier/Expression APIs, and TableFunction optimizer hooks.
* [DuckDB Community Extensions Registry, Multi-Version Distribution & Invariants](domains/community-extensions.md) - Architecture of the official DuckDB community extensions registry, multi-track version routing (`ref` vs `variegata`), zero-compile-time-dependency packaging, and compatibility matrix mechanics.

### Patterns
* [OCI RAII Handle & Descriptor Management](patterns/oci-raii-handles.md) - Smart pointer deleters and RAII wrappers for OCI handles and descriptors preventing resource leaks in C++.
* [OCI Batch Operations: Array Fetch & Array Bind](patterns/oci-batch-operations.md) - High-throughput batch data transfer between DuckDB vectors and Oracle OCI using array fetching and bulk array binding.
* [DuckDB Filter & Projection Pushdown Optimization](patterns/duckdb-pushdown.md) - Translating DuckDB TableFilterSet expressions and column projections into Oracle SQL WHERE clauses and projection lists.
* [DuckDB Write Pipeline: Sink, Combine & Finalize](patterns/duckdb-write-pipeline.md) - Implementation of DuckDB's PhysicalOperator Sink pattern for high-speed writes into Oracle tables via PhysicalOracleInsert.

### Standards
* [Project Management & Flow OKF v0.2 Lifecycle](standards/project-management.md) - Spec-first chaptered planning, OKF v0.2 bundle architecture, authoritative task worksheets, and direct Markdown + Git continuity.
* [Engineering Discipline & Verification Standards](standards/development-discipline.md) - Public-seam vertical-slice TDD, hypothesis-driven bug diagnosis, signed conventional commits, and zero-slop code standards.

### Testing
* [DuckDB Test Harness Architecture & Suites](testing/duckdb-test-harness.md) - DuckDB testing framework, sqllogictest syntax, C++ unit test runner, and containerized Oracle integration tests.

### Gotchas
* [Oracle OCI Edge Cases, Compatibility & Pitfalls](gotchas/oracle-oci.md) - Verified edge cases in Oracle Call Interface development, Instant Client quirks, data type precision traps, and timeout behaviors.

### Decisions (ADRs)
* [ADR-001: In-Tree OCI C ABI Declarations & Lazy Runtime Dynamic Loading](decisions/adr-001-in-tree-oci-c-abi-lazy-loading.md) - Replacing compile-time `<oci.h>` and `DT_NEEDED` `libclntsh` linkage with in-tree C ABI declarations and on-demand `dlopen`/`LoadLibraryA` loading.
