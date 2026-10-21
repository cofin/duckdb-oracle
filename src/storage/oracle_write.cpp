#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/time.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/limits.hpp"
#include "oracle_write.hpp"
#include "oracle_connection.hpp"
#include "oracle_debug_stats.hpp"
#include "oracle_utils.hpp"
#include "oracle_connection_manager.hpp"
#include "oracle_type_conversion.hpp"
#include "oracle_type_registry.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace duckdb {

void RejectOracleWriteInExplicitTransaction(ClientContext &context) {
	if (!context.transaction.IsAutoCommit()) {
		throw InvalidInputException("Oracle writes are statement-atomic and cannot run inside an explicit DuckDB "
		                            "transaction block; commit or rollback the DuckDB transaction before writing to "
		                            "Oracle");
	}
}

static void ResolveOracleWriteTargetMetadata(OracleWriteBindData &data) {
	if (data.connection_string.empty()) {
		return;
	}

	OracleConnection temp_conn;
	temp_conn.Connect(data.connection_string, data.wallet_path, data.settings);

	string query;
	vector<string> bind_values;
	if (data.schema_name.empty()) {
		query = "SELECT owner, table_name, column_name, data_type, data_type_owner, data_precision, data_scale, "
		        "data_length, char_used, char_length FROM all_tab_columns "
		        "WHERE owner = SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA') "
		        "AND (table_name = :1 OR table_name = UPPER(:2)) "
		        "ORDER BY CASE WHEN table_name = :3 THEN 0 WHEN table_name = UPPER(:4) THEN 1 ELSE 2 END, "
		        "table_name, column_id";
		bind_values.push_back(data.object_name);
		bind_values.push_back(data.object_name);
		bind_values.push_back(data.object_name);
		bind_values.push_back(data.object_name);
	} else {
		query = "SELECT owner, table_name, column_name, data_type, data_type_owner, data_precision, data_scale, "
		        "data_length, char_used, char_length FROM all_tab_columns "
		        "WHERE (owner = :1 OR owner = UPPER(:2)) "
		        "AND (table_name = :3 OR table_name = UPPER(:4)) "
		        "ORDER BY CASE WHEN owner = :5 THEN 0 WHEN owner = UPPER(:6) THEN 1 ELSE 2 END, "
		        "CASE WHEN table_name = :7 THEN 0 WHEN table_name = UPPER(:8) THEN 1 ELSE 2 END, "
		        "owner, table_name, column_id";
		bind_values.push_back(data.schema_name);
		bind_values.push_back(data.schema_name);
		bind_values.push_back(data.object_name);
		bind_values.push_back(data.object_name);
		bind_values.push_back(data.schema_name);
		bind_values.push_back(data.schema_name);
		bind_values.push_back(data.object_name);
		bind_values.push_back(data.object_name);
	}

	auto query_res = temp_conn.QueryWithStringBinds(query, bind_values);
	if (query_res.rows.empty()) {
		return;
	}

	const string best_owner = query_res.rows[0][0];
	const string best_table_name = query_res.rows[0][1];

	bool has_vector = false;
	for (auto &row : query_res.rows) {
		if (row.size() >= 4 && row[0] == best_owner && row[1] == best_table_name) {
			auto upper_dt = StringUtil::Upper(row[3]);
			if (upper_dt == "VECTOR" || StringUtil::StartsWith(upper_dt, "VECTOR(")) {
				has_vector = true;
				break;
			}
		}
	}

	std::unordered_map<string, string> vec_type_map;
	if (has_vector) {
		try {
			auto vec_res =
			    temp_conn.QueryWithStringBinds("SELECT column_name, vector_dimension_count, vector_data_format "
			                                   "FROM all_vec_columns "
			                                   "WHERE (owner = :1 OR owner = UPPER(:2)) "
			                                   "AND (table_name = :3 OR table_name = UPPER(:4))",
			                                   {best_owner, best_owner, best_table_name, best_table_name});
			for (auto &vrow : vec_res.rows) {
				if (vrow.size() < 3 || vrow[0].empty()) {
					continue;
				}
				auto col_upper = StringUtil::Upper(vrow[0]);
				auto dim_str = vrow[1];
				StringUtil::Trim(dim_str);
				auto fmt_str = StringUtil::Upper(vrow[2]);
				StringUtil::Trim(fmt_str);
				if (!dim_str.empty() || !fmt_str.empty()) {
					vec_type_map[col_upper] =
					    StringUtil::Format("VECTOR(%s, %s)", dim_str.empty() ? "*" : dim_str.c_str(),
					                       fmt_str.empty() ? "*" : fmt_str.c_str());
				}
			}
		} catch (...) {
		}
	}

	std::unordered_map<string, OracleTypeMetadata> col_type_map;
	std::unordered_map<string, string> col_name_map;

	for (auto &row : query_res.rows) {
		if (row.size() < 10) {
			continue;
		}
		const string &owner = row[0];
		const string &table = row[1];
		const string &col = row[2];
		if (owner != best_owner || table != best_table_name) {
			continue;
		}
		auto col_upper = StringUtil::Upper(col);
		string data_type = row[3];
		if (StringUtil::Upper(data_type) == "VECTOR") {
			auto vec_it = vec_type_map.find(col_upper);
			if (vec_it != vec_type_map.end()) {
				data_type = vec_it->second;
			}
		}

		idx_t precision = 0;
		TryParseOracleMetadataIndex(row[5], precision);
		int32_t scale = 0;
		bool has_scale = TryParseOracleMetadataInt(row[6], scale);
		idx_t data_length = 0;
		TryParseOracleMetadataIndex(row[7], data_length);
		idx_t char_length = 0;
		TryParseOracleMetadataIndex(row[9], char_length);

		OracleTypeMetadata metadata;
		metadata.schema_name = owner;
		metadata.table_name = table;
		metadata.column_name = col;
		metadata.oracle_data_type = data_type;
		metadata.type_owner = row[4];
		metadata.precision = precision;
		metadata.scale = scale;
		metadata.has_scale = has_scale;
		metadata.data_length = data_length;
		metadata.char_used = row[8] == "C";
		metadata.char_length = char_length;
		col_type_map[col] = metadata;
		if (!col_name_map.count(col_upper)) {
			col_name_map[col_upper] = col;
		} else {
			col_name_map[col_upper] = "";
		}
	}

	data.object_name = best_table_name;
	data.schema_name = best_owner;

	for (idx_t i = 0; i < data.column_names.size(); i++) {
		auto col_upper = StringUtil::Upper(data.column_names[i]);
		string actual_name;
		if (col_type_map.count(data.column_names[i])) {
			actual_name = data.column_names[i];
		} else if (col_name_map.count(col_upper)) {
			if (col_name_map[col_upper].empty()) {
				throw BinderException("Ambiguous Oracle write target column name \"%s\" after case folding",
				                      data.column_names[i]);
			}
			actual_name = col_name_map[col_upper];
		}

		if (!actual_name.empty()) {
			data.column_names[i] = actual_name;
			if (col_type_map.count(actual_name)) {
				const auto &type_metadata = col_type_map[actual_name];
				auto decision = OracleTypeRegistry::ResolveWrite(type_metadata, data.column_types[i], data.settings);
				OracleTypeRegistry::ValidateSupported(decision, type_metadata);
				data.oracle_types[i] = decision.normalized_type;
				data.bind_types[i] = decision.write_bind_type;
			}
		}
	}
}

