#pragma once

#include "duckdb/common/exception.hpp"
#include <oci.h>
#include <memory>
#include <string>

#ifdef _WIN32
#include <cstdlib>
static inline int setenv(const char *name, const char *value, int overwrite) {
	if (!overwrite && getenv(name) != nullptr) {
		return 0;
	}
	return _putenv_s(name, value);
}

static inline int unsetenv(const char *name) {
	return _putenv_s(name, "");
}
#endif

namespace duckdb {

struct OCIHandleFreeDeleter {
	OCIHandleFreeDeleter() : handle_type(0) {
	}

	explicit OCIHandleFreeDeleter(ub4 handle_type) : handle_type(handle_type) {
	}

	ub4 handle_type;

	void operator()(void *handle) const {
		if (handle) {
			OCIHandleFree(handle, handle_type);
		}
	}
};

template <class T>
using OCIHandlePtr = std::unique_ptr<T, OCIHandleFreeDeleter>;

struct OCIDescriptorFreeDeleter {
	OCIDescriptorFreeDeleter() : descriptor_type(0) {
	}

	explicit OCIDescriptorFreeDeleter(ub4 descriptor_type) : descriptor_type(descriptor_type) {
	}

	ub4 descriptor_type;

	void operator()(void *descriptor) const {
		if (descriptor) {
			OCIDescriptorFree(descriptor, descriptor_type);
		}
	}
};

template <class T>
using OCIDescriptorPtr = std::unique_ptr<T, OCIDescriptorFreeDeleter>;

inline void CheckOCIError(sword status, OCIError *errhp, const std::string &msg) {
	if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO) {
		return;
	}
	text errbuf[512];
	sb4 errcode = 0;
	if (errhp) {
		OCIErrorGet(reinterpret_cast<dvoid *>(errhp), (ub4)1, nullptr, &errcode, errbuf, (ub4)sizeof(errbuf),
		            OCI_HTYPE_ERROR);
		throw IOException(msg + ": " + std::string(reinterpret_cast<char *>(errbuf)));
	}
	throw IOException(msg + ": (No Error Handle)");
}

inline OCIHandlePtr<OCIStmt> AllocateOCIStatement(OCIEnv *envhp, OCIError *errhp, const std::string &msg) {
	OCIStmt *stmt = nullptr;
	CheckOCIError(OCIHandleAlloc(envhp, reinterpret_cast<dvoid **>(&stmt), OCI_HTYPE_STMT, 0, nullptr), errhp, msg);
	return OCIHandlePtr<OCIStmt>(stmt, OCIHandleFreeDeleter {OCI_HTYPE_STMT});
}

inline std::shared_ptr<OCIStmt> AllocateSharedOCIStatement(OCIEnv *envhp, OCIError *errhp, const std::string &msg) {
	auto stmt = AllocateOCIStatement(envhp, errhp, msg);
	return std::shared_ptr<OCIStmt>(stmt.release(), [](OCIStmt *ptr) {
		if (ptr) {
			OCIHandleFree(ptr, OCI_HTYPE_STMT);
		}
	});
}

} // namespace duckdb
