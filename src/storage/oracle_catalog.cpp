#include "oracle_catalog_state.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/algorithm.hpp"
#include "duckdb/common/limits.hpp"
#include <mutex>
#include <memory>

namespace duckdb {
namespace {
static std::mutex &RegistryLock() {
	static std::mutex lock;
	return lock;
}

static std::vector<weak_ptr<OracleCatalogState>> &Registry() {
	static std::vector<weak_ptr<OracleCatalogState>> registry;
	return registry;
}

static unordered_map<string, weak_ptr<OracleCatalogState>> &AliasRegistry() {
	static unordered_map<string, weak_ptr<OracleCatalogState>> registry;
	return registry;
}
} // namespace

void OracleCatalogState::Connect() {
	lock_guard<std::mutex> guard(lock);
	EnsureConnectionInternal();
}

OracleResult OracleCatalogState::Query(const std::string &query) {
	lock_guard<std::mutex> guard(lock);
	return EnsureConnectionInternal().Query(query);
}

OracleConnection &OracleCatalogState::EnsureConnectionInternal() {
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
		} else if (key == "lazy_schema_loading") {
			settings.lazy_schema_loading = entry.second.GetValue<bool>();
		} else if (key == "metadata_object_types") {
			settings.metadata_object_types = entry.second.ToString();
		} else if (key == "metadata_result_limit") {
			auto val = entry.second.GetValue<int64_t>();
			settings.metadata_result_limit = val <= 0 ? 0 : static_cast<idx_t>(val);
		} else if (key == "use_current_schema") {
			settings.use_current_schema = entry.second.GetValue<bool>();
		} else if (key == "try_native_lobs") {
			settings.try_native_lobs = entry.second.GetValue<bool>();
		} else if (key == "lob_max_size") {
			auto val = entry.second.GetValue<int64_t>();
			settings.lob_max_size = val <= 0 ? 0 : static_cast<idx_t>(val);
		} else if (key == "vector_to_list") {
			settings.vector_to_list = entry.second.GetValue<bool>();
		} else if (key == "enable_type_conversion") {
			settings.enable_type_conversion = entry.second.GetValue<bool>();
		} else if (key == "enable_spatial_types") {
			settings.enable_spatial_types = entry.second.GetValue<bool>();
		}
	}
}

void OracleCatalogState::ClearCaches() {
	lock_guard<std::mutex> guard(lock);
	schema_cache.clear();
	table_cache.clear();
	object_cache.clear();
	current_schema.clear();
	version_detected = false;
	version_info = OracleVersionInfo();
	// Reset connection if caching is enabled; a fresh connect will be created lazily.
	connection = make_uniq<OracleConnection>();
}

void OracleCatalogState::Register(const shared_ptr<OracleCatalogState> &state) {
	lock_guard<std::mutex> guard(RegistryLock());
	Registry().push_back(weak_ptr<OracleCatalogState>(state));
}

void OracleCatalogState::Register(const shared_ptr<OracleCatalogState> &state, const string &alias) {
	lock_guard<std::mutex> guard(RegistryLock());
	Registry().push_back(weak_ptr<OracleCatalogState>(state));
	if (!alias.empty()) {
		auto key = StringUtil::Lower(alias);
		AliasRegistry()[key] = weak_ptr<OracleCatalogState>(state);
	}
}

void OracleCatalogState::ClearAllCaches() {
	lock_guard<std::mutex> guard(RegistryLock());
	auto &registry = Registry();
	for (auto it = registry.begin(); it != registry.end();) {
		auto ptr = it->lock();
		if (!ptr) {
			it = registry.erase(it);
			continue;
		}
		ptr->ClearCaches();
		++it;
	}
}

shared_ptr<OracleCatalogState> OracleCatalogState::LookupByAlias(const string &alias) {
	lock_guard<std::mutex> guard(RegistryLock());
	auto key = StringUtil::Lower(alias);
	auto &areg = AliasRegistry();
	auto entry = areg.find(key);
	if (entry == areg.end()) {
		return nullptr;
	}
	auto ptr = entry->second.lock();
	if (!ptr) {
		areg.erase(entry);
		return nullptr;
	}
	return ptr;
}