static string OracleWritePlaceholderSQL(const string &oracle_type, ub2 bind_type, idx_t param_index) {
	string placeholder = ":" + std::to_string(param_index);
	if (bind_type == SQLT_ODT) {
		return placeholder;
	}

	auto type = StringUtil::Upper(oracle_type);
	if (type == "DATE") {
		return "TO_DATE(" + placeholder + ", 'YYYY-MM-DD HH24:MI:SS')";
	}
	if (type.find("TIMESTAMP") != string::npos) {
		return "TO_TIMESTAMP(" + placeholder + ", 'YYYY-MM-DD HH24:MI:SS.FF')";
	}
	if (type == "SDO_GEOMETRY" || type == "MDSYS.SDO_GEOMETRY") {
		return "SDO_UTIL.FROM_WKTGEOMETRY(" + placeholder + ")";
	}
	return placeholder;
}

static size_t OracleWriteInitialBufferSize(ub2 bind_type) {
	if (bind_type == SQLT_INT || bind_type == SQLT_BDOUBLE) {
		return 8;
	}
	if (bind_type == SQLT_ODT) {
		return sizeof(OCIDate);
	}
	return 4096;
}

static bool IsOracleVectorType(const string &oracle_type) {
	auto type = StringUtil::Upper(oracle_type);
	StringUtil::Trim(type);
	return type == "VECTOR" || StringUtil::StartsWith(type, "VECTOR(");
}

