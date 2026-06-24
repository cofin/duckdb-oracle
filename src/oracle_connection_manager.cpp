#include "oracle_connection_manager.hpp"
#include "oracle_utils.hpp"
#include "duckdb/common/string_util.hpp"
#include <cstdlib>
#include <cstdio>

namespace duckdb {

namespace {

static std::mutex &WalletEnvLock() {
	static std::mutex lock;
	return lock;
}

struct ScopedTnsAdmin {
	explicit ScopedTnsAdmin(const std::string &wallet_path) {
		if (wallet_path.empty()) {
			return;
		}
		const auto *current = getenv("TNS_ADMIN");
		if (current) {
			previous = current;
		}
		changed = true;
		setenv("TNS_ADMIN", wallet_path.c_str(), 1);
	}

	~ScopedTnsAdmin() {
		if (changed) {
			if (previous.empty()) {
				unsetenv("TNS_ADMIN");
			} else {
				setenv("TNS_ADMIN", previous.c_str(), 1);
			}
		}
	}

	std::string previous;
	bool changed = false;
};

} // namespace

static void ParseOracleConnectionString(const std::string &connection_string, std::string &user, std::string &password,
                                        std::string &db) {
	auto slash_pos = connection_string.find('/');
	auto at_pos = connection_string.find('@', slash_pos == std::string::npos ? 0 : slash_pos);
	if (slash_pos == std::string::npos || at_pos == std::string::npos || slash_pos == 0 || at_pos <= slash_pos + 1 ||
	    at_pos == connection_string.size() - 1) {
		throw IOException("Invalid Oracle connection string. Expected user/password@connect_identifier");
	}
	user = connection_string.substr(0, slash_pos);
	password = connection_string.substr(slash_pos + 1, at_pos - slash_pos - 1);
	db = connection_string.substr(at_pos + 1);
}

OracleContext::~OracleContext() {
	if (svchp && authp && errhp) {
		OCISessionEnd(svchp, errhp, authp, OCI_DEFAULT);
	}
	if (authp) {
		OCIHandleFree(authp, OCI_HTYPE_SESSION);
		authp = nullptr;
	}
	if (svchp) {
		OCIHandleFree(svchp, OCI_HTYPE_SVCCTX);
		svchp = nullptr;
	}
	if (srvhp) {
		if (errhp) {
			OCIServerDetach(srvhp, errhp, OCI_DEFAULT);
		}
		OCIHandleFree(srvhp, OCI_HTYPE_SERVER);
		srvhp = nullptr;
	}
	if (errhp) {
		OCIHandleFree(errhp, OCI_HTYPE_ERROR);
		errhp = nullptr;
	}
	if (envhp && owns_env) {
		OCIHandleFree(envhp, OCI_HTYPE_ENV);
		envhp = nullptr;
	}
}

OracleConnectionHandle::OracleConnectionHandle(std::shared_ptr<OracleConnectionPool> pool_p,
                                               std::shared_ptr<OracleContext> ctx_p, idx_t pool_generation_p)
    : pool(std::move(pool_p)), ctx(std::move(ctx_p)), pool_generation(pool_generation_p) {
}

OracleConnectionHandle::~OracleConnectionHandle() {
	if (pool && ctx) {
		std::shared_ptr<OracleContext> ctx_to_close;
		{
			std::lock_guard<std::mutex> lock(pool->lock);
			if (unusable || pool->stale || pool_generation != pool->generation || pool->total > pool->limit) {
				if (pool->total > 0) {
					pool->total--;
				}
				ctx_to_close = std::move(ctx);
			} else {
				pool->idle.push_back(std::move(ctx));
			}
			pool->cv.notify_one();
		}
	}
}

OracleConnectionManager &OracleConnectionManager::Instance() {
	static OracleConnectionManager instance;
	return instance;
}

OracleConnectionManager::OracleConnectionManager() {
	// OCI_THREADED allows concurrent usage across threads with separate handles
	CheckOCIError(OCIEnvCreate(&envhp, OCI_THREADED, nullptr, nullptr, nullptr, nullptr, 0, nullptr), nullptr,
	              "Failed to create OCI environment");
}

OracleConnectionManager::~OracleConnectionManager() {
	Clear();
	if (envhp) {
		OCIHandleFree(envhp, OCI_HTYPE_ENV);
		envhp = nullptr;
	}
}

void OracleConnectionManager::Clear() {
	std::vector<std::shared_ptr<OracleConnectionPool>> old_pools;
	{
		std::lock_guard<std::mutex> lock(manager_mutex);
		generation++;
		old_pools.reserve(pools.size());
		for (auto &entry : pools) {
			old_pools.push_back(entry.second);
		}
		pools.clear();
	}

	for (auto &pool : old_pools) {
		std::vector<std::shared_ptr<OracleContext>> idle_to_close;
		{
			std::lock_guard<std::mutex> lock(pool->lock);
			pool->stale = true;
			pool->generation++;
			idle_to_close.swap(pool->idle);
			if (pool->total >= idle_to_close.size()) {
				pool->total -= idle_to_close.size();
			} else {
				pool->total = 0;
			}
			pool->cv.notify_all();
		}
	}
}

std::shared_ptr<OracleConnectionHandle> OracleConnectionManager::Acquire(const std::string &connection_string,
                                                                         const std::string &wallet_path,
                                                                         const OracleSettings &settings,
                                                                         idx_t wait_timeout_ms) {
	// If caching disabled, create a standalone connection
	if (!settings.connection_cache) {
		auto ctx = CreateConnection(connection_string, wallet_path, settings);
		return std::make_shared<OracleConnectionHandle>(nullptr, std::move(ctx));
	}

	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_timeout_ms);

