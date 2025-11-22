#include "oracle_catalog.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/storage/storage_extension.hpp"
#include "duckdb/transaction/duck_transaction_manager.hpp"
#include "duckdb/main/config.hpp"
#include "oracle_storage_extension.hpp"
#include "oracle_transaction_manager.hpp"

namespace duckdb {

static unique_ptr<Catalog> OracleAttach(optional_ptr<StorageExtensionInfo> storage_info, ClientContext &context,
                                        AttachedDatabase &db, const string &name, AttachInfo &info,
                                        AttachOptions &options) {
	// For remote Oracle we maintain an in-memory catalog; ensure read-only semantics.
	options.access_mode = AccessMode::READ_ONLY;

	string connection_string = info.path;
	// Use in-memory storage underneath the DuckDB catalog
	info.path = ":memory:";

	auto *oracle_info = storage_info ? dynamic_cast<duckdb::OracleStorageInfo *>(storage_info.get()) : nullptr;
	shared_ptr<OracleCatalogState> state;
	if (oracle_info && oracle_info->state) {
		state = oracle_info->state;
	} else {
		state = make_shared_ptr<OracleCatalogState>(connection_string);
		OracleCatalogState::Register(state);
		if (oracle_info) {
			oracle_info->state = state;
		}
	}
	// Map attach options to state settings (best-effort, ignore unknown keys).
	state->ApplyOptions(options.options);
	return CreateOracleCatalog(db, state);
}

static unique_ptr<TransactionManager> OracleCreateTransactionManager(optional_ptr<StorageExtensionInfo> storage_info,
                                                                     AttachedDatabase &db, Catalog &catalog) {
	shared_ptr<OracleCatalogState> state;
	if (storage_info) {
		auto *info = dynamic_cast<duckdb::OracleStorageInfo *>(storage_info.get());
		if (info) {
			state = info->state;
		}
	}
	if (!state) {
		// fallback: no state available, create empty manager
		return make_uniq<DuckTransactionManager>(db);
	}
	return make_uniq<OracleTransactionManager>(db, state);
}

unique_ptr<StorageExtension> CreateOracleStorageExtension() {
	auto ext = make_uniq<StorageExtension>();
	ext->attach = OracleAttach;
	ext->create_transaction_manager = OracleCreateTransactionManager;
	ext->storage_info = make_shared_ptr<duckdb::OracleStorageInfo>();
	return ext;
}

} // namespace duckdb
