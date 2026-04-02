# Product Guide: DuckDB Oracle Extension

## Initial Concept
This extension allows DuckDB to directly read from and write to Oracle databases using the Oracle Call Interface (OCI) for high-performance data transfer.

## Core Purpose
Provide a high-performance, robust, and seamless integration between DuckDB and Oracle databases, enabling efficient querying, data extraction, and loading.

## Target Audience & Use Cases
- **Data Engineers**: Performing fast ETL (Extract, Transform, Load) operations and large-scale data migrations.
- **Data Analysts**: Needing ad-hoc SQL querying against Oracle directly from DuckDB without complex intermediary setups.
- **Backend Services**: Programmatic data access from custom applications needing high-throughput reads/writes.
- **Oracle DBAs**: Managing data snapshots or moving data between Oracle and analytical stores like Parquet.
- **Cloud Engineers & Python Developers**: Integrating Oracle data into modern, cloud-native data pipelines or Python-based data science workflows.

## Key Differentiators
- **High Performance**: Leverages OCI Array Fetch and Array Bind for batch processing, optimizing network round-trips.
- **Advanced Types**: Built-in support for Oracle 23ai `VECTOR` and Spatial `SDO_GEOMETRY`.
- **Smart Pushdown**: Pushes `WHERE` clauses and column projections down to Oracle to minimize data transfer.
- **Scalability**: Handles large Oracle schemas efficiently with lazy loading and configurable metadata limits.
