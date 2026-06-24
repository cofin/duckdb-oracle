#include "oracle_table_function.hpp"
#include "oracle_catalog_state.hpp"
#include "oracle_connection_manager.hpp"
#include "oracle_connection_resolver.hpp"
#include "oracle_debug_stats.hpp"
#include "oracle_type_conversion.hpp"
#include "oracle_type_registry.hpp"
#include "oracle_utils.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include <cstdio>

namespace duckdb {

static string TryGetParamStringAttr(OCIParam *param, OCIError *errhp, ub4 attr) {
	OraText *text = nullptr;
	ub4 text_len = 0;
	auto status = OCIAttrGet(param, OCI_DTYPE_PARAM, &text, &text_len, attr, errhp);
	if (status != OCI_SUCCESS || !text || text_len == 0) {
		return string();
	}
	return string(reinterpret_cast<char *>(text), text_len);
}

OracleBindData::OracleBindData() {
}

unique_ptr<FunctionData> OracleBindData::Copy() const {
	auto copy = make_uniq<OracleBindData>();
	copy->connection_string = connection_string;
	copy->wallet_path = wallet_path;
	copy->base_query = base_query;
	copy->query = query;
	copy->oci_types = oci_types;
	copy->oci_sizes = oci_sizes;
	copy->oracle_type_names = oracle_type_names;
	copy->pushdown_eligible = pushdown_eligible;
	copy->pushdown_clauses = pushdown_clauses;
	copy->column_names = column_names;
	copy->original_types = original_types;
	copy->original_names = original_names;
	copy->settings = settings;
	copy->stmt = stmt; // Copy shared pointer
	return copy;
}

bool OracleBindData::Equals(const FunctionData &other) const {
	auto &other_bind_data = (const OracleBindData &)other;
	return query == other_bind_data.query && connection_string == other_bind_data.connection_string &&
	       wallet_path == other_bind_data.wallet_path;
}

unique_ptr<FunctionData> OracleBindInternal(ClientContext &context, string connection_string, string query,
                                            vector<LogicalType> &return_types, vector<string> &names,
                                            OracleBindData *bind_data_ptr, OracleCatalogState *state,
                                            bool reject_bare_identifier, const char *surface) {
	auto result = bind_data_ptr ? unique_ptr<OracleBindData>(bind_data_ptr) : make_uniq<OracleBindData>();
	auto resolved = ResolveOracleConnection(context, connection_string, state, reject_bare_identifier, surface);
	result->connection_string = resolved.connection_string;
	result->wallet_path = resolved.wallet_path;

	// Clear output vectors - they will be populated from OCI describe below.
	// This prevents duplication when caller pre-populates vectors (e.g., GetScanFunction).
	names.clear();
	return_types.clear();
	result->base_query = query;
	result->query = query;
	result->settings = resolved.settings;
	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] resolved connection reference for %s\n", surface);
	}
	result->conn_handle =
	    OracleConnectionManager::Instance().Acquire(result->connection_string, result->wallet_path, result->settings);
	auto ctx = result->conn_handle->Get();

	// Allocate statement handle on the shared environment and keep it for fetch phase
	result->stmt = AllocateSharedOCIStatement(ctx->envhp, ctx->errhp, "Failed to allocate OCI statement handle");

	// Bound call timeout for describe/execute to avoid hangs
	ub4 call_timeout_ms = 30000; // 30s (per-call upper bound)
	// Some OCI clients reject statement call timeout attributes (ORA-24315).
	// Keep this best-effort rather than failing otherwise valid scans.
	OCIAttrSet(result->stmt.get(), OCI_HTYPE_STMT, &call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, ctx->errhp);

	// Set prefetch/array tuning from settings
	ub4 prefetch_rows = result->settings.prefetch_rows;
	CheckOCIError(OCIAttrSet(result->stmt.get(), OCI_HTYPE_STMT, &prefetch_rows, 0, OCI_ATTR_PREFETCH_ROWS, ctx->errhp),
	              ctx->errhp, "Failed to set OCI prefetch rows");
	if (result->settings.prefetch_memory > 0) {
		ub4 prefetch_mem = result->settings.prefetch_memory;
		CheckOCIError(
		    OCIAttrSet(result->stmt.get(), OCI_HTYPE_STMT, &prefetch_mem, 0, OCI_ATTR_PREFETCH_MEMORY, ctx->errhp),
		    ctx->errhp, "Failed to set OCI prefetch memory");
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

	vector<OracleTypeDecision> type_decisions;
	type_decisions.reserve(param_count);
	OracleVersionInfo conversion_version;
	if (state) {
		conversion_version = state->GetVersionInfo();
	} else {
		// A direct OCI describe result that reports JSON/VECTOR already proves server support.
		conversion_version.supports_json_type = true;
		conversion_version.supports_vector = true;
		conversion_version.supports_vector_serialize = true;
	}

	for (ub4 i = 1; i <= param_count; i++) {
		OCIParam *param_raw = nullptr;
		CheckOCIError(OCIParamGet(result->stmt.get(), OCI_HTYPE_STMT, ctx->errhp, (dvoid **)&param_raw, i), ctx->errhp,
		              "Failed to get OCI parameter");
		OCIDescriptorPtr<OCIParam> param(param_raw, OCIDescriptorFreeDeleter {OCI_DTYPE_PARAM});

		ub2 data_type;
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &data_type, 0, OCI_ATTR_DATA_TYPE, ctx->errhp),
		              ctx->errhp, "Failed to get OCI data type");

		OraText *col_name;
		ub4 col_name_len;
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &col_name, &col_name_len, OCI_ATTR_NAME, ctx->errhp),
		              ctx->errhp, "Failed to get OCI column name");

		names.emplace_back((char *)col_name, col_name_len);
		result->column_names.emplace_back((char *)col_name, col_name_len);
		result->oci_types.push_back(data_type);

		ub2 precision = 0;
		sb1 scale = 0;
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &precision, 0, OCI_ATTR_PRECISION, ctx->errhp),
		              ctx->errhp, "Failed to get OCI precision");
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &scale, 0, OCI_ATTR_SCALE, ctx->errhp), ctx->errhp,
		              "Failed to get OCI scale");

		ub4 char_len = 0;
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &char_len, 0, OCI_ATTR_CHAR_SIZE, ctx->errhp),
		              ctx->errhp, "Failed to get OCI char size");
		result->oci_sizes.push_back(char_len > 0 ? char_len : 4000); // Default buffer size

		OracleTypeMetadata type_metadata;
		type_metadata.column_name = result->column_names.back();
		type_metadata.oci_type = data_type;
		type_metadata.has_oci_type = true;
		type_metadata.type_name = TryGetParamStringAttr(param.get(), ctx->errhp, OCI_ATTR_TYPE_NAME);
		type_metadata.oracle_data_type = type_metadata.type_name;
		type_metadata.type_owner = TryGetParamStringAttr(param.get(), ctx->errhp, OCI_ATTR_SCHEMA_NAME);
		type_metadata.precision = precision;
		type_metadata.scale = scale;
		type_metadata.has_scale = true;
		type_metadata.char_length = char_len;
		auto decision = OracleTypeRegistry::ResolveOciDescribe(type_metadata, result->settings);
		OracleTypeRegistry::ValidateSupported(decision, type_metadata);
		result->oracle_type_names.push_back(decision.normalized_type);
		result->pushdown_eligible.push_back(decision.pushdown_eligible);
		if (decision.category == OracleTypeCategory::JSON) {
			result->oci_sizes.back() = MaxValue<ub4>(result->oci_sizes.back(), 32767);
		}
		return_types.push_back(decision.duckdb_type);
		type_decisions.push_back(decision);
	}

	vector<string> converted_select_list;
	converted_select_list.reserve(result->column_names.size());
	bool needs_wrapper = false;
	for (idx_t i = 0; i < result->column_names.size(); i++) {
		auto quoted_col = KeywordHelper::WriteQuoted(result->column_names[i], '"');
		auto &decision = type_decisions[i];
		if (decision.RequiresQueryRewrite(conversion_version, result->settings.try_native_lobs)) {
			needs_wrapper = true;
			converted_select_list.push_back(StringUtil::Format(
			    "%s AS %s", decision.ConversionExpression(quoted_col, conversion_version).c_str(), quoted_col.c_str()));
		} else {
			converted_select_list.push_back(quoted_col);
		}
	}
	if (needs_wrapper) {
		result->base_query = "SELECT " + StringUtil::Join(converted_select_list, ", ") + " FROM (" + query + ")";
		result->query = result->base_query;
		result->stmt.reset();
	}

	result->original_types = return_types;
	result->original_names = names;

	// Reset handles to avoid holding connections in plan cache
	result->stmt.reset();
	result->conn_handle.reset();

	return result;
}

