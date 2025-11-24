#define DUCKDB_EXTENSION_MAIN

#include "oracle_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression/bound_operator_expression.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/client_context.hpp"
#include "oracle_storage_extension.hpp"
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <condition_variable>
#include "oracle_table_function.hpp"
#include "oracle_catalog_state.hpp"
#include "oracle_secret.hpp"
#include "oracle_connection_manager.hpp"
#include <oci.h>
#include <cstdio>
#include <sys/stat.h>

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>

#ifdef _WIN32
#include <direct.h>
#define S_ISDIR(mode) (((mode)&_S_IFDIR) == _S_IFDIR)
static int setenv(const char *name, const char *value, int overwrite) {
	if (!overwrite && getenv(name) != nullptr) {
		return 0;
	}
	return _putenv_s(name, value);
}
#endif

#ifndef SQLT_JSON
#define SQLT_JSON 119
#endif

#ifndef SQLT_VEC
#define SQLT_VEC 127
#endif

namespace duckdb {

//===--------------------------------------------------------------------===//
// Environment helpers
//===--------------------------------------------------------------------===//

static string OracleGetEnv(const string &key, const string &default_value) {
	auto val = getenv(key.c_str());
	if (val && val[0] != '\0') {
		return string(val);
	}
	return default_value;
}

static void OracleEnvFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	BinaryExecutor::Execute<string_t, string_t, string_t>(args.data[0], args.data[1], result, args.size(),
	                                                      [&](string_t name, string_t deflt) {
		                                                      auto key = name.GetString();
		                                                      auto def_str = deflt.GetString();
		                                                      auto value = OracleGetEnv(key, def_str);
		                                                      return StringVector::AddString(result, value);
	                                                      });
}

OracleBindData::OracleBindData() {
}

unique_ptr<FunctionData> OracleBindData::Copy() const {
	auto copy = make_uniq<OracleBindData>();
	copy->connection_string = connection_string;
	copy->base_query = base_query;
	copy->query = query;
	copy->oci_types = oci_types;
	copy->oci_sizes = oci_sizes;
	copy->column_names = column_names;
	copy->original_types = original_types;
	copy->original_names = original_names;
	copy->settings = settings;
	copy->stmt = stmt; // Copy shared pointer
	return std::move(copy);
}

bool OracleBindData::Equals(const FunctionData &other) const {
	auto &other_bind_data = (const OracleBindData &)other;
	return query == other_bind_data.query && connection_string == other_bind_data.connection_string;
}

static void CheckOCIError(sword status, OCIError *errhp, const string &msg) {
	if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
		text errbuf[512];
		sb4 errcode = 0;
		if (errhp) {
			OCIErrorGet((dvoid *)errhp, (ub4)1, (text *)NULL, &errcode, errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR);
			throw IOException(msg + ": " + string((char *)errbuf));
		} else {
			throw IOException(msg + ": (No Error Handle)");
		}
	}
}

static timestamp_t ParseOciTimestamp(const char *data, ub2 len) {
	if (len == 0 || !data) {
		return timestamp_t();
	}
	// Oracle default string representation for DATE/TIMESTAMP is ISO-like; rely on DuckDB parser.
	string s(data, len);
	try {
		return Timestamp::FromString(s, false);
	} catch (...) {
		// Fallback: remove trailing fractional seconds if present
		auto dot = s.find('.');
		if (dot != string::npos) {
			s = s.substr(0, dot);
			return Timestamp::FromString(s, false);
		}
		throw IOException("Failed to parse Oracle timestamp: " + s);
	}
}

