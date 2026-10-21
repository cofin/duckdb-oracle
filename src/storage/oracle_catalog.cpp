#include "oracle_catalog_state.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/algorithm.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/vector_size.hpp"
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

static vector<string> FirstColumnValues(const OracleResult &result) {
	vector<string> values;
	values.reserve(result.rows.size());
	for (auto &row : result.rows) {
		if (!row.empty()) {
			values.push_back(row[0]);
		}
	}
	return values;
}
static vector<string> ParseNormalizedObjectTypes(const string &object_types) {
	auto raw_types = StringUtil::Split(object_types, ',');
	vector<string> normalized;
	for (auto &raw : raw_types) {
		string token = raw;
		StringUtil::Trim(token);
		if (token.size() >= 2 && token.front() == '\'' && token.back() == '\'') {
			token = token.substr(1, token.size() - 2);
			StringUtil::Trim(token);
		}
		if (!token.empty()) {
			normalized.push_back(StringUtil::Upper(token));
		}
	}
	return normalized;
}
} // namespace

string OraclePartitionMetadata::ToDebugString() const {
	return StringUtil::Format(
	    "partitioned=%s;partitioning=%s;subpartitioning=%s;keys=%s;subkeys=%s;partitions=%s;"
	    "subpartitions=%s",
	    is_partitioned ? "true" : "false", partitioning_type.c_str(), subpartitioning_type.c_str(),
	    StringUtil::Join(partition_keys, ",").c_str(), StringUtil::Join(subpartition_keys, ",").c_str(),
	    StringUtil::Join(partition_names, ",").c_str(), StringUtil::Join(subpartition_names, ",").c_str());
}

void OracleCatalogState::Connect() {
	OracleConnection conn;
	conn.Connect(connection_string, wallet_path, settings);
}

OracleResult OracleCatalogState::ExecuteMetadataQuery(const std::string &query) {
	OracleConnection conn;
	conn.Connect(connection_string, wallet_path, settings);
	return conn.Query(query);
}

OracleResult OracleCatalogState::ExecuteMetadataQueryWithStringBinds(const std::string &query,
                                                                     const std::vector<std::string> &bind_values) {
	OracleConnection conn;
	conn.Connect(connection_string, wallet_path, settings);
	return conn.QueryWithStringBinds(query, bind_values);
}

void OracleCatalogState::ApplyOptions(const unordered_map<string, Value> &options) {
	case_insensitive_map_t<Value> ci_options;
	for (auto &entry : options) {
		auto key = StringUtil::Lower(entry.first);
		if (key == "secret" || key == "wallet_path" || key == "type" || key == "read_only" || key == "readwrite" ||
		    key == "read_write") {
			continue;
		}
		if (key == "current_schema" || key == "oracle_current_schema") {
			current_schema = entry.second.ToString();
			ci_options[entry.first] = entry.second;
		} else if (key == "enable_pushdown" || key == "oracle_enable_pushdown" || key == "prefetch_rows" ||
		           key == "oracle_prefetch_rows" || key == "prefetch_memory" || key == "oracle_prefetch_memory" ||
		           key == "array_size" || key == "oracle_array_size" || key == "connection_cache" ||
		           key == "oracle_connection_cache" || key == "connection_limit" || key == "oracle_connection_limit" ||
		           key == "debug_show_queries" || key == "oracle_debug_show_queries" || key == "lazy_schema_loading" ||
		           key == "oracle_lazy_schema_loading" || key == "metadata_object_types" ||
		           key == "oracle_metadata_object_types" || key == "metadata_result_limit" ||
		           key == "oracle_metadata_result_limit" || key == "use_current_schema" ||
		           key == "oracle_use_current_schema" || key == "enable_spatial_types" ||
		           key == "oracle_enable_spatial_types" || key == "spatial_as_wkt" || key == "oracle_spatial_as_wkt" ||
		           key == "call_timeout_ms" || key == "oracle_call_timeout_ms" || key == "stmt_cache_size" ||
		           key == "oracle_stmt_cache_size") {
			ci_options[entry.first] = entry.second;
		} else {
			throw InvalidInputException("Unknown Oracle attach option \"%s\"", entry.first.c_str());
		}
	}
	ApplyAttachOptions(ci_options, settings);
}

