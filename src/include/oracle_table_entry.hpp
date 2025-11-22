#pragma once

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "oracle_catalog_state.hpp"

namespace duckdb {

class OracleTableEntry : public TableCatalogEntry {
public:
	OracleTableEntry(Catalog &catalog, SchemaCatalogEntry &schema, unique_ptr<CreateTableInfo> info,
	                 shared_ptr<OracleCatalogState> state, const string &schema_name, const string &table_name);

	TableFunction GetScanFunction(ClientContext &context, unique_ptr<FunctionData> &bind_data) override;
	TableStorageInfo GetStorageInfo(ClientContext &context) override;
	unique_ptr<BaseStatistics> GetStatistics(ClientContext &context, column_t column_id) override;

	static unique_ptr<OracleTableEntry> Create(Catalog &catalog, SchemaCatalogEntry &schema, const string &schema_name,
	                                           const string &table_name, shared_ptr<OracleCatalogState> state);

private:
	shared_ptr<OracleCatalogState> state;
	string schema_name;
	string table_name;
};

} // namespace duckdb
