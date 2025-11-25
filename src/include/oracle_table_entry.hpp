#pragma once

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/common/string_util.hpp"
#include "oracle_catalog_state.hpp"

namespace duckdb {

//! Classification of Oracle column types for fetch/conversion strategy
enum class OracleTypeCategory {
	STANDARD,    // VARCHAR, CHAR, NCHAR - no conversion needed
	NUMERIC,     // NUMBER - fetch as string, parse to int/double
	TEMPORAL,    // DATE/TIMESTAMP - fetch as string, parse to timestamp
	SPATIAL,     // SDO_GEOMETRY - convert via SDO_UTIL.TO_WKTGEOMETRY
	VECTOR,      // VECTOR (23ai) - convert via VECTOR_SERIALIZE or native
	JSON,        // JSON (21c+) - convert via JSON_SERIALIZE
	LOB_CLOB,    // CLOB - try native, fallback to TO_CHAR
	LOB_BLOB,    // BLOB - try native, fallback to RAWTOHEX
	RAW,         // RAW - try native SQLT_BIN, fallback to RAWTOHEX
	XML,         // XMLTYPE - convert via XMLSERIALIZE
	UNKNOWN      // Fallback to VARCHAR
};

//! Metadata about Oracle column types for query rewriting and fetch strategy
struct OracleColumnMetadata {
	string column_name;
	string oracle_data_type;
	OracleTypeCategory category;
	bool needs_server_conversion;  // Determined at runtime based on type category

	OracleColumnMetadata(const string &name, const string &data_type)
	    : column_name(name), oracle_data_type(data_type) {
		auto upper = StringUtil::Upper(data_type);

		// Classify Oracle type
		if (upper == "SDO_GEOMETRY" || upper == "MDSYS.SDO_GEOMETRY") {
			category = OracleTypeCategory::SPATIAL;
			needs_server_conversion = true;
		} else if (upper == "VECTOR" || StringUtil::StartsWith(upper, "VECTOR(")) {
			category = OracleTypeCategory::VECTOR;
			needs_server_conversion = true;
		} else if (upper == "JSON") {
			category = OracleTypeCategory::JSON;
			needs_server_conversion = true;
		} else if (upper == "BLOB" || upper == "BFILE") {
			category = OracleTypeCategory::LOB_BLOB;
			needs_server_conversion = false;  // Try native first
		} else if (upper == "CLOB" || upper == "NCLOB") {
			category = OracleTypeCategory::LOB_CLOB;
			needs_server_conversion = false;  // Try native first
		} else if (upper == "RAW" || StringUtil::StartsWith(upper, "RAW(")) {
			category = OracleTypeCategory::RAW;
			needs_server_conversion = false;  // Try native first
		} else if (upper == "XMLTYPE" || upper == "SYS.XMLTYPE") {
			category = OracleTypeCategory::XML;
			needs_server_conversion = true;
		} else if (upper == "NUMBER" || upper == "FLOAT" || upper == "BINARY_FLOAT" ||
		           upper == "BINARY_DOUBLE" || StringUtil::StartsWith(upper, "NUMBER(")) {
			category = OracleTypeCategory::NUMERIC;
			needs_server_conversion = false;
		} else if (upper == "DATE" || StringUtil::Contains(upper, "TIMESTAMP")) {
			category = OracleTypeCategory::TEMPORAL;
			needs_server_conversion = false;
		} else if (StringUtil::Contains(upper, "CHAR") || StringUtil::Contains(upper, "VARCHAR")) {
			category = OracleTypeCategory::STANDARD;
			needs_server_conversion = false;
		} else {
			category = OracleTypeCategory::UNKNOWN;
			needs_server_conversion = false;
		}
	}

	//! Convenience method for backward compatibility
	bool is_spatial() const {
		return category == OracleTypeCategory::SPATIAL;
	}

	//! Check if this column type needs query rewriting for reliable fetch
	//! @param version Oracle version info for version-specific decisions
	//! @param try_native_lobs If false, always use hex conversion for LOB/RAW (safer)
	bool RequiresQueryRewrite(const OracleVersionInfo &version, bool try_native_lobs = true) const {
		switch (category) {
		case OracleTypeCategory::SPATIAL:
		case OracleTypeCategory::XML:
			return true;
		case OracleTypeCategory::VECTOR:
			return true;  // Always rewrite VECTOR for now (safer)
		case OracleTypeCategory::JSON:
			return version.supports_json_type;  // Only rewrite if native JSON type
		case OracleTypeCategory::LOB_BLOB:
		case OracleTypeCategory::RAW:
			// If try_native_lobs is false, always convert to hex for safety
			// This avoids OCI buffer alignment issues with binary types
			return !try_native_lobs || needs_server_conversion;
		case OracleTypeCategory::LOB_CLOB:
			// CLOB is generally safe with native OCI fetch
			return needs_server_conversion;
		default:
			return false;
		}
	}
};

class OracleTableEntry : public TableCatalogEntry {
public:
	OracleTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, unique_ptr<CreateTableInfo> info,
	                 shared_ptr<OracleCatalogState> state, const string &schema_name, const string &table_name,
	                 vector<OracleColumnMetadata> metadata);

	TableFunction GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) override;
	TableStorageInfo GetStorageInfo(ClientContext &context) override;
	unique_ptr<BaseStatistics> GetStatistics(ClientContext &context, column_t column_id) override;

	static unique_ptr<OracleTableEntry> Create(Catalog &catalog, SchemaCatalogEntry &schema, const string &schema_name,
	                                           const string &table_name, shared_ptr<OracleCatalogState> state,
	                                           const string &duckdb_entry_name = "");

private:
	shared_ptr<OracleCatalogState> state;
	string schema_name;
	string table_name;
	vector<OracleColumnMetadata> column_metadata;
};

} // namespace duckdb
