#include "oracle_transaction_manager.hpp"
#include "oracle_transaction.hpp"
#include "duckdb/common/exception.hpp"

namespace duckdb {

OracleTransactionManager::OracleTransactionManager(AttachedDatabase &db, const shared_ptr<OracleCatalogState> &)
    : TransactionManager(db) {
}

Transaction &OracleTransactionManager::StartTransaction(ClientContext &context) {
	auto txn = make_uniq<OracleTransaction>(*this, context);
	auto &result = *txn;
	active.push_back(std::move(txn));
	return result;
}

bool OracleTransactionManager::EraseTransaction(Transaction &transaction) {
	for (auto it = active.begin(); it != active.end(); ++it) {
		if (it->get() == &transaction) {
			active.erase(it);
			return true;
		}
	}
	return false;
}

ErrorData OracleTransactionManager::CommitTransaction(ClientContext &, Transaction &transaction) {
	try {
		transaction.Cast<OracleTransaction>();
	} catch (Exception &e) {
		return ErrorData(e);
	}
	if (!EraseTransaction(transaction)) {
		return ErrorData(ExceptionType::INTERNAL, "Oracle transaction marker was not active during commit");
	}
	return ErrorData();
}

void OracleTransactionManager::RollbackTransaction(Transaction &transaction) {
	try {
		transaction.Cast<OracleTransaction>();
		EraseTransaction(transaction);
	} catch (...) {
		// Rollback must not throw while DuckDB is unwinding a failed transaction.
	}
}

void OracleTransactionManager::Checkpoint(ClientContext &, bool) {
	// not applicable
}

} // namespace duckdb
