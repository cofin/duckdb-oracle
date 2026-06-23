#pragma once

#include "duckdb/common/types.hpp"
#include "duckdb/common/exception.hpp"
#include "oracle_connection_manager.hpp" // Include the manager
#include <memory>
#include <string>
#include <vector>

namespace duckdb {

struct OracleResult {
	std::vector<std::string> columns;
	std::vector<std::vector<std::string>> rows;

	std::string GetString(idx_t row, idx_t col) const;
};

class OracleConnection {
public:
	OracleConnection();
	~OracleConnection();

	void Connect(const std::string &connection_string, const std::string &wallet_path, const OracleSettings &settings);
	bool IsConnected() const;

	//! Execute a query and return all rows as strings (used for metadata discovery).
	OracleResult Query(const std::string &query);

	//! Commit the current transaction
	void Commit();

	//! Rollback the current transaction
	void Rollback();

	//! Get the underlying connection handle
	std::shared_ptr<OracleConnectionHandle> GetHandle() const;

private:
	std::shared_ptr<OracleConnectionHandle> conn_handle;
};

} // namespace duckdb
