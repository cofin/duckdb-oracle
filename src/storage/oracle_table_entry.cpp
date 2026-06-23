#include "oracle_table_entry.hpp"
#include "oracle_pushdown.hpp"
#include "oracle_table_function.hpp"
#include "oracle_transaction.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/types/geometry_crs.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/parser/parsed_data/create_table_info.hpp"
#include "duckdb/parser/constraints/list.hpp"
#include "duckdb/parser/column_definition.hpp"
#include "duckdb/storage/table_storage_info.hpp"

namespace duckdb {

//! Query ALL_SDO_GEOM_METADATA for SRID values of spatial columns in a table
//! Returns a map of uppercase column_name -> SRID (0 if not found or query fails)
static unordered_map<string, idx_t> LoadSpatialSRIDs(OracleCatalogState &state, const string &schema,
                                                     const string &table) {
	unordered_map<string, idx_t> srid_map;
	try {
		auto query = StringUtil::Format("SELECT COLUMN_NAME, SRID FROM ALL_SDO_GEOM_METADATA "
		                                "WHERE OWNER = UPPER(%s) AND TABLE_NAME = UPPER(%s)",
		                                Value(schema).ToSQLString().c_str(), Value(table).ToSQLString().c_str());
		auto result = state.Query(query);
		for (auto &row : result.rows) {
			if (row.size() < 2) {
				continue;
			}
			auto col_name = StringUtil::Upper(row[0]);
			idx_t srid = 0;
			if (!row[1].empty()) {
				try {
					srid = static_cast<idx_t>(std::stoll(row[1]));
				} catch (...) {
					srid = 0;
				}
			}
			srid_map[col_name] = srid;
		}
	} catch (...) {
		// Permission denied, view doesn't exist, or other error — no CRS info available
	}
	return srid_map;
}

static void LoadColumns(OracleCatalogState &state, const string &schema, const string &table,
                        vector<ColumnDefinition> &columns, vector<OracleColumnMetadata> &metadata) {
	auto query = "SELECT column_name, data_type, data_length, data_precision, data_scale, nullable, "
	             "data_type_owner, char_used, char_length "
	             "FROM all_tab_columns WHERE owner = :1 AND table_name = :2 "
	             "ORDER BY column_id";
	auto result = state.QueryWithStringBinds(query, {schema, table});

	// Check if any spatial columns exist before querying SRID metadata
	bool has_spatial = false;
	for (auto &row : result.rows) {
		if (row.size() >= 2) {
			auto upper = StringUtil::Upper(row[1]);
			if (upper == "SDO_GEOMETRY" || upper == "MDSYS.SDO_GEOMETRY") {
				has_spatial = true;
				break;
			}
		}
	}

	// Only query SRID metadata if spatial columns exist
	unordered_map<string, idx_t> srid_map;
	if (has_spatial) {
		srid_map = LoadSpatialSRIDs(state, schema, table);
	}

	for (auto &row : result.rows) {
		if (row.size() < 9) {
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
		auto parse_int = [](const string &s) -> int32_t {
			if (s.empty()) {
				return 0;
			}
			try {
				return static_cast<int32_t>(std::stoll(s));
			} catch (...) {
				return 0;
			}
		};
		idx_t data_len = parse_idx(row[2]);
		idx_t precision = parse_idx(row[3]);
		int32_t scale = parse_int(row[4]);

		OracleTypeMetadata type_metadata;
		type_metadata.schema_name = schema;
		type_metadata.table_name = table;
		type_metadata.column_name = col_name;
		type_metadata.oracle_data_type = data_type;
		type_metadata.data_length = data_len;
		type_metadata.precision = precision;
		type_metadata.scale = scale;
		type_metadata.has_scale = !row[4].empty();
		type_metadata.type_owner = row[6];
		type_metadata.char_used = row[7] == "C";
		type_metadata.char_length = parse_idx(row[8]);

		auto decision = OracleTypeRegistry::ResolveMetadata(type_metadata, state.settings);
		OracleTypeRegistry::ValidateSupported(decision, type_metadata);
		type_metadata.srid = 0;
		if (decision.category == OracleTypeCategory::SPATIAL) {
			auto it = srid_map.find(StringUtil::Upper(col_name));
			if (it != srid_map.end()) {
				type_metadata.srid = it->second;
				decision = OracleTypeRegistry::ResolveMetadata(type_metadata, state.settings);
				OracleTypeRegistry::ValidateSupported(decision, type_metadata);
			}
		}

		OracleColumnMetadata meta;
		meta.column_name = col_name;
		meta.oracle_data_type = data_type;
		meta.schema_name = schema;
		meta.table_name = table;
		meta.srid = type_metadata.srid;
		meta.decision = decision;

		ColumnDefinition col_def(col_name, decision.duckdb_type);
		columns.push_back(std::move(col_def));

		metadata.push_back(std::move(meta));
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
				auto converted = meta.ConversionExpression(quoted_col, version_info);
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
