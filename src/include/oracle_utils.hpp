#pragma once

#include "duckdb/common/exception.hpp"
#include <oci.h>
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

} // namespace duckdb