static idx_t ParseOracleVectorDimension(const string &oracle_type) {
	auto type = StringUtil::Upper(oracle_type);
	auto open = type.find('(');
	auto close = type.find(')', open == string::npos ? 0 : open);
	if (open == string::npos || close == string::npos || close <= open + 1) {
		return 0;
	}
	auto args = StringUtil::Split(type.substr(open + 1, close - open - 1), ',');
	if (args.empty()) {
		return 0;
	}
	auto dimension = args[0];
	StringUtil::Trim(dimension);
	if (dimension.empty() || dimension == "*") {
		return 0;
	}
	idx_t parsed_dim = 0;
	if (!TryParseOracleMetadataIndex(dimension, parsed_dim)) {
		return 0;
	}
	return parsed_dim;
}

static string OracleVectorErrorPrefix(const string &column_name, const string &oracle_type) {
	auto column = column_name.empty() ? "<unknown>" : column_name;
	auto type = oracle_type.empty() ? "VECTOR" : oracle_type;
	return StringUtil::Format("Invalid Oracle VECTOR value for column \"%s\" (target %s)", column.c_str(),
	                          type.c_str());
}

static void ValidateOracleVectorTargetFormat(const string &column_name, const string &oracle_type) {
	auto type = StringUtil::Upper(oracle_type);
	if (StringUtil::Contains(type, "INT8") || StringUtil::Contains(type, "BINARY")) {
		throw InvalidInputException("%s: VECTOR INT8 and BINARY target formats are not supported yet",
		                            OracleVectorErrorPrefix(column_name, oracle_type).c_str());
	}
}

static string FormatOracleVector(const vector<double> &values) {
	std::ostringstream result;
	result << "[";
	for (idx_t i = 0; i < values.size(); i++) {
		if (i > 0) {
			result << ", ";
		}
		result << std::setprecision(17) << values[i];
	}
	result << "]";
	return result.str();
}

static void ValidateOracleVectorDimension(const vector<double> &values, const string &column_name,
                                          const string &oracle_type) {
	auto expected_dimension = ParseOracleVectorDimension(oracle_type);
	if (expected_dimension > 0 && values.size() != expected_dimension) {
		auto expected = std::to_string(expected_dimension);
		auto actual = std::to_string(values.size());
		throw InvalidInputException("%s: expected %s dimensions but got %s",
		                            OracleVectorErrorPrefix(column_name, oracle_type).c_str(), expected.c_str(),
		                            actual.c_str());
	}
}

static string NormalizeOracleVectorString(const string &input, const string &column_name, const string &oracle_type) {
	ValidateOracleVectorTargetFormat(column_name, oracle_type);
	auto expected_dimension = ParseOracleVectorDimension(oracle_type);
	auto element_type =
	    StringUtil::Contains(StringUtil::Upper(oracle_type), "FLOAT64") ? LogicalType::DOUBLE : LogicalType::FLOAT;
	vector<Value> element_values;
	try {
		ParseOracleVectorElements(input, element_type, expected_dimension, element_values);
	} catch (const std::exception &ex) {
		throw InvalidInputException("%s: %s. Value: \"%s\"", OracleVectorErrorPrefix(column_name, oracle_type).c_str(),
		                            ex.what(), input.c_str());
	}
	vector<double> values;
	values.reserve(element_values.size());
	for (auto &elem : element_values) {
		values.push_back(elem.GetValue<double>());
	}
	return FormatOracleVector(values);
}

