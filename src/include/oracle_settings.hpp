#pragma once

#include "duckdb/common/types.hpp"

namespace duckdb {

static constexpr idx_t DEFAULT_ORACLE_METADATA_RESULT_LIMIT = 10000;
static constexpr idx_t DEFAULT_ORACLE_PREFETCH_ROWS = 1024;
static constexpr idx_t DEFAULT_ORACLE_PREFETCH_MEMORY = 0;
static constexpr idx_t DEFAULT_ORACLE_ARRAY_SIZE = 256;
static constexpr idx_t DEFAULT_ORACLE_CONNECTION_LIMIT = 8;
static constexpr idx_t MAX_ORACLE_PREFETCH_ROWS = 1000000;
static constexpr idx_t MAX_ORACLE_PREFETCH_MEMORY = 1073741824;
static constexpr idx_t MAX_ORACLE_CONNECTION_LIMIT = 1024;
static constexpr idx_t MAX_ORACLE_METADATA_RESULT_LIMIT = 1000000000;

class DBConfig;

struct OracleSettings {
	// Filter pushdown: push WHERE clauses to Oracle for server-side filtering
	// Enabled by default for performance (industry standard for remote DB connectors)
	// Disable with SET oracle_enable_pushdown = false for debugging
	bool enable_pushdown = true;
	idx_t prefetch_rows = DEFAULT_ORACLE_PREFETCH_ROWS;
	idx_t prefetch_memory = DEFAULT_ORACLE_PREFETCH_MEMORY;
	idx_t array_size = DEFAULT_ORACLE_ARRAY_SIZE;
	bool connection_cache = true;
	idx_t connection_limit = DEFAULT_ORACLE_CONNECTION_LIMIT;
	bool debug_show_queries = false;

	// Advanced features
	bool lazy_schema_loading = true;
	string metadata_object_types = "TABLE,VIEW,SYNONYM,MATERIALIZED VIEW";
	idx_t metadata_result_limit = DEFAULT_ORACLE_METADATA_RESULT_LIMIT;
	bool use_current_schema = true;

	// Type conversion settings (for OCI array fetch buffer alignment issues)
	bool try_native_lobs = true;        // Try native LOB/RAW fetch first, fallback on corruption
	bool vector_to_list = true;         // Parse VECTOR JSON to LIST<FLOAT> (vs raw VARCHAR)
	bool enable_type_conversion = true; // Enable server-side type conversion for problematic types
	bool enable_spatial_types = true;   // Map SDO_GEOMETRY to GEOMETRY type (requires spatial extension)
};

//! Return the effective OCI array size, capped to one DuckDB output chunk.
idx_t OracleEffectiveArraySize(const OracleSettings &settings);
idx_t OracleEffectivePrefetchRows(const OracleSettings &settings);
idx_t OracleEffectivePrefetchMemory(const OracleSettings &settings);
idx_t OracleEffectiveConnectionLimit(const OracleSettings &settings);
//! Return the bounded metadata row limit. A configured zero uses the default limit.
idx_t OracleEffectiveMetadataResultLimit(const OracleSettings &settings);
idx_t OracleValidatedSettingValue(int64_t value, const char *name, idx_t min_value, idx_t max_value);
idx_t OracleValidatedMetadataResultLimit(int64_t value);

//! Register Oracle extension settings and storage extension with DuckDB.
void RegisterOracleExtensionOptions(DBConfig &config);

} // namespace duckdb