static bool PathIsDirectory(const string &path) {
	struct stat st {};
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static OracleSettings GetOracleSettings(ClientContext &context, OracleCatalogState *state) {
	OracleSettings settings;
	if (state) {
		settings = state->settings;
	}

	Value option_value;
	if (context.TryGetCurrentSetting("oracle_enable_pushdown", option_value)) {
		settings.enable_pushdown = option_value.GetValue<bool>();
	}
	if (context.TryGetCurrentSetting("oracle_prefetch_rows", option_value)) {
		auto val = option_value.GetValue<int64_t>();
		settings.prefetch_rows = MaxValue<idx_t>(1, static_cast<idx_t>(val));
	}
	if (context.TryGetCurrentSetting("oracle_prefetch_memory", option_value)) {
		auto val = option_value.GetValue<int64_t>();
		settings.prefetch_memory = val <= 0 ? 0 : static_cast<idx_t>(val);
	}
	if (context.TryGetCurrentSetting("oracle_array_size", option_value)) {
		auto val = option_value.GetValue<int64_t>();
		settings.array_size = MaxValue<idx_t>(1, static_cast<idx_t>(val));
	}
	if (context.TryGetCurrentSetting("oracle_connection_cache", option_value)) {
		settings.connection_cache = option_value.GetValue<bool>();
	}
	if (context.TryGetCurrentSetting("oracle_connection_limit", option_value)) {
		auto val = option_value.GetValue<int64_t>();
		settings.connection_limit = MaxValue<idx_t>(1, static_cast<idx_t>(val));
	}
	if (context.TryGetCurrentSetting("oracle_debug_show_queries", option_value)) {
		settings.debug_show_queries = option_value.GetValue<bool>();
	}
	if (context.TryGetCurrentSetting("oracle_lazy_schema_loading", option_value)) {
		settings.lazy_schema_loading = option_value.GetValue<bool>();
	}
	if (context.TryGetCurrentSetting("oracle_metadata_object_types", option_value)) {
		settings.metadata_object_types = option_value.ToString();
	}
	if (context.TryGetCurrentSetting("oracle_metadata_result_limit", option_value)) {
		auto val = option_value.GetValue<int64_t>();
		settings.metadata_result_limit = val <= 0 ? 0 : static_cast<idx_t>(val);
	}
	if (context.TryGetCurrentSetting("oracle_use_current_schema", option_value)) {
		settings.use_current_schema = option_value.GetValue<bool>();
	}
	return settings;
}

//! Oracle Execute Function - Execute arbitrary SQL without expecting result set
static void OracleExecuteFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	// We only expect a single row input (scalar function); operate on the first tuple.
	auto connection_string = args.data[0].GetValue(0).ToString();
	auto sql_statement = args.data[1].GetValue(0).ToString();

	if (connection_string.empty()) {
		result.SetValue(0, Value());
		return;
	}

	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] execute start: %s\n", sql_statement.c_str());
	}

	try {
		OracleSettings settings; // defaults; execute does not depend on session options
		auto conn_handle = OracleConnectionManager::Instance().Acquire(connection_string, settings);
		auto ctx = conn_handle->Get();

		// Allocate statement handle
		OCIStmt *stmthp_raw = nullptr;
		CheckOCIError(OCIHandleAlloc(ctx->envhp, (dvoid **)&stmthp_raw, OCI_HTYPE_STMT, 0, nullptr), ctx->errhp,
		              "Failed to allocate OCI statement handle");
		auto stmthp = std::unique_ptr<OCIStmt, std::function<void(OCIStmt *)>>(
		    stmthp_raw, [](OCIStmt *p) { OCIHandleFree(p, OCI_HTYPE_STMT); });

		// Prepare statement
		sword status = OCIStmtPrepare(stmthp.get(), ctx->errhp, (OraText *)sql_statement.c_str(), sql_statement.size(),
		                              OCI_NTV_SYNTAX, OCI_DEFAULT);
		CheckOCIError(status, ctx->errhp, "Failed to prepare OCI statement");

		// Execute statement with auto-commit
		status = OCIStmtExecute(ctx->svchp, stmthp.get(), ctx->errhp, 1, 0, nullptr, nullptr, OCI_COMMIT_ON_SUCCESS);
		CheckOCIError(status, ctx->errhp, "Failed to execute OCI statement");

		// Format result message
		string result_msg;
		if (stmthp) {
			// Get statement type
			ub2 stmt_type = 0;
			CheckOCIError(OCIAttrGet(stmthp.get(), OCI_HTYPE_STMT, &stmt_type, 0, OCI_ATTR_STMT_TYPE, ctx->errhp),
			              ctx->errhp, "Failed to get OCI statement type");

			// Extract row count (for DML statements)
			ub4 row_count = 0;
			CheckOCIError(OCIAttrGet(stmthp.get(), OCI_HTYPE_STMT, &row_count, 0, OCI_ATTR_ROW_COUNT, ctx->errhp),
			              ctx->errhp, "Failed to get OCI row count");

			if (getenv("ORACLE_DEBUG")) {
				fprintf(stderr, "[oracle] execute stmt_type: %d, row_count: %llu\n", stmt_type,
				        static_cast<unsigned long long>(row_count));
			}

			bool is_dml = (stmt_type == OCI_STMT_UPDATE || stmt_type == OCI_STMT_DELETE ||
			               stmt_type == OCI_STMT_INSERT || stmt_type == OCI_STMT_MERGE);

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

	} catch (...) {
		throw;
	}
}

