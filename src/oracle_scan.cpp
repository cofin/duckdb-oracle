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
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include <cstdio>

namespace duckdb {

static string TryGetParamStringAttr(OCIParam *param, OCIError *errhp, ub4 attr) {
	OraText *text = nullptr;
	ub4 text_len = 0;
	auto status = OCIAttrGet(param, OCI_DTYPE_PARAM, reinterpret_cast<dvoid *>(&text), &text_len, attr, errhp);
	if (status != OCI_SUCCESS || !text || text_len == 0) {
		return string();
	}
	return string(reinterpret_cast<char *>(text), text_len);
}

static vector<string> SplitTopLevelSelectExpressions(const string &select_list_sql) {
	vector<string> result;
	if (select_list_sql.empty()) {
		return result;
	}
	int paren_depth = 0;
	bool in_single_quote = false;
	bool in_double_quote = false;
	idx_t token_start = 0;
	for (idx_t i = 0; i < select_list_sql.size(); i++) {
		char c = select_list_sql[i];
		if (c == '\'' && !in_double_quote) {
			in_single_quote = !in_single_quote;
		} else if (c == '"' && !in_single_quote) {
			in_double_quote = !in_double_quote;
		} else if (!in_single_quote && !in_double_quote) {
			if (c == '(') {
				paren_depth++;
			} else if (c == ')' && paren_depth > 0) {
				paren_depth--;
			} else if (c == ',' && paren_depth == 0) {
				auto item = select_list_sql.substr(token_start, i - token_start);
				StringUtil::Trim(item);
				result.push_back(std::move(item));
				token_start = i + 1;
			}
		}
	}
	auto last_item = select_list_sql.substr(token_start);
	StringUtil::Trim(last_item);
	if (!last_item.empty()) {
		result.push_back(std::move(last_item));
	}
	return result;
}

OracleBindData::OracleBindData() {
}

unique_ptr<FunctionData> OracleBindData::Copy() const {
	auto copy = make_uniq<OracleBindData>();
	copy->connection_string = connection_string;
	copy->wallet_path = wallet_path;
	copy->base_query = base_query;
	copy->query = query;
	copy->direct_from_sql = direct_from_sql;
	copy->direct_select_list_sql = direct_select_list_sql;
	copy->oci_types = oci_types;
	copy->oci_sizes = oci_sizes;
	copy->oracle_type_names = oracle_type_names;
	copy->pushdown_eligible = pushdown_eligible;
	copy->pushdown_clauses = pushdown_clauses;
	copy->column_names = column_names;
	copy->original_types = original_types;
	copy->original_names = original_names;
	copy->converted_select_exprs = converted_select_exprs;
	copy->settings = settings;
	return copy;
}

bool OracleBindData::Equals(const FunctionData &other) const {
	const auto &other_bind_data = other.Cast<OracleBindData>();
	return query == other_bind_data.query && connection_string == other_bind_data.connection_string &&
	       wallet_path == other_bind_data.wallet_path;
}

unique_ptr<FunctionData> OracleBindInternal(ClientContext &context, const string &connection_string,
                                            const string &query, vector<LogicalType> &return_types,
                                            vector<string> &names, OracleBindData *bind_data_ptr,
                                            OracleCatalogState *state, bool reject_bare_identifier,
                                            const char *surface) {
	auto result = bind_data_ptr ? unique_ptr<OracleBindData>(bind_data_ptr) : make_uniq<OracleBindData>();
	auto resolved = ResolveOracleConnection(context, connection_string, state, reject_bare_identifier, surface);
	result->connection_string = resolved.connection_string;
	result->wallet_path = resolved.wallet_path;

	names.clear();
	return_types.clear();
	result->column_names.clear();
	result->oci_types.clear();
	result->oci_sizes.clear();
	result->oracle_type_names.clear();
	result->pushdown_eligible.clear();
	result->converted_select_exprs.clear();
	result->base_query = query;
	result->query = query;
	result->settings = resolved.settings;
	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] resolved connection reference for %s\n", surface);
	}
	OracleVersionInfo conversion_version;
	if (state) {
		conversion_version = state->GetVersionInfo();
	} else if (resolved.catalog_state) {
		conversion_version = resolved.catalog_state->GetVersionInfo();
	} else {
		conversion_version.supports_json_type = true;
		conversion_version.supports_vector = true;
		conversion_version.supports_vector_serialize = true;
	}

	auto conn_handle =
	    OracleConnectionManager::Instance().Acquire(result->connection_string, result->wallet_path, result->settings);
	auto ctx = conn_handle->Get();

	auto stmt = AllocateSharedOCIStatement(ctx->envhp, ctx->errhp, "Failed to allocate OCI statement handle");
	OracleDebugRecordSettings(result->settings);

	ub4 call_timeout_ms = static_cast<ub4>(result->settings.call_timeout_ms);
	if (call_timeout_ms > 0) {
		CheckOCIOptionalAttributeORA24315(
		    OCIAttrSet(stmt.get(), OCI_HTYPE_STMT, &call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, ctx->errhp), ctx->errhp,
		    "Failed to set OCI statement call timeout");
	}

	ub4 prefetch_rows = static_cast<ub4>(OracleEffectivePrefetchRows(result->settings));
	CheckOCIError(OCIAttrSet(stmt.get(), OCI_HTYPE_STMT, &prefetch_rows, 0, OCI_ATTR_PREFETCH_ROWS, ctx->errhp),
	              ctx->errhp, "Failed to set OCI prefetch rows");
	auto effective_prefetch_memory = OracleEffectivePrefetchMemory(result->settings);
	if (effective_prefetch_memory > 0) {
		ub4 prefetch_mem = static_cast<ub4>(effective_prefetch_memory);
		CheckOCIError(OCIAttrSet(stmt.get(), OCI_HTYPE_STMT, &prefetch_mem, 0, OCI_ATTR_PREFETCH_MEMORY, ctx->errhp),
		              ctx->errhp, "Failed to set OCI prefetch memory");
	}

	sword status;

	if (result->settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] prepare (bind): %s\n", result->query.c_str());
	}
	status = OCIStmtPrepare(stmt.get(), ctx->errhp, reinterpret_cast<const OraText *>(result->query.c_str()),
	                        result->query.size(), OCI_NTV_SYNTAX, OCI_DEFAULT);
	CheckOCIError(status, ctx->errhp, "Failed to prepare OCI statement");

	status = OCIStmtExecute(ctx->svchp, stmt.get(), ctx->errhp, 0, 0, nullptr, nullptr, OCI_DESCRIBE_ONLY);
	CheckOCIError(status, ctx->errhp, "Failed to execute OCI statement (Describe)");

	ub4 param_count = 0;
	CheckOCIError(OCIAttrGet(stmt.get(), OCI_HTYPE_STMT, &param_count, nullptr, OCI_ATTR_PARAM_COUNT, ctx->errhp),
	              ctx->errhp, "Failed to get OCI parameter count");

	vector<OracleTypeDecision> type_decisions;
	type_decisions.reserve(param_count);

	for (ub4 i = 1; i <= param_count; i++) {
		OCIParam *param_raw = nullptr;
		CheckOCIError(OCIParamGet(stmt.get(), OCI_HTYPE_STMT, ctx->errhp, reinterpret_cast<dvoid **>(&param_raw), i),
		              ctx->errhp, "Failed to get OCI parameter");
		OCIDescriptorPtr<OCIParam> param(param_raw, OCIDescriptorFreeDeleter {OCI_DTYPE_PARAM});

		ub2 data_type = 0;
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &data_type, nullptr, OCI_ATTR_DATA_TYPE, ctx->errhp),
		              ctx->errhp, "Failed to get OCI data type");

		OraText *col_name = nullptr;
		ub4 col_name_len = 0;
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, reinterpret_cast<dvoid *>(&col_name), &col_name_len,
		                         OCI_ATTR_NAME, ctx->errhp),
		              ctx->errhp, "Failed to get OCI column name");

		names.emplace_back(reinterpret_cast<const char *>(col_name), col_name_len);
		result->column_names.emplace_back(reinterpret_cast<const char *>(col_name), col_name_len);
		result->oci_types.push_back(data_type);

		ub2 precision = 0;
		sb1 scale = 0;
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &precision, nullptr, OCI_ATTR_PRECISION, ctx->errhp),
		              ctx->errhp, "Failed to get OCI precision");
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &scale, nullptr, OCI_ATTR_SCALE, ctx->errhp), ctx->errhp,
		              "Failed to get OCI scale");

		ub2 char_len = 0;
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &char_len, nullptr, OCI_ATTR_CHAR_SIZE, ctx->errhp),
		              ctx->errhp, "Failed to get OCI char size");
		ub2 data_size = 0;
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &data_size, nullptr, OCI_ATTR_DATA_SIZE, ctx->errhp),
		              ctx->errhp, "Failed to get OCI data size");
		ub4 col_size = char_len > 0 ? static_cast<ub4>(char_len) : (data_size > 0 ? static_cast<ub4>(data_size) : 4000);
		result->oci_sizes.push_back(col_size);

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
		type_metadata.data_length = data_size;

