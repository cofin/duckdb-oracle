#pragma once

#include "duckdb/common/mutex.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/common/types/value.hpp"
#include "oracle_connection.hpp"
#include "oracle_settings.hpp"
#include <atomic>
#include <string>
#include <memory>

namespace duckdb {

//! Oracle database version information with feature flags
struct OracleVersionInfo {
	int major = 0;
	int minor = 0;
	int patch = 0;
	bool supports_json_type = false;        // Oracle 21c+ has native JSON type
	bool supports_vector = false;           // Oracle 23ai+ has VECTOR type
	bool supports_vector_serialize = false; // Oracle 23.4+ has VECTOR_SERIALIZE function
};

//! Bounded Oracle partition metadata used for planning diagnostics.
struct OraclePartitionMetadata {
	bool is_partitioned = false;
	string partitioning_type;
	string subpartitioning_type;
	vector<string> partition_keys;
	vector<string> subpartition_keys;
	vector<string> partition_names;
	vector<string> subpartition_names;

	string ToDebugString() const;
};

//! Shared state per attached Oracle database used by generators for schemas/tables.
class OracleCatalogState {
public:
	explicit OracleCatalogState(std::string connection_string_p, std::string wallet_path_p = "")
	    : connection_string(std::move(connection_string_p)), wallet_path(std::move(wallet_path_p)) {
	}

	void Connect();
	OracleResult ExecuteMetadataQuery(const std::string &query);
	OracleResult ExecuteMetadataQueryWithStringBinds(const std::string &query,
	                                                 const std::vector<std::string> &bind_values);
	void ApplyOptions(const unordered_map<string, Value> &options);
	void ClearCaches();
	uint64_t GetCacheGeneration() const noexcept {
		return cache_generation.load(std::memory_order_acquire);
	}

	static void Register(const shared_ptr<OracleCatalogState> &state, const string &alias);
	static void ClearAllCaches();
	static shared_ptr<OracleCatalogState> LookupByAlias(const string &alias);

	OracleSettings settings;
	string wallet_path;

	// Current schema detection
	void DetectCurrentSchema();
	string GetCurrentSchema() const;

	// Oracle version detection
	void DetectOracleVersion();
	OracleVersionInfo GetVersionInfo();

	// Metadata enumeration
	vector<string> ListSchemas();
	vector<string> ListObjects(const string &schema, const string &object_types);
	OraclePartitionMetadata LoadPartitionMetadata(const string &schema, const string &table);

	// Synonym resolution (returns empty pair if not found)
	pair<string, string> ResolveSynonym(const string &schema, const string &synonym_name, bool &found);

	// On-demand table loading
	bool GetObjectName(const string &schema, const string &object, const string &object_type, string &out_owner,
	                   string &out_name);
	string GetObjectName(const string &schema, const string &object_name, const string &object_types);
	string GetObjectName(const string &schema, const string &object_name);
	string GetRealSchemaName(const string &name);

	const string connection_string;

private:
	void DetectOracleVersionInternal();

	mutable std::mutex lock;
	string current_schema;
	OracleVersionInfo version_info;
	bool version_detected = false;
	vector<string> schema_cache;
	unordered_map<string, vector<string>> object_cache;
	std::atomic<uint64_t> cache_generation {1};
};

} // namespace duckdb