	while (true) {
		std::vector<std::shared_ptr<OracleContext>> idle_to_close;
		std::shared_ptr<OracleConnectionPool> pool;

		{
			std::unique_lock<std::mutex> lock(manager_mutex);
			OraclePoolKey key {connection_string, wallet_path};
			auto &p = pools[key];
			if (!p) {
				p = std::make_shared<OracleConnectionPool>();
				p->generation = generation;
			}
			pool = p;
		}

		// Lock the specific pool
		std::unique_lock<std::mutex> lock(pool->lock);
		if (pool->stale) {
			continue;
		}

		// Update pool limit from the latest settings and close excess idle contexts.
		auto requested_limit = OracleEffectiveConnectionLimit(settings);
		pool->limit = requested_limit;
		while (!pool->idle.empty() && pool->total > pool->limit) {
			idle_to_close.push_back(std::move(pool->idle.back()));
			pool->idle.pop_back();
			pool->total--;
		}
		if (!idle_to_close.empty()) {
			pool->cv.notify_all();
			lock.unlock();
			idle_to_close.clear();
			lock.lock();
			if (pool->stale) {
				continue;
			}
		}

		while (true) {
			if (pool->stale) {
				break;
			}

			if (!pool->idle.empty()) {
				auto ctx = pool->idle.back();
				pool->idle.pop_back();
				return std::make_shared<OracleConnectionHandle>(pool, std::move(ctx), pool->generation);
			}

			if (pool->total < pool->limit) {
				// Reserve a slot
				pool->total++;
				lock.unlock(); // Unlock pool to create connection
				std::shared_ptr<OracleContext> ctx;
				try {
					ctx = CreateConnection(connection_string, wallet_path, settings);
				} catch (...) {
					// Rollback reservation
					lock.lock();
					pool->total--;
					pool->cv.notify_one();
					throw;
				}

				lock.lock();
				if (pool->stale) {
					if (pool->total > 0) {
						pool->total--;
					}
					pool->cv.notify_all();
					lock.unlock();
					ctx.reset();
					break;
				}
				auto handle_generation = pool->generation;
				lock.unlock();
				return std::make_shared<OracleConnectionHandle>(pool, std::move(ctx), handle_generation);
			}

			if (pool->cv.wait_until(lock, deadline) == std::cv_status::timeout) {
				throw IOException("Oracle connection pool timeout waiting for available session");
			}
		}
	}
}

