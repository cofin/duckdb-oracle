#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/time.hpp"
#include "duckdb/common/types/date.hpp"
#include "oracle_write.hpp"
#include "oracle_connection.hpp"
#include "oracle_utils.hpp"
#include "oracle_connection_manager.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include <cstdio>
#include <cstring>
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
		query = "SELECT owner, table_name, column_name, data_type FROM all_tab_columns "
		        "WHERE owner = SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA') "
		        "AND (table_name = :1 OR table_name = UPPER(:2)) "
		        "ORDER BY CASE WHEN table_name = :3 THEN 0 WHEN table_name = UPPER(:4) THEN 1 ELSE 2 END, "
		        "table_name, column_id";
		bind_values.push_back(data.object_name);
		bind_values.push_back(data.object_name);
		bind_values.push_back(data.object_name);
		bind_values.push_back(data.object_name);
	} else {
		query = "SELECT owner, table_name, column_name, data_type FROM all_tab_columns "
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
	std::unordered_map<string, string> col_type_map;
	std::unordered_map<string, string> col_name_map;

	for (auto &row : query_res.rows) {
		if (row.size() < 4) {
			continue;
		}
		const string &owner = row[0];
		const string &table = row[1];
		const string &col = row[2];
		const string &type = row[3];
		if (owner != best_owner || table != best_table_name) {
			continue;
		}
		col_type_map[col] = type;
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
				data.oracle_types[i] = col_type_map[actual_name];
			}
		}
	}
}

static ub2 OracleBindTypeForDuckDBType(const LogicalType &type) {
	switch (type.id()) {
	case LogicalTypeId::TINYINT:
	case LogicalTypeId::SMALLINT:
	case LogicalTypeId::INTEGER:
	case LogicalTypeId::BIGINT:
		return SQLT_INT;
	case LogicalTypeId::FLOAT:
	case LogicalTypeId::DOUBLE:
		return SQLT_BDOUBLE;
	case LogicalTypeId::DATE:
		return SQLT_ODT;
	case LogicalTypeId::BLOB:
		return SQLT_BIN;
	default:
		return SQLT_CHR;
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

static string OracleWriteValueToString(const Value &val) {
	if (val.type().id() == LogicalTypeId::BLOB) {
		return StringValue::Get(val);
	}
	return val.ToString();
}

void PrepareOracleWriteBindData(OracleWriteBindData &data) {
	data.oracle_types.resize(data.column_types.size(), "VARCHAR2");
	data.bind_types.resize(data.column_types.size(), SQLT_CHR);

	for (idx_t i = 0; i < data.column_types.size(); i++) {
		data.bind_types[i] = OracleBindTypeForDuckDBType(data.column_types[i]);
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

OracleWriteGlobalState::OracleWriteGlobalState(std::shared_ptr<OracleConnectionHandle> conn, const string &query)
    : connection(std::move(conn)) {
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

	return make_uniq<OracleWriteGlobalState>(conn, sql);
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
		gstate.Sink(input, data.oracle_types, data.bind_types);
	} catch (...) {
		gstate.RollbackUncommitted();
		throw;
	}
}

void OracleWriteGlobalState::Sink(DataChunk &chunk, const vector<string> &oracle_types, const vector<ub2> &bind_types) {
	std::lock_guard<std::mutex> guard(sink_lock);
	idx_t count = chunk.size();
	if (count == 0) {
		return;
	}
	MarkUncommittedWork();

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
					auto s = OracleWriteValueToString(chunk.data[col_idx].GetValue(i));
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
				bind_buffers[col_idx].resize(MAX_BATCH_SIZE * current_buffer_sizes[col_idx]);
			}
		}

		for (idx_t col_idx = 0; col_idx < chunk.ColumnCount(); col_idx++) {
			indicator_buffers[col_idx].resize(MAX_BATCH_SIZE);
			length_buffers[col_idx].resize(MAX_BATCH_SIZE);
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

	for (idx_t col_idx = 0; col_idx < chunk.ColumnCount(); col_idx++) {
		BindColumn(chunk.data[col_idx], col_idx, count, bind_types[col_idx]);
	}

	ExecuteBatch(count);
}

void OracleWriteGlobalState::BindColumn(Vector &col, idx_t col_idx, idx_t count, ub2 bind_type) {
	auto &validity = FlatVector::Validity(col);
	auto &bind_buffer = bind_buffers[col_idx];
	auto &indicators = indicator_buffers[col_idx];
	auto &lengths = length_buffers[col_idx];
	size_t element_size = current_buffer_sizes[col_idx];

	for (idx_t i = 0; i < count; i++) {
		if (!validity.RowIsValid(i)) {
			indicators[i] = -1;
			lengths[i] = 0;
		} else {
			indicators[i] = 0;

			if (bind_type == SQLT_INT) {
				int64_t val = col.GetValue(i).GetValue<int64_t>();
				memcpy(bind_buffer.data() + (i * element_size), &val, sizeof(int64_t));
				lengths[i] = sizeof(int64_t);
			} else if (bind_type == SQLT_BDOUBLE) {
				double val = col.GetValue(i).GetValue<double>();
				memcpy(bind_buffer.data() + (i * element_size), &val, sizeof(double));
				lengths[i] = sizeof(double);
			} else if (bind_type == SQLT_ODT) {
				Value val = col.GetValue(i);
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
				Value val = col.GetValue(i);
				auto str_val = OracleWriteValueToString(val);

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
	if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
		connection->MarkUnusable();
		OCITransRollback(ctx->svchp, ctx->errhp, OCI_DEFAULT);
	}
	CheckOCIError(status, ctx->errhp, "OCIStmtExecute Insert");
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