void OracleCatalogState::ClearCaches() {
	lock_guard<std::mutex> guard(lock);
	schema_cache.clear();
	object_cache.clear();
	current_schema.clear();
	cache_generation.fetch_add(1, std::memory_order_acq_rel);
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

string OracleCatalogState::GetCurrentSchema() const {
	lock_guard<std::mutex> guard(lock);
	return current_schema;
}

void OracleCatalogState::DetectCurrentSchema() {
	lock_guard<std::mutex> guard(lock);
	if (!current_schema.empty()) {
		return;
	}
	try {
		auto result = ExecuteMetadataQuery("SELECT SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA') FROM DUAL");
		if (!result.rows.empty() && !result.rows[0].empty()) {
			current_schema = result.rows[0][0];
		}
	} catch (const std::exception &) {
	}
}

OracleVersionInfo OracleCatalogState::GetVersionInfo() {
	lock_guard<std::mutex> guard(lock);
	if (!version_detected) {
		DetectOracleVersionInternal();
	}
	return version_info;
}

void OracleCatalogState::DetectOracleVersion() {
	lock_guard<std::mutex> guard(lock);
	if (!version_detected) {
		DetectOracleVersionInternal();
	}
}

void OracleCatalogState::DetectOracleVersionInternal() {
	if (version_detected) {
		return;
	}
	try {
		auto result = ExecuteMetadataQuery("SELECT VERSION_FULL FROM V$INSTANCE");
		string version_str;
		if (!result.rows.empty() && !result.rows[0].empty()) {
			version_str = result.rows[0][0];
		} else {
			result = ExecuteMetadataQuery("SELECT VERSION FROM PRODUCT_COMPONENT_VERSION WHERE ROWNUM = 1");
			if (!result.rows.empty() && !result.rows[0].empty()) {
				version_str = result.rows[0][0];
			}
		}

		if (!version_str.empty()) {
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

	if (settings.lazy_schema_loading) {
		if (current_schema.empty()) {
			auto result = ExecuteMetadataQuery("SELECT SYS_CONTEXT('USERENV', 'CURRENT_SCHEMA') FROM DUAL");
			if (!result.rows.empty() && !result.rows[0].empty()) {
				current_schema = result.rows[0][0];
			}
		}

		if (!current_schema.empty()) {
			if (settings.use_current_schema) {
				schema_cache.push_back("main");
			}
			if (current_schema != "main") {
				schema_cache.push_back(current_schema);
			}
			return schema_cache;
		}
	}

	if (settings.use_current_schema) {
		schema_cache.push_back("main");
	}
	auto result = ExecuteMetadataQuery("SELECT username FROM all_users ORDER BY username");
	for (auto &row : result.rows) {
		if (!row.empty() && row[0] != "main") {
			schema_cache.push_back(row[0]);
		}
	}
	return schema_cache;
}

vector<string> OracleCatalogState::ListObjects(const string &schema, const string &object_types) {
	lock_guard<std::mutex> guard(lock);

	auto cache_key = schema + ":" + object_types;
	auto entry = object_cache.find(cache_key);
	if (entry != object_cache.end()) {
		return entry->second;
	}

	auto normalized_types = ParseNormalizedObjectTypes(object_types);
	if (normalized_types.empty()) {
		object_cache.emplace(cache_key, vector<string> {});
		return {};
	}

	vector<string> placeholders;
	vector<string> bind_values;
	bind_values.reserve(2 + normalized_types.size());
	bind_values.push_back(schema);
	bind_values.push_back(schema);
	for (idx_t i = 0; i < normalized_types.size(); i++) {
		placeholders.push_back(":" + std::to_string(i + 3));
		bind_values.push_back(normalized_types[i]);
	}
	string types_sql = StringUtil::Join(placeholders, ", ");

	auto metadata_result_limit = OracleEffectiveMetadataResultLimit(settings);
	string query = StringUtil::Format(
	    "SELECT * FROM ("
	    "SELECT DISTINCT o.object_name FROM all_objects o "
	    "WHERE (o.owner = :1 OR o.owner = UPPER(:2)) AND o.object_type IN (%s) "
	    "AND (o.object_type != 'SYNONYM' OR EXISTS ("
	    "SELECT 1 FROM all_synonyms s "
	    "JOIN all_objects target_obj ON target_obj.owner = s.table_owner AND target_obj.object_name = s.table_name "
	    "WHERE s.owner = o.owner AND s.synonym_name = o.object_name "
	    "AND s.db_link IS NULL AND target_obj.object_type IN ('TABLE', 'VIEW', 'MATERIALIZED VIEW')"
	    ")) "
	    "ORDER BY o.object_name"
	    ") WHERE ROWNUM <= %llu",
	    types_sql.c_str(), static_cast<uint64_t>(metadata_result_limit));

	auto result = ExecuteMetadataQueryWithStringBinds(query, bind_values);
	vector<string> objects;
	for (auto &row : result.rows) {
		if (!row.empty()) {
			objects.push_back(row[0]);
		}
	}

	if (objects.size() >= metadata_result_limit) {
		fprintf(stderr,
		        "[oracle] Warning: Metadata enumeration limit reached (%lu objects). "
		        "Tables beyond this limit are still accessible via on-demand loading, "
		        "but may not appear in autocomplete. Increase oracle_metadata_result_limit "
		        "or filter with oracle_metadata_object_types for better discovery.\n",
		        (unsigned long)metadata_result_limit);
	}

	object_cache.emplace(cache_key, objects);
	return objects;
}

OraclePartitionMetadata OracleCatalogState::LoadPartitionMetadata(const string &schema, const string &table) {
	OraclePartitionMetadata metadata;
	try {
		auto table_result =
		    ExecuteMetadataQueryWithStringBinds("SELECT owner, table_name, partitioning_type, subpartitioning_type "
		                                        "FROM all_part_tables "
		                                        "WHERE (owner = :1 OR owner = UPPER(:2)) "
		                                        "AND (table_name = :3 OR table_name = UPPER(:4)) "
		                                        "ORDER BY CASE WHEN owner = :5 THEN 0 ELSE 1 END, "
		                                        "CASE WHEN table_name = :6 THEN 0 ELSE 1 END "
		                                        "FETCH FIRST 1 ROWS ONLY",
		                                        {schema, schema, table, table, schema, table});
		if (table_result.rows.empty() || table_result.rows[0].size() < 4) {
			return metadata;
		}

		const string resolved_owner = table_result.rows[0][0];
		const string resolved_table = table_result.rows[0][1];
		metadata.is_partitioned = true;
		metadata.partitioning_type = table_result.rows[0][2];
		metadata.subpartitioning_type = table_result.rows[0][3];

		auto key_result =
		    ExecuteMetadataQueryWithStringBinds("SELECT column_name FROM all_part_key_columns "
		                                        "WHERE (owner = :1 OR owner = UPPER(:2)) "
		                                        "AND (name = :3 OR name = UPPER(:4)) "
		                                        "AND object_type = 'TABLE' "
		                                        "ORDER BY column_position",
		                                        {resolved_owner, resolved_owner, resolved_table, resolved_table});
		metadata.partition_keys = FirstColumnValues(key_result);

		try {
			auto subkey_result =
			    ExecuteMetadataQueryWithStringBinds("SELECT column_name FROM all_subpart_key_columns "
			                                        "WHERE (owner = :1 OR owner = UPPER(:2)) "
			                                        "AND (name = :3 OR name = UPPER(:4)) "
			                                        "AND object_type = 'TABLE' "
			                                        "ORDER BY column_position",
			                                        {resolved_owner, resolved_owner, resolved_table, resolved_table});
			metadata.subpartition_keys = FirstColumnValues(subkey_result);
		} catch (...) {
			metadata.subpartition_keys.clear();
		}

		auto metadata_result_limit = OracleEffectiveMetadataResultLimit(settings);
		auto limit_sql = std::to_string(metadata_result_limit);
		auto partition_query = StringUtil::Format("SELECT partition_name FROM ("
		                                          "SELECT partition_name FROM all_tab_partitions "
		                                          "WHERE (table_owner = :1 OR table_owner = UPPER(:2)) "
		                                          "AND (table_name = :3 OR table_name = UPPER(:4)) "
		                                          "ORDER BY partition_position"
		                                          ") WHERE ROWNUM <= %s",
		                                          limit_sql.c_str());
		auto partition_result = ExecuteMetadataQueryWithStringBinds(
		    partition_query, {resolved_owner, resolved_owner, resolved_table, resolved_table});
		metadata.partition_names = FirstColumnValues(partition_result);

		try {
			auto subpartition_query = StringUtil::Format("SELECT subpartition_name FROM ("
			                                             "SELECT subpartition_name FROM all_tab_subpartitions "
			                                             "WHERE (table_owner = :1 OR table_owner = UPPER(:2)) "
			                                             "AND (table_name = :3 OR table_name = UPPER(:4)) "
			                                             "ORDER BY subpartition_position"
			                                             ") WHERE ROWNUM <= %s",
			                                             limit_sql.c_str());
			auto subpartition_result = ExecuteMetadataQueryWithStringBinds(
			    subpartition_query, {resolved_owner, resolved_owner, resolved_table, resolved_table});
			metadata.subpartition_names = FirstColumnValues(subpartition_result);
		} catch (...) {
			metadata.subpartition_names.clear();
		}
	} catch (...) {
		return OraclePartitionMetadata();
	}
	return metadata;
}

pair<string, string> OracleCatalogState::ResolveSynonym(const string &schema, const string &synonym_name, bool &found) {
	auto query = "SELECT s.table_owner, s.table_name FROM all_synonyms s "
	             "WHERE (s.owner = :1 OR s.owner = UPPER(:2) OR s.owner = 'PUBLIC') "
	             "AND (s.synonym_name = :3 OR s.synonym_name = UPPER(:4)) "
	             "AND s.db_link IS NULL "
	             "AND EXISTS ("
	             "SELECT 1 FROM all_objects o "
	             "WHERE o.owner = s.table_owner AND o.object_name = s.table_name "
	             "AND o.object_type IN ('TABLE', 'VIEW', 'MATERIALIZED VIEW')"
	             ") "
	             "ORDER BY CASE WHEN s.owner = :5 THEN 0 WHEN s.owner = UPPER(:6) THEN 1 ELSE 2 END, "
	             "CASE WHEN s.synonym_name = :7 THEN 0 WHEN s.synonym_name = UPPER(:8) THEN 1 ELSE 2 END "
	             "FETCH FIRST 1 ROWS ONLY";

	auto result = ExecuteMetadataQueryWithStringBinds(
	    query, {schema, schema, synonym_name, synonym_name, schema, schema, synonym_name, synonym_name});
	if (result.rows.empty() || result.rows[0].size() < 2) {
		found = false;
		return std::make_pair("", "");
	}

	found = true;
	return std::make_pair(result.rows[0][0], result.rows[0][1]);
}

bool OracleCatalogState::GetObjectName(const string &schema, const string &object, const string &object_type,
                                       string &out_owner, string &out_name) {
	auto normalized_types = ParseNormalizedObjectTypes(object_type);
	if (normalized_types.empty()) {
		normalized_types = {"TABLE", "VIEW", "MATERIALIZED VIEW"};
	}

	vector<string> bind_values = {schema, schema, object, object};
	vector<string> placeholders;
	placeholders.reserve(normalized_types.size());
	for (auto &type : normalized_types) {
		bind_values.push_back(type);
		placeholders.push_back(":" + std::to_string(bind_values.size()));
	}
	idx_t order_base = bind_values.size();
	bind_values.push_back(schema);
	bind_values.push_back(schema);
	bind_values.push_back(object);
	bind_values.push_back(object);

	auto types_sql = StringUtil::Join(placeholders, ", ");
	auto p1 = std::to_string(order_base + 1);
	auto p2 = std::to_string(order_base + 2);
	auto p3 = std::to_string(order_base + 3);
	auto p4 = std::to_string(order_base + 4);
	auto query =
	    StringUtil::Format("SELECT owner, object_name FROM all_objects "
	                       "WHERE (owner = :1 OR owner = UPPER(:2)) "
	                       "AND (object_name = :3 OR object_name = UPPER(:4)) "
	                       "AND object_type IN (%s) "
	                       "ORDER BY CASE WHEN owner = :%s THEN 0 WHEN owner = UPPER(:%s) THEN 1 ELSE 2 END, "
	                       "CASE WHEN object_name = :%s THEN 0 WHEN object_name = UPPER(:%s) THEN 1 ELSE 2 END, "
	                       "owner, object_name "
	                       "FETCH FIRST 1 ROWS ONLY",
	                       types_sql.c_str(), p1.c_str(), p2.c_str(), p3.c_str(), p4.c_str());

	auto result = ExecuteMetadataQueryWithStringBinds(query, bind_values);
	if (!result.rows.empty() && result.rows[0].size() >= 2) {
		out_owner = result.rows[0][0];
		out_name = result.rows[0][1];
		return true;
	}
	return false;
}

string OracleCatalogState::GetRealSchemaName(const string &name) {
	auto query = "SELECT username FROM all_users "
	             "WHERE (username = :1 OR username = UPPER(:2)) "
	             "ORDER BY CASE WHEN username = :3 THEN 0 ELSE 1 END, username "
	             "FETCH FIRST 1 ROWS ONLY";

	auto result = ExecuteMetadataQueryWithStringBinds(query, {name, name, name});
	if (!result.rows.empty() && !result.rows[0].empty()) {
		return result.rows[0][0];
	}
	auto current = GetCurrentSchema();
	if (!current.empty() && StringUtil::CIEquals(name, current)) {
		return current;
	}
	return "";
}

} // namespace duckdb
