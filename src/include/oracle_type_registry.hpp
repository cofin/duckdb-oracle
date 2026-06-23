#pragma once

#include "duckdb.hpp"
#include "oracle_catalog_state.hpp"
#include "oracle_settings.hpp"
#include <oci.h>

namespace duckdb {

//! Classification of Oracle column types for transport and conversion policy.
enum class OracleTypeCategory {
	STANDARD,
	LONG_TEXT,
	NUMERIC,
	FLOATING,
	TEMPORAL,
	TIMESTAMP_TZ,
	TIMESTAMP_LTZ,
	INTERVAL,
	SPATIAL,
	VECTOR,
	JSON,
	LOB_CLOB,
	LOB_BLOB,
	BFILE,
	RAW,
	LONG_RAW,
	ROW_ID,
	XML,
	BOOLEAN,
	OBJECT,
	UNKNOWN
};

//! Normalized metadata used to resolve an Oracle datatype capability decision.
struct OracleTypeMetadata {
	string schema_name;
	string table_name;
	string column_name;
	string oracle_data_type;
	string type_owner;
	string type_name;
	idx_t data_length = 0;
	idx_t char_length = 0;
	bool char_used = false;
	idx_t precision = 0;
	int32_t scale = 0;
	bool has_scale = false;
	idx_t srid = 0;
	ub2 oci_type = 0;
	bool has_oci_type = false;
};

//! One authoritative decision for an Oracle datatype across scan, conversion, write, and pushdown code.
struct OracleTypeDecision {
	OracleTypeCategory category = OracleTypeCategory::UNKNOWN;
	LogicalType duckdb_type = LogicalType::VARCHAR;
	string normalized_type;
	string unsupported_reason;
	bool supported = true;
	bool needs_server_conversion = false;
	bool pushdown_eligible = false;
	ub2 fetch_type = SQLT_STR;
	ub2 write_bind_type = SQLT_CHR;

	bool RequiresQueryRewrite(const OracleVersionInfo &version, bool try_native_lobs = true) const;
	string ConversionExpression(const string &quoted_col, const OracleVersionInfo &version) const;
	string UnsupportedError(const OracleTypeMetadata &metadata) const;
};

//! Metadata about Oracle column types for query rewriting and fetch strategy.
struct OracleColumnMetadata {
	string column_name;
	string oracle_data_type;
	string schema_name;
	string table_name;
	idx_t srid = 0;
	OracleTypeDecision decision;

	OracleColumnMetadata() = default;

	OracleTypeCategory Category() const {
		return decision.category;
	}
	bool RequiresQueryRewrite(const OracleVersionInfo &version, bool try_native_lobs = true) const {
		return decision.RequiresQueryRewrite(version, try_native_lobs);
	}
	string ConversionExpression(const string &quoted_col, const OracleVersionInfo &version) const {
		return decision.ConversionExpression(quoted_col, version);
	}
};

//! Shared Oracle datatype capability registry.
class OracleTypeRegistry {
public:
	static OracleTypeDecision ResolveMetadata(const OracleTypeMetadata &metadata, const OracleSettings &settings);
	static OracleTypeDecision ResolveOciDescribe(const OracleTypeMetadata &metadata, const OracleSettings &settings);
	static OracleTypeDecision ResolveWrite(const OracleTypeMetadata &metadata, const LogicalType &duckdb_type,
	                                       const OracleSettings &settings);
	static void ValidateSupported(const OracleTypeDecision &decision, const OracleTypeMetadata &metadata);
};

} // namespace duckdb
