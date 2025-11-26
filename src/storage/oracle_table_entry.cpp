#include "oracle_table_entry.hpp"
#include "oracle_table_function.hpp"
#include "oracle_transaction.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/parser/parsed_data/create_table_info.hpp"
#include "duckdb/parser/constraints/list.hpp"
#include "duckdb/parser/column_definition.hpp"
#include "duckdb/storage/table_storage_info.hpp"

namespace duckdb {

//! Generate Oracle SQL expression for type conversion based on column category and Oracle version
static string GetConversionExpression(const string &quoted_col, const OracleColumnMetadata &meta,
                                      const OracleVersionInfo &version) {
	switch (meta.category) {
	case OracleTypeCategory::SPATIAL:
		// Convert SDO_GEOMETRY to WKT string using Oracle's built-in function
		return StringUtil::Format("SDO_UTIL.TO_WKTGEOMETRY(%s)", quoted_col.c_str());

	case OracleTypeCategory::VECTOR:
		// VECTOR type (Oracle 23ai+): always use VECTOR_SERIALIZE
		// This handles version detection failures (e.g. no V$INSTANCE access) where TO_CHAR fallback might fail
		return StringUtil::Format("VECTOR_SERIALIZE(%s)", quoted_col.c_str());

	case OracleTypeCategory::JSON:
		// JSON type (Oracle 21c+): serialize to string for reliable fetch
		if (version.supports_json_type) {
			return StringUtil::Format("JSON_SERIALIZE(%s RETURNING VARCHAR2(32767))", quoted_col.c_str());
		}
		// Pre-21c: JSON stored as VARCHAR/CLOB, no conversion needed
		return quoted_col;

	case OracleTypeCategory::XML:
		// XMLTYPE: serialize to CLOB for reliable fetch
		return StringUtil::Format("XMLSERIALIZE(CONTENT %s AS CLOB)", quoted_col.c_str());

	case OracleTypeCategory::LOB_BLOB:
		// BLOB: convert to hex for reliable fetch (avoids OCI buffer alignment issues)
		if (meta.needs_server_conversion) {
			return StringUtil::Format("RAWTOHEX(%s)", quoted_col.c_str());
		}
		return quoted_col;

	case OracleTypeCategory::LOB_CLOB:
		// CLOB: convert to VARCHAR if needed (for very large CLOBs)
		if (meta.needs_server_conversion) {
			return StringUtil::Format("TO_CHAR(%s)", quoted_col.c_str());
		}
		return quoted_col;

	case OracleTypeCategory::RAW:
		// RAW: convert to hex for reliable fetch (avoids OCI buffer alignment issues)
		if (meta.needs_server_conversion) {
			return StringUtil::Format("RAWTOHEX(%s)", quoted_col.c_str());
		}
		return quoted_col;

	case OracleTypeCategory::STANDARD:
	case OracleTypeCategory::NUMERIC:
	case OracleTypeCategory::TEMPORAL:
	case OracleTypeCategory::UNKNOWN:
	default:
		// No conversion needed
		return quoted_col;
	}
}

static LogicalType MapOracleColumn(const string &data_type, idx_t precision, idx_t scale, idx_t char_len,
                                   const OracleSettings &settings) {
	auto upper = StringUtil::Upper(data_type);

	// VECTOR type (Oracle 23ai+) - map to LIST<FLOAT> or VARCHAR based on setting
	if (upper == "VECTOR" || StringUtil::StartsWith(upper, "VECTOR(")) {
		if (settings.vector_to_list) {
			// VECTOR_SERIALIZE returns JSON array "[1.0, 2.0, 3.0]"
			// We parse this in OracleQueryFunction and return as LIST<FLOAT>
			return LogicalType::LIST(LogicalType::FLOAT);
		}
		return LogicalType::VARCHAR;
	}

	// Spatial geometry type detection
	if (upper == "SDO_GEOMETRY" || upper == "MDSYS.SDO_GEOMETRY") {
		if (settings.enable_spatial_types) {
			return LogicalType::USER("geometry");
		}
		// Map to VARCHAR for WKT string representation
		return LogicalType::VARCHAR;
	}

	// JSON type (Oracle 21c+) - always VARCHAR (JSON_SERIALIZE output)
	if (upper == "JSON") {
		return LogicalType::VARCHAR;
	}

	// XML type - always VARCHAR (XMLSERIALIZE output)
	if (upper == "XMLTYPE" || upper == "SYS.XMLTYPE") {
		return LogicalType::VARCHAR;
	}

	if (upper == "NUMBER") {
		if (precision == 0) {
			return LogicalType::DOUBLE;
		}
		if (precision > 38) {
			return LogicalType::DOUBLE;
		}
		auto dec_precision = static_cast<uint8_t>(precision == 0 ? 38 : precision);
		auto dec_scale = static_cast<uint8_t>(scale);
		return LogicalType::DECIMAL(dec_precision, dec_scale);
	}
	if (upper == "FLOAT" || upper == "BINARY_FLOAT" || upper == "BINARY_DOUBLE") {
		return LogicalType::DOUBLE;
	}
	if (upper == "DATE" || upper.find("TIMESTAMP") != string::npos) {
		return LogicalType::TIMESTAMP;
	}
	if (upper.find("CHAR") != string::npos || upper.find("CLOB") != string::npos ||
	    upper.find("VARCHAR") != string::npos || upper.find("NCHAR") != string::npos) {
		return LogicalType::VARCHAR;
	}
	if (upper.find("BLOB") != string::npos || upper.find("RAW") != string::npos ||
	    upper.find("BFILE") != string::npos) {
		return LogicalType::BLOB;
	}
	return LogicalType::VARCHAR;
}

static void LoadColumns(OracleCatalogState &state, const string &schema, const string &table,
                        vector<ColumnDefinition> &columns, vector<OracleColumnMetadata> &metadata) {
	auto query = StringUtil::Format("SELECT column_name, data_type, data_length, data_precision, data_scale, nullable "
	                                "FROM all_tab_columns WHERE owner = UPPER(%s) AND table_name = UPPER(%s) "
	                                "ORDER BY column_id",
	                                Value(schema).ToSQLString().c_str(), Value(table).ToSQLString().c_str());
	auto result = state.Query(query);
	for (auto &row : result.rows) {
		if (row.size() < 6) {
			continue;
		}
		auto col_name = row[0];
		auto data_type = row[1];
		auto parse_idx = [](const string &s) -> idx_t {
			if (s.empty()) {
				return 0;
			}
			try {
				return static_cast<idx_t>(std::stoll(s));
			} catch (...) {
				return 0;
			}
		};
		idx_t data_len = parse_idx(row[2]);
		idx_t precision = parse_idx(row[3]);
		idx_t scale = parse_idx(row[4]);
		auto nullable = row[5] == "Y";

		auto logical = MapOracleColumn(data_type, precision, scale, data_len, state.settings);
		ColumnDefinition col_def(col_name, logical);
		columns.push_back(std::move(col_def));

		// Store original Oracle type metadata
		metadata.emplace_back(col_name, data_type);
	}
}

OracleTableEntry::OracleTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, unique_ptr<CreateTableInfo> info,
                                   shared_ptr<OracleCatalogState> state, const string &schema_name,
                                   const string &table_name, vector<OracleColumnMetadata> metadata)
    : TableCatalogEntry(catalog, schema, *info), state(std::move(state)), schema_name(schema_name),
      table_name(table_name), column_metadata(std::move(metadata)) {
	// info consumed by base; nothing else to store
}

