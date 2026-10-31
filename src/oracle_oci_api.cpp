#include "oracle_oci_api.hpp"
#include "duckdb/common/exception.hpp"
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace duckdb {

namespace {

struct OCIFunctionTable {
	decltype(&::duckdb::OCIEnvCreate) pOCIEnvCreate = nullptr;
	decltype(&::duckdb::OCIHandleAlloc) pOCIHandleAlloc = nullptr;
	decltype(&::duckdb::OCIHandleFree) pOCIHandleFree = nullptr;
	decltype(&::duckdb::OCIDescriptorFree) pOCIDescriptorFree = nullptr;
	decltype(&::duckdb::OCIAttrSet) pOCIAttrSet = nullptr;
	decltype(&::duckdb::OCIAttrGet) pOCIAttrGet = nullptr;
	decltype(&::duckdb::OCIServerAttach) pOCIServerAttach = nullptr;
	decltype(&::duckdb::OCIServerDetach) pOCIServerDetach = nullptr;
	decltype(&::duckdb::OCISessionBegin) pOCISessionBegin = nullptr;
	decltype(&::duckdb::OCISessionEnd) pOCISessionEnd = nullptr;
	decltype(&::duckdb::OCIPing) pOCIPing = nullptr;
	decltype(&::duckdb::OCIStmtPrepare) pOCIStmtPrepare = nullptr;
	decltype(&::duckdb::OCIStmtExecute) pOCIStmtExecute = nullptr;
	decltype(&::duckdb::OCIStmtFetch2) pOCIStmtFetch2 = nullptr;
	decltype(&::duckdb::OCIParamGet) pOCIParamGet = nullptr;
	decltype(&::duckdb::OCIDefineByPos) pOCIDefineByPos = nullptr;
	decltype(&::duckdb::OCIDefineArrayOfStruct) pOCIDefineArrayOfStruct = nullptr;
	decltype(&::duckdb::OCIBindByPos) pOCIBindByPos = nullptr;
	decltype(&::duckdb::OCIBindArrayOfStruct) pOCIBindArrayOfStruct = nullptr;
	decltype(&::duckdb::OCITransCommit) pOCITransCommit = nullptr;
	decltype(&::duckdb::OCITransRollback) pOCITransRollback = nullptr;
	decltype(&::duckdb::OCIErrorGet) pOCIErrorGet = nullptr;
};

static OCIFunctionTable g_oci;
static std::atomic<bool> g_oci_loaded {false};
static std::mutex g_oci_load_mutex;
static void *g_oci_handle = nullptr;

static void *TryOpenSharedLibrary(const std::string &path) {
#ifdef _WIN32
	return reinterpret_cast<void *>(LoadLibraryA(path.c_str()));
#else
	return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
}

static void CloseSharedLibrary(void *handle) {
	if (!handle) {
		return;
	}
#ifdef _WIN32
	FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
	dlclose(handle);
#endif
}

template <typename FnPtr>
static FnPtr ResolveOCISymbol(void *handle, const char *symbol_name) {
#ifdef _WIN32
	auto raw = reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(handle), symbol_name));
#else
	auto raw = dlsym(handle, symbol_name);
#endif
	if (!raw) {
		CloseSharedLibrary(handle);
		throw IOException("Oracle Instant Client library is missing required OCI symbol '%s'", symbol_name);
	}
	return reinterpret_cast<FnPtr>(raw);
}

static std::vector<std::string> GetOCICandidateNames() {
#if defined(_WIN32)
	return {"oci.dll"};
#elif defined(__APPLE__)
	return {"libclntsh.dylib", "libclntsh.dylib.23.1", "libclntsh.dylib.21.1", "libclntsh.dylib.19.1"};
#else
	return {"libclntsh.so", "libclntsh.so.23.1", "libclntsh.so.21.1", "libclntsh.so.19.1", "libclntsh.so.18.1"};
#endif
}

static std::string JoinPath(const std::string &dir, const std::string &file) {
	if (dir.empty()) {
		return file;
	}
#ifdef _WIN32
	char sep = '\\';
#else
	char sep = '/';
#endif
	if (dir.back() == '/' || dir.back() == '\\') {
		return dir + file;
	}
	return dir + sep + file;
}

} // namespace

