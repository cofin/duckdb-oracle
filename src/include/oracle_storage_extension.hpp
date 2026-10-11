#pragma once

#include "duckdb/storage/storage_extension.hpp"

namespace duckdb {

unique_ptr<StorageExtension> CreateOracleStorageExtension();

} // namespace duckdb
