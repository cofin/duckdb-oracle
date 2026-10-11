#include "oracle_catalog.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/storage/storage_extension.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/parser/parsed_data/attach_info.hpp"
#include "oracle_storage_extension.hpp"
#include "oracle_transaction_manager.hpp"
#include "oracle_connection_resolver.hpp"
#include "oracle_secret.hpp"

namespace duckdb {

static unique_ptr<Catalog> OracleAttach(optional_ptr<StorageExtensionInfo> storage_info, ClientContext &context,
                                        AttachedDatabase &db, const string &name, AttachInfo &info,
                                        AttachOptions &options) {
	(void)storage_info;

	unordered_map<string, Value> attach_options = info.options;
	for (auto &entry : options.options) {
		attach_options[entry.first] = entry.second;
	}

	bool has_secret_option = false;
	string secret_name;
	bool has_wallet_option = false;
	string wallet_option_value;
	for (auto &entry : attach_options) {
		auto key = StringUtil::Lower(entry.first);
		if (key == "secret") {
			has_secret_option = true;
			secret_name = entry.second.ToString();
		} else if (key == "wallet_path") {
			has_wallet_option = true;
			wallet_option_value = entry.second.ToString();
		}
	}

	if (!info.path.empty() && has_secret_option) {
		throw BinderException("Cannot specify both a connection string and a SECRET in Oracle ATTACH");
	}

	string connection_string;
	string wallet_path;

	if (!info.path.empty()) {
		connection_string = info.path;
	} else {
		auto &secret_manager = SecretManager::Get(context);
		auto transaction = CatalogTransaction::GetSystemCatalogTransaction(context);

		unique_ptr<SecretEntry> secret_entry;
		if (has_secret_option) {
			secret_entry = secret_manager.GetSecretByName(transaction, secret_name);
			if (!secret_entry) {
				throw BinderException("Secret '%s' not found. Create it with: CREATE SECRET %s (TYPE oracle, HOST "
				                      "'localhost', PORT 1521, SERVICE 'XEPDB1', USER 'user', PASSWORD 'pass')",
				                      secret_name, secret_name);
			}
		} else {
			auto secret_match = secret_manager.LookupSecret(transaction, "", "oracle");
			if (!secret_match.HasMatch()) {
				throw BinderException(
				    "No Oracle secret found. Create one with: CREATE SECRET (TYPE oracle, HOST 'localhost', PORT "
				    "1521, SERVICE 'XEPDB1', USER 'user', PASSWORD 'pass')");
			}
			secret_entry = make_uniq<SecretEntry>(*secret_match.secret_entry);
		}

		auto &base_secret = secret_entry->secret;
		if (base_secret->GetType() != "oracle") {
			throw BinderException("Secret type mismatch. Expected 'oracle', got '%s'", base_secret->GetType());
		}

		auto kv_secret = dynamic_cast<const KeyValueSecret *>(base_secret.get());
		if (!kv_secret) {
			throw BinderException("Oracle secret must be a KeyValueSecret");
		}

		connection_string = BuildConnectionStringFromSecret(*kv_secret);
		wallet_path = GetWalletPathFromSecret(*kv_secret);
	}

	if (has_wallet_option) {
		wallet_path = wallet_option_value;
		ValidateOracleWalletPath(wallet_path);
	}

	auto state = make_shared_ptr<OracleCatalogState>(connection_string, wallet_path);
	state->ApplyOptions(attach_options);
	auto resolved = ResolveOracleConnection(context, state->connection_string, state.get());
	state->settings = resolved.settings;
	state->wallet_path = resolved.wallet_path;
	OracleCatalogState::Register(state, name);
	return CreateOracleCatalog(db, state);
}

static unique_ptr<TransactionManager> OracleCreateTransactionManager(optional_ptr<StorageExtensionInfo> storage_info,
                                                                     AttachedDatabase &db, Catalog &catalog) {
	(void)storage_info;
	auto state = catalog.Cast<OracleCatalog>().GetState();
	if (!state) {
		state = OracleCatalogState::LookupByAlias(db.GetName());
	}
	return make_uniq<OracleTransactionManager>(db, state);
}

unique_ptr<StorageExtension> CreateOracleStorageExtension() {
	auto ext = make_uniq<StorageExtension>();
	ext->attach = OracleAttach;
	ext->create_transaction_manager = OracleCreateTransactionManager;
	return ext;
}

} // namespace duckdb
