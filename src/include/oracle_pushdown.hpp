#pragma once

#include "duckdb.hpp"
#include "duckdb/planner/operator/logical_get.hpp"

namespace duckdb {

//! Rewrite supported DuckDB filters/projections into Oracle SQL for table functions.
void OraclePushdownComplexFilter(ClientContext &context, LogicalGet &get, FunctionData *bind_data_p,
                                 vector<unique_ptr<Expression>> &expressions);

} // namespace duckdb