static string NormalizeOracleVectorValue(const Value &val, const string &column_name, const string &oracle_type) {
	ValidateOracleVectorTargetFormat(column_name, oracle_type);
	vector<double> values;
	if (val.type().id() == LogicalTypeId::LIST || val.type().id() == LogicalTypeId::ARRAY) {
		const auto &children =
		    val.type().id() == LogicalTypeId::LIST ? ListValue::GetChildren(val) : ArrayValue::GetChildren(val);
		values.reserve(children.size());
		for (auto &child : children) {
			if (child.IsNull()) {
				throw InvalidInputException("%s: NULL vector elements are not supported",
				                            OracleVectorErrorPrefix(column_name, oracle_type).c_str());
			}
			try {
				auto numeric = child.DefaultCastAs(LogicalType::DOUBLE).GetValue<double>();
				if (!std::isfinite(numeric)) {
					throw InvalidInputException("%s: NaN and Infinity are not supported for VECTOR writes",
					                            OracleVectorErrorPrefix(column_name, oracle_type).c_str());
				}
				values.push_back(numeric);
			} catch (const InvalidInputException &) {
				throw;
			} catch (const std::exception &ex) {
				throw InvalidInputException("%s: expected numeric vector element: %s",
				                            OracleVectorErrorPrefix(column_name, oracle_type).c_str(), ex.what());
			}
		}
		ValidateOracleVectorDimension(values, column_name, oracle_type);
		return FormatOracleVector(values);
	}

	return NormalizeOracleVectorString(val.ToString(), column_name, oracle_type);
}

static string OracleWriteValueToString(const Value &val, const string &column_name, const string &oracle_type) {
	if (IsOracleVectorType(oracle_type)) {
		return NormalizeOracleVectorValue(val, column_name, oracle_type);
	}
	if (val.type().id() == LogicalTypeId::BLOB) {
		return StringValue::Get(val);
	}
	return val.ToString();
}

void PrepareOracleWriteBindData(OracleWriteBindData &data) {
	data.oracle_types.resize(data.column_types.size(), "VARCHAR2");
	data.bind_types.resize(data.column_types.size(), SQLT_CHR);

	for (idx_t i = 0; i < data.column_types.size(); i++) {
		OracleTypeMetadata type_metadata;
		type_metadata.column_name = i < data.column_names.size() ? data.column_names[i] : "";
		type_metadata.oracle_data_type = data.oracle_types[i];
		auto decision = OracleTypeRegistry::ResolveWrite(type_metadata, data.column_types[i], data.settings);
		data.bind_types[i] = decision.write_bind_type;
	}

	ResolveOracleWriteTargetMetadata(data);
}

static string BuildOracleWriteInsertSQL(const OracleWriteBindData &data) {
	string sql = "INSERT INTO ";
	if (!data.schema_name.empty()) {
		sql += KeywordHelper::WriteQuotedAndEscaped(data.schema_name, '"') + ".";
	}
	sql += KeywordHelper::WriteQuotedAndEscaped(data.object_name, '"') + " (";

	for (idx_t i = 0; i < data.column_names.size(); i++) {
		if (i > 0) {
			sql += ", ";
		}
		sql += KeywordHelper::WriteQuotedAndEscaped(data.column_names[i], '"');
	}
	sql += ") VALUES (";
	for (idx_t i = 0; i < data.column_names.size(); i++) {
		if (i > 0) {
			sql += ", ";
		}

		sql += OracleWritePlaceholderSQL(data.oracle_types[i], data.bind_types[i], i + 1);
	}
	sql += ")";
	return sql;
}

OracleWriteGlobalState::OracleWriteGlobalState(std::shared_ptr<OracleConnectionHandle> conn, const string &query,
                                               idx_t max_batch_size_p)
    : connection(std::move(conn)), max_batch_size(max_batch_size_p) {
	auto ctx = connection->Get();
	stmthp = AllocateOCIStatement(ctx->envhp, ctx->errhp, "OCIHandleAlloc stmthp");

	std::vector<char> query_buffer(query.begin(), query.end());
	query_buffer.push_back(0);

	CheckOCIError(OCIStmtPrepare(stmthp.get(), ctx->errhp, reinterpret_cast<OraText *>(query_buffer.data()),
	                             static_cast<ub4>(query.size()), OCI_NTV_SYNTAX, OCI_DEFAULT),
	              ctx->errhp, "OCIStmtPrepare");
}

