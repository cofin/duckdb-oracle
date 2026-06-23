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
#include <functional>

namespace duckdb {

struct OracleContext {
	OCIEnv *envhp = nullptr;
	OCIError *errhp = nullptr;
	OCISvcCtx *svchp = nullptr;
	OCIServer *srvhp = nullptr;
	OCISession *authp = nullptr;
	bool connected = false;
	bool owns_env = false;

	~OracleContext();
};

struct OracleConnectionPool {
	std::mutex lock;
	std::vector<std::shared_ptr<OracleContext>> idle;
	idx_t total = 0;
	idx_t limit = 8;
	idx_t generation = 0;
	bool stale = false;
	std::condition_variable cv;
};

struct OraclePoolKey {
	std::string connection_string;
	std::string wallet_path;

	bool operator==(const OraclePoolKey &other) const {
		return connection_string == other.connection_string && wallet_path == other.wallet_path;
	}
};

struct OraclePoolKeyHash {
	std::size_t operator()(const OraclePoolKey &key) const {
		auto connection_hash = std::hash<std::string> {}(key.connection_string);
		auto wallet_hash = std::hash<std::string> {}(key.wallet_path);
		return connection_hash ^
		       (wallet_hash + 0x9e3779b97f4a7c15ULL + (connection_hash << 6U) + (connection_hash >> 2U));
	}
};

struct OracleConnectionHandle {
	OracleConnectionHandle(std::shared_ptr<OracleConnectionPool> pool, std::shared_ptr<OracleContext> ctx,
	                       idx_t pool_generation = 0);
	~OracleConnectionHandle();

	std::shared_ptr<OracleContext> Get() {
		return ctx;
	}

	void MarkUnusable() {
		unusable = true;
	}

private:
	std::shared_ptr<OracleConnectionPool> pool;
	std::shared_ptr<OracleContext> ctx;
	idx_t pool_generation;
	bool unusable = false;
};

class OracleConnectionManager {
public:
	static OracleConnectionManager &Instance();

	std::shared_ptr<OracleConnectionHandle> Acquire(const std::string &connection_string,
	                                                const std::string &wallet_path, const OracleSettings &settings,
	                                                idx_t wait_timeout_ms = 10000);

	void Clear();

	OCIEnv *Env() {
		return envhp;
	}

private:
	std::mutex manager_mutex;
	std::unordered_map<OraclePoolKey, std::shared_ptr<OracleConnectionPool>, OraclePoolKeyHash> pools;
	OCIEnv *envhp = nullptr;
	idx_t generation = 0;

	OracleConnectionManager();
	~OracleConnectionManager();

	std::shared_ptr<OracleContext> CreateConnection(const std::string &connection_string,
	                                                const std::string &wallet_path, const OracleSettings &settings);
};

} // namespace duckdb
