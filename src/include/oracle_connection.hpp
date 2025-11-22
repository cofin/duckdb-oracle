#pragma once

#include "duckdb/common/types.hpp"
#include "duckdb/common/exception.hpp"
#include <oci.h>
#include <memory>
#include <string>
#include <vector>

namespace duckdb {

struct OracleResult {
	std::vector<std::string> columns;
	std::vector<std::vector<std::string>> rows;

	std::string GetString(idx_t row, idx_t col) const;
	int64_t GetInt64(idx_t row, idx_t col) const;
	double GetDouble(idx_t row, idx_t col) const;
};

class OracleConnection {
public:
	OracleConnection();
	~OracleConnection();

	void Connect(const std::string &connection_string);
	bool IsConnected() const;

	//! Execute a query and return all rows as strings (used for metadata discovery).
	OracleResult Query(const std::string &query);

private:
	void Reset();
	void CheckError(sword status, const std::string &msg);

	OCIEnv *envhp = nullptr;
	OCIError *errhp = nullptr;
	OCISvcCtx *svchp = nullptr;
	bool connected = false;
};

} // namespace duckdb