bool IsOCILoaded() noexcept {
	return g_oci_loaded.load(std::memory_order_acquire);
}

void EnsureOCILoaded() {
	if (g_oci_loaded.load(std::memory_order_acquire)) {
		return;
	}
	std::lock_guard<std::mutex> lock(g_oci_load_mutex);
	if (g_oci_loaded.load(std::memory_order_relaxed)) {
		return;
	}

	auto candidates = GetOCICandidateNames();
	void *handle = nullptr;

	const char *oci_lib_dir = std::getenv("OCI_LIB_DIR");
	if (oci_lib_dir && oci_lib_dir[0] != '\0') {
		for (const auto &name : candidates) {
			handle = TryOpenSharedLibrary(JoinPath(oci_lib_dir, name));
			if (handle) {
				break;
			}
		}
	}

	if (!handle) {
		const char *oracle_home = std::getenv("ORACLE_HOME");
		if (oracle_home && oracle_home[0] != '\0') {
			for (const auto &name : candidates) {
				handle = TryOpenSharedLibrary(JoinPath(JoinPath(oracle_home, "lib"), name));
				if (handle) {
					break;
				}
				handle = TryOpenSharedLibrary(JoinPath(oracle_home, name));
				if (handle) {
					break;
				}
			}
		}
	}

	if (!handle) {
		for (const auto &name : candidates) {
			handle = TryOpenSharedLibrary(name);
			if (handle) {
				break;
			}
		}
	}

	if (!handle) {
		throw IOException("Oracle Instant Client shared library (libclntsh.so / libclntsh.dylib / oci.dll) could not "
		                  "be loaded. Install Oracle Instant Client Basic (19c, 21c, or 23ai) and set OCI_LIB_DIR, "
		                  "ORACLE_HOME, or LD_LIBRARY_PATH / DYLD_LIBRARY_PATH / PATH.");
	}

	OCIFunctionTable resolved;
	resolved.pOCIEnvCreate = ResolveOCISymbol<decltype(resolved.pOCIEnvCreate)>(handle, "OCIEnvCreate");
	resolved.pOCIHandleAlloc = ResolveOCISymbol<decltype(resolved.pOCIHandleAlloc)>(handle, "OCIHandleAlloc");
	resolved.pOCIHandleFree = ResolveOCISymbol<decltype(resolved.pOCIHandleFree)>(handle, "OCIHandleFree");
	resolved.pOCIDescriptorFree = ResolveOCISymbol<decltype(resolved.pOCIDescriptorFree)>(handle, "OCIDescriptorFree");
	resolved.pOCIAttrSet = ResolveOCISymbol<decltype(resolved.pOCIAttrSet)>(handle, "OCIAttrSet");
	resolved.pOCIAttrGet = ResolveOCISymbol<decltype(resolved.pOCIAttrGet)>(handle, "OCIAttrGet");
	resolved.pOCIServerAttach = ResolveOCISymbol<decltype(resolved.pOCIServerAttach)>(handle, "OCIServerAttach");
	resolved.pOCIServerDetach = ResolveOCISymbol<decltype(resolved.pOCIServerDetach)>(handle, "OCIServerDetach");
	resolved.pOCISessionBegin = ResolveOCISymbol<decltype(resolved.pOCISessionBegin)>(handle, "OCISessionBegin");
	resolved.pOCISessionEnd = ResolveOCISymbol<decltype(resolved.pOCISessionEnd)>(handle, "OCISessionEnd");
	resolved.pOCIPing = ResolveOCISymbol<decltype(resolved.pOCIPing)>(handle, "OCIPing");
	resolved.pOCIStmtPrepare = ResolveOCISymbol<decltype(resolved.pOCIStmtPrepare)>(handle, "OCIStmtPrepare");
	resolved.pOCIStmtExecute = ResolveOCISymbol<decltype(resolved.pOCIStmtExecute)>(handle, "OCIStmtExecute");
	resolved.pOCIStmtFetch2 = ResolveOCISymbol<decltype(resolved.pOCIStmtFetch2)>(handle, "OCIStmtFetch2");
	resolved.pOCIParamGet = ResolveOCISymbol<decltype(resolved.pOCIParamGet)>(handle, "OCIParamGet");
	resolved.pOCIDefineByPos = ResolveOCISymbol<decltype(resolved.pOCIDefineByPos)>(handle, "OCIDefineByPos");
	resolved.pOCIDefineArrayOfStruct =
	    ResolveOCISymbol<decltype(resolved.pOCIDefineArrayOfStruct)>(handle, "OCIDefineArrayOfStruct");
	resolved.pOCIBindByPos = ResolveOCISymbol<decltype(resolved.pOCIBindByPos)>(handle, "OCIBindByPos");
	resolved.pOCIBindArrayOfStruct =
	    ResolveOCISymbol<decltype(resolved.pOCIBindArrayOfStruct)>(handle, "OCIBindArrayOfStruct");
	resolved.pOCITransCommit = ResolveOCISymbol<decltype(resolved.pOCITransCommit)>(handle, "OCITransCommit");
	resolved.pOCITransRollback = ResolveOCISymbol<decltype(resolved.pOCITransRollback)>(handle, "OCITransRollback");
	resolved.pOCIErrorGet = ResolveOCISymbol<decltype(resolved.pOCIErrorGet)>(handle, "OCIErrorGet");

	g_oci = resolved;
	g_oci_handle = handle;
	g_oci_loaded.store(true, std::memory_order_release);
}

