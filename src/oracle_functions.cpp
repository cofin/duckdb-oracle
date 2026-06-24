#include "oracle_functions.hpp"
#include "oracle_catalog_state.hpp"
#include "oracle_connection_manager.hpp"
#include "oracle_connection_resolver.hpp"
#include "oracle_debug_stats.hpp"
#include "oracle_pushdown.hpp"
#include "oracle_secret.hpp"
#include "oracle_table_function.hpp"
#include "oracle_utils.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include <oci.h>
#include <cstdio>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define S_ISDIR(mode) (((mode) & _S_IFDIR) == _S_IFDIR)
#endif

namespace duckdb {

static bool PathIsDirectory(const string &path) {
	struct stat st {};
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static void OracleExecuteFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto connection_value = args.data[0].GetValue(0);
	auto sql_value = args.data[1].GetValue(0);

	if (connection_value.IsNull() || sql_value.IsNull()) {
		result.SetValue(0, Value());
		return;
	}

	auto connection_string = connection_value.ToString();
	auto sql_statement = sql_value.ToString();
	if (connection_string.empty()) {
		result.SetValue(0, Value());
		return;
	}

	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] execute start: %s\n", sql_statement.c_str());
	}

	auto resolved = ResolveOracleConnection(state.GetContext(), connection_string, nullptr, true, "oracle_execute");
	auto conn_handle = OracleConnectionManager::Instance().Acquire(resolved.connection_string, resolved.wallet_path,
	                                                               resolved.settings);
	auto ctx = conn_handle->Get();

	auto stmthp = AllocateOCIStatement(ctx->envhp, ctx->errhp, "Failed to allocate OCI statement handle");

	sword status = OCIStmtPrepare(stmthp.get(), ctx->errhp, (OraText *)sql_statement.c_str(), sql_statement.size(),
	                              OCI_NTV_SYNTAX, OCI_DEFAULT);
	CheckOCIError(status, ctx->errhp, "Failed to prepare OCI statement");

	status = OCIStmtExecute(ctx->svchp, stmthp.get(), ctx->errhp, 1, 0, nullptr, nullptr, OCI_COMMIT_ON_SUCCESS);
	CheckOCIError(status, ctx->errhp, "Failed to execute OCI statement");

	string result_msg;
	if (stmthp) {
		ub2 stmt_type = 0;
		CheckOCIError(OCIAttrGet(stmthp.get(), OCI_HTYPE_STMT, &stmt_type, 0, OCI_ATTR_STMT_TYPE, ctx->errhp),
		              ctx->errhp, "Failed to get OCI statement type");

		ub4 row_count = 0;
		CheckOCIError(OCIAttrGet(stmthp.get(), OCI_HTYPE_STMT, &row_count, 0, OCI_ATTR_ROW_COUNT, ctx->errhp),
		              ctx->errhp, "Failed to get OCI row count");

		if (getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] execute stmt_type: %d, row_count: %llu\n", stmt_type,
			        static_cast<unsigned long long>(row_count));
		}

		bool is_dml = (stmt_type == OCI_STMT_UPDATE || stmt_type == OCI_STMT_DELETE || stmt_type == OCI_STMT_INSERT ||
		               stmt_type == OCI_STMT_MERGE);

		if (row_count > 0 || is_dml) {
			result_msg = StringUtil::Format("Statement executed successfully (%llu rows affected)",
			                                static_cast<unsigned long long>(row_count));
		} else {
			result_msg = "Statement executed successfully";
		}
	} else {
		result_msg = "Statement executed successfully";
	}

	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] execute success: %s\n", result_msg.c_str());
	}

	result.SetValue(0, Value(result_msg));
}

static void OracleAttachWallet(DataChunk &args, ExpressionState &, Vector &result) {
	auto wallet_path = args.data[0].GetValue(0).ToString();
	if (!PathIsDirectory(wallet_path)) {
		throw IOException("Wallet path does not exist or is not a directory: " + wallet_path);
	}
	setenv("TNS_ADMIN", wallet_path.c_str(), 1);
	result.SetValue(0, Value("Wallet attached: " + wallet_path));
}

static void OracleClearCache(DataChunk &, ExpressionState &, Vector &result) {
	OracleCatalogState::ClearAllCaches();
	OracleConnectionManager::Instance().Clear();
	result.SetValue(0, Value("oracle caches cleared"));
}

static void OracleDebugResetStatsFunction(DataChunk &, ExpressionState &, Vector &result) {
	OracleDebugResetStats();
	result.SetValue(0, Value("oracle debug stats reset"));
}

static void OracleDebugCounterFunction(DataChunk &args, ExpressionState &, Vector &result) {
	auto count = args.size();
	for (idx_t i = 0; i < count; i++) {
		auto value = args.data[0].GetValue(i);
		if (value.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}
		result.SetValue(i, Value::UBIGINT(OracleDebugGetCounter(value.ToString())));
	}
}