#ifdef OCI_ATTR_VECTOR_DATA_FORMAT
		if (data_type == SQLT_VEC) {
			ub1 vec_format = 0;
			if (OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &vec_format, nullptr, OCI_ATTR_VECTOR_DATA_FORMAT,
			               ctx->errhp) == OCI_SUCCESS) {
				if (vec_format == 3) {
					type_metadata.oracle_data_type = "VECTOR(*, FLOAT64)";
				} else if (vec_format == 4) {
					type_metadata.oracle_data_type = "VECTOR(*, INT8)";
				} else if (vec_format == 5) {
					type_metadata.oracle_data_type = "VECTOR(*, BINARY)";
				}
			}
		}
#endif

		auto decision = OracleTypeRegistry::ResolveOciDescribe(type_metadata, result->settings);
		OracleTypeRegistry::ValidateSupported(decision, type_metadata);
		result->oracle_type_names.push_back(decision.normalized_type);
		result->pushdown_eligible.push_back(decision.pushdown_eligible);
		if (IsLargeOracleCategory(decision.category)) {
			result->oci_sizes.back() = NumericLimits<ub2>::Maximum();
		}
		return_types.push_back(decision.duckdb_type);
		type_decisions.push_back(decision);
	}

	result->converted_select_exprs.reserve(result->column_names.size());
	bool needs_wrapper = false;
	for (idx_t i = 0; i < result->column_names.size(); i++) {
		auto quoted_col = KeywordHelper::WriteQuoted(result->column_names[i], '"');
		auto &decision = type_decisions[i];
		if (decision.RequiresQueryRewrite(conversion_version)) {
			needs_wrapper = true;
			result->pushdown_eligible[i] = false;
			result->converted_select_exprs.push_back(StringUtil::Format(
			    "%s AS %s", decision.ConversionExpression(quoted_col, conversion_version).c_str(), quoted_col.c_str()));
		} else {
			result->converted_select_exprs.push_back(quoted_col);
		}
	}
	if (needs_wrapper) {
		result->base_query =
		    "SELECT " + StringUtil::Join(result->converted_select_exprs, ", ") + " FROM (\n" + query + "\n)";
		result->query = result->base_query;
	}

	result->original_types = return_types;
	result->original_names = names;

	return result;
}

