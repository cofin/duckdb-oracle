#include "oracle_table_entry.hpp"
#include "oracle_pushdown.hpp"
#include "oracle_table_function.hpp"
#include "oracle_transaction.hpp"
#include "oracle_type_conversion.hpp"
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

namespace {

static bool IsLargeOracleCategory(OracleTypeCategory category) {
	switch (category) {
	case OracleTypeCategory::JSON:
	case OracleTypeCategory::VECTOR:
	case OracleTypeCategory::SPATIAL:
	case OracleTypeCategory::XML:
	case OracleTypeCategory::LOB_CLOB:
	case OracleTypeCategory::LOB_BLOB:
	case OracleTypeCategory::LONG_TEXT:
	case OracleTypeCategory::LONG_RAW:
		return true;
	default:
		return false;
	}
}

} // namespace

//! Query ALL_SDO_GEOM_METADATA for SRID values of spatial columns in a table
//! Returns a map of uppercase column_name -> SRID (0 if not found or query fails)
static unordered_map<string, idx_t> LoadSpatialSRIDs(OracleCatalogState &state, const string &schema,
                                                     const string &table) {
	unordered_map<string, idx_t> srid_map;
	try {
		auto query = "SELECT COLUMN_NAME, SRID FROM ALL_SDO_GEOM_METADATA "
		             "WHERE (OWNER = :1 OR OWNER = UPPER(:2)) "
		             "AND (TABLE_NAME = :3 OR TABLE_NAME = UPPER(:4))";
		auto result = state.ExecuteMetadataQueryWithStringBinds(query, {schema, schema, table, table});
		for (auto &row : result.rows) {
			if (row.size() < 2) {
				continue;
			}
			auto col_name = StringUtil::Upper(row[0]);
			idx_t srid = 0;
			if (!row[1].empty()) {
				TryParseOracleMetadataIndex(row[1], srid);
			}
			srid_map[col_name] = srid;
		}
	} catch (...) {
	}
	return srid_map;
}

static unordered_map<string, string> LoadVectorColumnTypes(OracleCatalogState &state, const string &schema,
                                                           const string &table) {
	unordered_map<string, string> vec_map;
	try {
		auto query = "SELECT column_name, vector_dimension_count, vector_data_format "
		             "FROM all_vec_columns "
		             "WHERE (owner = :1 OR owner = UPPER(:2)) "
		             "AND (table_name = :3 OR table_name = UPPER(:4))";
		auto result = state.ExecuteMetadataQueryWithStringBinds(query, {schema, schema, table, table});
		for (auto &row : result.rows) {
			if (row.size() < 3 || row[0].empty()) {
				continue;
			}
			auto col_name = StringUtil::Upper(row[0]);
			auto dim_str = row[1];
			StringUtil::Trim(dim_str);
			auto fmt_str = StringUtil::Upper(row[2]);
			StringUtil::Trim(fmt_str);
			if (!dim_str.empty() || !fmt_str.empty()) {
				vec_map[col_name] = StringUtil::Format("VECTOR(%s, %s)", dim_str.empty() ? "*" : dim_str.c_str(),
				                                       fmt_str.empty() ? "*" : fmt_str.c_str());
			}
		}
	} catch (...) {
	}
	return vec_map;
}

