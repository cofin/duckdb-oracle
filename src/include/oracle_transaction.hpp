#pragma once

#include "duckdb/transaction/transaction.hpp"
#include "oracle_connection.hpp"

namespace duckdb {

class OracleTransaction : public Transaction {
public:
	OracleTransaction(TransactionManager &manager, ClientContext &context, string connection_string, string wallet_path,
	                  OracleSettings settings);
	~OracleTransaction() override = default;

	OracleConnection &GetConnection();

private:
	string connection_string;
	string wallet_path;
	OracleSettings settings;
	OracleConnection connection;
};

} // namespace duckdb
