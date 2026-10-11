#pragma once

#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/unordered_set.hpp"
#include <unordered_set>

namespace duckdb {

static constexpr idx_t DEFAULT_ORACLE_METADATA_RESULT_LIMIT = 10000;
static constexpr idx_t DEFAULT_ORACLE_PREFETCH_ROWS = 1024;
static constexpr idx_t DEFAULT_ORACLE_PREFETCH_MEMORY = 0;
static constexpr idx_t DEFAULT_ORACLE_ARRAY_SIZE = 256;
static constexpr idx_t DEFAULT_ORACLE_CONNECTION_LIMIT = 8;
static constexpr idx_t DEFAULT_ORACLE_CALL_TIMEOUT_MS = 30000;
static constexpr idx_t DEFAULT_ORACLE_STMT_CACHE_SIZE = 20;
static constexpr idx_t MAX_ORACLE_PREFETCH_ROWS = 1000000;
static constexpr idx_t MAX_ORACLE_PREFETCH_MEMORY = 1073741824;
static constexpr idx_t MAX_ORACLE_CONNECTION_LIMIT = 1024;
static constexpr idx_t MAX_ORACLE_METADATA_RESULT_LIMIT = 1000000000;
static constexpr idx_t MAX_ORACLE_CALL_TIMEOUT_MS = 3600000;
static constexpr idx_t MAX_ORACLE_STMT_CACHE_SIZE = 1024;

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
	idx_t call_timeout_ms = DEFAULT_ORACLE_CALL_TIMEOUT_MS;
	idx_t stmt_cache_size = DEFAULT_ORACLE_STMT_CACHE_SIZE;

	// Advanced features
	bool lazy_schema_loading = true;
	string metadata_object_types = "TABLE,VIEW,SYNONYM,MATERIALIZED VIEW";
	idx_t metadata_result_limit = DEFAULT_ORACLE_METADATA_RESULT_LIMIT;
	bool use_current_schema = true;

	// Type conversion settings
	bool enable_spatial_types = true; // Map SDO_GEOMETRY to GEOMETRY type (requires spatial extension)

	unordered_set<string> attach_overridden_keys;
};

//! Return the effective OCI array size, capped to one DuckDB output chunk.
idx_t OracleEffectiveArraySize(const OracleSettings &settings);
idx_t OracleEffectivePrefetchRows(const OracleSettings &settings);
idx_t OracleEffectivePrefetchMemory(const OracleSettings &settings);
idx_t OracleEffectiveConnectionLimit(const OracleSettings &settings);
idx_t OracleEffectiveCallTimeoutMs(const OracleSettings &settings);
idx_t OracleEffectiveStmtCacheSize(const OracleSettings &settings);
//! Return the bounded metadata row limit. A configured zero uses the default limit.
idx_t OracleEffectiveMetadataResultLimit(const OracleSettings &settings);
idx_t OracleValidatedSettingValue(int64_t value, const char *name, idx_t min_value, idx_t max_value);
idx_t OracleValidatedMetadataResultLimit(int64_t value);
void ValidatePositiveRange(const string &name, int64_t val, int64_t min_val, int64_t max_val);
void ApplyAttachOptions(const case_insensitive_map_t<Value> &options, OracleSettings &settings);

//! Register Oracle extension settings and storage extension with DuckDB.
void RegisterOracleExtensionOptions(DBConfig &config);

} // namespace duckdb