unique_ptr<FunctionData> OracleBindInternal(ClientContext &context, string connection_string, string query,
                                            vector<LogicalType> &return_types, vector<string> &names,
                                            OracleBindData *bind_data_ptr /* = nullptr */,
                                            OracleCatalogState *state /* = nullptr */) {
	auto result = bind_data_ptr ? unique_ptr<OracleBindData>(bind_data_ptr) : make_uniq<OracleBindData>();
	result->connection_string = connection_string;
	result->base_query = query;
	result->query = query;
	result->settings = GetOracleSettings(context, state);
	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] raw connection: %s\n", connection_string.c_str());
	}
	result->conn_handle = OracleConnectionManager::Instance().Acquire(connection_string, result->settings);
	auto ctx = result->conn_handle->Get();

	// Allocate statement handle on the shared environment and keep it for fetch phase
	OCIStmt *stmt_raw = nullptr;
	CheckOCIError(OCIHandleAlloc(ctx->envhp, (dvoid **)&stmt_raw, OCI_HTYPE_STMT, 0, nullptr), ctx->errhp,
	              "Failed to allocate OCI statement handle");

	result->stmt = std::shared_ptr<OCIStmt>(stmt_raw, [](OCIStmt *stmt) {
		if (stmt) {
			OCIHandleFree(stmt, OCI_HTYPE_STMT);
		}
	});

	try {
		// Bound call timeout for describe/execute to avoid hangs
		ub4 call_timeout_ms = 30000; // 30s (per-call upper bound)
		OCIAttrSet(result->stmt.get(), OCI_HTYPE_STMT, &call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, ctx->errhp);

		// Set prefetch/array tuning from settings
		ub4 prefetch_rows = result->settings.prefetch_rows;
		OCIAttrSet(result->stmt.get(), OCI_HTYPE_STMT, &prefetch_rows, 0, OCI_ATTR_PREFETCH_ROWS, ctx->errhp);
		if (result->settings.prefetch_memory > 0) {
			ub4 prefetch_mem = result->settings.prefetch_memory;
			OCIAttrSet(result->stmt.get(), OCI_HTYPE_STMT, &prefetch_mem, 0, OCI_ATTR_PREFETCH_MEMORY, ctx->errhp);
		}

		sword status;

		if (result->settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] prepare (bind): %s\n", result->query.c_str());
		}
		status = OCIStmtPrepare(result->stmt.get(), ctx->errhp, (OraText *)result->query.c_str(), result->query.size(),
		                        OCI_NTV_SYNTAX, OCI_DEFAULT);
		CheckOCIError(status, ctx->errhp, "Failed to prepare OCI statement");

		status = OCIStmtExecute(ctx->svchp, result->stmt.get(), ctx->errhp, 0, 0, nullptr, nullptr, OCI_DESCRIBE_ONLY);
		CheckOCIError(status, ctx->errhp, "Failed to execute OCI statement (Describe)");

		ub4 param_count;
		CheckOCIError(OCIAttrGet(result->stmt.get(), OCI_HTYPE_STMT, &param_count, 0, OCI_ATTR_PARAM_COUNT, ctx->errhp),
		              ctx->errhp, "Failed to get OCI parameter count");

		for (ub4 i = 1; i <= param_count; i++) {
			OCIParam *param;
			CheckOCIError(OCIParamGet(result->stmt.get(), OCI_HTYPE_STMT, ctx->errhp, (dvoid **)&param, i), ctx->errhp,
			              "Failed to get OCI parameter");

			ub2 data_type;
			CheckOCIError(OCIAttrGet(param, OCI_DTYPE_PARAM, &data_type, 0, OCI_ATTR_DATA_TYPE, ctx->errhp), ctx->errhp,
			              "Failed to get OCI data type");

			OraText *col_name;
			ub4 col_name_len;
			CheckOCIError(OCIAttrGet(param, OCI_DTYPE_PARAM, &col_name, &col_name_len, OCI_ATTR_NAME, ctx->errhp),
			              ctx->errhp, "Failed to get OCI column name");

			names.emplace_back((char *)col_name, col_name_len);
			result->column_names.emplace_back((char *)col_name, col_name_len);
			result->oci_types.push_back(data_type);

			ub2 precision = 0;
			sb1 scale = 0;
			CheckOCIError(OCIAttrGet(param, OCI_DTYPE_PARAM, &precision, 0, OCI_ATTR_PRECISION, ctx->errhp), ctx->errhp,
			              "Failed to get OCI precision");
			CheckOCIError(OCIAttrGet(param, OCI_DTYPE_PARAM, &scale, 0, OCI_ATTR_SCALE, ctx->errhp), ctx->errhp,
			              "Failed to get OCI scale");

			ub4 char_len = 0;
			CheckOCIError(OCIAttrGet(param, OCI_DTYPE_PARAM, &char_len, 0, OCI_ATTR_CHAR_SIZE, ctx->errhp), ctx->errhp,
			              "Failed to get OCI char size");
			result->oci_sizes.push_back(char_len > 0 ? char_len : 4000); // Default buffer size

			switch (data_type) {
			case SQLT_CHR:
			case SQLT_AFC:
			case SQLT_VCS:
			case SQLT_AVC:
				return_types.push_back(LogicalType::VARCHAR);
				break;
			case SQLT_NUM:
			case SQLT_VNU:
				if (scale == 0) {
					if (precision > 18) {
						return_types.push_back(LogicalType::DOUBLE);
					} else {
						return_types.push_back(LogicalType::BIGINT);
					}
				} else {
					return_types.push_back(LogicalType::DOUBLE);
				}
				break;
			case SQLT_INT:
			case SQLT_UIN:
				return_types.push_back(LogicalType::BIGINT);
				break;
			case SQLT_FLT:
			case SQLT_BFLOAT:
			case SQLT_BDOUBLE:
			case SQLT_IBFLOAT:
			case SQLT_IBDOUBLE:
				return_types.push_back(LogicalType::DOUBLE);
				break;
			case SQLT_DAT:
			case SQLT_ODT:
			case SQLT_TIMESTAMP:
			case SQLT_TIMESTAMP_TZ:
			case SQLT_TIMESTAMP_LTZ:
				return_types.push_back(LogicalType::TIMESTAMP);
				break;
			case SQLT_CLOB:
			case SQLT_BLOB:
			case SQLT_BIN:
			case SQLT_LBI:
			case SQLT_LNG:
			case SQLT_LVC:
				return_types.push_back(LogicalType::BLOB);
				break;
			case SQLT_JSON:
				return_types.push_back(LogicalType::VARCHAR); // Fetch JSON as string
				break;
			case SQLT_VEC:
				return_types.push_back(LogicalType::VARCHAR);
				break;
			default:
				return_types.push_back(LogicalType::VARCHAR);
				break;
			}
		}

		result->original_types = return_types;
		result->original_names = names;
		result->finished = false;
		return std::move(result);
	} catch (...) {
		throw;
	}
}

