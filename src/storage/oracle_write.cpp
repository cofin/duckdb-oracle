#include "oracle_write.hpp"
#include "oracle_connection_manager.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "oracle_connection.hpp" // For OracleConnection wrapper
#include <cstring>
#include <unordered_map>

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
	
	auto &options = input.info.options;
	for (auto &op : options) {
		string key = StringUtil::Lower(op.first);
		if (key != "connection_string" && key != "table") {
			throw BinderException("Unrecognized option for Oracle COPY: %s", op.first);
		}
	}

	auto conn_it = options.find("connection_string");
	if (conn_it == options.end()) {
		// Try uppercase if not found (DuckDB might preserve case in options map?)
		// Actually we just iterate and find.
		for (auto &op : options) {
			if (StringUtil::Lower(op.first) == "connection_string") {
				conn_it = options.find(op.first);
				break;
			}
		}
	}
	
	if (conn_it != options.end()) {
		result->connection_string = conn_it->second.front().ToString();
	}
	
	// Check for TABLE option override
	auto table_it = options.find("table");
	if (table_it == options.end()) {
		for (auto &op : options) {
			if (StringUtil::Lower(op.first) == "table") {
				table_it = options.find(op.first);
				break;
			}
		}
	}
	
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
	result->oracle_types.resize(names.size(), "VARCHAR2"); // Default

	// Introspect Oracle table to get actual types
	if (!result->connection_string.empty()) {
		try {
			OracleSettings settings;
			// Use temporary connection logic to avoid catalog state dependency if simple string
			OracleConnection temp_conn;
			temp_conn.Connect(result->connection_string);
			
			// Try to find table metadata
			string schema_filter = result->schema_name.empty() 
				? "owner = SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA')" 
				: "owner = upper('" + result->schema_name + "')";
			
			// Handle case sensitivity: try exact match or upper case match
			string table_filter = "(table_name = '" + result->object_name + "' OR table_name = upper('" + result->object_name + "'))";
			
			string query = "SELECT owner, table_name, column_name, data_type FROM all_tab_columns WHERE " + 
			               schema_filter + " AND " + table_filter + " ORDER BY owner, table_name, column_id";
			
			auto query_res = temp_conn.Query(query);
			
			// Map column names to types
			// We might get multiple tables if we are unlucky (e.g. "abc" and "ABC" both exist).
			// We prioritize exact match if found, otherwise upper match.
			// Actually, for simplicity, we'll just take the first table found, but prefer the one matching our casing logic?
			// Let's iterate and pick the 'best' table name.
			
			string best_table_name;
			string best_owner;
			bool found_exact = false;
			
			std::unordered_map<string, string> col_type_map; // Name -> Type
			std::unordered_map<string, string> col_name_map; // UpperName -> ActualName
			
			for (auto &row : query_res.rows) {
				if (row.size() < 4) continue;
				string owner = row[0];
				string table = row[1];
				string col = row[2];
				string type = row[3];
				
				if (best_table_name.empty()) {
					best_table_name = table;
					best_owner = owner;
				}
				
				if (table != best_table_name) {
					if (table == result->object_name && !found_exact) {
						best_table_name = table;
						best_owner = owner;
						col_type_map.clear();
						col_name_map.clear();
						found_exact = true;
					} else if (found_exact) {
						continue;
					}
				}
				
				if (table == result->object_name) found_exact = true;
				
				if (table == best_table_name) {
					col_type_map[col] = type;
					col_name_map[StringUtil::Upper(col)] = col;
				}
			}
			
			if (!best_table_name.empty()) {
				result->object_name = best_table_name;
				if (result->schema_name.empty()) {
					result->schema_name = best_owner;
				}
			}
			
			// Update result->oracle_types and column_names based on DuckDB column names
			for (idx_t i = 0; i < names.size(); i++) {
				string col_upper = StringUtil::Upper(names[i]);
				
				// Try to find mapping
				string actual_name;
				if (col_name_map.count(col_upper)) {
					actual_name = col_name_map[col_upper];
				} else {
					// If not found (maybe exact match required for mixed case that conflicts?)
					// Or just column doesn't exist.
					// Try exact match lookup in type map (if we didn't map it via upper)
					if (col_type_map.count(names[i])) {
						actual_name = names[i];
					}
				}
				
				if (!actual_name.empty()) {
					result->column_names[i] = actual_name; // Update to correct casing
					if (col_type_map.count(actual_name)) {
						result->oracle_types[i] = col_type_map[actual_name];
					}
				}
			}
		} catch (std::exception &e) {
			// If metadata fetch fails, we proceed with defaults but log if debug
			if (getenv("ORACLE_DEBUG")) {
				fprintf(stderr, "Warning: Failed to fetch metadata in Bind: %s\n", e.what());
			}
		}
	}
	
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
		
		string type = StringUtil::Upper(data.oracle_types[i]);
		string placeholder = ":" + std::to_string(i + 1);
		
		if (type == "DATE") {
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

unique_ptr<LocalFunctionData> OracleWriteInitLocal(ExecutionContext &context, FunctionData &bind_data) {
	return make_uniq<OracleWriteLocalState>(nullptr, nullptr);
}

//--- Sink ---

void OracleWriteSink(ExecutionContext &context, FunctionData &bind_data, GlobalFunctionData &gstate_p,
                     LocalFunctionData &lstate_p, DataChunk &input) {
	auto &gstate = (OracleWriteGlobalState &)gstate_p;
	auto &lstate = (OracleWriteLocalState &)lstate_p;
	auto &data = (OracleWriteBindData &)bind_data;
	
	if (!lstate.connection) {
		lstate = OracleWriteLocalState(gstate.connection, gstate.stmthp);
	}
	
	lstate.Sink(input, data.oracle_types);
}

void OracleWriteLocalState::Sink(DataChunk &chunk, const vector<string> &oracle_types) {
	idx_t count = chunk.size();
	if (count == 0) return;

	// Determine max sizes for buffer allocation
	vector<size_t> required_sizes(chunk.ColumnCount(), 4096);
	
	for (idx_t col_idx = 0; col_idx < chunk.ColumnCount(); col_idx++) {
		if (chunk.data[col_idx].GetVectorType() != VectorType::FLAT_VECTOR) {
			chunk.data[col_idx].Flatten(count);
		}
		
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
				if (s.size() > max_len) max_len = s.size();
			}
		}
		
		if (max_len > required_sizes[col_idx]) {
			required_sizes[col_idx] = max_len + 32;
		}
		// Align
		required_sizes[col_idx] = (required_sizes[col_idx] + 3) & ~3;
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
			string type = StringUtil::Upper(oracle_types[col_idx]);
			ub2 bind_type = SQLT_CHR; // Use SQLT_CHR (VARCHAR2) instead of SQLT_STR for length-based binding
			
			if (type == "BLOB" || type == "RAW") {
				bind_type = SQLT_LBI;
			} else if (type == "CLOB") {
				bind_type = SQLT_LNG;
			}
			
			auto ctx = connection->Get();
			CheckOCIError(OCIBindByPos(stmthp, &binds[col_idx], ctx->errhp, col_idx + 1,
			                           bind_buffers[col_idx].data(), current_buffer_sizes[col_idx], bind_type,
			                           indicator_buffers[col_idx].data(), length_buffers[col_idx].data(),
			                           nullptr, 0, nullptr, OCI_DEFAULT),
			              ctx->errhp, "OCIBindByPos");
		}
	}

	for (idx_t col_idx = 0; col_idx < chunk.ColumnCount(); col_idx++) {
		BindColumn(chunk.data[col_idx], col_idx, count);
	}

	ExecuteBatch(count);
}

