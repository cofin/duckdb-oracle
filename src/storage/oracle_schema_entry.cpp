#include "oracle_catalog.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/default/default_schemas.hpp"
#include "duckdb/catalog/similar_catalog_entry.hpp"
#include "duckdb/common/algorithm.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/execution/physical_plan_generator.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/parser/parsed_data/create_schema_info.hpp"
#include "duckdb/parser/parsed_data/create_view_info.hpp"
#include "duckdb/planner/operator/logical_create_table.hpp"
#include "duckdb/planner/operator/logical_delete.hpp"
#include "duckdb/planner/operator/logical_insert.hpp"
#include "duckdb/planner/operator/logical_update.hpp"
#include "duckdb/storage/database_size.hpp"
#include "oracle_catalog_state.hpp"
#include "oracle_connection_resolver.hpp"
#include "oracle_insert.hpp"
#include "oracle_table_entry.hpp"
#include "oracle_utils.hpp"
#include "oracle_write.hpp"
#include <memory>
#include <mutex>

namespace duckdb {

class OracleSchemaEntry : public SchemaCatalogEntry {
public:
	OracleSchemaEntry(Catalog &catalog, CreateSchemaInfo &info, shared_ptr<OracleCatalogState> state_p,
	                  string oracle_schema_p = "")
	    : SchemaCatalogEntry(catalog, info), state(std::move(state_p)), oracle_schema(std::move(oracle_schema_p)) {
		if (oracle_schema.empty()) {
			oracle_schema = info.schema;
		}
	}

	void Scan(ClientContext &context, CatalogType type, const std::function<void(CatalogEntry &)> &callback) override {
		(void)context;
		Scan(type, callback);
	}

	void Scan(CatalogType type, const std::function<void(CatalogEntry &)> &callback) override {
		if (type != CatalogType::TABLE_ENTRY) {
			return;
		}
		vector<reference<CatalogEntry>> entries;
		{
			lock_guard<std::mutex> guard(table_lock);
			SyncCacheGenerationLocked();
			if (!tables_enumerated) {
				auto object_names = state->ListObjects(oracle_schema, state->settings.metadata_object_types);
				for (auto &obj_name : object_names) {
					if (tables.find(obj_name) != tables.end() ||
					    unresolvable_entries.find(obj_name) != unresolvable_entries.end()) {
						continue;
					}
					try {
						auto loaded = LoadTableEntryLocked(obj_name);
						if (!loaded) {
							unresolvable_entries.insert(obj_name);
						}
					} catch (const Exception &) {
						unresolvable_entries.insert(obj_name);
					}
				}
				tables_enumerated = true;
			}
			entries.reserve(tables.size());
			for (auto &entry : tables) {
				entries.push_back(*entry.second);
			}
		}
		sort(entries.begin(), entries.end(), [](const reference<CatalogEntry> &a, const reference<CatalogEntry> &b) {
			return a.get().name < b.get().name;
		});
		for (auto &entry_ref : entries) {
			callback(entry_ref.get());
		}
	}

	optional_ptr<CatalogEntry> LookupEntry(CatalogTransaction transaction,
	                                       const EntryLookupInfo &lookup_info) override {
		(void)transaction;
		if (lookup_info.GetCatalogType() != CatalogType::TABLE_ENTRY) {
			return nullptr;
		}
		lock_guard<std::mutex> guard(table_lock);
		SyncCacheGenerationLocked();
		return LoadTableEntryLocked(lookup_info.GetEntryName());
	}

	SimilarCatalogEntry GetSimilarEntry(CatalogTransaction transaction, const EntryLookupInfo &lookup_info) override {
		(void)transaction;
		SimilarCatalogEntry result;
		if (lookup_info.GetCatalogType() != CatalogType::TABLE_ENTRY) {
			return result;
		}
		const auto &target_name = lookup_info.GetEntryName();
		auto consider_candidate = [&](const string &candidate) {
			auto score = StringUtil::SimilarityRating(candidate, target_name);
			if (score > result.score) {
				result.score = score;
				result.name = candidate;
			}
		};
		try {
			auto object_names = state->ListObjects(oracle_schema, state->settings.metadata_object_types);
			for (auto &obj_name : object_names) {
				consider_candidate(obj_name);
			}
		} catch (...) {
		}
		lock_guard<std::mutex> guard(table_lock);
		for (auto &entry : tables) {
			consider_candidate(entry.second->name);
		}
		return result;
	}

	optional_ptr<CatalogEntry> CreateTable(CatalogTransaction transaction, BoundCreateTableInfo &info) override {
		(void)transaction;
		(void)info;
		throw BinderException("Oracle catalog does not support DDL operations directly; use oracle_execute() instead");
	}

