#pragma once

#include "duckdb/common/types.hpp"

namespace duckdb {

struct OracleSettings {
	// Filter pushdown: push WHERE clauses to Oracle for server-side filtering
	// Enabled by default for performance (industry standard for remote DB connectors)
	// Disable with SET oracle_enable_pushdown = false for debugging
	bool enable_pushdown = true;
	idx_t prefetch_rows = 200;
	idx_t prefetch_memory = 0;
	idx_t array_size = 256;
	bool connection_cache = true;
	idx_t connection_limit = 8;
	bool debug_show_queries = false;

	// Advanced features
	bool lazy_schema_loading = true;
	string metadata_object_types = "TABLE,VIEW,SYNONYM,MATERIALIZED VIEW";
	idx_t metadata_result_limit = 10000;
	bool use_current_schema = true;

	// Type conversion settings (for OCI array fetch buffer alignment issues)
	bool try_native_lobs = true;        // Try native LOB/RAW fetch first, fallback on corruption
	idx_t lob_max_size = 33554432;      // 32MB - Oracle's practical limit for inline LOB fetch
	bool vector_to_list = true;         // Parse VECTOR JSON to LIST<FLOAT> (vs raw VARCHAR)
	bool enable_type_conversion = true; // Enable server-side type conversion for problematic types
};

} // namespace duckdb
