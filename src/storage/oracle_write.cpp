#include "oracle_write.hpp"
#include "oracle_connection_manager.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include <cstring>

namespace duckdb {

static void CheckOCIError(sword status, OCIError *errhp, const std::string &msg) {
	if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO) {
		return;
	}
	text errbuf[512];
	sb4 errcode = 0;
	if (errhp) {
		OCIErrorGet((dvoid *)errhp, (ub4)1, (text *)NULL, &errcode, errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR);
		throw IOException(msg + ": " + std::string((char *)errbuf));
	}
	throw IOException(msg + ": (No Error Handle)");
}

//--- Bind Data ---

unique_ptr<FunctionData> OracleWriteBind(ClientContext &context, CopyFunctionBindInput &input,
                                         const vector<string> &names, const vector<LogicalType> &sql_types) {
	auto result = make_uniq<OracleWriteBindData>();
	
	// Target table name passed as "file path" or via TABLE option
	string target_table = input.info.file_path;
	
	// We need to parse connection options if provided (e.g. CONNECTION '...')
	// CopyFunctionBindInput doesn't expose options map directly in all versions, but let's check input.info
	
	auto &options = input.info.options;
	auto conn_it = options.find("connection_string");
	if (conn_it != options.end()) {
		result->connection_string = conn_it->second.front().ToString();
	}
	
	// Check for TABLE option override
	auto table_it = options.find("table");
	if (table_it != options.end()) {
		target_table = table_it->second.front().ToString();
	}
	
	if (target_table.empty()) {
		throw BinderException("Oracle COPY TO requires a table name (use TO 'table' or TABLE 'table')");
	}
	
	result->table_name = target_table;
	
	// Parse schema.table
	auto parts = StringUtil::Split(target_table, ".");
	if (parts.size() == 2) {
		result->schema_name = parts[0];
		result->object_name = parts[1];
	} else {
		result->object_name = target_table;
	}
	
	result->column_names = names;
	result->column_types = sql_types;
	
	return std::move(result);
}

//--- Global State ---

OracleWriteGlobalState::OracleWriteGlobalState(std::shared_ptr<OracleConnectionHandle> conn, const string &query)
    : connection(std::move(conn)), stmthp(nullptr) {
	auto ctx = connection->Get();
	CheckOCIError(OCIHandleAlloc(ctx->envhp, (dvoid **)&stmthp, OCI_HTYPE_STMT, 0, nullptr), ctx->errhp,
	              "OCIHandleAlloc stmthp");
	
	CheckOCIError(
	    OCIStmtPrepare(stmthp, ctx->errhp, (OraText *)query.c_str(), query.size(), OCI_NTV_SYNTAX, OCI_DEFAULT),
	    ctx->errhp, "OCIStmtPrepare");
}

OracleWriteGlobalState::~OracleWriteGlobalState() {
	if (stmthp) {
		OCIHandleFree(stmthp, OCI_HTYPE_STMT);
	}
}

unique_ptr<GlobalFunctionData> OracleWriteInitGlobal(ClientContext &context, FunctionData &bind_data,
                                                     const string &file_path) {
	auto &data = (OracleWriteBindData &)bind_data;
	
	// Acquire connection
	OracleSettings settings; // Default settings
	auto conn = OracleConnectionManager::Instance().Acquire(data.connection_string, settings);
	
	// Generate SQL
	string sql = "INSERT /*+ APPEND_VALUES */ INTO ";
	if (!data.schema_name.empty()) {
		sql += KeywordHelper::WriteQuoted(data.schema_name, '"') + ".";
	}
	sql += KeywordHelper::WriteQuoted(data.object_name, '"') + " (";
	
	for (idx_t i = 0; i < data.column_names.size(); i++) {
		if (i > 0) sql += ", ";
		sql += KeywordHelper::WriteQuoted(data.column_names[i], '"');
	}
	sql += ") VALUES (";
	for (idx_t i = 0; i < data.column_names.size(); i++) {
		if (i > 0) sql += ", ";
		sql += ":" + std::to_string(i + 1);
	}
	sql += ")";
	
	return make_uniq<OracleWriteGlobalState>(conn, sql);
}

//--- Local State ---

OracleWriteLocalState::OracleWriteLocalState(std::shared_ptr<OracleConnectionHandle> conn, OCIStmt *stmthp)
    : connection(std::move(conn)), stmthp(stmthp) {
}

