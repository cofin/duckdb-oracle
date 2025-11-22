#include "oracle_table_entry.hpp"
#include "oracle_table_function.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/parser/parsed_data/create_table_info.hpp"
#include "duckdb/parser/constraints/list.hpp"
#include "duckdb/parser/column_definition.hpp"
#include "duckdb/storage/table_storage_info.hpp"

namespace duckdb {

static LogicalType MapOracleColumn(const string &data_type, idx_t precision, idx_t scale, idx_t char_len) {
	auto upper = StringUtil::Upper(data_type);
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

static vector<ColumnDefinition> LoadColumns(OracleCatalogState &state, const string &schema, const string &table) {
	auto query = StringUtil::Format("SELECT column_name, data_type, data_length, data_precision, data_scale, nullable "
	                                "FROM all_tab_columns WHERE owner = UPPER(%s) AND table_name = UPPER(%s) "
	                                "ORDER BY column_id",
	                                Value(schema).ToSQLString().c_str(), Value(table).ToSQLString().c_str());
	auto result = state.EnsureConnection().Query(query);
	vector<ColumnDefinition> columns;
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

		auto logical = MapOracleColumn(data_type, precision, scale, data_len);
		ColumnDefinition col_def(col_name, logical);
		columns.push_back(std::move(col_def));
	}
	return columns;
}

OracleTableEntry::OracleTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, unique_ptr<CreateTableInfo> info,
                                   shared_ptr<OracleCatalogState> state, const string &schema_name,
                                   const string &table_name)
    : TableCatalogEntry(catalog, schema, *info), state(std::move(state)), schema_name(schema_name),
      table_name(table_name) {
	// info consumed by base; nothing else to store
}

unique_ptr<OracleTableEntry> OracleTableEntry::Create(Catalog &catalog, SchemaCatalogEntry &schema,
                                                      const string &schema_name, const string &table_name,
                                                      shared_ptr<OracleCatalogState> state) {
	auto info = make_uniq<CreateTableInfo>();
	info->schema = schema_name;
	info->table = table_name;
	auto cols = LoadColumns(*state, schema_name, table_name);
	for (auto &col : cols) {
		info->columns.AddColumn(col.Copy());
	}
	info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	return make_uniq<OracleTableEntry>(catalog, schema, std::move(info), std::move(state), schema_name, table_name);
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
	auto query = StringUtil::Format("SELECT * FROM %s.%s", quoted_schema.c_str(), quoted_table.c_str());

	auto bind = make_uniq<OracleBindData>();
	bind->column_names = names;
	bind_data =
	    OracleBindInternal(context, state->connection_string, query, return_types, names, bind.release(), state.get());

	TableFunction tf({}, OracleQueryFunction, nullptr, nullptr, nullptr);
	tf.filter_pushdown = true;
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
