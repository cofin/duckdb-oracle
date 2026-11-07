---
name: duckdb-extension
description: "Use when modifying DuckDB 2.0 extension entrypoints, StorageExtension/Catalog/Schema/Table entries, TableFunction scan and optimizer callbacks, filter/projection pushdown, PhysicalOracleInsert sink writes, sqllogictest suites, or community extension packaging in src/oracle_extension.cpp, src/oracle_scan.cpp, src/oracle_pushdown.cpp, src/oracle_functions.cpp, src/storage/*, test/*, or description.yml."
---

# DuckDB Extension Engineering Skill (`duckdb-extension`)

<!-- truth: start -->
- Always call `output.SetChildCardinality(row_count)` (never `SetCardinalityUnsafe(row_count)`) after populating flat vectors via `FlatVector::GetDataMutable`, `FlatVector::SetNull`, or `StringVector::AddString`.
- Subclass `duckdb::Catalog` and `duckdb::SchemaCatalogEntry` directly (`info.internal = false`, never `DuckCatalog`), isolate per-`ATTACH` state in `OracleCatalogState`, and synchronize cached entries via `cache_generation`.
- Wire DuckDB 2.0 `TableFunction` optimizer callbacks (`projection_pushdown`, `filter_pushdown`, `filter_prune`, `pushdown_complex_filter`, `get_bind_info`, `get_table_entry`, `cardinality`, `to_string`) on `oracle_scan`.
- Never push empty string literals (`''`) into Oracle `WHERE` clauses (`src/oracle_pushdown.cpp`) because Oracle evaluates `''` as `NULL`.
- Normalize physical sink inputs via `col_vec.ToUnifiedFormat(count, vdata)` in `OracleWriteBatch` (`src/storage/oracle_write.cpp`).
<!-- truth: end -->

## Workflow

1. **Catalog, Schema & TableFunction Scans (`src/storage/oracle_*.cpp`, `src/oracle_scan.cpp`)**:
   - Follow [`references/catalog-and-scan.md`](references/catalog-and-scan.md) when modifying `OracleCatalogState`, `OracleCatalog`, `OracleSchemaEntry`, `OracleTableEntry`, or `oracle_scan` / `oracle_query`.
2. **Filter/Projection Pushdown & Physical Sink Writes (`src/oracle_pushdown.cpp`, `src/storage/oracle_insert.cpp`, `src/storage/oracle_write.cpp`)**:
   - Follow [`references/pushdown-and-sink.md`](references/pushdown-and-sink.md) when modifying filter pushdown rules or `INSERT INTO` sink execution.
3. **Public-Seam sqllogictest Verification (`test/unit_tests/`, `test/integration_tests/`)**:
   - Add unit tests into the canonical `test/unit_tests/*.test` file and ensure `make test-unit` passes without Oracle Instant Client installed.
   - Add live Oracle tests into the canonical `test/integration_tests/*.test` file and verify with `make integration`.

## Guardrails

- Never call `SetCardinalityUnsafe` on `DataChunk` outputs populated via `FlatVector::GetDataMutable` or `StringVector::AddString`.
- Never access private `Expression` members directly in DuckDB 2.0 (`expr.expression_class`, `expr.type`, `expr.return_type`, `expr.alias`); use `GetExpressionClass()`, `GetExpressionType()`, `GetReturnType()`, and `GetAlias()`.
- Never replace `variegata:` with `vcpkg_commit` in `scripts/bump_duckdb_version.sh` or `description.yml`.

## References

- [Catalog, Schema & TableFunction Scans](references/catalog-and-scan.md)
- [Filter/Projection Pushdown & Physical Sink Writes](references/pushdown-and-sink.md)
- [DuckDB Extension Architecture](../../bundles/knowledge/architecture/duckdb-extension-architecture.md)
- [DuckDB 2.0 Vectorized Engine & APIs](../../bundles/knowledge/domains/duckdb-extension.md)
- [DuckDB Community Extensions Registry](../../bundles/knowledge/domains/community-extensions.md)
