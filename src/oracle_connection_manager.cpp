#include "oracle_connection_manager.hpp"
#include "oracle_utils.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace duckdb {

namespace {

static std::mutex &WalletEnvLock() {
	static std::mutex lock;
	return lock;
}

struct ScopedTnsAdmin {
	explicit ScopedTnsAdmin(const std::string &wallet_path) : lock(WalletEnvLock(), std::defer_lock) {
		if (wallet_path.empty()) {
			return;
		}
		lock.lock();
		const auto *current = std::getenv("TNS_ADMIN");
		if (current) {
			had_previous = true;
			previous_value = current;
			if (wallet_path == current) {
				return;
			}
		}
		mutated = true;
		SetOracleEnvVar("TNS_ADMIN", wallet_path.c_str());
	}

	void Release() {
		if (mutated) {
			if (had_previous) {
				SetOracleEnvVar("TNS_ADMIN", previous_value.c_str());
			} else {
				UnsetOracleEnvVar("TNS_ADMIN");
			}
			mutated = false;
		}
		if (lock.owns_lock()) {
			lock.unlock();
		}
	}

	~ScopedTnsAdmin() {
		Release();
	}

	std::unique_lock<std::mutex> lock;
	std::string previous_value;
	bool had_previous = false;
	bool mutated = false;
};

static bool IsOracleContextHealthy(OracleContext &ctx, bool ping_server) {
	if (!ctx.srvhp || !ctx.svchp || !ctx.errhp) {
		return false;
	}
	ub4 server_status = OCI_SERVER_NOT_CONNECTED;
	if (OCIAttrGet(ctx.srvhp, OCI_HTYPE_SERVER, &server_status, nullptr, OCI_ATTR_SERVER_STATUS, ctx.errhp) !=
	        OCI_SUCCESS ||
	    server_status != OCI_SERVER_NORMAL) {
		return false;
	}
	if (ping_server) {
		if (OCIPing(ctx.svchp, ctx.errhp, OCI_DEFAULT) != OCI_SUCCESS) {
			return false;
		}
	}
	return true;
}

} // namespace