sword OCIEnvCreate(OCIEnv **envp, ub4 mode, dvoid *ctxp, dvoid *(*malocfp)(dvoid *ctxp, size_t size),
                   dvoid *(*ralocfp)(dvoid *ctxp, dvoid *memptr, size_t newsize),
                   void (*mfreefp)(dvoid *ctxp, dvoid *memptr), size_t xtramem_sz, dvoid **usrmempp) {
	EnsureOCILoaded();
	return g_oci.pOCIEnvCreate(envp, mode, ctxp, malocfp, ralocfp, mfreefp, xtramem_sz, usrmempp);
}

sword OCIHandleAlloc(const dvoid *parenth, dvoid **hndlpp, const ub4 type, const size_t xtramem_sz, dvoid **usrmempp) {
	EnsureOCILoaded();
	return g_oci.pOCIHandleAlloc(parenth, hndlpp, type, xtramem_sz, usrmempp);
}

sword OCIHandleFree(dvoid *hndlp, const ub4 type) {
	if (!IsOCILoaded()) {
		return OCI_SUCCESS;
	}
	return g_oci.pOCIHandleFree(hndlp, type);
}

sword OCIDescriptorFree(dvoid *descp, const ub4 type) {
	if (!IsOCILoaded()) {
		return OCI_SUCCESS;
	}
	return g_oci.pOCIDescriptorFree(descp, type);
}

sword OCIAttrSet(dvoid *trgthndlp, ub4 trghndltyp, dvoid *attributep, ub4 size, ub4 attrtype, OCIError *errhp) {
	EnsureOCILoaded();
	return g_oci.pOCIAttrSet(trgthndlp, trghndltyp, attributep, size, attrtype, errhp);
}

sword OCIAttrGet(const dvoid *trgthndlp, ub4 trghndltyp, dvoid *attributep, ub4 *sizep, ub4 attrtype, OCIError *errhp) {
	EnsureOCILoaded();
	return g_oci.pOCIAttrGet(trgthndlp, trghndltyp, attributep, sizep, attrtype, errhp);
}

sword OCIServerAttach(OCIServer *srvhp, OCIError *errhp, const OraText *dblink, sb4 dblink_len, ub4 mode) {
	EnsureOCILoaded();
	return g_oci.pOCIServerAttach(srvhp, errhp, dblink, dblink_len, mode);
}

sword OCIServerDetach(OCIServer *srvhp, OCIError *errhp, ub4 mode) {
	if (!IsOCILoaded()) {
		return OCI_SUCCESS;
	}
	return g_oci.pOCIServerDetach(srvhp, errhp, mode);
}

sword OCISessionBegin(OCISvcCtx *svchp, OCIError *errhp, OCISession *usrhp, ub4 credt, ub4 mode) {
	EnsureOCILoaded();
	return g_oci.pOCISessionBegin(svchp, errhp, usrhp, credt, mode);
}

sword OCISessionEnd(OCISvcCtx *svchp, OCIError *errhp, OCISession *usrhp, ub4 mode) {
	if (!IsOCILoaded()) {
		return OCI_SUCCESS;
	}
	return g_oci.pOCISessionEnd(svchp, errhp, usrhp, mode);
}

