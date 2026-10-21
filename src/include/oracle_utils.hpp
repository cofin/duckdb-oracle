#pragma once

#include "duckdb/common/exception.hpp"
#include "oracle_oci_api.hpp"
#include <cstdlib>
#include <memory>
#include <string>

namespace duckdb {

inline int SetOracleEnvVar(const char *name, const char *value) {
#ifdef _WIN32
	std::string entry = std::string(name ? name : "") + "=" + std::string(value ? value : "");
	return _putenv(entry.c_str());
#else
	return setenv(name, value ? value : "", 1);
#endif
}

inline int UnsetOracleEnvVar(const char *name) {
#ifdef _WIN32
	return _putenv_s(name, "");
#else
	return unsetenv(name);
#endif
}

inline std::string RedactOracleConnectionString(const std::string &conn_str) {
	auto slash_pos = conn_str.find('/');
	auto at_pos = conn_str.rfind('@');
	if (slash_pos == std::string::npos || at_pos == std::string::npos || slash_pos >= at_pos) {
		return conn_str;
	}
	if (slash_pos == 0 && at_pos == 1) {
		return conn_str;
	}
	return conn_str.substr(0, slash_pos + 1) + "***" + conn_str.substr(at_pos);
}

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

inline bool OCIErrorCodeMatches(OCIError *errhp, sb4 expected_errcode) {
	if (!errhp) {
		return false;
	}
	text errbuf[512] = {0};
	sb4 errcode = 0;
	sword rc = OCIErrorGet(reinterpret_cast<dvoid *>(errhp), (ub4)1, nullptr, &errcode, errbuf, (ub4)sizeof(errbuf),
	                       OCI_HTYPE_ERROR);
	return rc == OCI_SUCCESS && errcode == expected_errcode;
}

inline void CheckOCIError(sword status, OCIError *errhp, const std::string &msg) {
	if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
		if (status == OCI_INVALID_HANDLE || !errhp) {
			throw IOException("%s: Invalid OCI handle (status=%d)", msg, static_cast<int>(status));
		}
		text errbuf[512] = {0};
		sb4 errcode = 0;
		sword get_rc = OCIErrorGet(reinterpret_cast<dvoid *>(errhp), (ub4)1, nullptr, &errcode, errbuf,
		                           (ub4)sizeof(errbuf), OCI_HTYPE_ERROR);
		if (get_rc != OCI_SUCCESS) {
			throw IOException("%s: OCI call failed with status %d (OCIErrorGet returned %d)", msg,
			                  static_cast<int>(status), static_cast<int>(get_rc));
		}
		throw IOException("%s: %s (Error code: %d)", msg, std::string(reinterpret_cast<char *>(errbuf)), errcode);
	}
}

inline void CheckOCIOptionalAttributeORA24315(sword status, OCIError *errhp, const std::string &msg) {
	if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO) {
		return;
	}
	// OCI 23.x Instant Client can reject timeout attrs on server pre-attach and statement handles with ORA-24315.
	// Keep only that compatibility case optional; every other failure remains fatal.
	if (OCIErrorCodeMatches(errhp, 24315)) {
		return;
	}
	CheckOCIError(status, errhp, msg);
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

inline bool HasDuplicateColumnNames(const vector<string> &names) {
	for (idx_t i = 0; i < names.size(); i++) {
		for (idx_t j = i + 1; j < names.size(); j++) {
			if (names[i] == names[j]) {
				return true;
			}
		}
	}
	return false;
}

} // namespace duckdb
