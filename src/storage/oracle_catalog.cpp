#include "oracle_catalog_state.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/algorithm.hpp"
#include "duckdb/common/limits.hpp"
#include <mutex>

namespace {
using namespace duckdb;
static mutex oracle_state_registry_lock;
static vector<weak_ptr<OracleCatalogState>> oracle_state_registry;
}

namespace duckdb {

OracleConnection &OracleCatalogState::EnsureConnection() {
	if (!settings.connection_cache) {
		connection = make_uniq<OracleConnection>();
	}
	if (!connection->IsConnected()) {
		connection->Connect(connection_string);
	}
	return *connection;
}

void OracleCatalogState::ApplyOptions(const unordered_map<string, Value> &options) {
	// Options are case-insensitive; normalize by lowering.
	for (auto &entry : options) {
		auto key = StringUtil::Lower(entry.first);
		if (key == "enable_pushdown") {
			settings.enable_pushdown = entry.second.GetValue<bool>();
		} else if (key == "prefetch_rows") {
			auto val = entry.second.GetValue<int64_t>();
			settings.prefetch_rows = MaxValue<idx_t>(1, static_cast<idx_t>(val));
		} else if (key == "prefetch_memory") {
			auto val = entry.second.GetValue<int64_t>();
			settings.prefetch_memory = val <= 0 ? 0 : static_cast<idx_t>(val);
		} else if (key == "array_size") {
			auto val = entry.second.GetValue<int64_t>();
			settings.array_size = MaxValue<idx_t>(1, static_cast<idx_t>(val));
		} else if (key == "connection_cache") {
			settings.connection_cache = entry.second.GetValue<bool>();
		} else if (key == "connection_limit") {
			auto val = entry.second.GetValue<int64_t>();
			settings.connection_limit = MaxValue<idx_t>(1, static_cast<idx_t>(val));
		} else if (key == "debug_show_queries") {
			settings.debug_show_queries = entry.second.GetValue<bool>();
		}
	}
}

void OracleCatalogState::ClearCaches() {
	lock_guard<mutex> guard(lock);
	schema_cache.clear();
	table_cache.clear();
	// Reset connection if caching is enabled; a fresh connect will be created lazily.
	connection = make_uniq<OracleConnection>();
}

void OracleCatalogState::Register(const shared_ptr<OracleCatalogState> &state) {
	lock_guard<mutex> guard(oracle_state_registry_lock);
	oracle_state_registry.push_back(state);
}

void OracleCatalogState::ClearAllCaches() {
	lock_guard<mutex> guard(oracle_state_registry_lock);
	for (auto it = oracle_state_registry.begin(); it != oracle_state_registry.end();) {
		auto ptr = it->lock();
		if (!ptr) {
			it = oracle_state_registry.erase(it);
			continue;
		}
		ptr->ClearCaches();
		++it;
	}
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