OracleWriteLocalState::~OracleWriteLocalState() {
}

unique_ptr<LocalFunctionData> OracleWriteInitLocal(ExecutionContext &context, FunctionData &bind_data) {
	// We need to access the global state here to get the statement handle?
	// Wait, InitLocal doesn't get GlobalState passed in.
	// Standard pattern is to initialize lightweight state and link in Sink.
	// Or we can't access global state here.
	
	// We'll create a dummy local state and populate it in Sink from GlobalState
	return make_uniq<OracleWriteLocalState>(nullptr, nullptr);
}

//--- Sink ---

void OracleWriteSink(ExecutionContext &context, FunctionData &bind_data, GlobalFunctionData &gstate_p,
                     LocalFunctionData &lstate_p, DataChunk &input) {
	auto &gstate = (OracleWriteGlobalState &)gstate_p;
	auto &lstate = (OracleWriteLocalState &)lstate_p;
	
	// Initialize local state from global if needed (first run)
	if (!lstate.connection) {
		lstate = OracleWriteLocalState(gstate.connection, gstate.stmthp);
	}
	
	lstate.Sink(input);
}

void OracleWriteLocalState::Sink(DataChunk &chunk) {
	idx_t count = chunk.size();
	if (count == 0) return;

	// Initialize buffers on first run
	if (bind_buffers.empty()) {
		bind_buffers.resize(chunk.ColumnCount());
		indicator_buffers.resize(chunk.ColumnCount());
		length_buffers.resize(chunk.ColumnCount());
		binds.resize(chunk.ColumnCount(), nullptr);

		for (idx_t col_idx = 0; col_idx < chunk.ColumnCount(); col_idx++) {
			size_t element_size = 4096; // TODO: Better sizing
			bind_buffers[col_idx].resize(MAX_BATCH_SIZE * element_size);
			indicator_buffers[col_idx].resize(MAX_BATCH_SIZE);
			length_buffers[col_idx].resize(MAX_BATCH_SIZE);

			auto ctx = connection->Get();
			CheckOCIError(OCIBindByPos(stmthp, &binds[col_idx], ctx->errhp, col_idx + 1,
			                           bind_buffers[col_idx].data(), element_size, SQLT_CHR,
			                           indicator_buffers[col_idx].data(), length_buffers[col_idx].data(),
			                           nullptr, 0, nullptr, OCI_DEFAULT),
			              ctx->errhp, "OCIBindByPos");
		}
	}

	// Convert and fill buffers
	for (idx_t col_idx = 0; col_idx < chunk.ColumnCount(); col_idx++) {
		BindColumn(chunk.data[col_idx], col_idx, count);
	}

	ExecuteBatch(count);
}

void OracleWriteLocalState::BindColumn(Vector &col, idx_t col_idx, idx_t count) {
	if (col.GetVectorType() != VectorType::FLAT_VECTOR) {
		col.Flatten(count);
	}

	auto &validity = FlatVector::Validity(col);
	auto &bind_buffer = bind_buffers[col_idx];
	auto &indicators = indicator_buffers[col_idx];
	auto &lengths = length_buffers[col_idx];
	
	const size_t element_size = 4096;

	for (idx_t i = 0; i < count; i++) {
		if (!validity.RowIsValid(i)) {
			indicators[i] = -1;
			lengths[i] = 0;
		} else {
			indicators[i] = 0;
			Value val = col.GetValue(i);
			string str_val = val.ToString();
			
			if (str_val.size() > element_size) {
				throw IOException("Value too large for buffer");
			}
			
			memcpy(bind_buffer.data() + (i * element_size), str_val.c_str(), str_val.size());
			lengths[i] = static_cast<ub2>(str_val.size());
		}
	}
}

void OracleWriteLocalState::ExecuteBatch(idx_t count) {
	auto ctx = connection->Get();
	CheckOCIError(OCIStmtExecute(ctx->svchp, stmthp, ctx->errhp, static_cast<ub4>(count), 0, nullptr, nullptr, OCI_DEFAULT),
	              ctx->errhp, "OCIStmtExecute Insert");
}

void OracleWriteLocalState::Flush() {
}

void OracleWriteFinalize(ClientContext &context, FunctionData &bind_data, GlobalFunctionData &gstate) {
	// No-op
}

} // namespace duckdb
