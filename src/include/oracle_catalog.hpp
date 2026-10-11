#pragma once

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/duck_catalog.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/storage/storage_manager.hpp"
#include "oracle_catalog_state.hpp"

namespace duckdb {

class OracleCatalog : public DuckCatalog {
public:
	OracleCatalog(AttachedDatabase &db, shared_ptr<OracleCatalogState> state);

	string GetCatalogType() override;
	bool IsDuckCatalog() override;
	optional_idx GetCatalogVersion(ClientContext &context) override;
	void Initialize(bool load_builtin) override;

	shared_ptr<OracleCatalogState> GetState() const {
		return state;
	}

	bool InMemory() override;
	string GetDBPath() override;
	bool IsEncrypted() const override;
	string GetEncryptionCipher() const override;
	DatabaseSize GetDatabaseSize(ClientContext &context) override;
	vector<MetadataBlockInfo> GetMetadataInfo(ClientContext &context) override;

	optional_ptr<CatalogEntry> CreateSchema(CatalogTransaction transaction, CreateSchemaInfo &info) override;
	void DropSchema(ClientContext &context, DropInfo &info) override;

	PhysicalOperator &PlanCreateTableAs(ClientContext &context, PhysicalPlanGenerator &planner, LogicalCreateTable &op,
	                                    PhysicalOperator &plan) override;
	PhysicalOperator &PlanInsert(ClientContext &context, PhysicalPlanGenerator &planner, LogicalInsert &op,
	                             optional_ptr<PhysicalOperator> plan) override;
	PhysicalOperator &PlanDelete(ClientContext &context, PhysicalPlanGenerator &planner, LogicalDelete &op,
	                             PhysicalOperator &plan) override;
	PhysicalOperator &PlanUpdate(ClientContext &context, PhysicalPlanGenerator &planner, LogicalUpdate &op,
	                             PhysicalOperator &plan) override;

private:
	shared_ptr<OracleCatalogState> state;
};

//! Factory to construct an Oracle-backed catalog used for ATTACH TYPE ORACLE.
unique_ptr<Catalog> CreateOracleCatalog(AttachedDatabase &db, shared_ptr<OracleCatalogState> state);

} // namespace duckdb
