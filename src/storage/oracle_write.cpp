#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/time.hpp"
#include "duckdb/common/types/date.hpp"
#include "oracle_write.hpp"
#include "oracle_utils.hpp"
#include "oracle_connection_manager.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include <cstdio>
#include <cstring>

namespace duckdb {

void RejectOracleWriteInExplicitTransaction(ClientContext &context) {
	if (!context.transaction.IsAutoCommit()) {
		throw InvalidInputException("Oracle writes are statement-atomic and cannot run inside an explicit DuckDB "
		                            "transaction block; commit or rollback the DuckDB transaction before writing to "
		                            "Oracle");
	}
}

//--- Global State ---

OracleWriteGlobalState::OracleWriteGlobalState(std::shared_ptr<OracleConnectionHandle> conn, const string &query)
    : connection(std::move(conn)) {
	auto ctx = connection->Get();
	stmthp = AllocateOCIStatement(ctx->envhp, ctx->errhp, "OCIHandleAlloc stmthp");

	// Make a mutable copy of query string for OCI
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

	// Generate SQL
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

		string type = StringUtil::Upper(data.oracle_types[i]);
		string placeholder = ":" + std::to_string(i + 1);

		if (data.bind_types[i] == SQLT_ODT) {
			sql += placeholder;
		} else if (type == "DATE") {
			sql += "TO_DATE(" + placeholder + ", 'YYYY-MM-DD HH24:MI:SS')";
		} else if (type.find("TIMESTAMP") != string::npos) {
			sql += "TO_TIMESTAMP(" + placeholder + ", 'YYYY-MM-DD HH24:MI:SS.FF')";
		} else if (type == "SDO_GEOMETRY" || type == "MDSYS.SDO_GEOMETRY") {
			sql += "SDO_UTIL.FROM_WKTGEOMETRY(" + placeholder + ")";
		} else {
			sql += placeholder;
		}
	}
	sql += ")";

	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] Insert SQL: %s\n", sql.c_str());
	}

	return make_uniq<OracleWriteGlobalState>(conn, sql);
}

//--- Local State ---

OracleWriteLocalState::OracleWriteLocalState(std::shared_ptr<OracleConnectionHandle> conn, OCIStmt *stmthp)
    : connection(std::move(conn)), stmthp(stmthp) {
}

OracleWriteLocalState::~OracleWriteLocalState() {
}

//--- Sink ---

void OracleWriteSink(ExecutionContext &context, OracleWriteBindData &data, OracleWriteGlobalState &gstate,
                     OracleWriteLocalState &lstate, DataChunk &input) {
	RejectOracleWriteInExplicitTransaction(context.client);

	if (!lstate.connection) {
		lstate = OracleWriteLocalState(gstate.connection, gstate.stmthp.get());
	}

	auto input_size = input.size();
	if (input_size > 0) {
		gstate.MarkUncommittedWork();
	}
	try {
		lstate.Sink(input, data.oracle_types, data.bind_types);
	} catch (...) {
		gstate.RollbackUncommitted();
		throw;
	}
}

void OracleWriteLocalState::Sink(DataChunk &chunk, const vector<string> &oracle_types, const vector<ub2> &bind_types) {
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
		if (bind_type == SQLT_INT || bind_type == SQLT_BDOUBLE) {
			required_sizes[col_idx] = 8;
		} else if (bind_type == SQLT_ODT) {
			required_sizes[col_idx] = 7; // OCIDate
		} else {
			auto &validity = FlatVector::Validity(chunk.data[col_idx]);
			size_t max_len = 0;

			for (idx_t i = 0; i < count; i++) {
				if (validity.RowIsValid(i)) {
					Value val = chunk.data[col_idx].GetValue(i);
					string s;
					if (val.type().id() == LogicalTypeId::BLOB) {
						s = StringValue::Get(val);
					} else {
						s = val.ToString();
					}
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
			CheckOCIError(OCIBindByPos(stmthp, &binds[col_idx], ctx->errhp, col_idx + 1, bind_buffers[col_idx].data(),
			                           static_cast<sb4>(current_buffer_sizes[col_idx]), bind_type,
			                           indicator_buffers[col_idx].data(), length_buffers[col_idx].data(), nullptr, 0,
			                           nullptr, OCI_DEFAULT),
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

void OracleWriteLocalState::BindColumn(Vector &col, idx_t col_idx, idx_t count, ub2 bind_type) {
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
				string str_val;

				if (val.type().id() == LogicalTypeId::BLOB) {
					str_val = StringValue::Get(val);
				} else {
					str_val = val.ToString();
				}

				if (str_val.size() > element_size) {
					throw IOException("Value too large for buffer");
				}

				memcpy(bind_buffer.data() + (i * element_size), str_val.c_str(), str_val.size());
				lengths[i] = static_cast<ub2>(str_val.size());
			}
		}
	}
}

void OracleWriteLocalState::ExecuteBatch(idx_t count) {
	auto ctx = connection->Get();
	auto status =
	    OCIStmtExecute(ctx->svchp, stmthp, ctx->errhp, static_cast<ub4>(count), 0, nullptr, nullptr, OCI_DEFAULT);
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
