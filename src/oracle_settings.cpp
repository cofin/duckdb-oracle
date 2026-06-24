#include "oracle_settings.hpp"
#include "oracle_storage_extension.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/main/config.hpp"

namespace duckdb {

idx_t OracleValidatedSettingValue(int64_t value, const char *name, idx_t min_value, idx_t max_value) {
	if (value < static_cast<int64_t>(min_value) || value > static_cast<int64_t>(max_value)) {
		auto min_str = std::to_string(min_value);
		auto max_str = std::to_string(max_value);
		throw InvalidInputException("%s must be between %s and %s", name, min_str.c_str(), max_str.c_str());
	}
	return static_cast<idx_t>(value);
}

idx_t OracleValidatedMetadataResultLimit(int64_t value) {
	if (value == 0) {
		return 0;
	}
	return OracleValidatedSettingValue(value, "oracle_metadata_result_limit", 1, MAX_ORACLE_METADATA_RESULT_LIMIT);
}

idx_t OracleEffectiveArraySize(const OracleSettings &settings) {
	return OracleValidatedSettingValue(static_cast<int64_t>(settings.array_size), "oracle_array_size", 1,
	                                   STANDARD_VECTOR_SIZE);
}

idx_t OracleEffectivePrefetchRows(const OracleSettings &settings) {
	return OracleValidatedSettingValue(static_cast<int64_t>(settings.prefetch_rows), "oracle_prefetch_rows", 1,
	                                   MAX_ORACLE_PREFETCH_ROWS);
}

idx_t OracleEffectivePrefetchMemory(const OracleSettings &settings) {
	return OracleValidatedSettingValue(static_cast<int64_t>(settings.prefetch_memory), "oracle_prefetch_memory", 0,
	                                   MAX_ORACLE_PREFETCH_MEMORY);
}

idx_t OracleEffectiveConnectionLimit(const OracleSettings &settings) {
	return OracleValidatedSettingValue(static_cast<int64_t>(settings.connection_limit), "oracle_connection_limit", 1,
	                                   MAX_ORACLE_CONNECTION_LIMIT);
}

idx_t OracleEffectiveMetadataResultLimit(const OracleSettings &settings) {
	if (settings.metadata_result_limit == 0) {
		return DEFAULT_ORACLE_METADATA_RESULT_LIMIT;
	}
	return OracleValidatedMetadataResultLimit(static_cast<int64_t>(settings.metadata_result_limit));
}

void RegisterOracleExtensionOptions(DBConfig &config) {
	config.AddExtensionOption("oracle_enable_pushdown", "Enable Oracle filter/projection pushdown",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(true));
	config.AddExtensionOption("oracle_prefetch_rows", "OCI prefetch row count", LogicalType::UBIGINT,
	                          Value::UBIGINT(DEFAULT_ORACLE_PREFETCH_ROWS));
	config.AddExtensionOption("oracle_prefetch_memory", "OCI prefetch memory (bytes, 0=auto)", LogicalType::UBIGINT,
	                          Value::UBIGINT(DEFAULT_ORACLE_PREFETCH_MEMORY));
	config.AddExtensionOption("oracle_array_size", "Rows fetched per OCI iteration (used for tuning)",
	                          LogicalType::UBIGINT, Value::UBIGINT(DEFAULT_ORACLE_ARRAY_SIZE));
	config.AddExtensionOption("oracle_connection_cache", "Reuse Oracle connections when possible", LogicalType::BOOLEAN,
	                          Value::BOOLEAN(true));
	config.AddExtensionOption("oracle_connection_limit", "Maximum cached Oracle connections", LogicalType::UBIGINT,
	                          Value::UBIGINT(DEFAULT_ORACLE_CONNECTION_LIMIT));
	config.AddExtensionOption("oracle_debug_show_queries", "Log generated Oracle SQL for debugging",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(false));

	// Advanced features settings
	config.AddExtensionOption("oracle_lazy_schema_loading", "Load only current schema by default", LogicalType::BOOLEAN,
	                          Value::BOOLEAN(true));
	config.AddExtensionOption("oracle_metadata_object_types",
	                          "Object types to enumerate (TABLE,VIEW,SYNONYM,MATERIALIZED VIEW)", LogicalType::VARCHAR,
	                          Value("TABLE,VIEW,SYNONYM,MATERIALIZED VIEW"));
	config.AddExtensionOption("oracle_metadata_result_limit",
	                          "Maximum rows returned from metadata queries (0=default bounded limit)",
	                          LogicalType::UBIGINT, Value::UBIGINT(DEFAULT_ORACLE_METADATA_RESULT_LIMIT));
	config.AddExtensionOption("oracle_use_current_schema", "Resolve unqualified table names to current schema first",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(true));
	config.AddExtensionOption("oracle_enable_spatial_types",
	                          "Map SDO_GEOMETRY to GEOMETRY type (requires spatial extension)", LogicalType::BOOLEAN,
	                          Value::BOOLEAN(true));

	StorageExtension::Register(config, "oracle", CreateOracleStorageExtension());
}

} // namespace duckdb