unique_ptr<FunctionData> OracleScanBind(ClientContext &context, TableFunctionBindInput &input,
                                        vector<LogicalType> &return_types, vector<string> &names) {
	if (input.inputs.size() < 3 || input.inputs[0].IsNull() || input.inputs[1].IsNull() || input.inputs[2].IsNull()) {
		throw InvalidInputException("oracle_scan connection, schema, and table arguments cannot be NULL");
	}
	auto connection_string = input.inputs[0].GetValue<string>();
	auto schema_name = input.inputs[1].GetValue<string>();
	auto table_name = input.inputs[2].GetValue<string>();
	StringUtil::Trim(connection_string);
	StringUtil::Trim(schema_name);
	StringUtil::Trim(table_name);
	if (connection_string.empty() || schema_name.empty() || table_name.empty()) {
		throw InvalidInputException("oracle_scan connection, schema, and table arguments cannot be empty");
	}

	bool has_scn = input.named_parameters.find("as_of_scn") != input.named_parameters.end();
	bool has_ts = input.named_parameters.find("as_of_timestamp") != input.named_parameters.end();
	if (has_scn && has_ts) {
		throw InvalidInputException("oracle_scan cannot specify both as_of_scn and as_of_timestamp");
	}
	string flashback_clause;
	if (has_scn) {
		const auto &scn_val = input.named_parameters.at("as_of_scn");
		if (scn_val.IsNull()) {
			throw InvalidInputException("oracle_scan as_of_scn cannot be NULL");
		}
		auto scn = scn_val.GetValue<uint64_t>();
		auto scn_str = std::to_string(scn);
		flashback_clause = StringUtil::Format(" AS OF SCN %s", scn_str.c_str());
	} else if (has_ts) {
		const auto &ts_val = input.named_parameters.at("as_of_timestamp");
		if (ts_val.IsNull()) {
			throw InvalidInputException("oracle_scan as_of_timestamp cannot be NULL");
		}
		auto ts_str = ts_val.GetValue<string>();
		StringUtil::Trim(ts_str);
		if (ts_str.empty()) {
			throw InvalidInputException("oracle_scan as_of_timestamp cannot be empty");
		}
		timestamp_t parsed_ts;
		if (Timestamp::TryConvertTimestamp(ts_str.c_str(), ts_str.size(), parsed_ts, false) !=
		        TimestampCastResult::SUCCESS ||
		    !Timestamp::IsFinite(parsed_ts)) {
			throw InvalidInputException("oracle_scan as_of_timestamp must be a valid finite timestamp: \"%s\"",
			                            ts_str.c_str());
		}
		auto formatted_ts = Timestamp::ToString(parsed_ts);
		auto escaped_ts = StringUtil::Replace(formatted_ts, "'", "''");
		flashback_clause =
		    StringUtil::Format(" AS OF TIMESTAMP TO_TIMESTAMP('%s', 'YYYY-MM-DD HH24:MI:SS.FF')", escaped_ts.c_str());
	}

	auto quoted_schema = KeywordHelper::WriteQuoted(schema_name, '"');
	auto quoted_table = KeywordHelper::WriteQuoted(table_name, '"');
	auto from_sql =
	    StringUtil::Format("%s.%s%s", quoted_schema.c_str(), quoted_table.c_str(), flashback_clause.c_str());
	string query = StringUtil::Format("SELECT * FROM %s", from_sql.c_str());
	auto result = OracleBindInternal(context, connection_string, query, return_types, names, nullptr, nullptr, true,
	                                 "oracle_scan");
	auto &bind = result->Cast<OracleBindData>();
	bind.direct_from_sql = from_sql;
	bind.direct_select_list_sql = StringUtil::Join(bind.converted_select_exprs, ", ");
	bind.base_query = "SELECT " + bind.direct_select_list_sql + " FROM " + bind.direct_from_sql;
	bind.query = bind.base_query;
	return result;
}

