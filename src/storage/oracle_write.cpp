#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/time.hpp"
#include "duckdb/common/types/date.hpp"
#include "oracle_write.hpp"
#include "oracle_connection.hpp"
#include "oracle_debug_stats.hpp"
#include "oracle_utils.hpp"
#include "oracle_connection_manager.hpp"
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
	std::unordered_map<string, OracleTypeMetadata> col_type_map;
	std::unordered_map<string, string> col_name_map;
	auto parse_idx = [](const string &s) -> idx_t {
		if (s.empty()) {
			return 0;
		}
		try {
			return static_cast<idx_t>(std::stoll(s));
		} catch (...) {
			return 0;
		}
	};
	auto parse_int = [](const string &s) -> int32_t {
		if (s.empty()) {
			return 0;
		}
		try {
			return static_cast<int32_t>(std::stoll(s));
		} catch (...) {
			return 0;
		}
	};

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
		OracleTypeMetadata metadata;
		metadata.schema_name = owner;
		metadata.table_name = table;
		metadata.column_name = col;
		metadata.oracle_data_type = row[3];
		metadata.type_owner = row[4];
		metadata.precision = parse_idx(row[5]);
		metadata.scale = parse_int(row[6]);
		metadata.has_scale = !row[6].empty();
		metadata.data_length = parse_idx(row[7]);
		metadata.char_used = row[8] == "C";
		metadata.char_length = parse_idx(row[9]);
		col_type_map[col] = metadata;
		auto col_upper = StringUtil::Upper(col);
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
	try {
		return static_cast<idx_t>(std::stoull(dimension));
	} catch (...) {
		return 0;
	}
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

static double ParseOracleVectorElement(const string &value, const string &full_value, const string &column_name,
                                       const string &oracle_type) {
	try {
		size_t parsed = 0;
		auto result = std::stod(value, &parsed);
		if (parsed != value.size()) {
			throw InvalidInputException("%s: expected numeric vector element. Value: \"%s\"",
			                            OracleVectorErrorPrefix(column_name, oracle_type).c_str(), full_value.c_str());
		}
		if (!std::isfinite(result)) {
			throw InvalidInputException("%s: NaN and Infinity are not supported for VECTOR writes. Value: \"%s\"",
			                            OracleVectorErrorPrefix(column_name, oracle_type).c_str(), full_value.c_str());
		}
		return result;
	} catch (const InvalidInputException &) {
		throw;
	} catch (const std::exception &ex) {
		throw InvalidInputException("%s: expected numeric vector element: %s. Value: \"%s\"",
		                            OracleVectorErrorPrefix(column_name, oracle_type).c_str(), ex.what(),
		                            full_value.c_str());
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
	auto value = input;
	StringUtil::Trim(value);
	if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
		throw InvalidInputException("%s: expected bracketed vector literal. Value: \"%s\"",
		                            OracleVectorErrorPrefix(column_name, oracle_type).c_str(), input.c_str());
	}

	auto body = value.substr(1, value.size() - 2);
	StringUtil::Trim(body);
	vector<double> values;
	if (!body.empty()) {
		auto parts = StringUtil::Split(body, ',');
		values.reserve(parts.size());
		for (auto &part : parts) {
			StringUtil::Trim(part);
			if (part.empty()) {
				throw InvalidInputException("%s: empty vector element. Value: \"%s\"",
				                            OracleVectorErrorPrefix(column_name, oracle_type).c_str(), input.c_str());
			}
			values.push_back(ParseOracleVectorElement(part, input, column_name, oracle_type));
		}
	}
	ValidateOracleVectorDimension(values, column_name, oracle_type);
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
		sql += KeywordHelper::WriteQuoted(data.schema_name, '"') + ".";
	}
	sql += KeywordHelper::WriteQuoted(data.object_name, '"') + " (";

	for (idx_t i = 0; i < data.column_names.size(); i++) {
		if (i > 0) {
			sql += ", ";
		}
		sql += KeywordHelper::WriteQuoted(data.column_names[i], '"');
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

//--- Global State ---

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

	// Acquire connection
	auto conn = OracleConnectionManager::Instance().Acquire(data.connection_string, data.wallet_path, data.settings);

	auto sql = BuildOracleWriteInsertSQL(data);

	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] Insert SQL: %s\n", sql.c_str());
	}

	OracleDebugRecordSettings(data.settings);
	return make_uniq<OracleWriteGlobalState>(conn, sql, OracleEffectiveArraySize(data.settings));
}

