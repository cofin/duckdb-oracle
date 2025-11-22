#pragma once

#include "duckdb/common/mutex.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/common/vector.hpp"
#include "oracle_connection.hpp"
#include <string>

namespace duckdb {

//! Shared state per attached Oracle database used by generators for schemas/tables.
class OracleCatalogState {
public:
	explicit OracleCatalogState(std::string connection_string_p)
	    : connection_string(std::move(connection_string_p)), connection(make_uniq<OracleConnection>()) {
	}

	OracleConnection &EnsureConnection();

	vector<string> ListSchemas();
	vector<string> ListTables(const string &schema);

	const string connection_string;

private:
	mutex lock;
	unique_ptr<OracleConnection> connection;
	vector<string> schema_cache;
	unordered_map<string, vector<string>> table_cache;
};

} // namespace duckdb
