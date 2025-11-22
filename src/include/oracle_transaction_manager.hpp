#pragma once

#include "duckdb/transaction/transaction_manager.hpp"
#include "oracle_catalog_state.hpp"
#include "oracle_transaction.hpp"

namespace duckdb {

class OracleTransactionManager : public TransactionManager {
public:
	OracleTransactionManager(AttachedDatabase &db, shared_ptr<OracleCatalogState> state_p);

	Transaction &StartTransaction(ClientContext &context) override;
	ErrorData CommitTransaction(ClientContext &context, Transaction &transaction) override;
	void RollbackTransaction(Transaction &transaction) override;
	void Checkpoint(ClientContext &context, bool force) override;

private:
	shared_ptr<OracleCatalogState> state;
	vector<unique_ptr<OracleTransaction>> active;
};

} // namespace duckdb
