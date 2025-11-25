#pragma once

#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "oracle_catalog_state.hpp"

namespace duckdb {

//! Metadata about Oracle column types
struct OracleColumnMetadata {
	string column_name;
	string oracle_data_type;
	bool is_spatial;

	OracleColumnMetadata(const string &name, const string &data_type)
	    : column_name(name), oracle_data_type(data_type),
	      is_spatial(data_type == "SDO_GEOMETRY" || data_type == "MDSYS.SDO_GEOMETRY") {
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
