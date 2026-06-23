#pragma once

#include "duckdb/transaction/transaction_manager.hpp"
#include "oracle_transaction.hpp"

namespace duckdb {

class OracleCatalogState;

class OracleTransactionManager : public TransactionManager {
public:
	OracleTransactionManager(AttachedDatabase &db, const shared_ptr<OracleCatalogState> &state_p);

	Transaction &StartTransaction(ClientContext &context) override;
	ErrorData CommitTransaction(ClientContext &context, Transaction &transaction) override;
	void RollbackTransaction(Transaction &transaction) override;
	void Checkpoint(ClientContext &context, bool force) override;

private:
	bool EraseTransaction(Transaction &transaction);

	vector<unique_ptr<OracleTransaction>> active;
};

} // namespace duckdb
