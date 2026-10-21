#pragma once

#include "duckdb.hpp"
#include "duckdb/common/types.hpp"
#include <oci.h>

namespace duckdb {

//! Context included in read conversion errors so malformed Oracle values are actionable.
struct OracleConversionContext {
	const string &column_name;
	const string &oracle_type;
	const LogicalType &duckdb_type;
	idx_t row_index;
};

//! Convert one non-NULL OCI fetch value into the target DuckDB output vector.
void SetOracleOutputValue(ClientContext &context, Vector &output_vector, idx_t row_index, const char *data, ub2 length,
                          const LogicalType &target_type, const OracleConversionContext &conversion_context);

//! Locale-independent integer parser for Oracle catalog metadata columns.
bool TryParseOracleMetadataInt(const string &raw, int32_t &out);

//! Locale-independent non-negative index parser for Oracle catalog metadata columns.
bool TryParseOracleMetadataIndex(const string &raw, idx_t &out);

//! Parse a serialized Oracle VECTOR JSON array ("[v1, v2, ...]") into DuckDB element values.
void ParseOracleVectorElements(const string &text, const LogicalType &element_type, idx_t expected_dims,
                               vector<Value> &elements);

} // namespace duckdb
