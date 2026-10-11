#include "oracle_settings.hpp"
#include "oracle_storage_extension.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/config.hpp"

namespace duckdb {

namespace {

static int64_t ExtractSignedOptionInt(const Value &val, const char *name, idx_t min_value, idx_t max_value) {
	if (val.type().id() == LogicalTypeId::UBIGINT) {
		auto raw = val.GetValue<uint64_t>();
		if (raw > static_cast<uint64_t>(NumericLimits<int64_t>::Maximum())) {
			auto min_str = std::to_string(min_value);
			auto max_str = std::to_string(max_value);
			throw InvalidInputException("%s must be between %s and %s", name, min_str.c_str(), max_str.c_str());
		}
		return static_cast<int64_t>(raw);
	}
	return val.GetValue<int64_t>();
}

} // namespace

void ValidatePositiveRange(const string &name, int64_t val, int64_t min_val, int64_t max_val) {
	if (val < 0 || val < min_val || val > max_val) {
		auto min_str = std::to_string(min_val);
		auto max_str = std::to_string(max_val);
		throw InvalidInputException("%s must be between %s and %s", name.c_str(), min_str.c_str(), max_str.c_str());
	}
}

idx_t OracleValidatedSettingValue(int64_t value, const char *name, idx_t min_value, idx_t max_value) {
	ValidatePositiveRange(name, value, static_cast<int64_t>(min_value), static_cast<int64_t>(max_value));
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

idx_t OracleEffectiveCallTimeoutMs(const OracleSettings &settings) {
	return OracleValidatedSettingValue(static_cast<int64_t>(settings.call_timeout_ms), "oracle_call_timeout_ms", 0,
	                                   MAX_ORACLE_CALL_TIMEOUT_MS);
}

idx_t OracleEffectiveStmtCacheSize(const OracleSettings &settings) {
	return OracleValidatedSettingValue(static_cast<int64_t>(settings.stmt_cache_size), "oracle_stmt_cache_size", 0,
	                                   MAX_ORACLE_STMT_CACHE_SIZE);
}

idx_t OracleEffectiveMetadataResultLimit(const OracleSettings &settings) {
	if (settings.metadata_result_limit == 0) {
		return DEFAULT_ORACLE_METADATA_RESULT_LIMIT;
	}
	return OracleValidatedMetadataResultLimit(static_cast<int64_t>(settings.metadata_result_limit));
}

void ApplyAttachOptions(const case_insensitive_map_t<Value> &options, OracleSettings &settings) {
	for (const auto &entry : options) {
		auto key = StringUtil::Lower(entry.first);
		const auto &val = entry.second;
		if (key == "enable_pushdown" || key == "oracle_enable_pushdown") {
			settings.enable_pushdown = val.GetValue<bool>();
			settings.attach_overridden_keys.insert("oracle_enable_pushdown");
		} else if (key == "prefetch_rows" || key == "oracle_prefetch_rows") {
			auto raw = ExtractSignedOptionInt(val, "oracle_prefetch_rows", 1, MAX_ORACLE_PREFETCH_ROWS);
			settings.prefetch_rows =
			    OracleValidatedSettingValue(raw, "oracle_prefetch_rows", 1, MAX_ORACLE_PREFETCH_ROWS);
			settings.attach_overridden_keys.insert("oracle_prefetch_rows");
		} else if (key == "prefetch_memory" || key == "oracle_prefetch_memory") {
			auto raw = ExtractSignedOptionInt(val, "oracle_prefetch_memory", 0, MAX_ORACLE_PREFETCH_MEMORY);
			settings.prefetch_memory =
			    OracleValidatedSettingValue(raw, "oracle_prefetch_memory", 0, MAX_ORACLE_PREFETCH_MEMORY);
			settings.attach_overridden_keys.insert("oracle_prefetch_memory");
		} else if (key == "array_size" || key == "oracle_array_size") {
			auto raw = ExtractSignedOptionInt(val, "oracle_array_size", 1, STANDARD_VECTOR_SIZE);
			settings.array_size = OracleValidatedSettingValue(raw, "oracle_array_size", 1, STANDARD_VECTOR_SIZE);
			settings.attach_overridden_keys.insert("oracle_array_size");
		} else if (key == "connection_cache" || key == "oracle_connection_cache") {
			settings.connection_cache = val.GetValue<bool>();
			settings.attach_overridden_keys.insert("oracle_connection_cache");
		} else if (key == "connection_limit" || key == "oracle_connection_limit") {
			auto raw = ExtractSignedOptionInt(val, "oracle_connection_limit", 1, MAX_ORACLE_CONNECTION_LIMIT);
			settings.connection_limit =
			    OracleValidatedSettingValue(raw, "oracle_connection_limit", 1, MAX_ORACLE_CONNECTION_LIMIT);
			settings.attach_overridden_keys.insert("oracle_connection_limit");
		} else if (key == "debug_show_queries" || key == "oracle_debug_show_queries") {
			settings.debug_show_queries = val.GetValue<bool>();
			settings.attach_overridden_keys.insert("oracle_debug_show_queries");
		} else if (key == "lazy_schema_loading" || key == "oracle_lazy_schema_loading") {
			settings.lazy_schema_loading = val.GetValue<bool>();
			settings.attach_overridden_keys.insert("oracle_lazy_schema_loading");
		} else if (key == "metadata_object_types" || key == "oracle_metadata_object_types") {
			settings.metadata_object_types = val.ToString();
			settings.attach_overridden_keys.insert("oracle_metadata_object_types");
		} else if (key == "metadata_result_limit" || key == "oracle_metadata_result_limit") {
			auto raw = ExtractSignedOptionInt(val, "oracle_metadata_result_limit", 0, MAX_ORACLE_METADATA_RESULT_LIMIT);
			settings.metadata_result_limit = OracleValidatedMetadataResultLimit(raw);
			settings.attach_overridden_keys.insert("oracle_metadata_result_limit");
		} else if (key == "use_current_schema" || key == "oracle_use_current_schema") {
			settings.use_current_schema = val.GetValue<bool>();
			settings.attach_overridden_keys.insert("oracle_use_current_schema");
		} else if (key == "current_schema" || key == "oracle_current_schema") {
			settings.attach_overridden_keys.insert("oracle_current_schema");
		} else if (key == "enable_spatial_types" || key == "oracle_enable_spatial_types" || key == "spatial_as_wkt" ||
		           key == "oracle_spatial_as_wkt") {
			if (key == "spatial_as_wkt" || key == "oracle_spatial_as_wkt") {
				settings.enable_spatial_types = !val.GetValue<bool>();
				settings.attach_overridden_keys.insert("oracle_spatial_as_wkt");
			} else {
				settings.enable_spatial_types = val.GetValue<bool>();
			}
			settings.attach_overridden_keys.insert("oracle_enable_spatial_types");
		} else if (key == "call_timeout_ms" || key == "oracle_call_timeout_ms") {
			auto raw = ExtractSignedOptionInt(val, "oracle_call_timeout_ms", 0, MAX_ORACLE_CALL_TIMEOUT_MS);
			settings.call_timeout_ms =
			    OracleValidatedSettingValue(raw, "oracle_call_timeout_ms", 0, MAX_ORACLE_CALL_TIMEOUT_MS);
			settings.attach_overridden_keys.insert("oracle_call_timeout_ms");
		} else if (key == "stmt_cache_size" || key == "oracle_stmt_cache_size") {
			auto raw = ExtractSignedOptionInt(val, "oracle_stmt_cache_size", 0, MAX_ORACLE_STMT_CACHE_SIZE);
			settings.stmt_cache_size =
			    OracleValidatedSettingValue(raw, "oracle_stmt_cache_size", 0, MAX_ORACLE_STMT_CACHE_SIZE);
			settings.attach_overridden_keys.insert("oracle_stmt_cache_size");
		}
	}
}

static void ValidatePrefetchRowsOption(ClientContext &, SetScope, Value &parameter) {
	auto raw = ExtractSignedOptionInt(parameter, "oracle_prefetch_rows", 1, MAX_ORACLE_PREFETCH_ROWS);
	OracleValidatedSettingValue(raw, "oracle_prefetch_rows", 1, MAX_ORACLE_PREFETCH_ROWS);
}

static void ValidatePrefetchMemoryOption(ClientContext &, SetScope, Value &parameter) {
	auto raw = ExtractSignedOptionInt(parameter, "oracle_prefetch_memory", 0, MAX_ORACLE_PREFETCH_MEMORY);
	OracleValidatedSettingValue(raw, "oracle_prefetch_memory", 0, MAX_ORACLE_PREFETCH_MEMORY);
}

static void ValidateArraySizeOption(ClientContext &, SetScope, Value &parameter) {
	auto raw = ExtractSignedOptionInt(parameter, "oracle_array_size", 1, STANDARD_VECTOR_SIZE);
	OracleValidatedSettingValue(raw, "oracle_array_size", 1, STANDARD_VECTOR_SIZE);
}

static void ValidateConnectionLimitOption(ClientContext &, SetScope, Value &parameter) {
	auto raw = ExtractSignedOptionInt(parameter, "oracle_connection_limit", 1, MAX_ORACLE_CONNECTION_LIMIT);
	OracleValidatedSettingValue(raw, "oracle_connection_limit", 1, MAX_ORACLE_CONNECTION_LIMIT);
}

static void ValidateCallTimeoutMsOption(ClientContext &, SetScope, Value &parameter) {
	auto raw = ExtractSignedOptionInt(parameter, "oracle_call_timeout_ms", 0, MAX_ORACLE_CALL_TIMEOUT_MS);
	OracleValidatedSettingValue(raw, "oracle_call_timeout_ms", 0, MAX_ORACLE_CALL_TIMEOUT_MS);
}

static void ValidateStmtCacheSizeOption(ClientContext &, SetScope, Value &parameter) {
	auto raw = ExtractSignedOptionInt(parameter, "oracle_stmt_cache_size", 0, MAX_ORACLE_STMT_CACHE_SIZE);
	OracleValidatedSettingValue(raw, "oracle_stmt_cache_size", 0, MAX_ORACLE_STMT_CACHE_SIZE);
}

static void ValidateMetadataResultLimitOption(ClientContext &, SetScope, Value &parameter) {
	auto raw = ExtractSignedOptionInt(parameter, "oracle_metadata_result_limit", 0, MAX_ORACLE_METADATA_RESULT_LIMIT);
	OracleValidatedMetadataResultLimit(raw);
}

void RegisterOracleExtensionOptions(DBConfig &config) {
	config.AddExtensionOption("oracle_enable_pushdown", "Enable Oracle filter/projection pushdown",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(true));
	config.AddExtensionOption("oracle_prefetch_rows", "OCI prefetch row count", LogicalType::UBIGINT,
	                          Value::UBIGINT(DEFAULT_ORACLE_PREFETCH_ROWS), ValidatePrefetchRowsOption);
	config.AddExtensionOption("oracle_prefetch_memory", "OCI prefetch memory (bytes, 0=auto)", LogicalType::UBIGINT,
	                          Value::UBIGINT(DEFAULT_ORACLE_PREFETCH_MEMORY), ValidatePrefetchMemoryOption);
	config.AddExtensionOption("oracle_array_size", "Rows fetched per OCI iteration (used for tuning)",
	                          LogicalType::UBIGINT, Value::UBIGINT(DEFAULT_ORACLE_ARRAY_SIZE), ValidateArraySizeOption);
	config.AddExtensionOption("oracle_connection_cache", "Reuse Oracle connections when possible", LogicalType::BOOLEAN,
	                          Value::BOOLEAN(true));
	config.AddExtensionOption("oracle_connection_limit", "Maximum cached Oracle connections", LogicalType::UBIGINT,
	                          Value::UBIGINT(DEFAULT_ORACLE_CONNECTION_LIMIT), ValidateConnectionLimitOption);
	config.AddExtensionOption("oracle_debug_show_queries", "Log generated Oracle SQL for debugging",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(false));
	config.AddExtensionOption("oracle_call_timeout_ms", "OCI call timeout in milliseconds (0=disabled)",
	                          LogicalType::UBIGINT, Value::UBIGINT(DEFAULT_ORACLE_CALL_TIMEOUT_MS),
	                          ValidateCallTimeoutMsOption);
	config.AddExtensionOption("oracle_stmt_cache_size", "OCI statement cache size per connection (0=disabled)",
	                          LogicalType::UBIGINT, Value::UBIGINT(DEFAULT_ORACLE_STMT_CACHE_SIZE),
	                          ValidateStmtCacheSizeOption);

	config.AddExtensionOption("oracle_lazy_schema_loading", "Load only current schema by default", LogicalType::BOOLEAN,
	                          Value::BOOLEAN(true));
	config.AddExtensionOption("oracle_metadata_object_types",
	                          "Object types to enumerate (TABLE,VIEW,SYNONYM,MATERIALIZED VIEW)", LogicalType::VARCHAR,
	                          Value("TABLE,VIEW,SYNONYM,MATERIALIZED VIEW"));
	config.AddExtensionOption(
	    "oracle_metadata_result_limit", "Maximum rows returned from metadata queries (0=default bounded limit)",
	    LogicalType::UBIGINT, Value::UBIGINT(DEFAULT_ORACLE_METADATA_RESULT_LIMIT), ValidateMetadataResultLimitOption);
	config.AddExtensionOption("oracle_use_current_schema", "Resolve unqualified table names to current schema first",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(true));
	config.AddExtensionOption("oracle_enable_spatial_types",
	                          "Map SDO_GEOMETRY to GEOMETRY type (requires spatial extension)", LogicalType::BOOLEAN,
	                          Value::BOOLEAN(true));

	StorageExtension::Register(config, "oracle", CreateOracleStorageExtension());
}

} // namespace duckdb
