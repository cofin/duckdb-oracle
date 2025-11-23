#include "oracle_connection_manager.hpp"
#include "duckdb/common/string_util.hpp"

namespace duckdb {

static void CheckOCIError(sword status, OCIError *errhp, const std::string &msg) {
	if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
		text errbuf[512];
		sb4 errcode = 0;
		if (errhp) {
			OCIErrorGet((dvoid *)errhp, (ub4)1, (text *)NULL, &errcode, errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR);
			throw IOException(msg + ": " + std::string((char *)errbuf));
		} else {
			throw IOException(msg + ": (No Error Handle)");
		}
	}
}

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
	if (stmthp) {
		OCIHandleFree(stmthp, OCI_HTYPE_STMT);
		stmthp = nullptr;
	}
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

OracleConnectionHandle::OracleConnectionHandle(const std::string &key, std::shared_ptr<OracleContext> ctx_p,
                                               bool return_to_pool_p)
    : pool_key(key), ctx(std::move(ctx_p)), return_to_pool(return_to_pool_p) {
}

OracleConnectionHandle::~OracleConnectionHandle() {
	if (return_to_pool && ctx) {
		OracleConnectionManager::Instance().Release(pool_key, ctx);
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
	std::lock_guard<std::mutex> lock(pool_mutex);
	pools.clear();
}

std::shared_ptr<OracleConnectionHandle> OracleConnectionManager::Acquire(const std::string &connection_string,
                                                                         const OracleSettings &settings,
                                                                         idx_t wait_timeout_ms) {
	// If caching disabled, create a standalone connection that is not returned to pool.
	if (!settings.connection_cache) {
		auto ctx = CreateConnection(connection_string, settings);
		return std::make_shared<OracleConnectionHandle>(connection_string, std::move(ctx), false);
	}

	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_timeout_ms);
	std::unique_lock<std::mutex> lock(pool_mutex);
	auto &pool = pools[connection_string];
	// Update pool limit from settings (largest requested value wins)
	if (settings.connection_limit > pool.limit) {
		pool.limit = settings.connection_limit;
	}

	while (true) {
		if (!pool.idle.empty()) {
			auto ctx = pool.idle.back();
			pool.idle.pop_back();
			return std::make_shared<OracleConnectionHandle>(connection_string, std::move(ctx), true);
		}

		if (pool.total < pool.limit) {
			// Reserve a slot, create outside lock
			pool.total++;
			lock.unlock();
			try {
				auto ctx = CreateConnection(connection_string, settings);
				return std::make_shared<OracleConnectionHandle>(connection_string, std::move(ctx), true);
			} catch (...) {
				// Rollback reservation
				lock.lock();
				pool.total--;
				pool.cv.notify_one();
				throw;
			}
		}

		if (pool.cv.wait_until(lock, deadline) == std::cv_status::timeout) {
			throw IOException("Oracle connection pool timeout waiting for available session");
		}
	}
}

void OracleConnectionManager::Release(const std::string &connection_string, std::shared_ptr<OracleContext> ctx) {
	std::lock_guard<std::mutex> lock(pool_mutex);
	auto it = pools.find(connection_string);
	if (it == pools.end()) {
		return; // pool cleared
	}
	it->second.idle.push_back(std::move(ctx));
	it->second.cv.notify_one();
}

std::shared_ptr<OracleContext> OracleConnectionManager::CreateConnection(const std::string &connection_string,
                                                                         const OracleSettings &settings) {
	auto ctx = std::make_shared<OracleContext>();
	ctx->envhp = envhp;
	ctx->owns_env = false;

	std::string user, password, db;
	ParseOracleConnectionString(connection_string, user, password, db);

	CheckOCIError(OCIHandleAlloc(ctx->envhp, (dvoid **)&ctx->errhp, OCI_HTYPE_ERROR, 0, nullptr), nullptr,
	              "Failed to allocate OCI error handle");
	CheckOCIError(OCIHandleAlloc(ctx->envhp, (dvoid **)&ctx->srvhp, OCI_HTYPE_SERVER, 0, nullptr), ctx->errhp,
	              "Failed to allocate OCI server handle");
	CheckOCIError(OCIHandleAlloc(ctx->envhp, (dvoid **)&ctx->svchp, OCI_HTYPE_SVCCTX, 0, nullptr), ctx->errhp,
	              "Failed to allocate OCI service context handle");

	// Set call/connection timeouts on server handle before attach
	ub4 call_timeout_ms = 10000;
	OCIAttrSet(ctx->srvhp, OCI_HTYPE_SERVER, &call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, ctx->errhp);
	ub4 conn_timeout_ms = 10000;
	OCIAttrSet(ctx->srvhp, OCI_HTYPE_SERVER, &conn_timeout_ms, 0, OCI_ATTR_CONN_TIMEOUT, ctx->errhp);

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

	// Enable statement cache
	ub4 stmt_cache_size = 32;
	OCIAttrSet(ctx->svchp, OCI_HTYPE_SVCCTX, &stmt_cache_size, 0, OCI_ATTR_STMTCACHESIZE, ctx->errhp);

	// Default call timeout for operations on this service context
	ub4 svc_call_timeout_ms = 30000;
	OCIAttrSet(ctx->svchp, OCI_HTYPE_SVCCTX, &svc_call_timeout_ms, 0, OCI_ATTR_CALL_TIMEOUT, ctx->errhp);

	ctx->connected = true;
	return ctx;
}

} // namespace duckdb