sword OCIPing(OCISvcCtx *svchp, OCIError *errhp, ub4 mode) {
	EnsureOCILoaded();
	return g_oci.pOCIPing(svchp, errhp, mode);
}

sword OCIStmtPrepare(OCIStmt *stmtp, OCIError *errhp, const OraText *stmt, ub4 stmt_len, ub4 language, ub4 mode) {
	EnsureOCILoaded();
	return g_oci.pOCIStmtPrepare(stmtp, errhp, stmt, stmt_len, language, mode);
}

sword OCIStmtExecute(OCISvcCtx *svchp, OCIStmt *stmtp, OCIError *errhp, ub4 iters, ub4 rowoff, const void *snap_in,
                     void *snap_out, ub4 mode) {
	EnsureOCILoaded();
	return g_oci.pOCIStmtExecute(svchp, stmtp, errhp, iters, rowoff, snap_in, snap_out, mode);
}

sword OCIStmtFetch2(OCIStmt *stmtp, OCIError *errhp, ub4 nrows, ub2 orientation, sb4 scrollOffset, ub4 mode) {
	EnsureOCILoaded();
	return g_oci.pOCIStmtFetch2(stmtp, errhp, nrows, orientation, scrollOffset, mode);
}

sword OCIParamGet(const dvoid *hndlp, ub4 htype, OCIError *errhp, dvoid **parmdpp, ub4 pos) {
	EnsureOCILoaded();
	return g_oci.pOCIParamGet(hndlp, htype, errhp, parmdpp, pos);
}

sword OCIDefineByPos(OCIStmt *stmtp, OCIDefine **defnp, OCIError *errhp, ub4 position, dvoid *valuep, sb4 value_sz,
                     ub2 dty, dvoid *indp, ub2 *rlenp, ub2 *rcodep, ub4 mode) {
	EnsureOCILoaded();
	return g_oci.pOCIDefineByPos(stmtp, defnp, errhp, position, valuep, value_sz, dty, indp, rlenp, rcodep, mode);
}

sword OCIDefineArrayOfStruct(OCIDefine *defnp, OCIError *errhp, ub4 pvskip, ub4 indskip, ub4 rlskip, ub4 rcskip) {
	EnsureOCILoaded();
	return g_oci.pOCIDefineArrayOfStruct(defnp, errhp, pvskip, indskip, rlskip, rcskip);
}

sword OCIBindByPos(OCIStmt *stmtp, OCIBind **bindpp, OCIError *errhp, ub4 position, dvoid *valuep, sb4 value_sz,
                   ub2 dty, dvoid *indp, ub2 *alenp, ub2 *rcodep, ub4 maxarr_len, ub4 *curelep, ub4 mode) {
	EnsureOCILoaded();
	return g_oci.pOCIBindByPos(stmtp, bindpp, errhp, position, valuep, value_sz, dty, indp, alenp, rcodep, maxarr_len,
	                           curelep, mode);
}

sword OCIBindArrayOfStruct(OCIBind *bindp, OCIError *errhp, ub4 pvskip, ub4 indskip, ub4 alskip, ub4 rcskip) {
	EnsureOCILoaded();
	return g_oci.pOCIBindArrayOfStruct(bindp, errhp, pvskip, indskip, alskip, rcskip);
}

sword OCITransCommit(OCISvcCtx *svchp, OCIError *errhp, ub4 flags) {
	EnsureOCILoaded();
	return g_oci.pOCITransCommit(svchp, errhp, flags);
}

sword OCITransRollback(OCISvcCtx *svchp, OCIError *errhp, ub4 flags) {
	if (!IsOCILoaded()) {
		return OCI_SUCCESS;
	}
	return g_oci.pOCITransRollback(svchp, errhp, flags);
}

sword OCIErrorGet(dvoid *hndlp, ub4 recordno, OraText *sqlstate, sb4 *errcodep, OraText *bufp, ub4 bufsiz, ub4 type) {
	EnsureOCILoaded();
	return g_oci.pOCIErrorGet(hndlp, recordno, sqlstate, errcodep, bufp, bufsiz, type);
}

} // namespace duckdb
