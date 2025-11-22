#pragma once

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/main/attached_database.hpp"
#include "oracle_catalog_state.hpp"

namespace duckdb {

//! Factory to construct an Oracle-backed catalog used for ATTACH TYPE ORACLE.
unique_ptr<Catalog> CreateOracleCatalog(AttachedDatabase &db, shared_ptr<OracleCatalogState> state);

} // namespace duckdb