static unique_ptr<FunctionData> OracleScanBind(ClientContext &context, TableFunctionBindInput &input,
                                               vector<LogicalType> &return_types, vector<string> &names) {
	auto connection_string = input.inputs[0].GetValue<string>();
	auto schema_name = input.inputs[1].GetValue<string>();
	auto table_name = input.inputs[2].GetValue<string>();
	auto quoted_schema = KeywordHelper::WriteQuoted(schema_name, '"');
	auto quoted_table = KeywordHelper::WriteQuoted(table_name, '"');
	string query = StringUtil::Format("SELECT * FROM %s.%s", quoted_schema.c_str(), quoted_table.c_str());
	return OracleBindInternal(context, connection_string, query, return_types, names);
}

static unique_ptr<FunctionData> OracleQueryBind(ClientContext &context, TableFunctionBindInput &input,
                                                vector<LogicalType> &return_types, vector<string> &names) {
	auto connection_string = input.inputs[0].GetValue<string>();

	// Support attached DB alias: if no '@' present, treat as alias of an attached Oracle database.
	if (connection_string.find('@') == string::npos) {
		auto state = OracleCatalogState::LookupByAlias(connection_string);
		if (state) {
			connection_string = state->connection_string;
		}
	}

	auto query = input.inputs[1].GetValue<string>();
	return OracleBindInternal(context, connection_string, query, return_types, names);
}