OracleWriteGlobalState::~OracleWriteGlobalState() {
	RollbackUncommitted();
}

void OracleWriteGlobalState::RollbackUncommitted() noexcept {
	if (!connection || !has_uncommitted_work || committed) {
		return;
	}
	aborted = true;
	connection->MarkUnusable();
	auto ctx = connection->Get();
	auto status = OCITransRollback(ctx->svchp, ctx->errhp, OCI_DEFAULT);
	has_uncommitted_work = false;
	if (status != OCI_SUCCESS) {
		connection->MarkUnusable();
	}
}

unique_ptr<OracleWriteGlobalState> OracleWriteInitGlobal(ClientContext &context, OracleWriteBindData &data) {
	RejectOracleWriteInExplicitTransaction(context);

	auto conn = OracleConnectionManager::Instance().Acquire(data.connection_string, data.wallet_path, data.settings);

	auto sql = BuildOracleWriteInsertSQL(data);

	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] Insert SQL: %s\n", sql.c_str());
	}

	OracleDebugRecordSettings(data.settings);
	return make_uniq<OracleWriteGlobalState>(conn, sql, OracleEffectiveArraySize(data.settings));
}

void OracleWriteSink(ExecutionContext &context, OracleWriteBindData &data, OracleWriteGlobalState &gstate,
                     DataChunk &input) {
	RejectOracleWriteInExplicitTransaction(context.client);
	try {
		gstate.Sink(input, data.column_names, data.oracle_types, data.bind_types);
	} catch (...) {
		gstate.RollbackUncommitted();
		throw;
	}
}