static void LoadColumns(OracleCatalogState &state, string &schema, string &table, vector<ColumnDefinition> &columns,
                        vector<OracleColumnMetadata> &metadata) {
	auto query = "SELECT owner, table_name, column_name, data_type, data_length, data_precision, data_scale, nullable, "
	             "data_type_owner, char_used, char_length "
	             "FROM all_tab_columns "
	             "WHERE (owner = :1 OR owner = UPPER(:2)) "
	             "AND (table_name = :3 OR table_name = UPPER(:4)) "
	             "ORDER BY CASE WHEN owner = :5 THEN 0 WHEN owner = UPPER(:6) THEN 1 ELSE 2 END, "
	             "CASE WHEN table_name = :7 THEN 0 WHEN table_name = UPPER(:8) THEN 1 ELSE 2 END, "
	             "owner, table_name, column_id";
	auto result =
	    state.ExecuteMetadataQueryWithStringBinds(query, {schema, schema, table, table, schema, schema, table, table});
	if (result.rows.empty()) {
		return;
	}

	const string best_owner = result.rows[0][0];
	const string best_table = result.rows[0][1];
	schema = best_owner;
	table = best_table;

	bool has_spatial = false;
	bool has_vector = false;
	for (auto &row : result.rows) {
		if (row.size() >= 4 && row[0] == best_owner && row[1] == best_table) {
			auto upper = StringUtil::Upper(row[3]);
			if (upper == "SDO_GEOMETRY" || upper == "MDSYS.SDO_GEOMETRY") {
				has_spatial = true;
			} else if (upper == "VECTOR" || StringUtil::StartsWith(upper, "VECTOR(")) {
				has_vector = true;
			}
		}
	}

	unordered_map<string, idx_t> srid_map;
	if (has_spatial) {
		srid_map = LoadSpatialSRIDs(state, best_owner, best_table);
	}
	unordered_map<string, string> vec_map;
	if (has_vector) {
		vec_map = LoadVectorColumnTypes(state, best_owner, best_table);
	}

	for (auto &row : result.rows) {
		if (row.size() < 11) {
			continue;
		}
		if (row[0] != best_owner || row[1] != best_table) {
			continue;
		}
		const auto &col_name = row[2];
		auto data_type = row[3];
		auto upper_col = StringUtil::Upper(col_name);
		if (StringUtil::Upper(data_type) == "VECTOR") {
			auto vec_it = vec_map.find(upper_col);
			if (vec_it != vec_map.end()) {
				data_type = vec_it->second;
			}
		}

		idx_t data_len = 0;
		TryParseOracleMetadataIndex(row[4], data_len);
		idx_t precision = 0;
		TryParseOracleMetadataIndex(row[5], precision);
		int32_t scale = 0;
		bool has_scale = TryParseOracleMetadataInt(row[6], scale);
		idx_t char_len = 0;
		TryParseOracleMetadataIndex(row[10], char_len);

		OracleTypeMetadata type_metadata;
		type_metadata.schema_name = best_owner;
		type_metadata.table_name = best_table;
		type_metadata.column_name = col_name;
		type_metadata.oracle_data_type = data_type;
		type_metadata.data_length = data_len;
		type_metadata.precision = precision;
		type_metadata.scale = scale;
		type_metadata.has_scale = has_scale;
		type_metadata.type_owner = row[8];
		type_metadata.char_used = row[9] == "C";
		type_metadata.char_length = char_len;

		auto decision = OracleTypeRegistry::ResolveMetadata(type_metadata, state.settings);
		OracleTypeRegistry::ValidateSupported(decision, type_metadata);
		type_metadata.srid = 0;
		if (decision.category == OracleTypeCategory::SPATIAL) {
			auto it = srid_map.find(upper_col);
			if (it != srid_map.end()) {
				type_metadata.srid = it->second;
				decision = OracleTypeRegistry::ResolveMetadata(type_metadata, state.settings);
				OracleTypeRegistry::ValidateSupported(decision, type_metadata);
			}
		}

		OracleColumnMetadata meta;
		meta.column_name = col_name;
		meta.oracle_data_type = data_type;
		meta.schema_name = best_owner;
		meta.table_name = best_table;
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
}

unique_ptr<OracleTableEntry> OracleTableEntry::Create(Catalog &catalog, SchemaCatalogEntry &schema,
                                                      const string &schema_name, const string &table_name,
                                                      shared_ptr<OracleCatalogState> state,
                                                      const string &duckdb_entry_name) {
	string resolved_schema = schema_name;
	string resolved_table = table_name;
	vector<ColumnDefinition> cols;
	vector<OracleColumnMetadata> metadata;
	LoadColumns(*state, resolved_schema, resolved_table, cols, metadata);
	if (cols.empty()) {
		bool found = false;
		auto syn = state->ResolveSynonym(schema_name, table_name, found);
		if (found) {
			resolved_schema = syn.first;
			resolved_table = syn.second;
			LoadColumns(*state, resolved_schema, resolved_table, cols, metadata);
		}
	}
	if (cols.empty()) {
		return nullptr;
	}

	auto info = make_uniq<CreateTableInfo>();
	info->schema = schema.name;
	info->table = duckdb_entry_name.empty() ? resolved_table : duckdb_entry_name;
	for (auto &col : cols) {
		info->columns.AddColumn(col.Copy());
	}
	info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	return make_uniq<OracleTableEntry>(catalog, schema, std::move(info), std::move(state), resolved_schema,
	                                   resolved_table, std::move(metadata));
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

	const auto version_info = state->GetVersionInfo();

	string column_list;
	vector<string> select_exprs;
	idx_t col_idx = 0;
	for (auto &col : columns.Physical()) {
		if (col_idx > 0) {
			column_list += ", ";
		}

		auto quoted_col = KeywordHelper::WriteQuoted(col.Name(), '"');
		string expr = quoted_col;

		if (col_idx < column_metadata.size()) {
			const auto &meta = column_metadata[col_idx];
			if (meta.RequiresQueryRewrite(version_info)) {
				auto converted = meta.ConversionExpression(quoted_col, version_info);
				expr = StringUtil::Format("%s AS %s", converted.c_str(), quoted_col.c_str());
			}
		}
		column_list += expr;
		select_exprs.push_back(std::move(expr));
		col_idx++;
	}

	auto query =
	    StringUtil::Format("SELECT %s FROM %s.%s", column_list.c_str(), quoted_schema.c_str(), quoted_table.c_str());

	auto bind = make_uniq<OracleBindData>();
	bind->direct_from_sql = StringUtil::Format("%s.%s", quoted_schema.c_str(), quoted_table.c_str());
	bind->direct_select_list_sql = column_list;
	bind_data =
	    OracleBindInternal(context, state->connection_string, query, return_types, names, bind.release(), state.get());
	auto &oracle_bind = bind_data->Cast<OracleBindData>();
	oracle_bind.converted_select_exprs = std::move(select_exprs);

	for (idx_t i = 0; i < column_metadata.size() && i < return_types.size(); i++) {
		return_types[i] = column_metadata[i].decision.duckdb_type;
		if (i < oracle_bind.original_types.size()) {
			oracle_bind.original_types[i] = column_metadata[i].decision.duckdb_type;
		}
		if (i < oracle_bind.oracle_type_names.size() && !column_metadata[i].decision.normalized_type.empty()) {
			oracle_bind.oracle_type_names[i] = column_metadata[i].decision.normalized_type;
		}
		if (i < oracle_bind.pushdown_eligible.size()) {
			oracle_bind.pushdown_eligible[i] = column_metadata[i].decision.pushdown_eligible;
		}
		if (i < oracle_bind.oci_sizes.size() && (IsLargeOracleCategory(column_metadata[i].Category()) ||
		                                         column_metadata[i].RequiresQueryRewrite(version_info))) {
			oracle_bind.oci_sizes[i] = 65535;
		}
	}

	TableFunction tf({}, OracleQueryFunction, nullptr, OracleInitGlobal, nullptr);
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