unique_ptr<OracleTableEntry> OracleTableEntry::Create(Catalog &catalog, SchemaCatalogEntry &schema,
                                                      const string &schema_name, const string &table_name,
                                                      shared_ptr<OracleCatalogState> state,
                                                      const string &duckdb_entry_name) {
	auto info = make_uniq<CreateTableInfo>();
	info->schema = schema.name;
	info->table = duckdb_entry_name.empty() ? table_name : duckdb_entry_name;
	vector<ColumnDefinition> cols;
	vector<OracleColumnMetadata> metadata;
	LoadColumns(*state, schema_name, table_name, cols, metadata);
	for (auto &col : cols) {
		info->columns.AddColumn(col.Copy());
	}
	info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	return make_uniq<OracleTableEntry>(catalog, schema, std::move(info), std::move(state), schema_name, table_name,
	                                   std::move(metadata));
}

TableFunction OracleTableEntry::GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) {
	vector<LogicalType> return_types;
	vector<string> names;
	for (auto &col : columns.Physical()) {
		return_types.push_back(col.Type());
		names.push_back(col.Name());
	}

	auto quoted_schema = KeywordHelper::WriteQuoted(schema_name, '"');
	auto quoted_table = KeywordHelper::WriteQuoted(table_name, '"');

	// Get Oracle version info and settings for version-aware type conversions
	const auto &version_info = state->GetVersionInfo();
	const auto &settings = state->settings;

	// Build column list with type conversions for problematic Oracle types
	// This handles: SPATIAL, VECTOR, JSON, XML, LOB, RAW types
	// Controlled by enable_type_conversion setting
	string column_list;
	idx_t col_idx = 0;
	for (auto &col : columns.Physical()) {
		if (col_idx > 0) {
			column_list += ", ";
		}

		auto quoted_col = KeywordHelper::WriteQuoted(col.Name(), '"');

		// Apply type-specific conversion if enabled and needed
		if (settings.enable_type_conversion && col_idx < column_metadata.size()) {
			const auto &meta = column_metadata[col_idx];
			if (meta.RequiresQueryRewrite(version_info, settings.try_native_lobs)) {
				// Generate conversion expression and alias
				auto converted = GetConversionExpression(quoted_col, meta, version_info);
				column_list += StringUtil::Format("%s AS %s", converted.c_str(), quoted_col.c_str());
			} else {
				column_list += quoted_col;
			}
		} else {
			column_list += quoted_col;
		}
		col_idx++;
	}

	auto query =
	    StringUtil::Format("SELECT %s FROM %s.%s", column_list.c_str(), quoted_schema.c_str(), quoted_table.c_str());

	auto bind = make_uniq<OracleBindData>();
	bind_data =
	    OracleBindInternal(context, state->connection_string, query, return_types, names, bind.release(), state.get());

	TableFunction tf({}, OracleQueryFunction, nullptr, OracleInitGlobal, nullptr);
	// We don't implement table_filters, so set filter_pushdown = false
	// This tells DuckDB to apply filters client-side via LogicalFilter operator
	// The pushdown_complex_filter callback handles Oracle-side WHERE clause generation when enabled
	tf.filter_pushdown = false;
	tf.pushdown_complex_filter = OraclePushdownComplexFilter;
	tf.projection_pushdown = true;
	tf.name = table_name;
	return tf;
}

TableStorageInfo OracleTableEntry::GetStorageInfo(ClientContext &) {
	TableStorageInfo info;
	return info;
}

unique_ptr<BaseStatistics> OracleTableEntry::GetStatistics(ClientContext &, column_t) {
	return nullptr;
}

} // namespace duckdb
