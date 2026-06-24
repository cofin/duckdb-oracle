#include "oracle_transaction.hpp"
#include "duckdb/transaction/transaction_manager.hpp"

namespace duckdb {

OracleTransaction::OracleTransaction(TransactionManager &manager, ClientContext &context)
    : Transaction(manager, context) {
}

} // namespace duckdb