unique_ptr<GlobalTableFunctionState> OracleInitGlobal(ClientContext &context, TableFunctionInitInput &input) {
	auto &bind = input.bind_data->Cast<OracleBindData>();
	auto state = make_uniq<OracleScanState>(bind.column_names.size());

	state->conn_handle = bind.conn_handle;
	auto ctx = state->conn_handle->Get();
	state->svc = ctx->svchp;
	state->err = ctx->errhp;

	// Determine if we need to re-prepare the statement
	bool need_reprepare = false;

	// 1. If query changed (pushdown)
	if (bind.query != bind.base_query) {
		need_reprepare = true;
	}

	// 2. If stmt handle is missing
	if (!bind.stmt) {
		need_reprepare = true;
	}

	// 3. If stmt handle matches column count (if available)
	// Note: We can't easily check param count without valid handle and describe.
	// We'll rely on binding logic.

	if (need_reprepare) {
		if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] InitGlobal: re-preparing query: %s\n", bind.query.c_str());
		}

		OCIStmt *stmt_raw = nullptr;
		CheckOCIError(OCIHandleAlloc(ctx->envhp, (dvoid **)&stmt_raw, OCI_HTYPE_STMT, 0, nullptr), ctx->errhp,
		              "Failed to allocate OCI statement handle");

		state->stmt = std::shared_ptr<OCIStmt>(stmt_raw, [](OCIStmt *stmt) {
			if (stmt) {
				OCIHandleFree(stmt, OCI_HTYPE_STMT);
			}
		});

		// Apply settings
		ub4 call_timeout_ms = 30000;
		OCIAttrSet(state->stmt.get(), OCI_HTYPE_STMT, &call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, ctx->errhp);

		ub4 prefetch_rows = bind.settings.prefetch_rows;
		OCIAttrSet(state->stmt.get(), OCI_HTYPE_STMT, &prefetch_rows, 0, OCI_ATTR_PREFETCH_ROWS, ctx->errhp);
		if (bind.settings.prefetch_memory > 0) {
			ub4 prefetch_mem = bind.settings.prefetch_memory;
			OCIAttrSet(state->stmt.get(), OCI_HTYPE_STMT, &prefetch_mem, 0, OCI_ATTR_PREFETCH_MEMORY, ctx->errhp);
		}

		CheckOCIError(OCIStmtPrepare(state->stmt.get(), ctx->errhp, (OraText *)bind.query.c_str(), bind.query.size(),
		                             OCI_NTV_SYNTAX, OCI_DEFAULT),
		              ctx->errhp, "Failed to prepare OCI statement");
	} else {
		// Reuse bind statement
		state->stmt = bind.stmt;
	}

	// Debug logging for columns
	if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
		ub4 param_count = 0;
		// Try to get param count (might fail if not described, which is fine/expected if we just prepared)
		OCIAttrGet(state->stmt.get(), OCI_HTYPE_STMT, &param_count, 0, OCI_ATTR_PARAM_COUNT, ctx->errhp);

		fprintf(stderr, "[oracle] InitGlobal: columns=%lu, OCI_ATTR_PARAM_COUNT=%u\n",
		        (unsigned long)bind.column_names.size(), (unsigned)param_count);
		for (idx_t i = 0; i < bind.column_names.size(); i++) {
			fprintf(stderr, "[oracle]   col[%lu]: %s\n", (unsigned long)i, bind.column_names[i].c_str());
		}
	}

	// Bind defines to persistent buffers
	for (idx_t col_idx = 0; col_idx < bind.column_names.size(); col_idx++) {
		ub4 size = 4000; // Default max
		if (col_idx < bind.oci_sizes.size() && bind.oci_sizes[col_idx] > 0) {
			size = bind.oci_sizes[col_idx] * 4; // UTF8 safety
		}

		// Resize for array fetch
		state->buffers[col_idx].resize(size * STANDARD_VECTOR_SIZE);
		state->indicators[col_idx].resize(STANDARD_VECTOR_SIZE);
		state->return_lens[col_idx].resize(STANDARD_VECTOR_SIZE);

		ub2 type = SQLT_STR;
		// Ensure bounds check for original_types
		if (col_idx < bind.original_types.size()) {
			switch (bind.original_types[col_idx].id()) {
			case LogicalTypeId::BIGINT:
				type = SQLT_INT;
				size = sizeof(int64_t);
				state->buffers[col_idx].resize(size * STANDARD_VECTOR_SIZE);
				break;
			case LogicalTypeId::DOUBLE:
				type = SQLT_FLT;
				size = sizeof(double);
				state->buffers[col_idx].resize(size * STANDARD_VECTOR_SIZE);
				break;
			default:
				type = SQLT_STR;
				break;
			}
		}

		CheckOCIError(OCIDefineByPos(state->stmt.get(), &state->defines[col_idx], state->err, col_idx + 1,
		                             state->buffers[col_idx].data(), size, type, state->indicators[col_idx].data(),
		                             state->return_lens[col_idx].data(), nullptr, OCI_DEFAULT),
		              state->err, "Failed to define OCI column");

		CheckOCIError(OCIDefineArrayOfStruct(state->defines[col_idx], state->err, size, sizeof(sb2), sizeof(ub2), 0),
		              state->err, "Failed to set OCI array of struct");
	}
	state->defines_bound = true;
	return std::move(state);
}