	optional_ptr<CatalogEntry> CreateFunction(CatalogTransaction transaction, CreateFunctionInfo &info) override {
		(void)transaction;
		(void)info;
		throw BinderException("Oracle catalog does not support DDL operations directly; use oracle_execute() instead");
	}

	optional_ptr<CatalogEntry> CreateIndex(CatalogTransaction transaction, CreateIndexInfo &info,
	                                       TableCatalogEntry &table) override {
		(void)transaction;
		(void)info;
		(void)table;
		throw BinderException("Oracle catalog does not support DDL operations directly; use oracle_execute() instead");
	}

	optional_ptr<CatalogEntry> CreateView(CatalogTransaction transaction, CreateViewInfo &info) override {
		(void)transaction;
		(void)info;
		throw BinderException("Oracle catalog does not support DDL operations directly; use oracle_execute() instead");
	}

	optional_ptr<CatalogEntry> CreateSequence(CatalogTransaction transaction, CreateSequenceInfo &info) override {
		(void)transaction;
		(void)info;
		throw BinderException("Oracle catalog does not support DDL operations directly; use oracle_execute() instead");
	}

	optional_ptr<CatalogEntry> CreateTableFunction(CatalogTransaction transaction,
	                                               CreateTableFunctionInfo &info) override {
		(void)transaction;
		(void)info;
		throw BinderException("Oracle catalog does not support DDL operations directly; use oracle_execute() instead");
	}

	optional_ptr<CatalogEntry> CreateCopyFunction(CatalogTransaction transaction,
	                                              CreateCopyFunctionInfo &info) override {
		(void)transaction;
		(void)info;
		throw BinderException("Oracle catalog does not support DDL operations directly; use oracle_execute() instead");
	}

	optional_ptr<CatalogEntry> CreatePragmaFunction(CatalogTransaction transaction,
	                                                CreatePragmaFunctionInfo &info) override {
		(void)transaction;
		(void)info;
		throw BinderException("Oracle catalog does not support DDL operations directly; use oracle_execute() instead");
	}

	optional_ptr<CatalogEntry> CreateCollation(CatalogTransaction transaction, CreateCollationInfo &info) override {
		(void)transaction;
		(void)info;
		throw BinderException("Oracle catalog does not support DDL operations directly; use oracle_execute() instead");
	}

	optional_ptr<CatalogEntry> CreateCoordinateSystem(CatalogTransaction transaction,
	                                                  CreateCoordinateSystemInfo &info) override {
		(void)transaction;
		(void)info;
		throw BinderException("Oracle catalog does not support DDL operations directly; use oracle_execute() instead");
	}

	optional_ptr<CatalogEntry> CreateType(CatalogTransaction transaction, CreateTypeInfo &info) override {
		(void)transaction;
		(void)info;
		throw BinderException("Oracle catalog does not support DDL operations directly; use oracle_execute() instead");
	}

	void Alter(CatalogTransaction transaction, AlterInfo &info) override {
		(void)transaction;
		(void)info;
		throw BinderException("Oracle catalog does not support DDL operations directly; use oracle_execute() instead");
	}

	void DropEntry(ClientContext &context, DropInfo &info) override {
		(void)context;
		(void)info;
		throw BinderException("Oracle catalog does not support DDL operations directly; use oracle_execute() instead");
	}

	shared_ptr<OracleCatalogState> state;
	string oracle_schema;

private:
	void SyncCacheGenerationLocked() {
		auto current_gen = state ? state->GetCacheGeneration() : 0;
		if (cached_generation != current_gen) {
			for (auto &entry : tables) {
				retired_tables.push_back(std::move(entry.second));
			}
			tables.clear();
			unresolvable_entries.clear();
			tables_enumerated = false;
			cached_generation = current_gen;
		}
	}

	optional_ptr<CatalogEntry> LoadTableEntryLocked(const string &entry_name) {
		auto existing = tables.find(entry_name);
		if (existing != tables.end()) {
			return existing->second.get();
		}

		unique_ptr<OracleTableEntry> created;
		string real_owner;
		string real_name;
		if (state->GetObjectName(oracle_schema, entry_name, "'TABLE','VIEW','MATERIALIZED VIEW'", real_owner,
		                         real_name)) {
			auto existing_real = tables.find(real_name);
			if (existing_real != tables.end()) {
				return existing_real->second.get();
			}
			created = OracleTableEntry::Create(catalog, *this, real_owner, real_name, state, real_name);
		} else {
			bool found = false;
			auto resolved = state->ResolveSynonym(oracle_schema, entry_name, found);
			if (found) {
				created = OracleTableEntry::Create(catalog, *this, resolved.first, resolved.second, state, entry_name);
			}
		}

		if (!created) {
			return nullptr;
		}
		auto *ptr = created.get();
		tables[created->name] = std::move(created);
		return ptr;
	}

