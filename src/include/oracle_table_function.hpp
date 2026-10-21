#pragma once

#include "duckdb.hpp"
#include "duckdb/common/insertion_order_preserving_map.hpp"
#include "duckdb/planner/logical_operator.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/storage/statistics/node_statistics.hpp"
#include "oracle_oci_api.hpp"
#include "oracle_settings.hpp"
#include "oracle_connection_manager.hpp"

namespace duckdb {

class OracleCatalogState;
class TableCatalogEntry;

struct OracleBindData : public FunctionData {
	string connection_string;
	string wallet_path;
	string base_query;
	string query;
	string direct_from_sql;
	string direct_select_list_sql;
	vector<ub2> oci_types;
	vector<ub4> oci_sizes;
	vector<string> oracle_type_names;
	vector<bool> pushdown_eligible;
	vector<string> pushdown_clauses;
	vector<string> column_names;
	vector<LogicalType> original_types;
	vector<string> original_names;
	vector<string> converted_select_exprs;
	OracleSettings settings;
	optional_ptr<TableCatalogEntry> table_entry;
	optional_idx estimated_rows;

	OracleBindData();

	unique_ptr<FunctionData> Copy() const override;
	bool Equals(const FunctionData &other) const override;
};

struct OracleScanState : public GlobalTableFunctionState {
	std::shared_ptr<OracleConnectionHandle> conn_handle;
	vector<vector<char>> buffers;
	vector<vector<sb2>> indicators;
	vector<vector<ub2>> return_lens;
	vector<OCIDefine *> defines;
	std::shared_ptr<OCIStmt> stmt;
	OCIError *err = nullptr;
	vector<idx_t> column_mapping;
	vector<idx_t> active_bind_cols;
	vector<idx_t> output_to_active_idx;
	vector<string> active_col_names;
	vector<string> active_oracle_types;
	string executed_query;
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

BindInfo OracleGetBindInfo(TableFunctionGetBindInfoInput &input);

optional_ptr<TableCatalogEntry> OracleGetTableEntry(optional_ptr<const FunctionData> bind_data);

unique_ptr<NodeStatistics> OracleCardinality(ClientContext &context, const FunctionData *bind_data);

InsertionOrderPreservingMap<string> OracleToString(TableFunctionToStringInput &input);

unique_ptr<FunctionData> OracleBindInternal(ClientContext &context, const string &connection_string,
                                            const string &query, vector<LogicalType> &return_types,
                                            vector<string> &names, OracleBindData *bind_data_ptr = nullptr,
                                            OracleCatalogState *state = nullptr, bool reject_bare_identifier = false,
                                            const char *surface = "Oracle table function");

void OracleQueryFunction(ClientContext &context, TableFunctionInput &data, DataChunk &output);

unique_ptr<GlobalTableFunctionState> OracleInitGlobal(ClientContext &context, TableFunctionInitInput &input);

unique_ptr<FunctionData> OracleScanBind(ClientContext &context, TableFunctionBindInput &input,
                                        vector<LogicalType> &return_types, vector<Identifier> &names);

unique_ptr<FunctionData> OracleQueryBind(ClientContext &context, TableFunctionBindInput &input,
                                         vector<LogicalType> &return_types, vector<Identifier> &names);

} // namespace duckdb