void OracleCatalogState::DetectCurrentSchema() {
	lock_guard<std::mutex> guard(lock);
	if (!current_schema.empty()) {
		return;
	}
	EnsureConnectionInternal();
	try {
		auto result = connection->Query("SELECT SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA') FROM DUAL");
		if (!result.rows.empty() && !result.rows[0].empty()) {
			current_schema = result.rows[0][0];
		}
	} catch (const std::exception &e) {
	}
}

void OracleCatalogState::DetectOracleVersion() {
	lock_guard<std::mutex> guard(lock);
	if (version_detected) {
		return;
	}
	EnsureConnectionInternal();
	try {
		// Try V$INSTANCE first (most reliable)
		auto result = connection->Query("SELECT VERSION_FULL FROM V$INSTANCE");
		string version_str;
		if (!result.rows.empty() && !result.rows[0].empty()) {
			version_str = result.rows[0][0];
		} else {
			// Fallback to PRODUCT_COMPONENT_VERSION
			result = connection->Query("SELECT VERSION FROM PRODUCT_COMPONENT_VERSION WHERE ROWNUM = 1");
			if (!result.rows.empty() && !result.rows[0].empty()) {
				version_str = result.rows[0][0];
			}
		}

		if (!version_str.empty()) {
			// Parse version string like "23.4.0.24.05" or "21.3.0.0.0"
			auto parts = StringUtil::Split(version_str, '.');
			if (!parts.empty()) {
				try {
					version_info.major = std::stoi(parts[0]);
				} catch (...) {
				}
			}
			if (parts.size() >= 2) {
				try {
					version_info.minor = std::stoi(parts[1]);
				} catch (...) {
				}
			}
			if (parts.size() >= 3) {
				try {
					version_info.patch = std::stoi(parts[2]);
				} catch (...) {
				}
			}

			// Set feature flags based on version
			version_info.supports_json_type = (version_info.major >= 21);
			version_info.supports_vector = (version_info.major >= 23);
			version_info.supports_vector_serialize =
			    (version_info.major > 23) || (version_info.major == 23 && version_info.minor >= 4);

			if (settings.debug_show_queries) {
				fprintf(stderr,
				        "[oracle] Detected Oracle version: %d.%d.%d (JSON=%s, VECTOR=%s, VECTOR_SERIALIZE=%s)\n",
				        version_info.major, version_info.minor, version_info.patch,
				        version_info.supports_json_type ? "yes" : "no", version_info.supports_vector ? "yes" : "no",
				        version_info.supports_vector_serialize ? "yes" : "no");
			}
		}
	} catch (const std::exception &e) {
		// Silently ignore version detection failures - assume older version
		if (settings.debug_show_queries) {
			fprintf(stderr, "[oracle] Version detection failed: %s\n", e.what());
		}
	}
	version_detected = true;
}

vector<string> OracleCatalogState::ListSchemas() {
	lock_guard<std::mutex> guard(lock);
	if (!schema_cache.empty()) {
		return schema_cache;
	}

	EnsureConnectionInternal();

	// Lazy loading: return only current schema by default
	if (settings.lazy_schema_loading) {
		if (current_schema.empty()) {
			auto result = connection->Query("SELECT SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA') FROM DUAL");
			if (!result.rows.empty() && !result.rows[0].empty()) {
				current_schema = result.rows[0][0];
			}
		}

		if (!current_schema.empty()) {
			if (settings.use_current_schema) {
				schema_cache.push_back("main");
			}
			schema_cache.push_back(current_schema);
			return schema_cache;
		}
	}

	// Non-lazy: load all schemas
	auto result = connection->Query("SELECT username FROM all_users ORDER BY username");
	for (auto &row : result.rows) {
		if (!row.empty()) {
			schema_cache.push_back(row[0]);
		}
	}
	return schema_cache;
}

