#include "oracle_catalog.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/storage/storage_extension.hpp"
#include "duckdb/transaction/duck_transaction_manager.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "oracle_storage_extension.hpp"
#include "oracle_transaction_manager.hpp"
#include "oracle_secret.hpp"

namespace duckdb {

static unique_ptr<Catalog> OracleAttach(optional_ptr<StorageExtensionInfo> storage_info, ClientContext &context,
                                        AttachedDatabase &db, const string &name, AttachInfo &info,
                                        AttachOptions &options) {
	// For remote Oracle we maintain an in-memory catalog; ensure read-only semantics.
	options.access_mode = AccessMode::READ_ONLY;

	string connection_string;

	// Decision logic: path vs secret
	if (!info.path.empty()) {
		// Existing behavior: use provided connection string
		connection_string = info.path;
	} else {
		// NEW: Retrieve from secret manager
		auto &secret_manager = SecretManager::Get(context);
		auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);

		// Check for named secret in options
		unique_ptr<SecretEntry> secret_entry;
		if (options.options.count("secret")) {
			auto secret_name = options.options["secret"].ToString();
			secret_entry = secret_manager.GetSecretByName(transaction, secret_name);
			if (!secret_entry) {
				throw BinderException("Secret '%s' not found. Create it with: CREATE SECRET %s (TYPE oracle, HOST "
				                      "'localhost', PORT 1521, SERVICE 'XEPDB1', USER 'user', PASSWORD 'pass')",
				                      secret_name, secret_name);
			}
		} else {
			// Use default secret (match by type)
			auto secret_match = secret_manager.LookupSecret(transaction, "", "oracle");
			if (!secret_match.HasMatch()) {
				throw BinderException(
				    "No Oracle secret found. Create one with: CREATE SECRET (TYPE oracle, HOST 'localhost', PORT "
				    "1521, SERVICE 'XEPDB1', USER 'user', PASSWORD 'pass')");
			}
			secret_entry = make_uniq<SecretEntry>(*secret_match.secret_entry);
		}

		// Cast to KeyValueSecret
		auto &base_secret = secret_entry->secret;
		if (base_secret->GetType() != "oracle") {
			throw BinderException("Secret type mismatch. Expected 'oracle', got '%s'", base_secret->GetType());
		}

		auto kv_secret = dynamic_cast<const KeyValueSecret *>(base_secret.get());
		if (!kv_secret) {
			throw BinderException("Oracle secret must be a KeyValueSecret");
		}

		// Build connection string from secret parameters
		connection_string = BuildConnectionStringFromSecret(*kv_secret);
	}

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
