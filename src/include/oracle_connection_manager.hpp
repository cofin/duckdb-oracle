#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/exception.hpp"
#include "oracle_settings.hpp"
#include <oci.h>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <condition_variable>

namespace duckdb {

struct OracleContext {
	OCIEnv *envhp = nullptr;
	OCIError *errhp = nullptr;
	OCISvcCtx *svchp = nullptr;
	OCIServer *srvhp = nullptr;
	OCISession *authp = nullptr;
	OCIStmt *stmthp = nullptr; // Shared statement handle for some ops
	bool connected = false;
	bool owns_env = false;

	~OracleContext();
};

struct OracleConnectionPool {
	std::mutex lock;
	std::vector<std::shared_ptr<OracleContext>> idle;
	idx_t total = 0;
	idx_t limit = 8;
	std::condition_variable cv;
};

struct OracleConnectionHandle {
	OracleConnectionHandle(std::shared_ptr<OracleConnectionPool> pool, std::shared_ptr<OracleContext> ctx);
	~OracleConnectionHandle();

	std::shared_ptr<OracleContext> Get() {
		return ctx;
	}

private:
	std::shared_ptr<OracleConnectionPool> pool;
	std::shared_ptr<OracleContext> ctx;
};

class OracleConnectionManager {
public:
	static OracleConnectionManager &Instance();

	std::shared_ptr<OracleConnectionHandle> Acquire(const std::string &connection_string,
	                                                const OracleSettings &settings, idx_t wait_timeout_ms = 10000);

	void Clear();

	OCIEnv *Env() {
		return envhp;
	}

private:
	std::mutex manager_mutex;
	std::unordered_map<std::string, std::shared_ptr<OracleConnectionPool>> pools;
	OCIEnv *envhp = nullptr;

	OracleConnectionManager();
	~OracleConnectionManager();

	std::shared_ptr<OracleContext> CreateConnection(const std::string &connection_string,
	                                                const OracleSettings &settings);
};

} // namespace duckdb
