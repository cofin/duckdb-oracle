#include "oracle_catalog_state.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

OracleConnection &OracleCatalogState::EnsureConnection() {
	if (!connection->IsConnected()) {
		connection->Connect(connection_string);
	}
	return *connection;
}

vector<string> OracleCatalogState::ListSchemas() {
	lock_guard<mutex> guard(lock);
	if (!schema_cache.empty()) {
		return schema_cache;
	}
	EnsureConnection();
	auto result = connection->Query("SELECT username FROM all_users ORDER BY username");
	for (auto &row : result.rows) {
		if (!row.empty()) {
			schema_cache.push_back(row[0]);
		}
	}
	return schema_cache;
}

vector<string> OracleCatalogState::ListTables(const string &schema) {
	lock_guard<mutex> guard(lock);
	auto entry = table_cache.find(schema);
	if (entry != table_cache.end()) {
		return entry->second;
	}
	EnsureConnection();
	auto query = StringUtil::Format("SELECT table_name FROM all_tables WHERE owner = UPPER(%s) ORDER BY table_name",
	                                Value(schema).ToSQLString().c_str());
	auto result = connection->Query(query);
	vector<string> tables;
	for (auto &row : result.rows) {
		if (!row.empty()) {
			tables.push_back(row[0]);
		}
	}
	table_cache.emplace(schema, tables);
	return tables;
}

} // namespace duckdb
