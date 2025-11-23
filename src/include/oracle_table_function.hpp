#pragma once

#include "duckdb.hpp"
#include "duckdb/planner/logical_operator.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include <oci.h>
#include "oracle_settings.hpp"

namespace duckdb {

class OracleCatalogState;

struct OracleContext {
	OCIEnv *envhp = nullptr;
	OCISvcCtx *svchp = nullptr;
	OCIStmt *stmthp = nullptr;
	OCIError *errhp = nullptr;
	bool connected = false;

	~OracleContext();
};

struct OracleBindData : public FunctionData {
	std::shared_ptr<OracleContext> ctx;
	string base_query;
	string query;
	vector<ub2> oci_types;
	vector<ub4> oci_sizes;
	vector<string> column_names;
	vector<LogicalType> original_types;
	vector<string> original_names;
	OracleSettings settings;

	OracleBindData();

	unique_ptr<FunctionData> Copy() const override;
	bool Equals(const FunctionData &other) const override;
};

unique_ptr<FunctionData> OracleBindInternal(ClientContext &context, string connection_string, string query,
                                            vector<LogicalType> &return_types, vector<string> &names,
                                            OracleBindData *bind_data_ptr = nullptr,
                                            OracleCatalogState *state = nullptr);

void OracleQueryFunction(ClientContext &context, TableFunctionInput &data, DataChunk &output);

void OraclePushdownComplexFilter(ClientContext &context, LogicalGet &get, FunctionData *bind_data_p,
                                 vector<unique_ptr<Expression>> &expressions);

} // namespace duckdb
