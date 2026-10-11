#pragma once

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/common/case_insensitive_map.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/storage/storage_manager.hpp"
#include "oracle_catalog_state.hpp"
#include <mutex>

namespace duckdb {

class OracleSchemaEntry;

class OracleCatalog : public Catalog {
public:
	OracleCatalog(AttachedDatabase &db, shared_ptr<OracleCatalogState> state);
	~OracleCatalog() override;

	string GetCatalogType() override;
	bool IsDuckCatalog() override;
	optional_idx GetCatalogVersion(ClientContext &context) override;
	void Initialize(bool load_builtin) override;
	string GetDefaultSchema() const override;

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
	optional_ptr<SchemaCatalogEntry> LookupSchema(CatalogTransaction transaction, const EntryLookupInfo &schema_lookup,
	                                              OnEntryNotFound if_not_found) override;
	void ScanSchemas(ClientContext &context, std::function<void(SchemaCatalogEntry &)> callback) override;

	PhysicalOperator &PlanCreateTableAs(ClientContext &context, PhysicalPlanGenerator &planner, LogicalCreateTable &op,
	                                    PhysicalOperator &plan) override;
	PhysicalOperator &PlanInsert(ClientContext &context, PhysicalPlanGenerator &planner, LogicalInsert &op,
	                             optional_ptr<PhysicalOperator> plan) override;
	PhysicalOperator &PlanDelete(ClientContext &context, PhysicalPlanGenerator &planner, LogicalDelete &op,
	                             PhysicalOperator &plan) override;
	PhysicalOperator &PlanUpdate(ClientContext &context, PhysicalPlanGenerator &planner, LogicalUpdate &op,
	                             PhysicalOperator &plan) override;

private:
	void SyncCacheGenerationLocked();
	optional_ptr<SchemaCatalogEntry> GetOrCreateSchemaLocked(const string &entry_name, bool verify_exists);

	shared_ptr<OracleCatalogState> state;
	mutable std::mutex schema_lock;
	case_insensitive_map_t<unique_ptr<OracleSchemaEntry>> schemas;
	vector<unique_ptr<OracleSchemaEntry>> retired_schemas;
	bool schemas_enumerated = false;
	uint64_t cached_generation = 0;
};

//! Factory to construct an Oracle-backed catalog used for ATTACH TYPE ORACLE.
unique_ptr<Catalog> CreateOracleCatalog(AttachedDatabase &db, shared_ptr<OracleCatalogState> state);

} // namespace duckdb