//--- Local State ---

OracleWriteLocalState::OracleWriteLocalState() {
}

OracleWriteLocalState::~OracleWriteLocalState() {
}

//--- Sink ---

void OracleWriteSink(ExecutionContext &context, OracleWriteBindData &data, OracleWriteGlobalState &gstate,
                     OracleWriteLocalState &lstate, DataChunk &input) {
	RejectOracleWriteInExplicitTransaction(context.client);
	(void)lstate;
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
	// Determine max sizes for buffer allocation
	vector<size_t> required_sizes(chunk.ColumnCount(), 4096);

	for (idx_t col_idx = 0; col_idx < chunk.ColumnCount(); col_idx++) {
		if (chunk.data[col_idx].GetVectorType() != VectorType::FLAT_VECTOR) {
			chunk.data[col_idx].Flatten(count);
		}

		ub2 bind_type = bind_types[col_idx];
		required_sizes[col_idx] = OracleWriteInitialBufferSize(bind_type);
		if (bind_type != SQLT_INT && bind_type != SQLT_BDOUBLE && bind_type != SQLT_ODT) {
			auto &validity = FlatVector::Validity(chunk.data[col_idx]);
			size_t max_len = 0;

			for (idx_t i = 0; i < count; i++) {
				if (validity.RowIsValid(i)) {
					auto column_name = col_idx < column_names.size() ? column_names[col_idx] : "";
					auto oracle_type = col_idx < oracle_types.size() ? oracle_types[col_idx] : "";
					auto s = OracleWriteValueToString(chunk.data[col_idx].GetValue(i), column_name, oracle_type);
					if (s.size() > max_len) {
						max_len = s.size();
					}
				}
			}

			if (max_len > required_sizes[col_idx]) {
				required_sizes[col_idx] = max_len + 32;
			}
			// Align
			required_sizes[col_idx] = (required_sizes[col_idx] + 3) & ~3;
		}
	}

	// Check if rebind needed
	bool need_rebind = false;
	if (bind_buffers.empty()) {
		bind_buffers.resize(chunk.ColumnCount());
		indicator_buffers.resize(chunk.ColumnCount());
		length_buffers.resize(chunk.ColumnCount());
		binds.resize(chunk.ColumnCount(), nullptr);
		current_buffer_sizes.resize(chunk.ColumnCount(), 0);
		need_rebind = true;
	} else {
		for (idx_t col_idx = 0; col_idx < chunk.ColumnCount(); col_idx++) {
			if (required_sizes[col_idx] > current_buffer_sizes[col_idx]) {
				need_rebind = true;
				break;
			}
		}
	}

	if (need_rebind) {
		for (idx_t col_idx = 0; col_idx < chunk.ColumnCount(); col_idx++) {
			if (required_sizes[col_idx] > current_buffer_sizes[col_idx]) {
				current_buffer_sizes[col_idx] = required_sizes[col_idx];
				bind_buffers[col_idx].resize(max_batch_size * current_buffer_sizes[col_idx]);
			}
		}

		for (idx_t col_idx = 0; col_idx < chunk.ColumnCount(); col_idx++) {
			indicator_buffers[col_idx].resize(max_batch_size);
			length_buffers[col_idx].resize(max_batch_size);
		}

		for (idx_t col_idx = 0; col_idx < chunk.ColumnCount(); col_idx++) {
			ub2 bind_type = bind_types[col_idx];

			auto ctx = connection->Get();
			CheckOCIError(OCIBindByPos(stmthp.get(), &binds[col_idx], ctx->errhp, col_idx + 1,
			                           bind_buffers[col_idx].data(), static_cast<sb4>(current_buffer_sizes[col_idx]),
			                           bind_type, indicator_buffers[col_idx].data(), length_buffers[col_idx].data(),
			                           nullptr, 0, nullptr, OCI_DEFAULT),
			              ctx->errhp, "OCIBindByPos");

			// Set up array binding stride for batch inserts
			CheckOCIError(OCIBindArrayOfStruct(binds[col_idx], ctx->errhp,
			                                   static_cast<ub4>(current_buffer_sizes[col_idx]), // data skip
			                                   static_cast<ub4>(sizeof(sb2)),                   // indicator skip
			                                   static_cast<ub4>(sizeof(ub2)),                   // length skip
			                                   0),                                              // return code skip
			              ctx->errhp, "OCIBindArrayOfStruct");
		}
	}

	idx_t write_buffer_bytes = 0;
	for (auto &buffer : bind_buffers) {
		write_buffer_bytes += buffer.size();
	}
	OracleDebugRecordWriteBufferBytes(write_buffer_bytes);

	for (idx_t offset = 0; offset < count; offset += max_batch_size) {
		auto iter_count = MinValue<idx_t>(max_batch_size, count - offset);
		for (idx_t col_idx = 0; col_idx < chunk.ColumnCount(); col_idx++) {
			auto column_name = col_idx < column_names.size() ? column_names[col_idx] : "";
			auto oracle_type = col_idx < oracle_types.size() ? oracle_types[col_idx] : "";
			BindColumn(chunk.data[col_idx], col_idx, offset, iter_count, column_name, oracle_type, bind_types[col_idx]);
		}
		ExecuteBatch(iter_count);
	}
}

