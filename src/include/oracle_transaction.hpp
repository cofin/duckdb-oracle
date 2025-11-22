#pragma once

#include "duckdb/transaction/transaction.hpp"
#include "oracle_connection.hpp"

namespace duckdb {

class OracleTransaction : public Transaction {
public:
	OracleTransaction(TransactionManager &manager, ClientContext &context, string connection_string);
	~OracleTransaction() override = default;

	OracleConnection &GetConnection();

private:
	string connection_string;
	OracleConnection connection;
};

} // namespace duckdb