unique_ptr<FunctionData> OracleQueryBind(ClientContext &context, TableFunctionBindInput &input,
                                         vector<LogicalType> &return_types, vector<string> &names) {
	if (input.inputs.size() < 2 || input.inputs[0].IsNull() || input.inputs[1].IsNull()) {
		throw InvalidInputException("oracle_query connection and query arguments cannot be NULL");
	}
	auto connection_string = input.inputs[0].GetValue<string>();
	auto query = input.inputs[1].GetValue<string>();
	StringUtil::Trim(connection_string);
	while (!query.empty() && (StringUtil::CharacterIsSpace(query.back()) || query.back() == ';')) {
		query.pop_back();
	}
	StringUtil::Trim(query);
	if (connection_string.empty() || query.empty()) {
		throw InvalidInputException("oracle_query connection and query arguments cannot be empty");
	}

	auto result = OracleBindInternal(context, connection_string, query, return_types, names, nullptr, nullptr, true,
	                                 "oracle_query");
	auto &bind = result->Cast<OracleBindData>();
	for (idx_t i = 0; i < bind.converted_select_exprs.size() && i < bind.pushdown_eligible.size(); i++) {
		if (StringUtil::Contains(StringUtil::Upper(bind.converted_select_exprs[i]), "TO_CHAR")) {
			bind.pushdown_eligible[i] = false;
		}
	}
	return result;
}