void OracleWriteGlobalState::Sink(DataChunk &chunk, const vector<string> &column_names,
                                  const vector<string> &oracle_types, const vector<ub2> &bind_types) {
	std::lock_guard<std::mutex> guard(sink_lock);
	idx_t count = chunk.size();
	if (count == 0) {
		return;
	}

	idx_t col_count = chunk.ColumnCount();
	vector<size_t> required_sizes(col_count, 4096);
	vector<bool> use_direct_string_slice(col_count, false);
	unified_formats.resize(col_count);
	converted_strings.resize(col_count);

	for (idx_t col_idx = 0; col_idx < col_count; col_idx++) {
		chunk.data[col_idx].ToUnifiedFormat(unified_formats[col_idx]);
		const auto &format = unified_formats[col_idx];
		const auto col_type = chunk.data[col_idx].GetType().id();

		ub2 bind_type = bind_types[col_idx];
		required_sizes[col_idx] = OracleWriteInitialBufferSize(bind_type);
		if (bind_type != SQLT_INT && bind_type != SQLT_BDOUBLE && bind_type != SQLT_ODT) {
			auto column_name = col_idx < column_names.size() ? column_names[col_idx] : "";
			auto oracle_type = col_idx < oracle_types.size() ? oracle_types[col_idx] : "";
			bool direct_slice = (col_type == LogicalTypeId::VARCHAR || col_type == LogicalTypeId::BLOB) &&
			                    !IsOracleVectorType(oracle_type);
			use_direct_string_slice[col_idx] = direct_slice;

			size_t max_len = 0;
			if (direct_slice) {
				auto str_data = UnifiedVectorFormat::GetData<string_t>(format);
				for (idx_t i = 0; i < count; i++) {
					auto sel_idx = format.sel->get_index(i);
					if (format.validity.RowIsValid(sel_idx)) {
						size_t len = str_data[sel_idx].GetSize();
						if (len > NumericLimits<ub2>::Maximum()) {
							auto len_str = std::to_string(len);
							throw InvalidInputException("Oracle write value for column '%s' exceeds maximum OCI bind "
							                            "length of 65535 bytes (%s bytes)",
							                            column_name.c_str(), len_str.c_str());
						}
						if (len > max_len) {
							max_len = len;
						}
					}
				}
			} else {
				auto &col_strings = converted_strings[col_idx];
				col_strings.resize(count);
				for (idx_t i = 0; i < count; i++) {
					auto sel_idx = format.sel->get_index(i);
					if (format.validity.RowIsValid(sel_idx)) {
						auto s = OracleWriteValueToString(chunk.data[col_idx].GetValue(i), column_name, oracle_type);
						if (s.size() > NumericLimits<ub2>::Maximum()) {
							auto len_str = std::to_string(s.size());
							throw InvalidInputException("Oracle write value for column '%s' exceeds maximum OCI bind "
							                            "length of 65535 bytes (%s bytes)",
							                            column_name.c_str(), len_str.c_str());
						}
						if (s.size() > max_len) {
							max_len = s.size();
						}
						col_strings[i] = std::move(s);
					}
				}
			}

			if (max_len > required_sizes[col_idx]) {
				size_t padded = (max_len + 32 + 3) & ~size_t(3);
				required_sizes[col_idx] = MinValue<size_t>(MaxValue<size_t>(padded, max_len),
				                                           static_cast<size_t>(NumericLimits<ub2>::Maximum()));
			} else {
				required_sizes[col_idx] = (required_sizes[col_idx] + 3) & ~size_t(3);
			}
		}
	}

	bool need_rebind = false;
	if (column_buffers.empty()) {
		column_buffers.resize(col_count);
		need_rebind = true;
	} else {
		for (idx_t col_idx = 0; col_idx < col_count; col_idx++) {
			if (required_sizes[col_idx] > column_buffers[col_idx].element_size) {
				need_rebind = true;
				break;
			}
		}
	}

	if (need_rebind) {
		for (idx_t col_idx = 0; col_idx < col_count; col_idx++) {
			auto &col_buf = column_buffers[col_idx];
			ub2 bind_type = bind_types[col_idx];
			if (required_sizes[col_idx] > col_buf.element_size) {
				col_buf.element_size = required_sizes[col_idx];
				if (bind_type == SQLT_ODT) {
					col_buf.date_values.resize(max_batch_size);
				} else {
					col_buf.char_buffer.resize(max_batch_size * col_buf.element_size);
				}
			}
			col_buf.indicators.resize(max_batch_size);
			col_buf.lengths.resize(max_batch_size);

			void *value_ptr = (bind_type == SQLT_ODT) ? static_cast<void *>(col_buf.date_values.data())
			                                          : static_cast<void *>(col_buf.char_buffer.data());
			auto ctx = connection->Get();
			CheckOCIError(OCIBindByPos(stmthp.get(), &col_buf.bind_handle, ctx->errhp, col_idx + 1, value_ptr,
			                           static_cast<sb4>(col_buf.element_size), bind_type, col_buf.indicators.data(),
			                           col_buf.lengths.data(), nullptr, 0, nullptr, OCI_DEFAULT),
			              ctx->errhp, "OCIBindByPos");

			CheckOCIError(OCIBindArrayOfStruct(col_buf.bind_handle, ctx->errhp, static_cast<ub4>(col_buf.element_size),
			                                   static_cast<ub4>(sizeof(sb2)), static_cast<ub4>(sizeof(ub2)), 0),
			              ctx->errhp, "OCIBindArrayOfStruct");
		}
	}

	idx_t write_buffer_bytes = 0;
	for (auto &col_buf : column_buffers) {
		write_buffer_bytes += col_buf.char_buffer.size() + (col_buf.date_values.size() * sizeof(OCIDate));
	}
	OracleDebugRecordWriteBufferBytes(write_buffer_bytes);

	for (idx_t offset = 0; offset < count; offset += max_batch_size) {
		auto iter_count = MinValue<idx_t>(max_batch_size, count - offset);
		for (idx_t col_idx = 0; col_idx < col_count; col_idx++) {
			auto column_name = col_idx < column_names.size() ? column_names[col_idx] : "";
			auto oracle_type = col_idx < oracle_types.size() ? oracle_types[col_idx] : "";
			BindColumn(chunk.data[col_idx], unified_formats[col_idx], col_idx, offset, iter_count, column_name,
			           oracle_type, bind_types[col_idx], converted_strings[col_idx], use_direct_string_slice[col_idx]);
		}
		ExecuteBatch(iter_count);
	}
}

