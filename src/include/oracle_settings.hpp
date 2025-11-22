#pragma once

#include "duckdb/common/types.hpp"

namespace duckdb {

struct OracleSettings {
	bool enable_pushdown = false;
	idx_t prefetch_rows = 200;
	idx_t prefetch_memory = 0;
	idx_t array_size = 256;
	bool connection_cache = true;
	idx_t connection_limit = 8;
	bool debug_show_queries = false;
};

} // namespace duckdb
