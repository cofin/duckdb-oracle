#pragma once

#include "duckdb/storage/storage_extension.hpp"
#include "oracle_catalog_state.hpp"

namespace duckdb {

struct OracleStorageInfo : public StorageExtensionInfo {
	shared_ptr<OracleCatalogState> state;
};

unique_ptr<StorageExtension> CreateOracleStorageExtension();

} // namespace duckdb