void OracleWriteLocalState::BindColumn(Vector &col, idx_t col_idx, idx_t count) {
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
			Value val = col.GetValue(i);
			string str_val;
			
			if (val.type().id() == LogicalTypeId::BLOB) {
				str_val = StringValue::Get(val);
			} else if (val.type().id() == LogicalTypeId::TIMESTAMP || val.type().id() == LogicalTypeId::DATE) {
				// Default timestamp format matches Oracle's standard if we use TO_TIMESTAMP
				// But DuckDB timestamp format is 'YYYY-MM-DD HH:MM:SS.MS'
				// Oracle default input might expect T separator or not.
				// But we use TO_DATE/TO_TIMESTAMP in SQL with format model.
				// DuckDB::ToString() -> "2023-01-01 12:00:00.123"
				// SQL -> TO_TIMESTAMP(:1, 'YYYY-MM-DD HH24:MI:SS.FF')
				// This matches perfectly.
				str_val = val.ToString();
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

void OracleWriteLocalState::ExecuteBatch(idx_t count) {
	auto ctx = connection->Get();
	CheckOCIError(OCIStmtExecute(ctx->svchp, stmthp, ctx->errhp, static_cast<ub4>(count), 0, nullptr, nullptr, OCI_DEFAULT),
	              ctx->errhp, "OCIStmtExecute Insert");
}

void OracleWriteLocalState::Flush() {
}

void OracleWriteFinalize(ClientContext &context, FunctionData &bind_data, GlobalFunctionData &gstate_p) {
	auto &gstate = (OracleWriteGlobalState &)gstate_p;
	if (gstate.connection) {
		auto ctx = gstate.connection->Get();
		CheckOCIError(OCITransCommit(ctx->svchp, ctx->errhp, OCI_DEFAULT), ctx->errhp, "OCITransCommit");
	}
}

} // namespace duckdb