std::shared_ptr<OracleContext> OracleConnectionManager::CreateConnection(const std::string &connection_string,
                                                                         const std::string &wallet_path,
                                                                         const OracleSettings &settings) {
	auto ctx = std::make_shared<OracleContext>();
	ctx->envhp = envhp;
	ctx->owns_env = false;

	std::string user, password, db;
	ParseOracleConnectionString(connection_string, user, password, db);
	std::unique_lock<std::mutex> wallet_lock(WalletEnvLock());
	ScopedTnsAdmin tns_admin(wallet_path);

	CheckOCIError(OCIHandleAlloc(ctx->envhp, (dvoid **)&ctx->errhp, OCI_HTYPE_ERROR, 0, nullptr), nullptr,
	              "Failed to allocate OCI error handle");
	CheckOCIError(OCIHandleAlloc(ctx->envhp, (dvoid **)&ctx->srvhp, OCI_HTYPE_SERVER, 0, nullptr), ctx->errhp,
	              "Failed to allocate OCI server handle");
	CheckOCIError(OCIHandleAlloc(ctx->envhp, (dvoid **)&ctx->svchp, OCI_HTYPE_SVCCTX, 0, nullptr), ctx->errhp,
	              "Failed to allocate OCI service context handle");

	// Set call/connection timeouts on server handle before attach
	ub4 call_timeout_ms = 10000;
	CheckOCIOptionalAttributeORA24315(
	    OCIAttrSet(ctx->srvhp, OCI_HTYPE_SERVER, &call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, ctx->errhp), ctx->errhp,
	    "Failed to set pre-attach OCI server call timeout");
	ub4 conn_timeout_ms = 10000;
	CheckOCIOptionalAttributeORA24315(
	    OCIAttrSet(ctx->srvhp, OCI_HTYPE_SERVER, &conn_timeout_ms, 0, OCI_ATTR_CONN_TIMEOUT, ctx->errhp), ctx->errhp,
	    "Failed to set pre-attach OCI server connection timeout");

	// Attach to server
	CheckOCIError(OCIServerAttach(ctx->srvhp, ctx->errhp, (OraText *)db.c_str(), (sb4)db.size(), OCI_DEFAULT),
	              ctx->errhp, "Failed to attach to Oracle server");

	// Set server handle into service context
	CheckOCIError(OCIAttrSet(ctx->svchp, OCI_HTYPE_SVCCTX, ctx->srvhp, 0, OCI_ATTR_SERVER, ctx->errhp), ctx->errhp,
	              "Failed to set OCI server on service context");

	// Allocate session
	CheckOCIError(OCIHandleAlloc(ctx->envhp, (dvoid **)&ctx->authp, OCI_HTYPE_SESSION, 0, nullptr), ctx->errhp,
	              "Failed to allocate OCI session handle");
	CheckOCIError(OCIAttrSet(ctx->authp, OCI_HTYPE_SESSION, (dvoid *)user.c_str(), (ub4)user.size(), OCI_ATTR_USERNAME,
	                         ctx->errhp),
	              ctx->errhp, "Failed to set OCI username");
	CheckOCIError(OCIAttrSet(ctx->authp, OCI_HTYPE_SESSION, (dvoid *)password.c_str(), (ub4)password.size(),
	                         OCI_ATTR_PASSWORD, ctx->errhp),
	              ctx->errhp, "Failed to set OCI password");

	// Establish session
	CheckOCIError(OCISessionBegin(ctx->svchp, ctx->errhp, ctx->authp, OCI_CRED_RDBMS, OCI_DEFAULT), ctx->errhp,
	              "Failed to begin OCI session");

	CheckOCIError(OCIAttrSet(ctx->svchp, OCI_HTYPE_SVCCTX, ctx->authp, 0, OCI_ATTR_SESSION, ctx->errhp), ctx->errhp,
	              "Failed to set OCI session on service context");

	// Set NLS date/timestamp format to ISO
	{
		auto stmt = AllocateOCIStatement(ctx->envhp, ctx->errhp, "Failed to allocate statement handle for NLS setup");
		std::string sql = "ALTER SESSION SET NLS_DATE_FORMAT = 'YYYY-MM-DD HH24:MI:SS' NLS_TIMESTAMP_FORMAT = "
		                  "'YYYY-MM-DD HH24:MI:SS.FF'";
		CheckOCIError(
		    OCIStmtPrepare(stmt.get(), ctx->errhp, (OraText *)sql.c_str(), sql.size(), OCI_NTV_SYNTAX, OCI_DEFAULT),
		    ctx->errhp, "Failed to prepare NLS setup statement");
		CheckOCIError(OCIStmtExecute(ctx->svchp, stmt.get(), ctx->errhp, 1, 0, nullptr, nullptr, OCI_DEFAULT),
		              ctx->errhp, "Failed to execute NLS setup statement");
	}

	// Enable statement cache (Disable for debugging shift issue)
	ub4 stmt_cache_size = 0;
	CheckOCIError(OCIAttrSet(ctx->svchp, OCI_HTYPE_SVCCTX, &stmt_cache_size, 0, OCI_ATTR_STMTCACHESIZE, ctx->errhp),
	              ctx->errhp, "Failed to set OCI statement cache size");

	// Default call timeout for operations on this service context
	ub4 svc_call_timeout_ms = 30000;
	CheckOCIError(OCIAttrSet(ctx->svchp, OCI_HTYPE_SVCCTX, &svc_call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, ctx->errhp),
	              ctx->errhp, "Failed to set OCI call timeout on service context");

	ctx->connected = true;
	return ctx;
}

} // namespace duckdb