void OracleQueryFunction(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &bind_data = (OracleBindData &)*data.bind_data;
	auto &gstate = data.global_state->Cast<OracleScanState>();

	if (gstate.finished) {
		output.SetCardinality(0);
		return;
	}

	auto ctx = gstate.conn_handle->Get();
	sword status;
	idx_t row_count = 0;

	try {
		// Execute cursor once
		if (!gstate.executed) {
			if (bind_data.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
				fprintf(stderr, "[oracle] executing SQL (once): %s\n", bind_data.query.c_str());
			}
			status = OCIStmtExecute(ctx->svchp, gstate.stmt.get(), ctx->errhp, 0, 0, nullptr, nullptr, OCI_DEFAULT);
			CheckOCIError(status, ctx->errhp, "Failed to execute OCI statement (open cursor)");
			gstate.executed = true;
		}

		ub4 rows_fetched = 0;
		status = OCIStmtFetch2(gstate.stmt.get(), ctx->errhp, STANDARD_VECTOR_SIZE, OCI_FETCH_NEXT, 0, OCI_DEFAULT);
		if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO && status != OCI_NO_DATA) {
			CheckOCIError(status, ctx->errhp, "Failed to fetch OCI data");
		}
		OCIAttrGet(gstate.stmt.get(), OCI_HTYPE_STMT, &rows_fetched, 0, OCI_ATTR_ROWS_FETCHED, ctx->errhp);
		if (getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] fetch status=%d rows=%u\n", status, (unsigned)rows_fetched);
		}

		if (status == OCI_NO_DATA && rows_fetched == 0) {
			gstate.finished = true;
			output.SetCardinality(0);
			return;
		}

		for (row_count = 0; row_count < rows_fetched; row_count++) {
			for (idx_t col_idx = 0; col_idx < output.ColumnCount(); col_idx++) {
				if (gstate.indicators[col_idx][row_count] == -1) {
					FlatVector::SetNull(output.data[col_idx], row_count, true);
					continue;
				}

				ub4 element_size = gstate.buffers[col_idx].size() / STANDARD_VECTOR_SIZE;
				char *ptr = (char *)gstate.buffers[col_idx].data() + (row_count * element_size);

				switch (output.GetTypes()[col_idx].id()) {
				case LogicalTypeId::VARCHAR:
				case LogicalTypeId::BLOB: {
					string_t val(ptr, gstate.return_lens[col_idx][row_count]);
					FlatVector::GetData<string_t>(output.data[col_idx])[row_count] =
					    StringVector::AddString(output.data[col_idx], val);
					break;
				}
				case LogicalTypeId::BIGINT:
					FlatVector::GetData<int64_t>(output.data[col_idx])[row_count] = *(int64_t *)ptr;
					break;
				case LogicalTypeId::DOUBLE:
					FlatVector::GetData<double>(output.data[col_idx])[row_count] = *(double *)ptr;
					break;
				case LogicalTypeId::TIMESTAMP:
					FlatVector::GetData<timestamp_t>(output.data[col_idx])[row_count] =
					    ParseOciTimestamp(ptr, gstate.return_lens[col_idx][row_count]);
					break;
				}
			}
		}
		output.SetCardinality(row_count);
		if (status == OCI_NO_DATA) {
			gstate.finished = true;
		}
	} catch (...) {
		throw;
	}
}

static string ColumnRefSQL(const string &col_name) {
	return KeywordHelper::WriteQuoted(col_name, '"');
}

static bool ConstantToSQL(Expression &expr, string &out_sql) {
	if (expr.type != ExpressionType::VALUE_CONSTANT) {
		return false;
	}
	auto &c = expr.Cast<BoundConstantExpression>();
	out_sql = c.value.ToSQLString();
	return true;
}

