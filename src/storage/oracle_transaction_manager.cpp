#include "oracle_transaction_manager.hpp"
#include "oracle_transaction.hpp"

namespace duckdb {

OracleTransactionManager::OracleTransactionManager(AttachedDatabase &db, shared_ptr<OracleCatalogState> state_p)
    : TransactionManager(db), state(std::move(state_p)) {
}

Transaction &OracleTransactionManager::StartTransaction(ClientContext &context) {
	auto txn = make_uniq<OracleTransaction>(*this, context, state->connection_string);
	auto &result = *txn;
	active.push_back(std::move(txn));
	return result;
}

ErrorData OracleTransactionManager::CommitTransaction(ClientContext &, Transaction &) {
	// read-only; nothing to commit
	return ErrorData();
}

void OracleTransactionManager::RollbackTransaction(Transaction &) {
	// no-op for read-only
}

void OracleTransactionManager::Checkpoint(ClientContext &, bool) {
	// not applicable
}

} // namespace duckdb
