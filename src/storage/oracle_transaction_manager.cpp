#include "oracle_transaction_manager.hpp"
#include "oracle_transaction.hpp"
#include "oracle_connection_resolver.hpp"
#include "duckdb/common/exception.hpp"

namespace duckdb {

OracleTransactionManager::OracleTransactionManager(AttachedDatabase &db, shared_ptr<OracleCatalogState> state_p)
    : TransactionManager(db), state(std::move(state_p)) {
}

Transaction &OracleTransactionManager::StartTransaction(ClientContext &context) {
	auto resolved = ResolveOracleConnection(context, state->connection_string, state.get());
	auto txn = make_uniq<OracleTransaction>(*this, context, resolved.connection_string, resolved.wallet_path,
	                                        resolved.settings);
	auto &result = *txn;
	active.push_back(std::move(txn));
	return result;
}

ErrorData OracleTransactionManager::CommitTransaction(ClientContext &, Transaction &transaction) {
	auto &oracle_txn = transaction.Cast<OracleTransaction>();
	try {
		oracle_txn.GetConnection().Commit();
	} catch (Exception &e) {
		return ErrorData(e);
	}
	return ErrorData();
}

void OracleTransactionManager::RollbackTransaction(Transaction &transaction) {
	auto &oracle_txn = transaction.Cast<OracleTransaction>();
	try {
		oracle_txn.GetConnection().Rollback();
	} catch (...) {
		// Suppress errors on rollback
	}
}

void OracleTransactionManager::Checkpoint(ClientContext &, bool) {
	// not applicable
}

} // namespace duckdb
