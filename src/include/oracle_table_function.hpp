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
	string base_query;
	string query;
	vector<ub2> oci_types;
	vector<ub4> oci_sizes;
	vector<string> column_names;
	vector<LogicalType> original_types;
	vector<string> original_names;
	OracleSettings settings;
	std::shared_ptr<OracleConnectionHandle> conn_handle;

	// Statement prepared in bind; executed in global scan state
	std::shared_ptr<OCIStmt> stmt;
	// Metadata kept in bind data
	bool finished = false;

	OracleBindData();

	unique_ptr<FunctionData> Copy() const override;
	bool Equals(const FunctionData &other) const override;
};

struct OracleScanState : public GlobalTableFunctionState {
	std::shared_ptr<OracleConnectionHandle> conn_handle;
	OCISvcCtx *svc = nullptr;
	std::shared_ptr<OCIStmt> stmt;
	OCIError *err = nullptr;
	vector<vector<char>> buffers;
	vector<OCIDefine *> defines;
	vector<vector<sb2>> indicators;
	vector<vector<ub2>> return_lens;
	vector<vector<OCILobLocator *>> lob_locators; // Added for LOB support
	bool executed = false;
	bool defines_bound = false;
	bool finished = false;

	explicit OracleScanState(idx_t column_count) {
		buffers.resize(column_count);
		defines.assign(column_count, nullptr);
		indicators.resize(column_count);
		return_lens.resize(column_count);
		lob_locators.resize(column_count);
	}

	idx_t MaxThreads() const override {
		return 1; // streaming cursor per scan
	}
};

unique_ptr<FunctionData> OracleBindInternal(ClientContext &context, string connection_string, string query,
                                            vector<LogicalType> &return_types, vector<string> &names,
                                            OracleBindData *bind_data_ptr = nullptr,
                                            OracleCatalogState *state = nullptr);

void OracleQueryFunction(ClientContext &context, TableFunctionInput &data, DataChunk &output);

void OraclePushdownComplexFilter(ClientContext &context, LogicalGet &get, FunctionData *bind_data_p,
                                 vector<unique_ptr<Expression>> &expressions);

unique_ptr<GlobalTableFunctionState> OracleInitGlobal(ClientContext &context, TableFunctionInitInput &input);

} // namespace duckdb
