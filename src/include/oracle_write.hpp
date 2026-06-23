#pragma once

#include "duckdb.hpp"
#include "oracle_connection.hpp"
#include "oracle_utils.hpp"
#include "oracle_settings.hpp"
#include "duckdb/common/vector.hpp"
#include <vector>

namespace duckdb {

struct OracleWriteBindData {
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
};

class OracleWriteGlobalState {
public:
	OracleWriteGlobalState(std::shared_ptr<OracleConnectionHandle> conn, const string &query);
	~OracleWriteGlobalState();

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

class OracleWriteLocalState {
public:
	OracleWriteLocalState(std::shared_ptr<OracleConnectionHandle> conn, OCIStmt *stmthp);
	~OracleWriteLocalState();

	void Sink(DataChunk &chunk, const vector<string> &oracle_types, const vector<ub2> &bind_types);

	friend void OracleWriteSink(ExecutionContext &context, OracleWriteBindData &bind_data,
	                            OracleWriteGlobalState &gstate, OracleWriteLocalState &lstate, DataChunk &input);

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

void RejectOracleWriteInExplicitTransaction(ClientContext &context);

unique_ptr<OracleWriteGlobalState> OracleWriteInitGlobal(ClientContext &context, OracleWriteBindData &bind_data);

void OracleWriteSink(ExecutionContext &context, OracleWriteBindData &bind_data, OracleWriteGlobalState &gstate,
                     OracleWriteLocalState &lstate, DataChunk &input);

void OracleWriteFinalize(OracleWriteGlobalState &gstate);

} // namespace duckdb
