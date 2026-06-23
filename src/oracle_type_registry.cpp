#include "oracle_type_registry.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"

namespace duckdb {

namespace {

static string NormalizeTypeName(const OracleTypeMetadata &metadata) {
	auto type = metadata.oracle_data_type.empty() ? metadata.type_name : metadata.oracle_data_type;
	type = StringUtil::Upper(type);
	StringUtil::Trim(type);
	return type;
}

static bool IsObjectTypeOwner(const OracleTypeMetadata &metadata) {
	if (metadata.type_owner.empty()) {
		return false;
	}
	auto owner = StringUtil::Upper(metadata.type_owner);
	return owner != "SYS" && owner != "MDSYS";
}

static LogicalType NumberLogicalType(idx_t precision, int32_t scale, bool has_scale, bool direct_oci_describe) {
	if (direct_oci_describe && precision == 0 && (!has_scale || scale == 0)) {
		return LogicalType::BIGINT;
	}
	if (precision == 0 || precision > 38 || scale < 0) {
		return LogicalType::DOUBLE;
	}
	if (scale > static_cast<int32_t>(precision)) {
		return LogicalType::DOUBLE;
	}
	return LogicalType::DECIMAL(static_cast<uint8_t>(precision), static_cast<uint8_t>(scale));
}

static ub2 BindTypeForDuckDBType(const LogicalType &type) {
	switch (type.id()) {
	case LogicalTypeId::TINYINT:
	case LogicalTypeId::SMALLINT:
	case LogicalTypeId::INTEGER:
	case LogicalTypeId::BIGINT:
		return SQLT_INT;
	case LogicalTypeId::FLOAT:
	case LogicalTypeId::DOUBLE:
		return SQLT_BDOUBLE;
	case LogicalTypeId::DATE:
		return SQLT_ODT;
	case LogicalTypeId::BLOB:
		return SQLT_BIN;
	default:
		return SQLT_CHR;
	}
}

static OracleTypeDecision Unsupported(OracleTypeCategory category, const string &normalized_type,
                                      const string &reason) {
	OracleTypeDecision decision;
	decision.category = category;
	decision.normalized_type = normalized_type;
	decision.supported = false;
	decision.unsupported_reason = reason;
	decision.duckdb_type = LogicalType::VARCHAR;
	decision.fetch_type = SQLT_STR;
	decision.write_bind_type = SQLT_CHR;
	return decision;
}

static OracleTypeDecision Supported(OracleTypeCategory category, const string &normalized_type,
                                    const LogicalType &duckdb_type, bool needs_server_conversion,
                                    bool pushdown_eligible) {
	OracleTypeDecision decision;
	decision.category = category;
	decision.normalized_type = normalized_type;
	decision.duckdb_type = duckdb_type;
	decision.supported = true;
	decision.needs_server_conversion = needs_server_conversion;
	decision.pushdown_eligible = pushdown_eligible;
	decision.fetch_type = duckdb_type.id() == LogicalTypeId::BLOB ? SQLT_BIN : SQLT_STR;
	decision.write_bind_type = BindTypeForDuckDBType(duckdb_type);
	return decision;
}

static bool IsCharType(const string &type) {
	return type == "CHAR" || type == "NCHAR" || type == "VARCHAR2" || type == "NVARCHAR2" || type == "VARCHAR" ||
	       type == "NATIONAL CHARACTER" || type == "NATIONAL CHAR";
}

static bool IsTimestampType(const string &type) {
	return type == "TIMESTAMP" || StringUtil::StartsWith(type, "TIMESTAMP(");
}

static bool IsTimestampTZType(const string &type) {
	return type == "TIMESTAMP WITH TIME ZONE" || StringUtil::Contains(type, "TIMESTAMP") &&
	                                                 StringUtil::Contains(type, "WITH TIME ZONE") &&
	                                                 !StringUtil::Contains(type, "LOCAL");
}

static bool IsTimestampLTZType(const string &type) {
	return type == "TIMESTAMP WITH LOCAL TIME ZONE" ||
	       StringUtil::Contains(type, "TIMESTAMP") && StringUtil::Contains(type, "LOCAL TIME ZONE");
}

static bool IsIntervalType(const string &type) {
	return type == "INTERVAL YEAR TO MONTH" || type == "INTERVAL DAY TO SECOND" ||
	       StringUtil::StartsWith(type, "INTERVAL YEAR") || StringUtil::StartsWith(type, "INTERVAL DAY");
}

} // namespace

bool OracleTypeDecision::RequiresQueryRewrite(const OracleVersionInfo &version, bool try_native_lobs) const {
	if (!supported) {
		return false;
	}
	switch (category) {
	case OracleTypeCategory::SPATIAL:
	case OracleTypeCategory::XML:
		return true;
	case OracleTypeCategory::VECTOR:
		return true;
	case OracleTypeCategory::JSON:
		return version.supports_json_type;
	case OracleTypeCategory::LOB_BLOB:
	case OracleTypeCategory::RAW:
	case OracleTypeCategory::LONG_RAW:
		return !try_native_lobs || needs_server_conversion;
	case OracleTypeCategory::LOB_CLOB:
		return needs_server_conversion;
	default:
		return false;
	}
}

string OracleTypeDecision::ConversionExpression(const string &quoted_col, const OracleVersionInfo &version) const {
	switch (category) {
	case OracleTypeCategory::SPATIAL:
		return StringUtil::Format("SDO_UTIL.TO_WKTGEOMETRY(%s)", quoted_col.c_str());
	case OracleTypeCategory::VECTOR:
		return StringUtil::Format("VECTOR_SERIALIZE(%s)", quoted_col.c_str());
	case OracleTypeCategory::JSON:
		if (version.supports_json_type) {
			return StringUtil::Format("JSON_SERIALIZE(%s RETURNING VARCHAR2(32767))", quoted_col.c_str());
		}
		return quoted_col;
	case OracleTypeCategory::XML:
		return StringUtil::Format("XMLSERIALIZE(CONTENT %s AS CLOB)", quoted_col.c_str());
	case OracleTypeCategory::LOB_BLOB:
	case OracleTypeCategory::RAW:
	case OracleTypeCategory::LONG_RAW:
		if (needs_server_conversion) {
			return StringUtil::Format("RAWTOHEX(%s)", quoted_col.c_str());
		}
		return quoted_col;
	case OracleTypeCategory::LOB_CLOB:
		if (needs_server_conversion) {
			return StringUtil::Format("TO_CHAR(%s)", quoted_col.c_str());
		}
		return quoted_col;
	default:
		return quoted_col;
	}
}

string OracleTypeDecision::UnsupportedError(const OracleTypeMetadata &metadata) const {
	auto schema = metadata.schema_name.empty() ? "<query>" : StringUtil::Upper(metadata.schema_name);
	auto table = metadata.table_name.empty() ? "<result>" : StringUtil::Upper(metadata.table_name);
	auto column = metadata.column_name.empty() ? "<unknown>" : StringUtil::Upper(metadata.column_name);
	auto type = normalized_type.empty() ? NormalizeTypeName(metadata) : normalized_type;
	return StringUtil::Format("Unsupported Oracle type %s for %s.%s.%s: %s", type.c_str(), schema.c_str(),
	                          table.c_str(), column.c_str(), unsupported_reason.c_str());
}

OracleTypeDecision OracleTypeRegistry::ResolveMetadata(const OracleTypeMetadata &metadata,
                                                       const OracleSettings &settings) {
	auto type = NormalizeTypeName(metadata);
	if (type.empty()) {
		return Unsupported(OracleTypeCategory::UNKNOWN, "UNKNOWN", "metadata did not include an Oracle type name");
	}
	if (type == "SDO_GEOMETRY" || type == "MDSYS.SDO_GEOMETRY") {
		auto logical = settings.enable_spatial_types
		                   ? (metadata.srid > 0 ? LogicalType::GEOMETRY("EPSG:" + std::to_string(metadata.srid))
		                                        : LogicalType::GEOMETRY())
		                   : LogicalType::VARCHAR;
		return Supported(OracleTypeCategory::SPATIAL, type, logical, true, false);
	}
	if (type == "VECTOR" || StringUtil::StartsWith(type, "VECTOR(")) {
		auto logical = settings.vector_to_list ? LogicalType::LIST(LogicalType::FLOAT) : LogicalType::VARCHAR;
		return Supported(OracleTypeCategory::VECTOR, type, logical, true, false);
	}
	if (type == "JSON") {
		return Supported(OracleTypeCategory::JSON, type, LogicalType::JSON(), true, false);
	}
	if (type == "XMLTYPE" || type == "SYS.XMLTYPE") {
		return Supported(OracleTypeCategory::XML, type, LogicalType::VARCHAR, true, false);
	}
	if (type == "BFILE") {
		return Unsupported(OracleTypeCategory::BFILE, type,
		                   "BFILE stores external file locators; reading file contents is not implemented");
	}
	if (type == "BLOB") {
		return Supported(OracleTypeCategory::LOB_BLOB, type, LogicalType::BLOB, false, false);
	}
	if (type == "CLOB" || type == "NCLOB") {
		return Supported(OracleTypeCategory::LOB_CLOB, type, LogicalType::VARCHAR, false, false);
	}
	if (type == "RAW" || StringUtil::StartsWith(type, "RAW(")) {
		return Supported(OracleTypeCategory::RAW, type, LogicalType::BLOB, false, false);
	}
	if (type == "LONG RAW") {
		return Supported(OracleTypeCategory::LONG_RAW, type, LogicalType::BLOB, false, false);
	}
	if (type == "LONG") {
		return Supported(OracleTypeCategory::LONG_TEXT, type, LogicalType::VARCHAR, false, false);
	}
	if (type == "ROWID" || type == "UROWID") {
		return Supported(OracleTypeCategory::ROW_ID, type, LogicalType::VARCHAR, false, false);
	}
	if (type == "BOOLEAN") {
		return Unsupported(OracleTypeCategory::BOOLEAN, type,
		                   "Oracle SQL BOOLEAN support is not wired to a DuckDB bool conversion yet");
	}
	if (type == "NUMBER") {
		return Supported(OracleTypeCategory::NUMERIC, type,
		                 NumberLogicalType(metadata.precision, metadata.scale, metadata.has_scale, false), false, true);
	}
	if (type == "FLOAT" || type == "BINARY_FLOAT" || type == "BINARY_DOUBLE") {
		return Supported(OracleTypeCategory::FLOATING, type, LogicalType::DOUBLE, false, true);
	}
	if (IsTimestampTZType(type)) {
		return Supported(OracleTypeCategory::TIMESTAMP_TZ, type, LogicalType::TIMESTAMP, false, false);
	}
	if (IsTimestampLTZType(type)) {
		return Supported(OracleTypeCategory::TIMESTAMP_LTZ, type, LogicalType::TIMESTAMP, false, false);
	}
	if (type == "DATE" || IsTimestampType(type)) {
		return Supported(OracleTypeCategory::TEMPORAL, type, LogicalType::TIMESTAMP, false, true);
	}
	if (IsIntervalType(type)) {
		return Supported(OracleTypeCategory::INTERVAL, type, LogicalType::VARCHAR, false, false);
	}
	if (IsCharType(type)) {
		return Supported(OracleTypeCategory::STANDARD, type, LogicalType::VARCHAR, false, true);
	}
	if (IsObjectTypeOwner(metadata)) {
		return Unsupported(OracleTypeCategory::OBJECT, type, "Oracle object/user-defined types are not supported");
	}
	return Unsupported(OracleTypeCategory::UNKNOWN, type, "no registry entry exists for this Oracle datatype");
}

OracleTypeDecision OracleTypeRegistry::ResolveOciDescribe(const OracleTypeMetadata &metadata,
                                                          const OracleSettings &settings) {
	if (!metadata.has_oci_type) {
		return ResolveMetadata(metadata, settings);
	}
	switch (metadata.oci_type) {
	case SQLT_CHR:
	case SQLT_AFC:
	case SQLT_VCS:
	case SQLT_AVC:
		return Supported(OracleTypeCategory::STANDARD, "OCI CHARACTER", LogicalType::VARCHAR, false, true);
	case SQLT_NUM:
	case SQLT_VNU:
		return Supported(OracleTypeCategory::NUMERIC, "OCI NUMBER",
		                 NumberLogicalType(metadata.precision, metadata.scale, metadata.has_scale, true), false, true);
	case SQLT_INT:
	case SQLT_UIN:
		return Supported(OracleTypeCategory::NUMERIC, "OCI INTEGER", LogicalType::BIGINT, false, true);
	case SQLT_FLT:
	case SQLT_BFLOAT:
	case SQLT_BDOUBLE:
	case SQLT_IBFLOAT:
	case SQLT_IBDOUBLE:
		return Supported(OracleTypeCategory::FLOATING, "OCI FLOAT", LogicalType::DOUBLE, false, true);
	case SQLT_DAT:
	case SQLT_ODT:
	case SQLT_TIMESTAMP:
		return Supported(OracleTypeCategory::TEMPORAL, "OCI TIMESTAMP", LogicalType::TIMESTAMP, false, true);
	case SQLT_TIMESTAMP_TZ:
		return Supported(OracleTypeCategory::TIMESTAMP_TZ, "OCI TIMESTAMP WITH TIME ZONE", LogicalType::TIMESTAMP,
		                 false, false);
	case SQLT_TIMESTAMP_LTZ:
		return Supported(OracleTypeCategory::TIMESTAMP_LTZ, "OCI TIMESTAMP WITH LOCAL TIME ZONE",
		                 LogicalType::TIMESTAMP, false, false);
	case SQLT_INTERVAL_YM:
	case SQLT_INTERVAL_DS:
		return Supported(OracleTypeCategory::INTERVAL, "OCI INTERVAL", LogicalType::VARCHAR, false, false);
	case SQLT_CLOB:
		return Supported(OracleTypeCategory::LOB_CLOB, "OCI CLOB", LogicalType::VARCHAR, false, false);
	case SQLT_BLOB:
	case SQLT_BIN:
		return Supported(OracleTypeCategory::LOB_BLOB, "OCI BLOB", LogicalType::BLOB, false, false);
	case SQLT_LBI:
		return Supported(OracleTypeCategory::LONG_RAW, "OCI LONG RAW", LogicalType::BLOB, false, false);
	case SQLT_LNG:
	case SQLT_LVC:
		return Supported(OracleTypeCategory::LONG_TEXT, "OCI LONG", LogicalType::VARCHAR, false, false);
	case SQLT_RDD:
		return Supported(OracleTypeCategory::ROW_ID, "OCI ROWID", LogicalType::VARCHAR, false, false);
	case SQLT_BFILE:
		return Unsupported(OracleTypeCategory::BFILE, "OCI BFILE",
		                   "BFILE stores external file locators; reading file contents is not implemented");
	case SQLT_JSON:
		return Supported(OracleTypeCategory::JSON, "OCI JSON", LogicalType::JSON(), true, false);
	case SQLT_VEC: {
		auto logical = settings.vector_to_list ? LogicalType::LIST(LogicalType::FLOAT) : LogicalType::VARCHAR;
		return Supported(OracleTypeCategory::VECTOR, "OCI VECTOR", logical, true, false);
	}
	case SQLT_NTY:
	case SQLT_REF:
		if (!metadata.type_name.empty() || !metadata.oracle_data_type.empty()) {
			return ResolveMetadata(metadata, settings);
		}
		return Unsupported(OracleTypeCategory::OBJECT, "OCI OBJECT",
		                   "Oracle object/user-defined query results are not supported");
	case SQLT_BOL:
		return Unsupported(OracleTypeCategory::BOOLEAN, "OCI BOOLEAN",
		                   "Oracle SQL BOOLEAN support is not wired to a DuckDB bool conversion yet");
	default:
		return Unsupported(OracleTypeCategory::UNKNOWN,
		                   StringUtil::Format("OCI type %u", static_cast<unsigned>(metadata.oci_type)),
		                   "no registry entry exists for this OCI datatype");
	}
}

OracleTypeDecision OracleTypeRegistry::ResolveWrite(const OracleTypeMetadata &metadata, const LogicalType &duckdb_type,
                                                    const OracleSettings &settings) {
	auto decision = ResolveMetadata(metadata, settings);
	if (!decision.supported) {
		return decision;
	}
	decision.write_bind_type = BindTypeForDuckDBType(duckdb_type);
	return decision;
}

void OracleTypeRegistry::ValidateSupported(const OracleTypeDecision &decision, const OracleTypeMetadata &metadata) {
	if (!decision.supported) {
		throw InvalidInputException("%s", decision.UnsupportedError(metadata).c_str());
	}
}

} // namespace duckdb