static void ParseOracleConnectionString(const std::string &connection_string, std::string &user, std::string &password,
                                        std::string &db) {
	auto slash_pos = connection_string.find('/');
	auto at_pos = connection_string.rfind('@');
	if (slash_pos == std::string::npos || at_pos == std::string::npos || slash_pos >= at_pos ||
	    at_pos + 1 >= connection_string.size()) {
		throw IOException(
		    "Invalid Oracle connection string. Expected user/password@connect_identifier or /@connect_identifier");
	}
	if (slash_pos == 0) {
		if (at_pos != 1) {
			throw IOException(
			    "Invalid Oracle connection string. Expected user/password@connect_identifier or /@connect_identifier");
		}
		user.clear();
		password.clear();
		db = connection_string.substr(2);
		return;
	}
	if (at_pos <= slash_pos + 1) {
		throw IOException(
		    "Invalid Oracle connection string. Expected user/password@connect_identifier or /@connect_identifier");
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
	envhp = nullptr;
	env_owner.reset();
}

OracleConnectionHandle::OracleConnectionHandle(std::shared_ptr<OracleConnectionPool> pool_p,
                                               std::shared_ptr<OracleContext> ctx_p, idx_t pool_generation_p)
    : pool(std::move(pool_p)), ctx(std::move(ctx_p)), pool_generation(pool_generation_p) {
}

OracleConnectionHandle::~OracleConnectionHandle() {
	if (pool && ctx) {
		if (!unusable && !pool->stale && ctx->svchp && ctx->errhp) {
			(void)OCITransRollback(ctx->svchp, ctx->errhp, OCI_DEFAULT);
			if (!IsOracleContextHealthy(*ctx, false)) {
				unusable = true;
			}
		}
		std::shared_ptr<OracleContext> ctx_to_close;
		std::unique_lock<std::mutex> guard(pool->lock);
		if (unusable || pool->stale || pool_generation != pool->generation || pool->total > pool->limit) {
			if (pool->total > 0) {
				pool->total--;
			}
			ctx_to_close = std::move(ctx);
		} else {
			pool->idle.push_back(std::move(ctx));
		}
		guard.unlock();
		pool->cv.notify_one();
	}
}

OracleConnectionManager &OracleConnectionManager::Instance() {
	static OracleConnectionManager instance;
	return instance;
}

OracleConnectionManager::OracleConnectionManager() = default;

OracleConnectionManager::~OracleConnectionManager() {
	Clear();
	env.reset();
}

std::shared_ptr<OCIEnv> OracleConnectionManager::GetOrCreateEnv() {
	std::lock_guard<std::mutex> lock(manager_mutex);
	if (!env) {
		EnsureOCILoaded();
		OCIEnv *raw_env = nullptr;
		CheckOCIError(OCIEnvCreate(&raw_env, OCI_THREADED | OCI_OBJECT, nullptr, nullptr, nullptr, nullptr, 0, nullptr),
		              nullptr, "Failed to create OCI environment");
		env = std::shared_ptr<OCIEnv>(raw_env, [](OCIEnv *p) {
			if (p) {
				OCIHandleFree(p, OCI_HTYPE_ENV);
			}
		});
	}
	return env;
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
			lock.unlock();
			if (std::chrono::steady_clock::now() >= deadline) {
				throw IOException("Oracle connection pool timeout waiting for available session");
			}
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
				lock.unlock();
				if (std::chrono::steady_clock::now() >= deadline) {
					throw IOException("Oracle connection pool timeout waiting for available session");
				}
				continue;
			}
		}

		while (true) {
			if (pool->stale) {
				if (std::chrono::steady_clock::now() >= deadline) {
					throw IOException("Oracle connection pool timeout waiting for available session");
				}
				break;
			}

			while (!pool->idle.empty()) {
				auto ctx = std::move(pool->idle.back());
				pool->idle.pop_back();
				auto handle_generation = pool->generation;
				lock.unlock();
				bool healthy = ctx && IsOracleContextHealthy(*ctx, true);
				if (healthy) {
					return std::make_shared<OracleConnectionHandle>(pool, std::move(ctx), handle_generation);
				}
				ctx.reset();
				lock.lock();
				if (pool->total > 0) {
					pool->total--;
				}
				pool->cv.notify_one();
				if (pool->stale) {
					break;
				}
			}
			if (pool->stale) {
				if (std::chrono::steady_clock::now() >= deadline) {
					throw IOException("Oracle connection pool timeout waiting for available session");
				}
				break;
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
					lock.unlock();
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
					if (std::chrono::steady_clock::now() >= deadline) {
						throw IOException("Oracle connection pool timeout waiting for available session");
					}
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
	std::string user, password, db;
	ParseOracleConnectionString(connection_string, user, password, db);

	auto env_ptr = GetOrCreateEnv();
	auto ctx = std::make_shared<OracleContext>();
	ctx->env_owner = env_ptr;
	ctx->envhp = env_ptr.get();

	CheckOCIError(OCIHandleAlloc(ctx->envhp, reinterpret_cast<dvoid **>(&ctx->errhp), OCI_HTYPE_ERROR, 0, nullptr),
	              nullptr, "Failed to allocate OCI error handle");
	CheckOCIError(OCIHandleAlloc(ctx->envhp, reinterpret_cast<dvoid **>(&ctx->srvhp), OCI_HTYPE_SERVER, 0, nullptr),
	              ctx->errhp, "Failed to allocate OCI server handle");
	CheckOCIError(OCIHandleAlloc(ctx->envhp, reinterpret_cast<dvoid **>(&ctx->svchp), OCI_HTYPE_SVCCTX, 0, nullptr),
	              ctx->errhp, "Failed to allocate OCI service context handle");

	auto effective_call_timeout_ms = OracleEffectiveCallTimeoutMs(settings);
	ub4 call_timeout_ms =
	    effective_call_timeout_ms > 0
	        ? static_cast<ub4>(MinValue<idx_t>(effective_call_timeout_ms, NumericLimits<ub4>::Maximum()))
	        : 10000;
	CheckOCIOptionalAttributeORA24315(
	    OCIAttrSet(ctx->srvhp, OCI_HTYPE_SERVER, &call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, ctx->errhp), ctx->errhp,
	    "Failed to set pre-attach OCI server call timeout");
	ub4 conn_timeout_ms = 10000;
	CheckOCIOptionalAttributeORA24315(
	    OCIAttrSet(ctx->srvhp, OCI_HTYPE_SERVER, &conn_timeout_ms, 0, OCI_ATTR_CONN_TIMEOUT, ctx->errhp), ctx->errhp,
	    "Failed to set pre-attach OCI server connection timeout");

	ScopedTnsAdmin tns_guard(wallet_path);

	CheckOCIError(OCIServerAttach(ctx->srvhp, ctx->errhp, reinterpret_cast<const OraText *>(db.c_str()),
	                              static_cast<sb4>(db.size()), OCI_DEFAULT),
	              ctx->errhp, "Failed to attach to Oracle server");

	CheckOCIError(OCIAttrSet(ctx->svchp, OCI_HTYPE_SVCCTX, ctx->srvhp, 0, OCI_ATTR_SERVER, ctx->errhp), ctx->errhp,
	              "Failed to set OCI server on service context");

	CheckOCIError(OCIHandleAlloc(ctx->envhp, reinterpret_cast<dvoid **>(&ctx->authp), OCI_HTYPE_SESSION, 0, nullptr),
	              ctx->errhp, "Failed to allocate OCI session handle");

	ub4 cred_type = OCI_CRED_RDBMS;
	if (user.empty() && password.empty()) {
		cred_type = OCI_CRED_EXT;
	} else {
		CheckOCIError(OCIAttrSet(ctx->authp, OCI_HTYPE_SESSION, user.empty() ? nullptr : &user[0],
		                         static_cast<ub4>(user.size()), OCI_ATTR_USERNAME, ctx->errhp),
		              ctx->errhp, "Failed to set OCI username");
		CheckOCIError(OCIAttrSet(ctx->authp, OCI_HTYPE_SESSION, password.empty() ? nullptr : &password[0],
		                         static_cast<ub4>(password.size()), OCI_ATTR_PASSWORD, ctx->errhp),
		              ctx->errhp, "Failed to set OCI password");
	}

	sword begin_rc = OCISessionBegin(ctx->svchp, ctx->errhp, ctx->authp, cred_type, OCI_DEFAULT);
	std::fill(password.begin(), password.end(), '\0');
	password.clear();
	tns_guard.Release();
	CheckOCIError(begin_rc, ctx->errhp, "Failed to begin OCI session");

	CheckOCIError(OCIAttrSet(ctx->svchp, OCI_HTYPE_SVCCTX, ctx->authp, 0, OCI_ATTR_SESSION, ctx->errhp), ctx->errhp,
	              "Failed to set OCI session on service context");

	{
		auto stmt = AllocateOCIStatement(ctx->envhp, ctx->errhp, "Failed to allocate statement handle for NLS setup");
		std::string sql = "ALTER SESSION SET NLS_DATE_FORMAT = 'YYYY-MM-DD HH24:MI:SS' "
		                  "NLS_TIMESTAMP_FORMAT = 'YYYY-MM-DD HH24:MI:SS.FF' "
		                  "NLS_TIMESTAMP_TZ_FORMAT = 'YYYY-MM-DD HH24:MI:SS.FF TZH:TZM' "
		                  "NLS_NUMERIC_CHARACTERS = '.,'";
		CheckOCIError(OCIStmtPrepare(stmt.get(), ctx->errhp, reinterpret_cast<const OraText *>(sql.c_str()), sql.size(),
		                             OCI_NTV_SYNTAX, OCI_DEFAULT),
		              ctx->errhp, "Failed to prepare NLS setup statement");
		CheckOCIError(OCIStmtExecute(ctx->svchp, stmt.get(), ctx->errhp, 1, 0, nullptr, nullptr, OCI_DEFAULT),
		              ctx->errhp, "Failed to execute NLS setup statement");
	}

	ub4 stmt_cache_size =
	    static_cast<ub4>(MinValue<idx_t>(OracleEffectiveStmtCacheSize(settings), NumericLimits<ub4>::Maximum()));
	CheckOCIOptionalAttributeORA24315(
	    OCIAttrSet(ctx->svchp, OCI_HTYPE_SVCCTX, &stmt_cache_size, 0, OCI_ATTR_STMTCACHESIZE, ctx->errhp), ctx->errhp,
	    "Failed to set OCI statement cache size");

	if (effective_call_timeout_ms > 0) {
		ub4 svc_call_timeout_ms =
		    static_cast<ub4>(MinValue<idx_t>(effective_call_timeout_ms, NumericLimits<ub4>::Maximum()));
		CheckOCIOptionalAttributeORA24315(
		    OCIAttrSet(ctx->svchp, OCI_HTYPE_SVCCTX, &svc_call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, ctx->errhp),
		    ctx->errhp, "Failed to set OCI call timeout on service context");
	}

	return ctx;
}

} // namespace duckdb
