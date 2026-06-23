#pragma once

#include "duckdb.hpp"
#include "oracle_connection.hpp"
#include "oracle_utils.hpp"
#include "oracle_settings.hpp"
#include "duckdb/function/copy_function.hpp"
#include "duckdb/common/vector.hpp"
#include <vector>

namespace duckdb {

struct OracleWriteBindData : public FunctionData {
	string table_name;
	string connection_string;
	string wallet_path;
	OracleSettings settings;

	// Helper to reconstruct SQL
	string schema_name;
	string object_name;

	vector<string> column_names;
	vector<LogicalType> column_types;

	// Oracle metadata for smart binding
	vector<string> oracle_types; // e.g., "NUMBER", "BLOB", "SDO_GEOMETRY"
	vector<ub2> bind_types;      // OCI bind type (e.g., SQLT_INT)

public:
	unique_ptr<FunctionData> Copy() const override {
		auto result = make_uniq<OracleWriteBindData>();
		result->table_name = table_name;
		result->connection_string = connection_string;
		result->wallet_path = wallet_path;
		result->settings = settings;
		result->schema_name = schema_name;
		result->object_name = object_name;
		result->column_names = column_names;
		result->column_types = column_types;
		result->oracle_types = oracle_types;
		result->bind_types = bind_types;
		return std::move(result);
	}

	bool Equals(const FunctionData &other_p) const override {
		auto &other = other_p.Cast<OracleWriteBindData>();
		return table_name == other.table_name && connection_string == other.connection_string &&
		       wallet_path == other.wallet_path;
	}
};

class OracleWriteGlobalState : public GlobalFunctionData {
public:
	OracleWriteGlobalState(std::shared_ptr<OracleConnectionHandle> conn, const string &query);
	~OracleWriteGlobalState() override;

	void MarkUncommittedWork() {
		has_uncommitted_work = true;
	}
	void MarkCommitted() {
		has_uncommitted_work = false;
		committed = true;
	}
	bool ShouldCommit() const {
		return has_uncommitted_work && !committed && !aborted;
	}
	void RollbackUncommitted() noexcept;

	std::shared_ptr<OracleConnectionHandle> connection;
	OCIHandlePtr<OCIStmt> stmthp;

private:
	bool has_uncommitted_work = false;
	bool committed = false;
	bool aborted = false;
};

class OracleWriteLocalState : public LocalFunctionData {
public:
	OracleWriteLocalState(std::shared_ptr<OracleConnectionHandle> conn, OCIStmt *stmthp);
	~OracleWriteLocalState() override;

	void Sink(DataChunk &chunk, const vector<string> &oracle_types, const vector<ub2> &bind_types);

	friend void OracleWriteSink(ExecutionContext &context, FunctionData &bind_data, GlobalFunctionData &gstate,
	                            LocalFunctionData &lstate, DataChunk &input);

private:
	void BindColumn(Vector &col, idx_t col_idx, idx_t count, ub2 bind_type);
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

void RejectOracleWriteInExplicitTransaction(ClientContext &context);

unique_ptr<GlobalFunctionData> OracleWriteInitGlobal(ClientContext &context, FunctionData &bind_data,
                                                     const string &file_path);

unique_ptr<LocalFunctionData> OracleWriteInitLocal(ExecutionContext &context, FunctionData &bind_data);

void OracleWriteSink(ExecutionContext &context, FunctionData &bind_data, GlobalFunctionData &gstate,
                     LocalFunctionData &lstate, DataChunk &input);

void OracleWriteFinalize(ClientContext &context, FunctionData &bind_data, GlobalFunctionData &gstate);

} // namespace duckdb
