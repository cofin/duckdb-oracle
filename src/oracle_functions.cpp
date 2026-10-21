#include "oracle_functions.hpp"
#include "oracle_catalog_state.hpp"
#include "oracle_connection_manager.hpp"
#include "oracle_connection_resolver.hpp"
#include "oracle_debug_stats.hpp"
#include "oracle_pushdown.hpp"
#include "oracle_secret.hpp"
#include "oracle_table_function.hpp"
#include "oracle_utils.hpp"
#include "oracle_write.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include <oci.h>
#include <cstdio>

namespace duckdb {

static void OracleExecuteFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	RejectOracleWriteInExplicitTransaction(state.GetContext());

	UnifiedVectorFormat conn_format;
	UnifiedVectorFormat sql_format;
	args.data[0].ToUnifiedFormat(conn_format);
	args.data[1].ToUnifiedFormat(sql_format);

	auto conn_data = UnifiedVectorFormat::GetData<string_t>(conn_format);
	auto sql_data = UnifiedVectorFormat::GetData<string_t>(sql_format);

	result.SetVectorType(VectorType::FLAT_VECTOR);
	auto result_data = FlatVector::GetDataMutable<string_t>(result);
	auto &result_validity = FlatVector::ValidityMutable(result);

	for (idx_t row_idx = 0; row_idx < args.size(); row_idx++) {
		auto conn_idx = conn_format.sel->get_index(row_idx);
		auto sql_idx = sql_format.sel->get_index(row_idx);

		if (!conn_format.validity.RowIsValid(conn_idx) || !sql_format.validity.RowIsValid(sql_idx)) {
			result_validity.SetInvalid(row_idx);
			continue;
		}

		auto connection_string = conn_data[conn_idx].GetString();
		auto sql_statement = sql_data[sql_idx].GetString();
		if (connection_string.empty()) {
			result_validity.SetInvalid(row_idx);
			continue;
		}

		if (getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] execute start: %s\n", sql_statement.c_str());
		}

		auto resolved = ResolveOracleConnection(state.GetContext(), connection_string, nullptr, true, "oracle_execute");
		StringUtil::Trim(sql_statement);
		if (sql_statement.empty()) {
			throw InvalidInputException("oracle_execute SQL statement cannot be empty");
		}

		auto conn_handle = OracleConnectionManager::Instance().Acquire(resolved.connection_string, resolved.wallet_path,
		                                                               resolved.settings);
		auto ctx = conn_handle->Get();

		auto stmthp = AllocateOCIStatement(ctx->envhp, ctx->errhp, "Failed to allocate OCI statement handle");

		ub4 call_timeout_ms = static_cast<ub4>(resolved.settings.call_timeout_ms);
		if (call_timeout_ms > 0) {
			CheckOCIOptionalAttributeORA24315(
			    OCIAttrSet(stmthp.get(), OCI_HTYPE_STMT, &call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, ctx->errhp),
			    ctx->errhp, "Failed to set OCI statement call timeout");
		}

		sword status =
		    OCIStmtPrepare(stmthp.get(), ctx->errhp, reinterpret_cast<const OraText *>(sql_statement.c_str()),
		                   sql_statement.size(), OCI_NTV_SYNTAX, OCI_DEFAULT);
		CheckOCIError(status, ctx->errhp, "Failed to prepare OCI statement");

		ub2 stmt_type = 0;
		CheckOCIError(OCIAttrGet(stmthp.get(), OCI_HTYPE_STMT, &stmt_type, nullptr, OCI_ATTR_STMT_TYPE, ctx->errhp),
		              ctx->errhp, "Failed to get OCI statement type");
		if (stmt_type == OCI_STMT_SELECT) {
			throw InvalidInputException("oracle_execute does not support SELECT statements; use oracle_query instead");
		}

		status = OCIStmtExecute(ctx->svchp, stmthp.get(), ctx->errhp, 1, 0, nullptr, nullptr, OCI_COMMIT_ON_SUCCESS);
		CheckOCIError(status, ctx->errhp, "Failed to execute OCI statement");

		ub8 row_count = 0;
		CheckOCIError(OCIAttrGet(stmthp.get(), OCI_HTYPE_STMT, &row_count, nullptr, OCI_ATTR_UB8_ROW_COUNT, ctx->errhp),
		              ctx->errhp, "Failed to get OCI row count");

