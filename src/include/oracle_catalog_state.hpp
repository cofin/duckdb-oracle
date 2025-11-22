#pragma once

#include "duckdb/common/mutex.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/common/types/value.hpp"
#include "oracle_connection.hpp"
#include "oracle_settings.hpp"
#include <string>
#include <memory>

namespace duckdb {

//! Shared state per attached Oracle database used by generators for schemas/tables.
class OracleCatalogState {
public:
	explicit OracleCatalogState(std::string connection_string_p)
	    : connection_string(std::move(connection_string_p)), connection(make_uniq<OracleConnection>()) {
	}

	OracleConnection &EnsureConnection();
	void ApplyOptions(const unordered_map<string, Value> &options);
	void ClearCaches();

	static void Register(const shared_ptr<OracleCatalogState> &state);
	static void ClearAllCaches();

	OracleSettings settings;

	vector<string> ListSchemas();
	vector<string> ListTables(const string &schema);

	const string connection_string;

private:
	std::mutex lock;
	unique_ptr<OracleConnection> connection;
	vector<string> schema_cache;
	unordered_map<string, vector<string>> table_cache;
};

} // namespace duckdb
