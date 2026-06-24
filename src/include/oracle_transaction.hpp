#pragma once

#include "duckdb/transaction/transaction.hpp"

namespace duckdb {

class OracleTransaction : public Transaction {
public:
	OracleTransaction(TransactionManager &manager, ClientContext &context);
	~OracleTransaction() override = default;
};

} // namespace duckdb