static void OracleDebugLastQueryFunction(DataChunk &, ExpressionState &, Vector &result) {
	result.SetValue(0, Value(OracleDebugGetLastQuery()));
}

static void OracleDebugPartitionMetadataFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	for (idx_t i = 0; i < count; i++) {
		auto connection_value = args.data[0].GetValue(i);
		auto schema_value = args.data[1].GetValue(i);
		auto table_value = args.data[2].GetValue(i);
		if (connection_value.IsNull() || schema_value.IsNull() || table_value.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}

		auto connection_ref = connection_value.ToString();
		auto schema = schema_value.ToString();
		auto table = table_value.ToString();

		auto catalog_state = OracleCatalogState::LookupByAlias(connection_ref);
		if (catalog_state) {
			result.SetValue(i, Value(catalog_state->LoadPartitionMetadata(schema, table).ToDebugString()));
			continue;
		}

		auto resolved = ResolveOracleConnection(state.GetContext(), connection_ref, nullptr, true,
		                                        "oracle_debug_partition_metadata");
		OracleCatalogState transient_state(resolved.connection_string, resolved.wallet_path);
		transient_state.settings = resolved.settings;
		result.SetValue(i, Value(transient_state.LoadPartitionMetadata(schema, table).ToDebugString()));
	}
}

void RegisterOracleFunctions(ExtensionLoader &loader) {
	SecretType secret_type;
	secret_type.name = "oracle";
	secret_type.deserializer = KeyValueSecret::Deserialize<KeyValueSecret>;
	secret_type.default_provider = "config";
	loader.RegisterSecretType(secret_type);

	CreateSecretFunction secret_function = {"oracle", "config", CreateOracleSecretFromConfig};
	secret_function.named_parameters["host"] = LogicalType::VARCHAR;
	secret_function.named_parameters["port"] = LogicalType::BIGINT;
	secret_function.named_parameters["service"] = LogicalType::VARCHAR;
	secret_function.named_parameters["database"] = LogicalType::VARCHAR;
	secret_function.named_parameters["user"] = LogicalType::VARCHAR;
	secret_function.named_parameters["password"] = LogicalType::VARCHAR;
	secret_function.named_parameters["wallet_path"] = LogicalType::VARCHAR;
	loader.RegisterFunction(secret_function);

	auto oracle_scan_func =
	    TableFunction("oracle_scan", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
	                  OracleQueryFunction, OracleScanBind, OracleInitGlobal, nullptr);
	oracle_scan_func.filter_pushdown = false;
	oracle_scan_func.pushdown_complex_filter = OraclePushdownComplexFilter;
	oracle_scan_func.projection_pushdown = true;
	loader.RegisterFunction(oracle_scan_func);

	auto oracle_query_func = TableFunction("oracle_query", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                       OracleQueryFunction, OracleQueryBind, OracleInitGlobal, nullptr);
	oracle_query_func.filter_pushdown = false;
	oracle_query_func.pushdown_complex_filter = OraclePushdownComplexFilter;
	oracle_query_func.projection_pushdown = true;
	loader.RegisterFunction(oracle_query_func);

	auto attach_wallet_func =
	    ScalarFunction("oracle_attach_wallet", {LogicalType::VARCHAR}, LogicalType::VARCHAR, OracleAttachWallet);
	loader.RegisterFunction(attach_wallet_func);

	auto clear_cache_func = ScalarFunction("oracle_clear_cache", {}, LogicalType::VARCHAR, OracleClearCache);
	loader.RegisterFunction(clear_cache_func);

	auto debug_reset_func =
	    ScalarFunction("oracle_debug_reset_stats", {}, LogicalType::VARCHAR, OracleDebugResetStatsFunction);
	loader.RegisterFunction(debug_reset_func);

	auto debug_counter_func = ScalarFunction("oracle_debug_counter", {LogicalType::VARCHAR}, LogicalType::UBIGINT,
	                                         OracleDebugCounterFunction);
	loader.RegisterFunction(debug_counter_func);

	auto debug_last_query_func =
	    ScalarFunction("oracle_debug_last_query", {}, LogicalType::VARCHAR, OracleDebugLastQueryFunction);
	loader.RegisterFunction(debug_last_query_func);

	auto debug_partition_metadata_func = ScalarFunction(
	    "oracle_debug_partition_metadata", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
	    LogicalType::VARCHAR, OracleDebugPartitionMetadataFunction);
	loader.RegisterFunction(debug_partition_metadata_func);

	auto oracle_execute_func = ScalarFunction("oracle_execute", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                          LogicalType::VARCHAR, OracleExecuteFunction);
	loader.RegisterFunction(oracle_execute_func);
}

} // namespace duckdb