static int64_t ReadUnifiedInt64(Vector &col, const UnifiedVectorFormat &format, idx_t sel_idx, idx_t row_idx) {
	switch (col.GetType().id()) {
	case LogicalTypeId::TINYINT:
		return static_cast<int64_t>(UnifiedVectorFormat::GetData<int8_t>(format)[sel_idx]);
	case LogicalTypeId::SMALLINT:
		return static_cast<int64_t>(UnifiedVectorFormat::GetData<int16_t>(format)[sel_idx]);
	case LogicalTypeId::INTEGER:
		return static_cast<int64_t>(UnifiedVectorFormat::GetData<int32_t>(format)[sel_idx]);
	case LogicalTypeId::BIGINT:
		return UnifiedVectorFormat::GetData<int64_t>(format)[sel_idx];
	case LogicalTypeId::UTINYINT:
		return static_cast<int64_t>(UnifiedVectorFormat::GetData<uint8_t>(format)[sel_idx]);
	case LogicalTypeId::USMALLINT:
		return static_cast<int64_t>(UnifiedVectorFormat::GetData<uint16_t>(format)[sel_idx]);
	case LogicalTypeId::UINTEGER:
		return static_cast<int64_t>(UnifiedVectorFormat::GetData<uint32_t>(format)[sel_idx]);
	default:
		return col.GetValue(row_idx).GetValue<int64_t>();
	}
}

static double ReadUnifiedDouble(Vector &col, const UnifiedVectorFormat &format, idx_t sel_idx, idx_t row_idx) {
	switch (col.GetType().id()) {
	case LogicalTypeId::FLOAT:
		return static_cast<double>(UnifiedVectorFormat::GetData<float>(format)[sel_idx]);
	case LogicalTypeId::DOUBLE:
		return UnifiedVectorFormat::GetData<double>(format)[sel_idx];
	default:
		return col.GetValue(row_idx).GetValue<double>();
	}
}

