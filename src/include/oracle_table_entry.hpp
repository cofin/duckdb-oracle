#pragma once

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/common/string_util.hpp"
#include "oracle_catalog_state.hpp"
#include "oracle_type_registry.hpp"

namespace duckdb {

class OracleTableEntry : public TableCatalogEntry {
public:
	OracleTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, unique_ptr<CreateTableInfo> info,
	                 shared_ptr<OracleCatalogState> state, const string &schema_name, const string &table_name,
	                 vector<OracleColumnMetadata> metadata, optional_idx estimated_rows = optional_idx());

	TableFunction GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) override;
	TableStorageInfo GetStorageInfo(ClientContext &context) override;
	unique_ptr<BaseStatistics> GetStatistics(ClientContext &context, column_t column_id) override;
	const ColumnList &GetColumns() const override {
		return columns;
	}

	const string &GetSchemaName() const {
		return schema_name;
	}
	const string &GetTableName() const {
		return table_name;
	}

	static unique_ptr<OracleTableEntry> Create(Catalog &catalog, SchemaCatalogEntry &schema, const string &schema_name,
	                                           const string &table_name, shared_ptr<OracleCatalogState> state,
	                                           const string &duckdb_entry_name = "");

private:
	ColumnList columns;
	shared_ptr<OracleCatalogState> state;
	string schema_name;
	string table_name;
	vector<OracleColumnMetadata> column_metadata;
	optional_idx estimated_rows;
};

} // namespace duckdb