		if (stmt_type == OCI_STMT_CREATE || stmt_type == OCI_STMT_DROP || stmt_type == OCI_STMT_ALTER) {
			if (resolved.catalog_state) {
				resolved.catalog_state->ClearCaches();
			} else {
				OracleCatalogState::ClearAllCaches();
			}
		}

		auto row_count_str = std::to_string(row_count);
		if (getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] execute stmt_type: %d, row_count: %s\n", stmt_type, row_count_str.c_str());
		}

		bool is_dml = (stmt_type == OCI_STMT_UPDATE || stmt_type == OCI_STMT_DELETE || stmt_type == OCI_STMT_INSERT ||
		               stmt_type == OCI_STMT_MERGE);

		string result_msg;
		if (row_count > 0 || is_dml) {
			result_msg =
			    StringUtil::Format("Statement executed successfully (%s rows affected)", row_count_str.c_str());
		} else {
			result_msg = "Statement executed successfully";
		}

		if (getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] execute success: %s\n", result_msg.c_str());
		}

		result_data[row_idx] = StringVector::AddString(result, result_msg);
	}
}

static void OracleClearCache(DataChunk &, ExpressionState &, Vector &result) {
	OracleCatalogState::ClearAllCaches();
	OracleConnectionManager::Instance().Clear();
	result.SetValue(0, Value("oracle caches cleared"));
	result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

static void OracleDebugResetStatsFunction(DataChunk &, ExpressionState &, Vector &result) {
	OracleDebugResetStats();
	result.SetValue(0, Value("oracle debug stats reset"));
	result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

static void OracleDebugCounterFunction(DataChunk &args, ExpressionState &, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
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
	result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

static void OracleDebugPartitionMetadataFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
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
	oracle_scan_func.get_bind_info = OracleGetBindInfo;
	oracle_scan_func.get_table_entry = OracleGetTableEntry;
	oracle_scan_func.cardinality = OracleCardinality;
	oracle_scan_func.to_string = OracleToString;
	oracle_scan_func.GetSignature().WithTypedKwargs("options", [](TypedKwargs &options) {
		options.Add("as_of_scn", LogicalType::UBIGINT).Add("as_of_timestamp", LogicalType::VARCHAR);
	});
	loader.RegisterFunction(oracle_scan_func);

	auto oracle_query_func = TableFunction("oracle_query", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                       OracleQueryFunction, OracleQueryBind, OracleInitGlobal, nullptr);
	oracle_query_func.filter_pushdown = false;
	oracle_query_func.pushdown_complex_filter = OraclePushdownComplexFilter;
	oracle_query_func.projection_pushdown = true;
	oracle_query_func.get_bind_info = OracleGetBindInfo;
	oracle_query_func.get_table_entry = OracleGetTableEntry;
	oracle_query_func.cardinality = OracleCardinality;
	oracle_query_func.to_string = OracleToString;
	loader.RegisterFunction(oracle_query_func);

	auto clear_cache_func = ScalarFunction("oracle_clear_cache", {}, LogicalType::VARCHAR, OracleClearCache);
	clear_cache_func.SetVolatile();
	loader.RegisterFunction(clear_cache_func);

	auto debug_reset_func =
	    ScalarFunction("oracle_debug_reset_stats", {}, LogicalType::VARCHAR, OracleDebugResetStatsFunction);
	debug_reset_func.SetVolatile();
	loader.RegisterFunction(debug_reset_func);

	auto debug_counter_func = ScalarFunction("oracle_debug_counter", {LogicalType::VARCHAR}, LogicalType::UBIGINT,
	                                         OracleDebugCounterFunction);
	debug_counter_func.SetVolatile();
	loader.RegisterFunction(debug_counter_func);

	auto debug_last_query_func =
	    ScalarFunction("oracle_debug_last_query", {}, LogicalType::VARCHAR, OracleDebugLastQueryFunction);
	debug_last_query_func.SetVolatile();
	loader.RegisterFunction(debug_last_query_func);

	auto debug_partition_metadata_func = ScalarFunction(
	    "oracle_debug_partition_metadata", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
	    LogicalType::VARCHAR, OracleDebugPartitionMetadataFunction);
	debug_partition_metadata_func.SetVolatile();
	debug_partition_metadata_func.SetFallible();
	loader.RegisterFunction(debug_partition_metadata_func);

	auto oracle_execute_func = ScalarFunction("oracle_execute", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                          LogicalType::VARCHAR, OracleExecuteFunction);
	oracle_execute_func.SetVolatile();
	oracle_execute_func.SetFallible();
	loader.RegisterFunction(oracle_execute_func);
}

} // namespace duckdb