	mutable std::mutex table_lock;
	case_insensitive_map_t<unique_ptr<OracleTableEntry>> tables;
	vector<unique_ptr<OracleTableEntry>> retired_tables;
	case_insensitive_set_t unresolvable_entries;
	bool tables_enumerated = false;
	uint64_t cached_generation = 0;
};

OracleCatalog::OracleCatalog(AttachedDatabase &db, shared_ptr<OracleCatalogState> state)
    : Catalog(db), state(std::move(state)) {
}

OracleCatalog::~OracleCatalog() = default;

string OracleCatalog::GetCatalogType() {
	return "oracle";
}

bool OracleCatalog::IsDuckCatalog() {
	return false;
}

optional_idx OracleCatalog::GetCatalogVersion(ClientContext &context) {
	(void)context;
	return optional_idx();
}

void OracleCatalog::Initialize(bool load_builtin) {
	(void)load_builtin;
	state->Connect();
	state->DetectCurrentSchema();
	state->DetectOracleVersion();
}

string OracleCatalog::GetDefaultSchema() const {
	if (state && !state->settings.use_current_schema) {
		auto current = state->GetCurrentSchema();
		if (!current.empty()) {
			return current;
		}
	}
	return DEFAULT_SCHEMA;
}

void OracleCatalog::SyncCacheGenerationLocked() {
	auto current_gen = state ? state->GetCacheGeneration() : 0;
	if (cached_generation != current_gen) {
		for (auto &entry : schemas) {
			retired_schemas.push_back(std::move(entry.second));
		}
		schemas.clear();
		schemas_enumerated = false;
		cached_generation = current_gen;
	}
}

optional_ptr<SchemaCatalogEntry> OracleCatalog::GetOrCreateSchemaLocked(const string &entry_name, bool verify_exists) {
	auto existing = schemas.find(entry_name);
	if (existing != schemas.end()) {
		return existing->second.get();
	}

	if ((StringUtil::CIEquals(entry_name, DEFAULT_SCHEMA) || StringUtil::CIEquals(entry_name, "main")) &&
	    state->settings.use_current_schema) {
		string current = state->GetCurrentSchema();
		if (current.empty()) {
			state->DetectCurrentSchema();
			current = state->GetCurrentSchema();
		}
		if (!current.empty()) {
			CreateSchemaInfo info;
			info.schema = DEFAULT_SCHEMA;
			info.internal = false;
			info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
			auto schema_entry = make_uniq<OracleSchemaEntry>(*this, info, state, current);
			auto *ptr = schema_entry.get();
			schemas[info.schema] = std::move(schema_entry);
			return ptr;
		}
	}

	string schema_name = entry_name;
	if (verify_exists) {
		string real_name = state->GetRealSchemaName(entry_name);
		if (real_name.empty()) {
			return nullptr;
		}
		schema_name = std::move(real_name);
		auto existing_real = schemas.find(schema_name);
		if (existing_real != schemas.end()) {
			return existing_real->second.get();
		}
	}

	CreateSchemaInfo info;
	info.schema = schema_name;
	info.internal = false;
	info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	auto schema_entry = make_uniq<OracleSchemaEntry>(*this, info, state, schema_name);
	auto *ptr = schema_entry.get();
	schemas[info.schema] = std::move(schema_entry);
	return ptr;
}

optional_ptr<SchemaCatalogEntry> OracleCatalog::LookupSchema(CatalogTransaction transaction,
                                                             const EntryLookupInfo &schema_lookup,
                                                             OnEntryNotFound if_not_found) {
	(void)transaction;
	optional_ptr<SchemaCatalogEntry> result;
	{
		lock_guard<std::mutex> guard(schema_lock);
		SyncCacheGenerationLocked();
		result = GetOrCreateSchemaLocked(schema_lookup.GetEntryName(), true);
	}
	if (!result && if_not_found == OnEntryNotFound::THROW_EXCEPTION) {
		throw CatalogException::MissingEntry(schema_lookup, "");
	}
	return result;
}

void OracleCatalog::ScanSchemas(ClientContext &context, std::function<void(SchemaCatalogEntry &)> callback) {
	(void)context;
	vector<reference<SchemaCatalogEntry>> result;
	{
		lock_guard<std::mutex> guard(schema_lock);
		SyncCacheGenerationLocked();
		if (!schemas_enumerated) {
			auto schema_names = state->ListSchemas();
			for (auto &schema_name : schema_names) {
				GetOrCreateSchemaLocked(schema_name, false);
			}
			schemas_enumerated = true;
		}
		result.reserve(schemas.size());
		for (auto &entry : schemas) {
			result.push_back(*entry.second);
		}
	}
	sort(result.begin(), result.end(),
	     [](const reference<SchemaCatalogEntry> &a, const reference<SchemaCatalogEntry> &b) {
		     return a.get().name < b.get().name;
	     });
	for (auto &schema_ref : result) {
		callback(schema_ref.get());
	}
}

bool OracleCatalog::InMemory() {
	return false;
}

string OracleCatalog::GetDBPath() {
	return state ? RedactOracleConnectionString(state->connection_string) : string();
}

bool OracleCatalog::IsEncrypted() const {
	return false;
}

string OracleCatalog::GetEncryptionCipher() const {
	return string();
}

DatabaseSize OracleCatalog::GetDatabaseSize(ClientContext &context) {
	(void)context;
	return DatabaseSize();
}

vector<MetadataBlockInfo> OracleCatalog::GetMetadataInfo(ClientContext &context) {
	(void)context;
	return {};
}

optional_ptr<CatalogEntry> OracleCatalog::CreateSchema(CatalogTransaction transaction, CreateSchemaInfo &info) {
	(void)transaction;
	(void)info;
	throw BinderException("Oracle catalog does not support CREATE SCHEMA; use oracle_execute() instead");
}

void OracleCatalog::DropSchema(ClientContext &context, DropInfo &info) {
	(void)context;
	(void)info;
	throw BinderException("Oracle catalog does not support DROP SCHEMA; use oracle_execute() instead");
}

PhysicalOperator &OracleCatalog::PlanCreateTableAs(ClientContext &context, PhysicalPlanGenerator &planner,
                                                   LogicalCreateTable &op, PhysicalOperator &plan) {
	(void)context;
	(void)planner;
	(void)op;
	(void)plan;
	throw BinderException(
	    "Oracle catalog does not support CREATE TABLE AS; create the target table in Oracle first and use INSERT INTO");
}

PhysicalOperator &OracleCatalog::PlanInsert(ClientContext &context, PhysicalPlanGenerator &planner, LogicalInsert &op,
                                            optional_ptr<PhysicalOperator> plan) {
	RejectOracleWriteInExplicitTransaction(context);

	auto &oracle_table = op.table.Cast<OracleTableEntry>();
	string insert_schema_name = oracle_table.GetSchemaName();
	string insert_table_name = oracle_table.GetTableName();

	// Get column names and types from the table entry
	vector<string> col_names;
	vector<LogicalType> col_types;
	for (auto &col : op.table.GetColumns().Physical()) {
		col_names.push_back(col.Name());
		col_types.push_back(col.Type());
	}

	// Handle partial column inserts (column_index_map)
	if (!op.column_index_map.empty()) {
		plan = planner.ResolveDefaultsProjection(op, *plan);
	}

	auto resolved = ResolveOracleConnection(context, state->connection_string, state.get());
	auto &insert = planner.Make<PhysicalOracleInsert>(
	    op.types, std::move(insert_schema_name), std::move(insert_table_name), resolved.connection_string,
	    resolved.wallet_path, resolved.settings, std::move(col_names), std::move(col_types), op.estimated_cardinality);
	if (plan) {
		insert.children.push_back(*plan);
	}
	return insert;
}

PhysicalOperator &OracleCatalog::PlanDelete(ClientContext &context, PhysicalPlanGenerator &planner, LogicalDelete &op,
                                            PhysicalOperator &plan) {
	(void)context;
	(void)planner;
	(void)op;
	(void)plan;
	throw NotImplementedException("DELETE is not supported for Oracle attached databases");
}

PhysicalOperator &OracleCatalog::PlanUpdate(ClientContext &context, PhysicalPlanGenerator &planner, LogicalUpdate &op,
                                            PhysicalOperator &plan) {
	(void)context;
	(void)planner;
	(void)op;
	(void)plan;
	throw NotImplementedException("UPDATE is not supported for Oracle attached databases");
}

unique_ptr<Catalog> CreateOracleCatalog(AttachedDatabase &db, shared_ptr<OracleCatalogState> state) {
	return make_uniq<OracleCatalog>(db, std::move(state));
}

} // namespace duckdb