unique_ptr<FunctionData> OracleScanBind(ClientContext &context, TableFunctionBindInput &input,
                                        vector<LogicalType> &return_types, vector<string> &names) {
	auto connection_string = input.inputs[0].GetValue<string>();
	auto schema_name = input.inputs[1].GetValue<string>();
	auto table_name = input.inputs[2].GetValue<string>();
	auto quoted_schema = KeywordHelper::WriteQuoted(schema_name, '"');
	auto quoted_table = KeywordHelper::WriteQuoted(table_name, '"');
	string query = StringUtil::Format("SELECT * FROM %s.%s", quoted_schema.c_str(), quoted_table.c_str());
	return OracleBindInternal(context, connection_string, query, return_types, names, nullptr, nullptr, true,
	                          "oracle_scan");
}

unique_ptr<FunctionData> OracleQueryBind(ClientContext &context, TableFunctionBindInput &input,
                                         vector<LogicalType> &return_types, vector<string> &names) {
	auto connection_string = input.inputs[0].GetValue<string>();

	auto query = input.inputs[1].GetValue<string>();
	return OracleBindInternal(context, connection_string, query, return_types, names, nullptr, nullptr, true,
	                          "oracle_query");
}

static vector<idx_t> BuildOutputColumnMapping(const vector<idx_t> &column_ids, const vector<idx_t> &projection_ids,
                                              idx_t projected_column_count) {
	vector<idx_t> mapping;
	if (!column_ids.empty()) {
		mapping.reserve(column_ids.size());
		for (auto column_id : column_ids) {
			mapping.push_back(column_id);
		}
		return mapping;
	}
	if (!projection_ids.empty()) {
		mapping.reserve(projection_ids.size());
		for (auto projection_id : projection_ids) {
			mapping.push_back(projection_id);
		}
		return mapping;
	}
	mapping.reserve(projected_column_count);
	for (idx_t i = 0; i < projected_column_count; i++) {
		mapping.push_back(i);
	}
	return mapping;
}

