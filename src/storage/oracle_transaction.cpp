#include "oracle_transaction.hpp"
#include "duckdb/transaction/transaction_manager.hpp"

namespace duckdb {

OracleTransaction::OracleTransaction(TransactionManager &manager, ClientContext &context, string connection_string)
    : Transaction(manager, context), connection_string(std::move(connection_string)) {
}

OracleConnection &OracleTransaction::GetConnection() {
	if (!connection.IsConnected()) {
		connection.Connect(connection_string);
	}
	return connection;
}

} // namespace duckdb
