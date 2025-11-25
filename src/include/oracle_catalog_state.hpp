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

//! Oracle database version information with feature flags
struct OracleVersionInfo {
	int major = 0;
	int minor = 0;
	int patch = 0;
	bool supports_json_type = false;         // Oracle 21c+ has native JSON type
	bool supports_vector = false;            // Oracle 23ai+ has VECTOR type
	bool supports_vector_serialize = false;  // Oracle 23.4+ has VECTOR_SERIALIZE function
};

//! Shared state per attached Oracle database used by generators for schemas/tables.
class OracleCatalogState {
public:
	explicit OracleCatalogState(std::string connection_string_p)
	    : connection_string(std::move(connection_string_p)), connection(make_uniq<OracleConnection>()) {
	}

	void Connect();
	OracleResult Query(const std::string &query);
	void ApplyOptions(const unordered_map<string, Value> &options);
	void ClearCaches();

	static void Register(const shared_ptr<OracleCatalogState> &state);
	static void Register(const shared_ptr<OracleCatalogState> &state, const string &alias);
	static void ClearAllCaches();
	static shared_ptr<OracleCatalogState> LookupByAlias(const string &alias);

	OracleSettings settings;

	// Current schema detection
	void DetectCurrentSchema();
	string GetCurrentSchema() const {
		return current_schema;
	}

	// Oracle version detection
	void DetectOracleVersion();
	const OracleVersionInfo &GetVersionInfo() const {
		return version_info;
	}

	// Metadata enumeration
	vector<string> ListSchemas();
	vector<string> ListTables(const string &schema);
	vector<string> ListObjects(const string &schema, const string &object_types);

	// Synonym resolution (returns empty pair if not found)
	pair<string, string> ResolveSynonym(const string &schema, const string &synonym_name, bool &found);

	// On-demand table loading
	bool ObjectExists(const string &schema, const string &object_name, const string &object_types);
	string GetObjectName(const string &schema, const string &object_name, const string &object_types);
	string GetRealSchemaName(const string &name);

	const string connection_string;

private:
	OracleConnection &EnsureConnectionInternal();
	std::mutex lock;
	unique_ptr<OracleConnection> connection;
	string current_schema;
	OracleVersionInfo version_info;
	bool version_detected = false;
	vector<string> schema_cache;
	unordered_map<string, vector<string>> table_cache;
	unordered_map<string, vector<string>> object_cache;
};

} // namespace duckdb