vector<string> OracleCatalogState::ListTables(const string &schema) {
	lock_guard<std::mutex> guard(lock);
	auto entry = table_cache.find(schema);
	if (entry != table_cache.end()) {
		return entry->second;
	}
	EnsureConnectionInternal();

	if (schema.empty()) {
		return {};
	}

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

vector<string> OracleCatalogState::ListObjects(const string &schema, const string &object_types) {
	lock_guard<std::mutex> guard(lock);

	auto cache_key = schema + ":" + object_types;
	auto entry = object_cache.find(cache_key);
	if (entry != object_cache.end()) {
		return entry->second;
	}

	EnsureConnectionInternal();

	// Build IN clause from comma-separated object_types
	auto types = StringUtil::Split(object_types, ',');
	vector<string> quoted_types;
	for (auto &type : types) {
		string trimmed = type;
		StringUtil::Trim(trimmed);
		quoted_types.push_back(Value(trimmed).ToSQLString());
	}
	string types_sql = StringUtil::Join(quoted_types, ",");

	string query = StringUtil::Format("SELECT object_name FROM all_objects "
	                                  "WHERE owner = UPPER(%s) AND object_type IN (%s) "
	                                  "ORDER BY object_name",
	                                  Value(schema).ToSQLString().c_str(), types_sql.c_str());

	// Apply metadata result limit
	if (settings.metadata_result_limit > 0) {
		query = StringUtil::Format("SELECT * FROM (%s) WHERE ROWNUM <= %llu", query.c_str(),
		                           static_cast<uint64_t>(settings.metadata_result_limit));
	}

	auto result = connection->Query(query);
	vector<string> objects;
	for (auto &row : result.rows) {
		if (!row.empty()) {
			objects.push_back(row[0]);
		}
	}

	// Log warning if limit reached
	if (settings.metadata_result_limit > 0 && objects.size() >= settings.metadata_result_limit) {
		fprintf(stderr,
		        "[oracle] Warning: Metadata enumeration limit reached (%lu objects). "
		        "Tables beyond this limit are still accessible via on-demand loading, "
		        "but may not appear in autocomplete. Increase oracle_metadata_result_limit "
		        "or filter with oracle_metadata_object_types for better discovery.\n",
		        (unsigned long)settings.metadata_result_limit);
	}

	object_cache.emplace(cache_key, objects);
	return objects;
}

pair<string, string> OracleCatalogState::ResolveSynonym(const string &schema, const string &synonym_name, bool &found) {
	lock_guard<std::mutex> guard(lock);
	EnsureConnectionInternal();

	auto query = StringUtil::Format("SELECT table_owner, table_name FROM all_synonyms "
	                                "WHERE synonym_name = UPPER(%s) "
	                                "AND (owner = UPPER(%s) OR owner = 'PUBLIC') "
	                                "ORDER BY CASE WHEN owner = UPPER(%s) THEN 0 ELSE 1 END",
	                                Value(synonym_name).ToSQLString().c_str(), Value(schema).ToSQLString().c_str(),
	                                Value(schema).ToSQLString().c_str());

	auto result = connection->Query(query);
	if (result.rows.empty()) {
		found = false;
		return std::make_pair("", "");
	}

	found = true;
	return std::make_pair(result.rows[0][0], result.rows[0][1]);
}

bool OracleCatalogState::ObjectExists(const string &schema, const string &object_name, const string &object_types) {
	lock_guard<std::mutex> guard(lock);
	EnsureConnectionInternal();

	auto query = StringUtil::Format("SELECT 1 FROM all_objects "
	                                "WHERE owner = UPPER(%s) AND object_name = UPPER(%s) "
	                                "AND object_type IN (%s)",
	                                Value(schema).ToSQLString().c_str(), Value(object_name).ToSQLString().c_str(),
	                                object_types.c_str());

	auto result = connection->Query(query);
	return !result.rows.empty();
}

string OracleCatalogState::GetObjectName(const string &schema, const string &object_name, const string &object_types) {
	lock_guard<std::mutex> guard(lock);
	EnsureConnectionInternal();

	auto query = StringUtil::Format("SELECT object_name FROM all_objects "
	                                "WHERE owner = UPPER(%s) AND UPPER(object_name) = UPPER(%s) "
	                                "AND object_type IN (%s)",
	                                Value(schema).ToSQLString().c_str(), Value(object_name).ToSQLString().c_str(),
	                                object_types.c_str());

	auto result = connection->Query(query);
	if (!result.rows.empty()) {
		return result.rows[0][0];
	}
	return "";
}

string OracleCatalogState::GetRealSchemaName(const string &name) {
	lock_guard<std::mutex> guard(lock);
	EnsureConnectionInternal();

	auto query = StringUtil::Format("SELECT username FROM all_users WHERE UPPER(username) = UPPER(%s)",
	                                Value(name).ToSQLString().c_str());

	auto result = connection->Query(query);
	if (!result.rows.empty()) {
		return result.rows[0][0];
	}
	return "";
}

} // namespace duckdb