void OracleWriteGlobalState::BindColumn(Vector &col, idx_t col_idx, idx_t offset, idx_t count,
                                        const string &column_name, const string &oracle_type, ub2 bind_type) {
	auto &validity = FlatVector::Validity(col);
	auto &bind_buffer = bind_buffers[col_idx];
	auto &indicators = indicator_buffers[col_idx];
	auto &lengths = length_buffers[col_idx];
	size_t element_size = current_buffer_sizes[col_idx];

	for (idx_t i = 0; i < count; i++) {
		auto source_idx = offset + i;
		if (!validity.RowIsValid(source_idx)) {
			indicators[i] = -1;
			lengths[i] = 0;
		} else {
			indicators[i] = 0;

			if (bind_type == SQLT_INT) {
				int64_t val = col.GetValue(source_idx).GetValue<int64_t>();
				memcpy(bind_buffer.data() + (i * element_size), &val, sizeof(int64_t));
				lengths[i] = sizeof(int64_t);
			} else if (bind_type == SQLT_BDOUBLE) {
				double val = col.GetValue(source_idx).GetValue<double>();
				memcpy(bind_buffer.data() + (i * element_size), &val, sizeof(double));
				lengths[i] = sizeof(double);
			} else if (bind_type == SQLT_ODT) {
				Value val = col.GetValue(source_idx);
				OCIDate date;
				memset(&date, 0, sizeof(OCIDate));

				if (val.type().id() == LogicalTypeId::DATE) {
					date_t duck_date = val.GetValueUnsafe<date_t>();
					int32_t year, month, day;
					Date::Convert(duck_date, year, month, day);
					date.OCIDateYYYY = static_cast<sb2>(year);
					date.OCIDateMM = month;
					date.OCIDateDD = day;
					date.OCIDateTime.OCITimeHH = 0;
					date.OCIDateTime.OCITimeMI = 0;
					date.OCIDateTime.OCITimeSS = 0;
				} else {
					timestamp_t duck_ts = val.GetValueUnsafe<timestamp_t>();
					date_t duck_date;
					dtime_t duck_time;
					Timestamp::Convert(duck_ts, duck_date, duck_time);
					int32_t year, month, day;
					Date::Convert(duck_date, year, month, day);
					int32_t hour, min, sec, micros;
					Time::Convert(duck_time, hour, min, sec, micros);

					date.OCIDateYYYY = static_cast<sb2>(year);
					date.OCIDateMM = month;
					date.OCIDateDD = day;
					date.OCIDateTime.OCITimeHH = hour;
					date.OCIDateTime.OCITimeMI = min;
					date.OCIDateTime.OCITimeSS = sec;
				}
				memcpy(bind_buffer.data() + (i * element_size), &date, sizeof(OCIDate));
				lengths[i] = sizeof(OCIDate);
			} else {
				Value val = col.GetValue(source_idx);
				auto str_val = OracleWriteValueToString(val, column_name, oracle_type);

				if (str_val.size() > element_size) {
					throw IOException("Value too large for buffer");
				}

				memcpy(bind_buffer.data() + (i * element_size), str_val.c_str(), str_val.size());
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
