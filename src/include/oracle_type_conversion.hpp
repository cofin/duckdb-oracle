#pragma once

#include "duckdb.hpp"
#include "duckdb/common/types.hpp"
#include <oci.h>

namespace duckdb {

//! Context included in read conversion errors so malformed Oracle values are actionable.
struct OracleConversionContext {
	string column_name;
	string oracle_type;
	LogicalType duckdb_type;
	idx_t row_index;
};

//! Convert one non-NULL OCI fetch value into the target DuckDB output vector.
void SetOracleOutputValue(ClientContext &context, Vector &output_vector, idx_t row_index, const char *data, ub2 length,
                          const LogicalType &target_type, const OracleConversionContext &conversion_context);

} // namespace duckdb
