#pragma once

#include "duckdb.hpp"
#include "duckdb/planner/logical_operator.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include <oci.h>
#include "oracle_settings.hpp"
#include "oracle_connection_manager.hpp"

namespace duckdb {

class OracleCatalogState;

struct OracleBindData : public FunctionData {
	string connection_string;
	string wallet_path;
	string base_query;
	string query;
	vector<ub2> oci_types;
	vector<ub4> oci_sizes;
	vector<string> oracle_type_names;
	vector<string> column_names;
	vector<LogicalType> original_types;
	vector<string> original_names;
	OracleSettings settings;
	std::shared_ptr<OracleConnectionHandle> conn_handle;

	// Statement prepared in bind; executed in global scan state
	std::shared_ptr<OCIStmt> stmt;

	OracleBindData();

	unique_ptr<FunctionData> Copy() const override;
	bool Equals(const FunctionData &other) const override;
};

struct OracleScanState : public GlobalTableFunctionState {
	std::shared_ptr<OracleConnectionHandle> conn_handle;
	std::shared_ptr<OCIStmt> stmt;
	OCIError *err = nullptr;
	vector<vector<char>> buffers;
	vector<OCIDefine *> defines;
	vector<vector<sb2>> indicators;
	vector<vector<ub2>> return_lens;
	vector<idx_t> column_mapping; // Map output column index to buffer index
	idx_t fetch_size = STANDARD_VECTOR_SIZE;
	bool executed = false;
	bool finished = false;

	explicit OracleScanState(idx_t column_count) {
		buffers.resize(column_count);
		defines.assign(column_count, nullptr);
		indicators.resize(column_count);
		return_lens.resize(column_count);
	}

	idx_t MaxThreads() const override {
		return 1; // streaming cursor per scan
	}
};

unique_ptr<FunctionData> OracleBindInternal(ClientContext &context, string connection_string, string query,
                                            vector<LogicalType> &return_types, vector<string> &names,
                                            OracleBindData *bind_data_ptr = nullptr,
                                            OracleCatalogState *state = nullptr, bool reject_bare_identifier = false,
                                            const char *surface = "Oracle table function");

void OracleQueryFunction(ClientContext &context, TableFunctionInput &data, DataChunk &output);

unique_ptr<GlobalTableFunctionState> OracleInitGlobal(ClientContext &context, TableFunctionInitInput &input);

unique_ptr<FunctionData> OracleScanBind(ClientContext &context, TableFunctionBindInput &input,
                                        vector<LogicalType> &return_types, vector<string> &names);

unique_ptr<FunctionData> OracleQueryBind(ClientContext &context, TableFunctionBindInput &input,
                                         vector<LogicalType> &return_types, vector<string> &names);

} // namespace duckdb
