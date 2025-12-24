# Initial Concept
A DuckDB extension that enables high-performance read/write access to Oracle databases using the Oracle Call Interface (OCI).

# Product Guide

## Target Audience
- Data Engineers and DBAs needing to move data between Oracle and modern data stacks.
- Data Scientists who want to query Oracle data directly from Python/R via DuckDB.
- Application Developers building lightweight apps that need occasional access to legacy Oracle systems.

## Primary Goals & Benefits
- **High-Performance Data Transfer:** Leverages OCI batch processing (Array Fetch and Array Bind) for high-throughput data movement.
- **Deep Integration:** Seamlessly integrates with DuckDB's core features, including the Secrets Manager, filter/projection pushdown, and native SQL dialect.
- **Advanced Type Support:** Provides robust support for specialized Oracle data types, including Spatial (SDO_GEOMETRY), Vector (23ai), and complex object types.
- **Data Lake Synergy:** Enables high-performance connectivity between Oracle and data lake components, leveraging Arrow-based data transfer where applicable.

## Core Features
- **Read/Write Capability:** Full support for `SELECT`, `INSERT`, and `COPY` operations between DuckDB and Oracle.
- **Intelligent Catalog:** Features automatic schema detection, resolution of synonyms, and lazy loading of metadata for responsiveness in large databases.
- **Advanced Mapping:** Precise mapping of Oracle types to DuckDB types, such as Oracle Vector to `LIST(FLOAT)` and `SDO_GEOMETRY` to `GEOMETRY` or WKT.
- **Production-Grade Focus:** A commitment to extreme stability and feature completeness, ensuring the extension is reliable for critical workloads.

## Design Principles
- **Intuitive Configuration:** Features requiring configuration should feel intuitive and native to either DuckDB or Oracle, depending on the context.
- **Zero-Config Experience:** Aim for an "it just works" experience using smart defaults and intelligent auto-detection of the environment.
- **Robust Security:** Built-in support for DuckDB Secrets and Oracle Wallets ensures credentials and connections are handled securely.
- **Transparent Tuning:** Provides power users with transparent controls for performance tuning, such as configurable prefetch and array sizes.

## Vision & Success Criteria
- **Industry Standard:** To be recognized as the definitive, high-performance bridge between the DuckDB ecosystem and Oracle databases.
- **Continuous Compatibility:** Maintain 100% compatibility with the latest versions of both Oracle (e.g., 23ai) and DuckDB.
- **Mission Critical Stability:** Achieve the level of stability and performance required for massive production data migration and integration tasks.