unique_ptr<GlobalTableFunctionState> OracleInitGlobal(ClientContext &, TableFunctionInitInput &input) {
	auto &bind = input.bind_data->Cast<OracleBindData>();
	auto state = make_uniq<OracleScanState>(bind.column_names.size());

	state->column_mapping = BuildOutputColumnMapping(input.column_ids, input.projection_ids, bind.column_names.size());

	state->conn_handle = bind.conn_handle;
	if (!state->conn_handle) {
		state->conn_handle =
		    OracleConnectionManager::Instance().Acquire(bind.connection_string, bind.wallet_path, bind.settings);
	}

	auto ctx = state->conn_handle->Get();
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

	if (need_reprepare) {
		if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] InitGlobal: re-preparing query: %s\n", bind.query.c_str());
		}

		state->stmt = AllocateSharedOCIStatement(ctx->envhp, ctx->errhp, "Failed to allocate OCI statement handle");

		ub4 call_timeout_ms = 30000;
		// Some OCI clients reject statement call timeout attributes (ORA-24315).
		// Keep this best-effort rather than failing otherwise valid scans.
		OCIAttrSet(state->stmt.get(), OCI_HTYPE_STMT, &call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, ctx->errhp);

		ub4 prefetch_rows = bind.settings.prefetch_rows;
		CheckOCIError(
		    OCIAttrSet(state->stmt.get(), OCI_HTYPE_STMT, &prefetch_rows, 0, OCI_ATTR_PREFETCH_ROWS, ctx->errhp),
		    ctx->errhp, "Failed to set OCI prefetch rows");
		if (bind.settings.prefetch_memory > 0) {
			ub4 prefetch_mem = bind.settings.prefetch_memory;
			CheckOCIError(
			    OCIAttrSet(state->stmt.get(), OCI_HTYPE_STMT, &prefetch_mem, 0, OCI_ATTR_PREFETCH_MEMORY, ctx->errhp),
			    ctx->errhp, "Failed to set OCI prefetch memory");
		}

		CheckOCIError(OCIStmtPrepare(state->stmt.get(), ctx->errhp, (OraText *)bind.query.c_str(), bind.query.size(),
		                             OCI_NTV_SYNTAX, OCI_DEFAULT),
		              ctx->errhp, "Failed to prepare OCI statement");
	} else {
		state->stmt = bind.stmt;
	}

	if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
		ub4 param_count = 0;
		OCIAttrGet(state->stmt.get(), OCI_HTYPE_STMT, &param_count, 0, OCI_ATTR_PARAM_COUNT, ctx->errhp);

		fprintf(stderr, "[oracle] InitGlobal: columns=%lu, OCI_ATTR_PARAM_COUNT=%u\n",
		        (unsigned long)bind.column_names.size(), (unsigned)param_count);
		for (idx_t i = 0; i < bind.column_names.size(); i++) {
			fprintf(stderr, "[oracle]   col[%lu]: %s\n", (unsigned long)i, bind.column_names[i].c_str());
		}
	}

	for (idx_t col_idx = 0; col_idx < bind.column_names.size(); col_idx++) {
		state->fetch_size = OracleEffectiveArraySize(bind.settings);
		ub4 size = 4000; // Default max
		if (col_idx < bind.oci_sizes.size() && bind.oci_sizes[col_idx] > 0) {
			size = bind.oci_sizes[col_idx] * 4; // UTF8 safety
		}
		if (size < 4000) {
			size = 4000;
		}
		if (size > NumericLimits<ub2>::Maximum()) {
			size = NumericLimits<ub2>::Maximum();
		}

		state->indicators[col_idx].resize(state->fetch_size);
		state->return_lens[col_idx].resize(state->fetch_size);

		if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] DefineCol[%lu]: name=%s size=%u oci_size=%lu original_type=%s\n",
			        (unsigned long)col_idx, bind.column_names[col_idx].c_str(), (unsigned)size,
			        col_idx < bind.oci_sizes.size() ? (unsigned long)bind.oci_sizes[col_idx] : 0UL,
			        col_idx < bind.original_types.size() ? bind.original_types[col_idx].ToString().c_str() : "N/A");
		}

		ub2 type = SQLT_STR;
		if (col_idx < bind.original_types.size()) {
			switch (bind.original_types[col_idx].id()) {
			case LogicalTypeId::BIGINT:
			case LogicalTypeId::DOUBLE:
				type = SQLT_STR;
				break;
			case LogicalTypeId::BLOB:
				if (col_idx < bind.oci_types.size()) {
					type = SQLT_BIN;
				} else {
					type = SQLT_STR;
				}
				state->buffers[col_idx].resize(size * state->fetch_size);
				break;
			case LogicalTypeId::VARCHAR:
				type = SQLT_STR;
				state->buffers[col_idx].resize(size * state->fetch_size);
				break;
			default:
				type = SQLT_STR;
				break;
			}
		}
		state->buffers[col_idx].resize(size * state->fetch_size);

		CheckOCIError(OCIDefineByPos(state->stmt.get(), &state->defines[col_idx], state->err, col_idx + 1,
		                             state->buffers[col_idx].data(), size, type, state->indicators[col_idx].data(),
		                             state->return_lens[col_idx].data(), nullptr, OCI_DEFAULT),
		              state->err, "Failed to define OCI column");

		CheckOCIError(OCIDefineArrayOfStruct(state->defines[col_idx], state->err, size, sizeof(sb2), sizeof(ub2), 0),
		              state->err, "Failed to set OCI array of struct");
	}
	idx_t scan_buffer_bytes = 0;
	for (auto &buffer : state->buffers) {
		scan_buffer_bytes += buffer.size();
	}
	OracleDebugRecordScanBufferBytes(scan_buffer_bytes);
	return state;
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

	if (!gstate.executed) {
		if (bind_data.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] executing SQL (once): %s\n", bind_data.query.c_str());
		}
		status = OCIStmtExecute(ctx->svchp, gstate.stmt.get(), ctx->errhp, 0, 0, nullptr, nullptr, OCI_DEFAULT);
		CheckOCIError(status, ctx->errhp, "Failed to execute OCI statement (open cursor)");
		gstate.executed = true;
	}

	ub4 rows_fetched = 0;
	status = OCIStmtFetch2(gstate.stmt.get(), ctx->errhp, static_cast<ub4>(gstate.fetch_size), OCI_FETCH_NEXT, 0,
	                       OCI_DEFAULT);
	if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO && status != OCI_NO_DATA) {
		CheckOCIError(status, ctx->errhp, "Failed to fetch OCI data");
	}
	OCIAttrGet(gstate.stmt.get(), OCI_HTYPE_STMT, &rows_fetched, 0, OCI_ATTR_ROWS_FETCHED, ctx->errhp);
	OracleDebugRecordFetch(rows_fetched);
	if (status == OCI_SUCCESS_WITH_INFO) {
		throw IOException("Oracle fetch returned diagnostic information; a value may exceed the bounded fetch buffer");
	}
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
			idx_t buffer_idx = col_idx;
			if (col_idx < gstate.column_mapping.size()) {
				buffer_idx = gstate.column_mapping[col_idx];
			} else if (gstate.column_mapping.empty()) {
				buffer_idx = col_idx;
			}

			if (buffer_idx >= gstate.indicators.size()) {
				continue;
			}

			if (gstate.indicators[buffer_idx][row_count] == -2) {
				throw IOException("Oracle value for column \"%s\" exceeds the bounded fetch buffer",
				                  bind_data.column_names[buffer_idx].c_str());
			}
			if (gstate.indicators[buffer_idx][row_count] == -1) {
				FlatVector::SetNull(output.data[col_idx], row_count, true);
				continue;
			}

			ub4 element_size = gstate.buffers[buffer_idx].size() / gstate.fetch_size;
			char *ptr = (char *)gstate.buffers[buffer_idx].data() + (row_count * element_size);
			ub2 actual_len = gstate.return_lens[buffer_idx][row_count];

			auto column_name = buffer_idx < bind_data.column_names.size()
			                       ? bind_data.column_names[buffer_idx]
			                       : StringUtil::Format("#%llu", static_cast<unsigned long long>(buffer_idx));
			auto oracle_type =
			    buffer_idx < bind_data.oracle_type_names.size()
			        ? bind_data.oracle_type_names[buffer_idx]
			        : (buffer_idx < bind_data.oci_types.size()
			               ? StringUtil::Format("OCI type %u", static_cast<unsigned>(bind_data.oci_types[buffer_idx]))
			               : "unknown OCI type");
			OracleConversionContext conversion_context {column_name, oracle_type, output.GetTypes()[col_idx],
			                                            row_count};
			SetOracleOutputValue(context, output.data[col_idx], row_count, ptr, actual_len, output.GetTypes()[col_idx],
			                     conversion_context);
		}
	}
	output.SetCardinality(row_count);
	if (status == OCI_NO_DATA) {
		gstate.finished = true;
	}
}

} // namespace duckdb
