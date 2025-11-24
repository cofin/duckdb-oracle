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
#include "oracle_catalog_state.hpp"
#include <memory>
#include "oracle_table_entry.hpp"

namespace duckdb {

class OracleTableGenerator : public DefaultGenerator {
public:
	OracleTableGenerator(Catalog &catalog, SchemaCatalogEntry &schema, shared_ptr<OracleCatalogState> state)
	    : DefaultGenerator(catalog), schema(schema), state(std::move(state)) {
	}

	vector<string> GetDefaultEntries() override {
		// Use ListObjects with metadata_object_types setting
		return state->ListObjects(schema.name, state->settings.metadata_object_types);
	}

	unique_ptr<CatalogEntry> CreateDefaultEntry(ClientContext &context, const string &entry_name) override {
		// Try on-demand loading if not in enumerated list (handles objects beyond limit)
		string real_name = state->GetObjectName(schema.name, entry_name, "'TABLE','VIEW','MATERIALIZED VIEW'");
		if (!real_name.empty()) {
			return OracleTableEntry::Create(catalog, schema, schema.name, real_name, state);
		}

		// Try synonym resolution as fallback
		bool found = false;
		auto resolved = state->ResolveSynonym(schema.name, entry_name, found);
		if (found) {
			return OracleTableEntry::Create(catalog, schema, resolved.first, resolved.second, state);
		}

		return nullptr;
	}

private:
	SchemaCatalogEntry &schema;
	shared_ptr<OracleCatalogState> state;
};

class OracleSchemaEntry : public DuckSchemaEntry {
public:
	OracleSchemaEntry(Catalog &catalog, CreateSchemaInfo &info, shared_ptr<OracleCatalogState> state)
	    : DuckSchemaEntry(catalog, info) {
		auto &table_set = GetCatalogSet(CatalogType::TABLE_ENTRY);
		table_set.SetDefaultGenerator(make_uniq<OracleTableGenerator>(catalog, *this, std::move(state)));
	}
};

class OracleSchemaGenerator : public DefaultGenerator {
public:
	OracleSchemaGenerator(Catalog &catalog, shared_ptr<OracleCatalogState> state)
	    : DefaultGenerator(catalog), state(std::move(state)) {
	}

	vector<string> GetDefaultEntries() override {
		return state->ListSchemas();
	}

	unique_ptr<CatalogEntry> CreateDefaultEntry(ClientContext &context, const string &entry_name) override {
		CreateSchemaInfo info;
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

class OracleCatalog : public DuckCatalog {
public:
	OracleCatalog(AttachedDatabase &db, shared_ptr<OracleCatalogState> state)
	    : DuckCatalog(db), state(std::move(state)) {
	}

	string GetCatalogType() override {
		return "oracle";
	}

	bool IsDuckCatalog() override {
		return false;
	}

	optional_idx GetCatalogVersion(ClientContext &context) override {
		return optional_idx();
	}

	void Initialize(bool load_builtin) override {
		// Attempt connection early to fail fast
		state->Connect();

		// Detect current schema after successful connection
		state->DetectCurrentSchema();

		DuckCatalog::Initialize(false);
		GetSchemaCatalogSet().SetDefaultGenerator(make_uniq<OracleSchemaGenerator>(*this, state));
	}

	shared_ptr<OracleCatalogState> GetState() {
		return state;
	}

private:
	shared_ptr<OracleCatalogState> state;
};

unique_ptr<Catalog> CreateOracleCatalog(AttachedDatabase &db, shared_ptr<OracleCatalogState> state) {
	auto catalog = make_uniq<OracleCatalog>(db, std::move(state));
	catalog->Initialize(false);
	return std::move(catalog);
}

} // namespace duckdb