static bool TryExtractComparison(Expression &expr, const vector<string> &names, string &out_clause) {
	if (expr.type != ExpressionType::COMPARE_EQUAL && expr.type != ExpressionType::COMPARE_LESSTHAN &&
	    expr.type != ExpressionType::COMPARE_GREATERTHAN && expr.type != ExpressionType::COMPARE_LESSTHANOREQUALTO &&
	    expr.type != ExpressionType::COMPARE_GREATERTHANOREQUALTO) {
		return false;
	}
	auto &cmp = expr.Cast<BoundComparisonExpression>();
	BoundReferenceExpression *col = nullptr;
	Expression *const_expr = nullptr;
	ExpressionType op_type = expr.type;

	if (cmp.left->type == ExpressionType::BOUND_REF && cmp.right->type == ExpressionType::VALUE_CONSTANT) {
		col = &cmp.left->Cast<BoundReferenceExpression>();
		const_expr = cmp.right.get();
	} else if (cmp.right->type == ExpressionType::BOUND_REF && cmp.left->type == ExpressionType::VALUE_CONSTANT) {
		col = &cmp.right->Cast<BoundReferenceExpression>();
		const_expr = cmp.left.get();
		// flip operator
		switch (op_type) {
		case ExpressionType::COMPARE_GREATERTHAN:
			op_type = ExpressionType::COMPARE_LESSTHAN;
			break;
		case ExpressionType::COMPARE_GREATERTHANOREQUALTO:
			op_type = ExpressionType::COMPARE_LESSTHANOREQUALTO;
			break;
		case ExpressionType::COMPARE_LESSTHAN:
			op_type = ExpressionType::COMPARE_GREATERTHAN;
			break;
		case ExpressionType::COMPARE_LESSTHANOREQUALTO:
			op_type = ExpressionType::COMPARE_GREATERTHANOREQUALTO;
			break;
		default:
			break;
		}
	} else {
		return false;
	}

	if (col->index >= names.size()) {
		return false;
	}
	string const_sql;
	if (!ConstantToSQL(*const_expr, const_sql)) {
		return false;
	}

	string op;
	switch (op_type) {
	case ExpressionType::COMPARE_EQUAL:
		op = "=";
		break;
	case ExpressionType::COMPARE_GREATERTHAN:
		op = ">";
		break;
	case ExpressionType::COMPARE_GREATERTHANOREQUALTO:
		op = ">=";
		break;
	case ExpressionType::COMPARE_LESSTHAN:
		op = "<";
		break;
	case ExpressionType::COMPARE_LESSTHANOREQUALTO:
		op = "<=";
		break;
	default:
		return false;
	}

	out_clause = ColumnRefSQL(names[col->index]) + " " + op + " " + const_sql;
	return true;
}

static bool TryExtractIsNull(Expression &expr, const vector<string> &names, string &out_clause) {
	if (expr.type != ExpressionType::OPERATOR_IS_NULL) {
		return false;
	}
	auto &op = expr.Cast<BoundOperatorExpression>();
	if (op.children.size() != 1 || op.children[0]->type != ExpressionType::BOUND_REF) {
		return false;
	}
	auto &col = op.children[0]->Cast<BoundReferenceExpression>();
	if (col.index >= names.size()) {
		return false;
	}
	out_clause = ColumnRefSQL(names[col.index]) + " IS NULL";
	return true;
}

