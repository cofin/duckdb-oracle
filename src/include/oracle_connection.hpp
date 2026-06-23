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
};

class OracleConnection {
public:
	OracleConnection();
	~OracleConnection();

	void Connect(const std::string &connection_string, const std::string &wallet_path, const OracleSettings &settings);
	bool IsConnected() const;

	//! Execute a query and return all rows as strings (used for metadata discovery).
	OracleResult Query(const std::string &query);
	//! Execute a metadata query with positional string bind values and return all rows as strings.
	OracleResult QueryWithStringBinds(const std::string &query, const std::vector<std::string> &bind_values);

	//! Commit the current transaction
	void Commit();

	//! Rollback the current transaction
	void Rollback();

private:
	std::shared_ptr<OracleConnectionHandle> conn_handle;
};

} // namespace duckdb
