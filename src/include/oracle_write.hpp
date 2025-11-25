#pragma once

#include "duckdb.hpp"
#include "oracle_connection.hpp"
#include "duckdb/function/copy_function.hpp"
#include "duckdb/common/vector.hpp"
#include <vector>

namespace duckdb {

struct OracleWriteBindData : public FunctionData {
	string table_name;
	string connection_string;

	// Helper to reconstruct SQL
	string schema_name;
	string object_name;

	vector<string> column_names;
	vector<LogicalType> column_types;

	// Oracle metadata for smart binding
	vector<string> oracle_types; // e.g., "NUMBER", "BLOB", "SDO_GEOMETRY"

public:
	unique_ptr<FunctionData> Copy() const override {
		auto result = make_uniq<OracleWriteBindData>();
		result->table_name = table_name;
		result->connection_string = connection_string;
		result->schema_name = schema_name;
		result->object_name = object_name;
		result->column_names = column_names;
		result->column_types = column_types;
		result->oracle_types = oracle_types;
		return std::move(result);
	}

	bool Equals(const FunctionData &other_p) const override {
		auto &other = (const OracleWriteBindData &)other_p;
		return table_name == other.table_name && connection_string == other.connection_string;
	}
};

class OracleWriteGlobalState : public GlobalFunctionData {
public:
	OracleWriteGlobalState(std::shared_ptr<OracleConnectionHandle> conn, const string &query);
	~OracleWriteGlobalState();

	std::shared_ptr<OracleConnectionHandle> connection;
	OCIStmt *stmthp;
};

class OracleWriteLocalState : public LocalFunctionData {
public:
	OracleWriteLocalState(std::shared_ptr<OracleConnectionHandle> conn, OCIStmt *stmthp);
	~OracleWriteLocalState();

	void Sink(DataChunk &chunk, const vector<string> &oracle_types);
	void Flush();

	friend void OracleWriteSink(ExecutionContext &context, FunctionData &bind_data, GlobalFunctionData &gstate,
	                            LocalFunctionData &lstate, DataChunk &input);

private:
	void BindColumn(Vector &col, idx_t col_idx, idx_t count);
	void ExecuteBatch(idx_t count);

	std::shared_ptr<OracleConnectionHandle> connection;
	OCIStmt *stmthp;

	// Buffers for binding
	std::vector<std::vector<char>> bind_buffers;
	std::vector<std::vector<sb2>> indicator_buffers;
	std::vector<std::vector<ub2>> length_buffers;
	std::vector<OCIBind *> binds;
	std::vector<size_t> current_buffer_sizes;

	static constexpr idx_t MAX_BATCH_SIZE = STANDARD_VECTOR_SIZE;
};

// CopyFunction implementations
unique_ptr<FunctionData> OracleWriteBind(ClientContext &context, CopyFunctionBindInput &input,
                                         const vector<string> &names, const vector<LogicalType> &sql_types);

unique_ptr<GlobalFunctionData> OracleWriteInitGlobal(ClientContext &context, FunctionData &bind_data,
                                                     const string &file_path);

unique_ptr<LocalFunctionData> OracleWriteInitLocal(ExecutionContext &context, FunctionData &bind_data);

void OracleWriteSink(ExecutionContext &context, FunctionData &bind_data, GlobalFunctionData &gstate,
                     LocalFunctionData &lstate, DataChunk &input);

void OracleWriteFinalize(ClientContext &context, FunctionData &bind_data, GlobalFunctionData &gstate);

} // namespace duckdb
