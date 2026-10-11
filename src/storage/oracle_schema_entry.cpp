#include "oracle_catalog.hpp"
#include "duckdb/catalog/catalog_entry/duck_schema_entry.hpp"
#include "duckdb/catalog/catalog_entry/view_catalog_entry.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/default/default_generator.hpp"
#include "duckdb/catalog/default/default_schemas.hpp"
#include "duckdb/parser/parsed_data/create_schema_info.hpp"
#include "duckdb/parser/parsed_data/create_view_info.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/transaction/duck_transaction_manager.hpp"
#include "duckdb/catalog/duck_catalog.hpp"
#include "duckdb/planner/operator/logical_create_table.hpp"
#include "duckdb/planner/operator/logical_insert.hpp"
#include "duckdb/planner/operator/logical_delete.hpp"
#include "duckdb/planner/operator/logical_update.hpp"
#include "duckdb/execution/physical_plan_generator.hpp"
#include "oracle_catalog_state.hpp"
#include "oracle_insert.hpp"
#include "oracle_write.hpp"
#include "oracle_connection_resolver.hpp"
#include "oracle_utils.hpp"
#include <memory>
#include "oracle_table_entry.hpp"

namespace duckdb {

// Forward declaration
class OracleTableGenerator;

class OracleSchemaEntry : public DuckSchemaEntry {
public:
	OracleSchemaEntry(Catalog &catalog, CreateSchemaInfo &info, shared_ptr<OracleCatalogState> state,
	                  string oracle_schema_p = "");

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

	string oracle_schema;
};

class OracleTableGenerator : public DefaultGenerator {
public:
	OracleTableGenerator(Catalog &catalog, SchemaCatalogEntry &schema, shared_ptr<OracleCatalogState> state)
	    : DefaultGenerator(catalog), schema(schema), state(std::move(state)) {
	}

	vector<string> GetDefaultEntries() override {
		auto &ora_schema = schema.Cast<OracleSchemaEntry>();
		// Use ListObjects with metadata_object_types setting
		return state->ListObjects(ora_schema.oracle_schema, state->settings.metadata_object_types);
	}

	unique_ptr<CatalogEntry> CreateDefaultEntry(ClientContext &context, const string &entry_name) override {
		(void)context;
		auto &ora_schema = schema.Cast<OracleSchemaEntry>();
		string schema_name = ora_schema.oracle_schema;

		// Try on-demand loading if not in enumerated list (handles objects beyond limit)
		string real_owner;
		string real_name;
		if (state->GetObjectName(schema_name, entry_name, "'TABLE','VIEW','MATERIALIZED VIEW'", real_owner,
		                         real_name)) {
			return OracleTableEntry::Create(catalog, schema, real_owner, real_name, state, entry_name);
		}

		// Try synonym resolution as fallback
		bool found = false;
		auto resolved = state->ResolveSynonym(schema_name, entry_name, found);
		if (found) {
			return OracleTableEntry::Create(catalog, schema, resolved.first, resolved.second, state, entry_name);
		}

		return nullptr;
	}

private:
	SchemaCatalogEntry &schema;
	shared_ptr<OracleCatalogState> state;
};

OracleSchemaEntry::OracleSchemaEntry(Catalog &catalog, CreateSchemaInfo &info, shared_ptr<OracleCatalogState> state,
                                     string oracle_schema_p)
    : DuckSchemaEntry(catalog, info), oracle_schema(std::move(oracle_schema_p)) {
	if (oracle_schema.empty()) {
		oracle_schema = info.schema;
	}
	auto &table_set = GetCatalogSet(CatalogType::TABLE_ENTRY);
	table_set.SetDefaultGenerator(make_uniq<OracleTableGenerator>(catalog, *this, std::move(state)));
}

class OracleSchemaGenerator : public DefaultGenerator {
public:
	OracleSchemaGenerator(Catalog &catalog, shared_ptr<OracleCatalogState> state)
	    : DefaultGenerator(catalog), state(std::move(state)) {
	}

	vector<string> GetDefaultEntries() override {
		return state->ListSchemas();
	}

	unique_ptr<CatalogEntry> CreateDefaultEntry(ClientContext &context, const string &entry_name) override {
		(void)context;
		CreateSchemaInfo info;
		string oracle_schema;

		// Handle default "main" schema mapping
		if ((entry_name == DEFAULT_SCHEMA || entry_name == "main") && state->settings.use_current_schema) {
			string current = state->GetCurrentSchema();
			if (current.empty()) {
				state->DetectCurrentSchema();
				current = state->GetCurrentSchema();
			}

			if (!current.empty()) {
				info.schema = entry_name; // Keep catalog name as "main"
				oracle_schema = current;  // Map to current schema (e.g., "HR")
				info.internal = true;
				info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
				return make_uniq<OracleSchemaEntry>(catalog, info, state, oracle_schema);
			}
		}

		string real_name = state->GetRealSchemaName(entry_name);
		if (!real_name.empty()) {
			info.schema = real_name;
		} else {
			info.schema = entry_name;
		}
		info.internal = true;
		info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;

		return make_uniq<OracleSchemaEntry>(catalog, info, state);
	}

private:
	shared_ptr<OracleCatalogState> state;
};

OracleCatalog::OracleCatalog(AttachedDatabase &db, shared_ptr<OracleCatalogState> state)
    : DuckCatalog(db), state(std::move(state)) {
}

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
	// Attempt connection early to fail fast
	state->Connect();

	// Detect current schema and Oracle version after successful connection
	state->DetectCurrentSchema();
	state->DetectOracleVersion();

	GetSchemaCatalogSet().SetDefaultGenerator(make_uniq<OracleSchemaGenerator>(*this, state));
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
