#include "oracle_transaction.hpp"
#include "duckdb/transaction/transaction_manager.hpp"

namespace duckdb {

OracleTransaction::OracleTransaction(TransactionManager &manager, ClientContext &context, string connection_string,
                                     string wallet_path, OracleSettings settings)
    : Transaction(manager, context), connection_string(std::move(connection_string)),
      wallet_path(std::move(wallet_path)), settings(std::move(settings)) {
}

OracleConnection &OracleTransaction::GetConnection() {
	if (!connection.IsConnected()) {
		connection.Connect(connection_string, wallet_path, settings);
	}
	return connection;
}

} // namespace duckdb