static vector<idx_t> BuildOutputColumnMapping(const vector<idx_t> &column_ids, const vector<idx_t> &projection_ids,
                                              idx_t projected_column_count) {
	vector<idx_t> mapping;
	if (!projection_ids.empty()) {
		mapping.reserve(projection_ids.size());
		for (auto projection_id : projection_ids) {
			if (projection_id < column_ids.size()) {
				mapping.push_back(column_ids[projection_id]);
			} else {
				mapping.push_back(projection_id);
			}
		}
		return mapping;
	}
	if (!column_ids.empty()) {
		mapping.reserve(column_ids.size());
		for (auto column_id : column_ids) {
			mapping.push_back(column_id);
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
	auto state = make_uniq<OracleScanState>(0);

	state->column_mapping = BuildOutputColumnMapping(input.column_ids, input.projection_ids, bind.column_names.size());

	vector<string> col_select_exprs = bind.converted_select_exprs;
	if (col_select_exprs.size() != bind.original_names.size()) {
		auto split_exprs = SplitTopLevelSelectExpressions(bind.direct_select_list_sql);
		if (split_exprs.size() == bind.original_names.size()) {
			col_select_exprs = std::move(split_exprs);
		} else {
			col_select_exprs.clear();
			col_select_exprs.reserve(bind.original_names.size());
			for (const auto &name : bind.original_names) {
				col_select_exprs.push_back(KeywordHelper::WriteQuoted(name, '"'));
			}
		}
	}

	bool cannot_prune_query_columns = bind.direct_from_sql.empty() && HasDuplicateColumnNames(bind.original_names);
	if (cannot_prune_query_columns) {
		state->active_bind_cols.reserve(bind.original_names.size());
		for (idx_t i = 0; i < bind.original_names.size(); i++) {
			state->active_bind_cols.push_back(i);
		}
		state->output_to_active_idx.reserve(state->column_mapping.size());
		for (auto col_id : state->column_mapping) {
			state->output_to_active_idx.push_back(col_id < bind.original_names.size() ? col_id
			                                                                          : DConstants::INVALID_INDEX);
		}
	} else {
		state->output_to_active_idx.reserve(state->column_mapping.size());
		for (auto col_id : state->column_mapping) {
			if (col_id == DConstants::INVALID_INDEX || col_id >= bind.original_names.size()) {
				state->output_to_active_idx.push_back(DConstants::INVALID_INDEX);
				continue;
			}
			idx_t active_idx = DConstants::INVALID_INDEX;
			for (idx_t i = 0; i < state->active_bind_cols.size(); i++) {
				if (state->active_bind_cols[i] == col_id) {
					active_idx = i;
					break;
				}
			}
			if (active_idx == DConstants::INVALID_INDEX) {
				active_idx = state->active_bind_cols.size();
				state->active_bind_cols.push_back(col_id);
			}
			state->output_to_active_idx.push_back(active_idx);
		}
	}

	string where_sql =
	    bind.pushdown_clauses.empty() ? string() : (" WHERE " + StringUtil::Join(bind.pushdown_clauses, " AND "));
	string query_to_run;
	if (!bind.direct_from_sql.empty()) {
		if (state->active_bind_cols.empty()) {
			query_to_run = "SELECT 1 FROM " + bind.direct_from_sql + where_sql;
		} else {
			vector<string> select_items;
			select_items.reserve(state->active_bind_cols.size());
			for (auto bind_col_idx : state->active_bind_cols) {
				select_items.push_back(col_select_exprs[bind_col_idx]);
			}
			query_to_run =
			    "SELECT " + StringUtil::Join(select_items, ", ") + " FROM " + bind.direct_from_sql + where_sql;
		}
	} else {
		bool is_identity = (state->active_bind_cols.size() == bind.original_names.size());
		if (is_identity) {
			for (idx_t i = 0; i < state->active_bind_cols.size(); i++) {
				if (state->active_bind_cols[i] != i) {
					is_identity = false;
					break;
				}
			}
		}
		if (is_identity && bind.pushdown_clauses.empty()) {
			query_to_run = bind.base_query;
		} else if (state->active_bind_cols.empty()) {
			query_to_run = "SELECT 1 FROM (\n" + bind.base_query + "\n)" + where_sql;
		} else {
			vector<string> select_items;
			select_items.reserve(state->active_bind_cols.size());
			for (auto bind_col_idx : state->active_bind_cols) {
				select_items.push_back(KeywordHelper::WriteQuoted(bind.original_names[bind_col_idx], '"'));
			}
			query_to_run =
			    "SELECT " + StringUtil::Join(select_items, ", ") + " FROM (\n" + bind.base_query + "\n)" + where_sql;
		}
	}
	state->executed_query = query_to_run;

	state->conn_handle =
	    OracleConnectionManager::Instance().Acquire(bind.connection_string, bind.wallet_path, bind.settings);
	auto ctx = state->conn_handle->Get();
	state->err = ctx->errhp;

	if (bind.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] InitGlobal: preparing query: %s\n", state->executed_query.c_str());
	}

	state->stmt = AllocateSharedOCIStatement(ctx->envhp, ctx->errhp, "Failed to allocate OCI statement handle");
	OracleDebugRecordSettings(bind.settings);

	ub4 call_timeout_ms = static_cast<ub4>(bind.settings.call_timeout_ms);
	if (call_timeout_ms > 0) {
		CheckOCIOptionalAttributeORA24315(
		    OCIAttrSet(state->stmt.get(), OCI_HTYPE_STMT, &call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, ctx->errhp),
		    ctx->errhp, "Failed to set OCI statement call timeout");
	}

	ub4 prefetch_rows = static_cast<ub4>(OracleEffectivePrefetchRows(bind.settings));
	CheckOCIError(OCIAttrSet(state->stmt.get(), OCI_HTYPE_STMT, &prefetch_rows, 0, OCI_ATTR_PREFETCH_ROWS, ctx->errhp),
	              ctx->errhp, "Failed to set OCI prefetch rows");
	auto effective_prefetch_memory = OracleEffectivePrefetchMemory(bind.settings);
	if (effective_prefetch_memory > 0) {
		ub4 prefetch_mem = static_cast<ub4>(effective_prefetch_memory);
		CheckOCIError(
		    OCIAttrSet(state->stmt.get(), OCI_HTYPE_STMT, &prefetch_mem, 0, OCI_ATTR_PREFETCH_MEMORY, ctx->errhp),
		    ctx->errhp, "Failed to set OCI prefetch memory");
	}

	CheckOCIError(OCIStmtPrepare(state->stmt.get(), ctx->errhp,
	                             reinterpret_cast<const OraText *>(state->executed_query.c_str()),
	                             state->executed_query.size(), OCI_NTV_SYNTAX, OCI_DEFAULT),
	              ctx->errhp, "Failed to prepare OCI statement");

	state->fetch_size = OracleEffectiveArraySize(bind.settings);
	if (state->active_bind_cols.empty()) {
		state->buffers.resize(1);
		state->defines.assign(1, nullptr);
		state->indicators.resize(1);
		state->return_lens.resize(1);

		ub4 dummy_size = 16;
		state->indicators[0].resize(state->fetch_size);
		state->return_lens[0].resize(state->fetch_size);
		state->buffers[0].resize(dummy_size * state->fetch_size);

		CheckOCIError(OCIDefineByPos(state->stmt.get(), &state->defines[0], state->err, 1, state->buffers[0].data(),
		                             static_cast<sb4>(dummy_size), SQLT_STR, state->indicators[0].data(),
		                             state->return_lens[0].data(), nullptr, OCI_DEFAULT),
		              state->err, "Failed to define OCI dummy column");
		CheckOCIError(OCIDefineArrayOfStruct(state->defines[0], state->err, dummy_size, sizeof(sb2), sizeof(ub2), 0),
		              state->err, "Failed to set OCI array of struct");
	} else {
		idx_t active_count = state->active_bind_cols.size();
		state->buffers.resize(active_count);
		state->defines.assign(active_count, nullptr);
		state->indicators.resize(active_count);
		state->return_lens.resize(active_count);

		for (idx_t active_idx = 0; active_idx < active_count; active_idx++) {
			idx_t bind_col_idx = state->active_bind_cols[active_idx];
			ub4 size = 4000;
			if (bind_col_idx < bind.oci_sizes.size() && bind.oci_sizes[bind_col_idx] > 0) {
				if (bind.oci_sizes[bind_col_idx] >= NumericLimits<ub2>::Maximum()) {
					size = NumericLimits<ub2>::Maximum();
				} else {
					size = bind.oci_sizes[bind_col_idx] * 4;
				}
			}
			if (size < 4000) {
				size = 4000;
			}
			if (size > NumericLimits<ub2>::Maximum()) {
				size = NumericLimits<ub2>::Maximum();
			}

			state->indicators[active_idx].resize(state->fetch_size);
			state->return_lens[active_idx].resize(state->fetch_size);

			ub2 type = SQLT_STR;
			if (bind_col_idx < bind.original_types.size() &&
			    bind.original_types[bind_col_idx].id() == LogicalTypeId::BLOB) {
				type = SQLT_BIN;
			}
			state->buffers[active_idx].resize(size * state->fetch_size);

			CheckOCIError(OCIDefineByPos(state->stmt.get(), &state->defines[active_idx], state->err, active_idx + 1,
			                             state->buffers[active_idx].data(), static_cast<sb4>(size), type,
			                             state->indicators[active_idx].data(), state->return_lens[active_idx].data(),
			                             nullptr, OCI_DEFAULT),
			              state->err, "Failed to define OCI column");

			CheckOCIError(
			    OCIDefineArrayOfStruct(state->defines[active_idx], state->err, size, sizeof(sb2), sizeof(ub2), 0),
			    state->err, "Failed to set OCI array of struct");
		}
	}

	idx_t scan_buffer_bytes = 0;
	for (auto &buffer : state->buffers) {
		scan_buffer_bytes += buffer.size();
	}
	OracleDebugRecordScanBufferBytes(scan_buffer_bytes);
	return state;
}

void OracleQueryFunction(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &bind_data = data.bind_data->Cast<OracleBindData>();
	auto &gstate = data.global_state->Cast<OracleScanState>();

	if (gstate.finished) {
		output.SetCardinality(0);
		return;
	}

	auto ctx = gstate.conn_handle->Get();
	sword status;
	idx_t row_count = 0;

	if (!gstate.executed) {
		const auto &sql_to_execute = gstate.executed_query.empty() ? bind_data.query : gstate.executed_query;
		if (bind_data.settings.debug_show_queries || getenv("ORACLE_DEBUG")) {
			fprintf(stderr, "[oracle] executing SQL (once): %s\n", sql_to_execute.c_str());
		}
		OracleDebugRecordQuery(sql_to_execute);
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
	CheckOCIError(
	    OCIAttrGet(gstate.stmt.get(), OCI_HTYPE_STMT, &rows_fetched, nullptr, OCI_ATTR_ROWS_FETCHED, ctx->errhp),
	    ctx->errhp, "Failed to get rows fetched");
	OracleDebugRecordFetch(rows_fetched);
	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] fetch status=%d rows=%u\n", status, static_cast<uint32_t>(rows_fetched));
	}

	for (idx_t r = 0; r < rows_fetched; r++) {
		for (idx_t active_idx = 0; active_idx < gstate.active_bind_cols.size(); active_idx++) {
			sb2 ind = gstate.indicators[active_idx][r];
			if (ind == -2 || ind > 0) {
				idx_t bind_col_idx = gstate.active_bind_cols[active_idx];
				auto col_name = bind_col_idx < bind_data.column_names.size() ? bind_data.column_names[bind_col_idx]
				                                                             : ("#" + std::to_string(bind_col_idx));
				throw IOException("Oracle value for column \"%s\" exceeds the bounded fetch buffer", col_name.c_str());
			}
		}
	}
	if (status == OCI_SUCCESS_WITH_INFO && OCIErrorCodeMatches(ctx->errhp, 1406)) {
		throw IOException("Oracle fetch returned diagnostic information; a value may exceed the bounded fetch buffer");
	}

	if (status == OCI_NO_DATA && rows_fetched == 0) {
		gstate.finished = true;
		output.SetCardinality(0);
		return;
	}

	for (row_count = 0; row_count < rows_fetched; row_count++) {
		for (idx_t col_idx = 0; col_idx < output.ColumnCount(); col_idx++) {
			if (col_idx >= gstate.output_to_active_idx.size()) {
				continue;
			}
			idx_t active_idx = gstate.output_to_active_idx[col_idx];
			if (active_idx == DConstants::INVALID_INDEX || active_idx >= gstate.indicators.size()) {
				continue;
			}
			idx_t bind_col_idx =
			    active_idx < gstate.active_bind_cols.size() ? gstate.active_bind_cols[active_idx] : active_idx;

			if (gstate.indicators[active_idx][row_count] == -1) {
				FlatVector::SetNull(output.data[col_idx], row_count, true);
				continue;
			}

			ub4 element_size = gstate.buffers[active_idx].size() / gstate.fetch_size;
			char *ptr = gstate.buffers[active_idx].data() + (row_count * element_size);
			ub2 actual_len = gstate.return_lens[active_idx][row_count];

			auto column_name = bind_col_idx < bind_data.column_names.size() ? bind_data.column_names[bind_col_idx]
			                                                                : ("#" + std::to_string(bind_col_idx));
			auto oracle_type =
			    bind_col_idx < bind_data.oracle_type_names.size()
			        ? bind_data.oracle_type_names[bind_col_idx]
			        : (bind_col_idx < bind_data.oci_types.size()
			               ? StringUtil::Format("OCI type %u", static_cast<unsigned>(bind_data.oci_types[bind_col_idx]))
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
