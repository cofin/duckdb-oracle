#include "oracle_settings.hpp"
#include "oracle_storage_extension.hpp"
#include "duckdb/main/config.hpp"

namespace duckdb {

void RegisterOracleExtensionOptions(DBConfig &config) {
	config.AddExtensionOption("oracle_enable_pushdown", "Enable Oracle filter/projection pushdown",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(true));
	config.AddExtensionOption("oracle_prefetch_rows", "OCI prefetch row count", LogicalType::UBIGINT,
	                          Value::UBIGINT(1024));
	config.AddExtensionOption("oracle_prefetch_memory", "OCI prefetch memory (bytes, 0=auto)", LogicalType::UBIGINT,
	                          Value::UBIGINT(0));
	config.AddExtensionOption("oracle_array_size", "Rows fetched per OCI iteration (used for tuning)",
	                          LogicalType::UBIGINT, Value::UBIGINT(256));
	config.AddExtensionOption("oracle_connection_cache", "Reuse Oracle connections when possible", LogicalType::BOOLEAN,
	                          Value::BOOLEAN(true));
	config.AddExtensionOption("oracle_connection_limit", "Maximum cached Oracle connections", LogicalType::UBIGINT,
	                          Value::UBIGINT(8));
	config.AddExtensionOption("oracle_debug_show_queries", "Log generated Oracle SQL for debugging",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(false));

	// Advanced features settings
	config.AddExtensionOption("oracle_lazy_schema_loading", "Load only current schema by default", LogicalType::BOOLEAN,
	                          Value::BOOLEAN(true));
	config.AddExtensionOption("oracle_metadata_object_types",
	                          "Object types to enumerate (TABLE,VIEW,SYNONYM,MATERIALIZED VIEW)", LogicalType::VARCHAR,
	                          Value("TABLE,VIEW,SYNONYM,MATERIALIZED VIEW"));
	config.AddExtensionOption("oracle_metadata_result_limit",
	                          "Maximum rows returned from metadata queries (0=unlimited)", LogicalType::UBIGINT,
	                          Value::UBIGINT(10000));
	config.AddExtensionOption("oracle_use_current_schema", "Resolve unqualified table names to current schema first",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(true));
	config.AddExtensionOption("oracle_enable_spatial_types",
	                          "Map SDO_GEOMETRY to GEOMETRY type (requires spatial extension)", LogicalType::BOOLEAN,
	                          Value::BOOLEAN(true));

	StorageExtension::Register(config, "oracle", CreateOracleStorageExtension());
}

} // namespace duckdb