void OracleWriteGlobalState::BindColumn(Vector &col, const UnifiedVectorFormat &format, idx_t col_idx, idx_t offset,
                                        idx_t count, const string &column_name, const string &oracle_type,
                                        ub2 bind_type, const vector<string> &col_strings,
                                        bool use_direct_string_slice) {
	(void)oracle_type;
	auto &col_buf = column_buffers[col_idx];
	auto &bind_buffer = col_buf.char_buffer;
	auto &indicators = col_buf.indicators;
	auto &lengths = col_buf.lengths;
	size_t element_size = col_buf.element_size;
	const auto col_type = col.GetType().id();
	const string_t *direct_str_data =
	    use_direct_string_slice ? UnifiedVectorFormat::GetData<string_t>(format) : nullptr;

	for (idx_t i = 0; i < count; i++) {
		auto source_idx = offset + i;
		auto sel_idx = format.sel->get_index(source_idx);
		if (!format.validity.RowIsValid(sel_idx)) {
			indicators[i] = -1;
			lengths[i] = 0;
		} else {
			indicators[i] = 0;

			if (bind_type == SQLT_INT) {
				int64_t val = ReadUnifiedInt64(col, format, sel_idx, source_idx);
				memcpy(bind_buffer.data() + (i * element_size), &val, sizeof(int64_t));
				lengths[i] = sizeof(int64_t);
			} else if (bind_type == SQLT_BDOUBLE) {
				double val = ReadUnifiedDouble(col, format, sel_idx, source_idx);
				memcpy(bind_buffer.data() + (i * element_size), &val, sizeof(double));
				lengths[i] = sizeof(double);
			} else if (bind_type == SQLT_ODT) {
				OCIDate &date = col_buf.date_values[i];
				memset(&date, 0, sizeof(OCIDate));

				if (col_type == LogicalTypeId::DATE) {
					date_t duck_date = UnifiedVectorFormat::GetData<date_t>(format)[sel_idx];
					if (!duck_date.IsFinite()) {
						throw InvalidInputException("Oracle write value for column '%s' cannot bind non-finite DATE",
						                            column_name.c_str());
					}
					int32_t year, month, day;
					Date::Convert(duck_date, year, month, day);
					OCIDateSetDate(&date, static_cast<sb2>(year), static_cast<ub1>(month), static_cast<ub1>(day));
					OCIDateSetTime(&date, 0, 0, 0);
				} else if (col_type == LogicalTypeId::TIMESTAMP || col_type == LogicalTypeId::TIMESTAMP_TZ) {
					timestamp_t duck_ts = UnifiedVectorFormat::GetData<timestamp_t>(format)[sel_idx];
					if (!duck_ts.IsFinite()) {
						throw InvalidInputException(
						    "Oracle write value for column '%s' cannot bind non-finite TIMESTAMP", column_name.c_str());
					}
					date_t duck_date;
					dtime_t duck_time;
					Timestamp::Convert(duck_ts, duck_date, duck_time);
					int32_t year, month, day;
					Date::Convert(duck_date, year, month, day);
					int32_t hour, min, sec, micros;
					Time::Convert(duck_time, hour, min, sec, micros);
					OCIDateSetDate(&date, static_cast<sb2>(year), static_cast<ub1>(month), static_cast<ub1>(day));
					OCIDateSetTime(&date, static_cast<ub1>(hour), static_cast<ub1>(min), static_cast<ub1>(sec));
				} else {
					Value val = col.GetValue(source_idx).DefaultCastAs(LogicalType::TIMESTAMP);
					timestamp_t duck_ts = val.GetValueUnsafe<timestamp_t>();
					date_t duck_date;
					dtime_t duck_time;
					Timestamp::Convert(duck_ts, duck_date, duck_time);
					int32_t year, month, day;
					Date::Convert(duck_date, year, month, day);
					int32_t hour, min, sec, micros;
					Time::Convert(duck_time, hour, min, sec, micros);
					OCIDateSetDate(&date, static_cast<sb2>(year), static_cast<ub1>(month), static_cast<ub1>(day));
					OCIDateSetTime(&date, static_cast<ub1>(hour), static_cast<ub1>(min), static_cast<ub1>(sec));
				}
				lengths[i] = sizeof(OCIDate);
			} else if (use_direct_string_slice) {
				const auto &str_val = direct_str_data[sel_idx];
				size_t len = str_val.GetSize();
				if (len > element_size) {
					throw IOException("Value too large for buffer");
				}
				if (len > 0) {
					memcpy(bind_buffer.data() + (i * element_size), str_val.GetData(), len);
				}
				lengths[i] = static_cast<ub2>(len);
			} else {
				const string &str_val = col_strings[source_idx];
				if (str_val.size() > NumericLimits<ub2>::Maximum()) {
					auto len_str = std::to_string(str_val.size());
					throw InvalidInputException("Oracle write value for column '%s' exceeds maximum OCI bind length of "
					                            "65535 bytes (%s bytes)",
					                            column_name.c_str(), len_str.c_str());
				}
				if (str_val.size() > element_size) {
					throw IOException("Value too large for buffer");
				}

				if (!str_val.empty()) {
					memcpy(bind_buffer.data() + (i * element_size), str_val.data(), str_val.size());
				}
				lengths[i] = static_cast<ub2>(str_val.size());
			}
		}
	}
}

void OracleWriteGlobalState::ExecuteBatch(idx_t count) {
	auto ctx = connection->Get();
	auto status =
	    OCIStmtExecute(ctx->svchp, stmthp.get(), ctx->errhp, static_cast<ub4>(count), 0, nullptr, nullptr, OCI_DEFAULT);
	OracleDebugRecordWriteExecute(count);
	if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
		connection->MarkUnusable();
		OCITransRollback(ctx->svchp, ctx->errhp, OCI_DEFAULT);
	}
	CheckOCIError(status, ctx->errhp, "OCIStmtExecute Insert");
	MarkUncommittedWork();
}

void OracleWriteFinalize(OracleWriteGlobalState &gstate) {
	if (gstate.connection && gstate.ShouldCommit()) {
		auto ctx = gstate.connection->Get();
		try {
			CheckOCIError(OCITransCommit(ctx->svchp, ctx->errhp, OCI_DEFAULT), ctx->errhp, "OCITransCommit");
			gstate.MarkCommitted();
		} catch (...) {
			gstate.RollbackUncommitted();
			throw;
		}
	}
}

} // namespace duckdb