void OraclePushdownComplexFilter(ClientContext &, LogicalGet &get, FunctionData *bind_data_p,
                                 vector<unique_ptr<Expression>> &expressions) {
	auto &bind = bind_data_p->Cast<OracleBindData>();
	if (!bind.settings.enable_pushdown) {
		return;
	}
	vector<unique_ptr<Expression>> remaining;
	vector<string> clauses;
	for (auto &expr : expressions) {
		string clause;
		if (TryExtractComparison(*expr, bind.column_names, clause) ||
		    TryExtractIsNull(*expr, bind.column_names, clause)) {
			clauses.push_back(std::move(clause));
			continue;
		}
		remaining.push_back(std::move(expr));
	}

	string where_sql;
	if (!clauses.empty()) {
		where_sql = " WHERE " + StringUtil::Join(clauses, " AND ");
	}

	// Projection pushdown (optional): if planner provided projection_ids, select only those columns.
	vector<string> projected_names = bind.original_names;
	vector<LogicalType> projected_types = bind.original_types;
	vector<ub2> projected_oci_types = bind.oci_types;
	vector<ub4> projected_oci_sizes = bind.oci_sizes;

	if (!get.projection_ids.empty()) {
		projected_names.clear();
		projected_types.clear();
		projected_oci_types.clear();
		projected_oci_sizes.clear();
		projected_names.reserve(get.projection_ids.size());
		projected_types.reserve(get.projection_ids.size());
		for (auto idx : get.projection_ids) {
			if (idx >= bind.original_names.size()) {
				continue;
			}
			projected_names.push_back(bind.original_names[idx]);
			projected_types.push_back(bind.original_types[idx]);
			projected_oci_types.push_back(bind.oci_types[idx]);
			projected_oci_sizes.push_back(bind.oci_sizes[idx]);
		}
		get.names = projected_names;
		get.returned_types = projected_types;
	}

	// Build SELECT list from projected names (or all).
	vector<string> select_list;
	select_list.reserve(projected_names.size());
	for (auto &n : projected_names) {
		select_list.push_back(ColumnRefSQL(n));
	}
	auto select_sql = StringUtil::Join(select_list, ", ");

	bind.column_names = projected_names;
	bind.oci_types = projected_oci_types;
	bind.oci_sizes = projected_oci_sizes;

	bind.query = "SELECT " + select_sql + " FROM (" + bind.base_query + ")" + where_sql;
	if (bind.settings.debug_show_queries) {
		fprintf(stderr, "[oracle] pushdown query: %s\n", bind.query.c_str());
	}
	expressions = std::move(remaining);
}

static void OracleAttachWallet(DataChunk &args, ExpressionState &state, Vector &result) {
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

static void LoadInternal(ExtensionLoader &loader) {
	// Register Oracle secret type
	SecretType secret_type;
	secret_type.name = "oracle";
	secret_type.deserializer = KeyValueSecret::Deserialize<KeyValueSecret>;
	secret_type.default_provider = "config";
	loader.RegisterSecretType(secret_type);

	// Register Oracle secret function for "config" provider
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
	oracle_scan_func.filter_pushdown = true;
	oracle_scan_func.pushdown_complex_filter = OraclePushdownComplexFilter;
	oracle_scan_func.projection_pushdown = true;
	loader.RegisterFunction(oracle_scan_func);

	auto oracle_query_func = TableFunction("oracle_query", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                       OracleQueryFunction, OracleQueryBind, OracleInitGlobal, nullptr);
	oracle_query_func.filter_pushdown = true;
	oracle_query_func.pushdown_complex_filter = OraclePushdownComplexFilter;
	oracle_query_func.projection_pushdown = true;
	loader.RegisterFunction(oracle_query_func);

	auto attach_wallet_func =
	    ScalarFunction("oracle_attach_wallet", {LogicalType::VARCHAR}, LogicalType::VARCHAR, OracleAttachWallet);
	loader.RegisterFunction(attach_wallet_func);

	auto clear_cache_func = ScalarFunction("oracle_clear_cache", {}, LogicalType::VARCHAR, OracleClearCache);
	loader.RegisterFunction(clear_cache_func);

	auto oracle_execute_func = ScalarFunction("oracle_execute", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                          LogicalType::VARCHAR, OracleExecuteFunction);
	loader.RegisterFunction(oracle_execute_func);

	// Env helper functions
	auto oracle_env_func = ScalarFunction("oracle_env", {LogicalType::VARCHAR, LogicalType::VARCHAR},
	                                      LogicalType::VARCHAR, OracleEnvFunction);
	loader.RegisterFunction(oracle_env_func);
}

void OracleExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
	auto &db = loader.GetDatabaseInstance();
	auto &config = DBConfig::GetConfig(db);
	config.AddExtensionOption("oracle_enable_pushdown", "Enable Oracle filter/projection pushdown",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(false));
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

	config.storage_extensions["oracle"] = CreateOracleStorageExtension();
}

std::string OracleExtension::Name() {
	return "oracle";
}

std::string OracleExtension::Version() const {
	return "1.0.0";
}

extern "C" {

DUCKDB_EXTENSION_API void oracle_init(duckdb::DatabaseInstance &db) {
	duckdb::DuckDB db_wrapper(db);
	db_wrapper.LoadStaticExtension<duckdb::OracleExtension>();
}

DUCKDB_EXTENSION_API void oracle_duckdb_cpp_init(duckdb::DatabaseInstance &db) {
	oracle_init(db);
}

DUCKDB_EXTENSION_API const char *oracle_version() {
	return duckdb::DuckDB::LibraryVersion();
}

} // extern "C"
} // namespace duckdb
