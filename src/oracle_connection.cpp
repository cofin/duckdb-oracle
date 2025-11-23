#include "oracle_connection.hpp"

#include "duckdb/common/string_util.hpp"
#include <cstring>
#include <functional>

namespace duckdb {

static std::string OCIErrorToString(OCIError *errhp, const std::string &msg) {
	text errbuf[512];
	sb4 errcode = 0;
	if (errhp) {
		OCIErrorGet((dvoid *)errhp, (ub4)1, (text *)NULL, &errcode, errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR);
		return msg + ": " + std::string((char *)errbuf);
	}
	return msg + ": (No Error Handle)";
}

OracleConnection::OracleConnection() {
}

OracleConnection::~OracleConnection() {
	Reset();
}

void OracleConnection::Reset() {
	if (svchp) {
		if (connected && errhp) {
			// Only log off when a session was successfully established.
			OCILogoff(svchp, errhp);
		} else {
			// Service context was allocated but no session was created; free the handle directly.
			OCIHandleFree(svchp, OCI_HTYPE_SVCCTX);
		}
	}
	if (envhp) {
		OCIHandleFree(envhp, OCI_HTYPE_ENV);
	}
	if (errhp) {
		OCIHandleFree(errhp, OCI_HTYPE_ERROR);
	}
	envhp = nullptr;
	errhp = nullptr;
	svchp = nullptr;
	connected = false;
}

void OracleConnection::CheckError(sword status, const std::string &msg) {
	if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO) {
		return;
	}
	throw IOException(OCIErrorToString(errhp, msg));
}

void OracleConnection::Connect(const std::string &connection_string) {
	if (connected) {
		return;
	}

	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] raw connection: %s\n", connection_string.c_str());
	}
	// Parse EZConnect style "user/password@db" into separate pieces for OCILogon.
	// OCI does not understand credentials embedded in the dbname parameter.
	auto slash_pos = connection_string.find('/');
	auto at_pos = connection_string.find('@', slash_pos == string::npos ? 0 : slash_pos);
	if (slash_pos == string::npos || at_pos == string::npos || slash_pos == 0 || at_pos <= slash_pos + 1 ||
	    at_pos == connection_string.size() - 1) {
		throw IOException("Invalid Oracle connection string. Expected user/password@connect_identifier");
	}
	auto user = connection_string.substr(0, slash_pos);
	auto password = connection_string.substr(slash_pos + 1, at_pos - slash_pos - 1);
	auto db = connection_string.substr(at_pos + 1);
	if (getenv("ORACLE_DEBUG")) {
		fprintf(stderr, "[oracle] Parsed connection: user=%s db=%s\n", user.c_str(), db.c_str());
	}

	CheckError(OCIEnvCreate(&envhp, OCI_DEFAULT, nullptr, nullptr, nullptr, nullptr, 0, nullptr), "OCIEnvCreate");
	CheckError(OCIHandleAlloc(envhp, (dvoid **)&errhp, OCI_HTYPE_ERROR, 0, nullptr), "OCIHandleAlloc errhp");
	CheckError(OCIHandleAlloc(envhp, (dvoid **)&svchp, OCI_HTYPE_SVCCTX, 0, nullptr), "OCIHandleAlloc svchp");

	auto status = OCILogon(envhp, errhp, &svchp, (OraText *)user.c_str(), user.size(), (OraText *)password.c_str(),
	                       password.size(), (OraText *)db.c_str(), db.size());
	CheckError(status, "OCILogon");
	connected = true;

	// Set timeouts to prevent infinite hangs
	ub4 timeout = 30000; // 30 seconds
	OCIServer *server_handle = nullptr;
	CheckError(OCIAttrGet(svchp, OCI_HTYPE_SVCCTX, &server_handle, 0, OCI_ATTR_SERVER, errhp), "OCIAttrGet SERVER");
	// Set call timeout (if supported by client/server)
	OCIAttrSet(server_handle, OCI_HTYPE_SERVER, &timeout, 0, OCI_ATTR_CALL_TIMEOUT, errhp);
	// We ignore errors here as some older clients/servers might not support it
}

bool OracleConnection::IsConnected() const {
	return connected;
}

OracleResult OracleConnection::Query(const std::string &query) {
	if (!connected) {
		throw IOException("OracleConnection::Query called before Connect");
	}

	OCIStmt *stmthp = nullptr;
	CheckError(OCIHandleAlloc(envhp, (dvoid **)&stmthp, OCI_HTYPE_STMT, 0, nullptr), "OCIHandleAlloc stmthp");

	auto cleanup_stmt = std::unique_ptr<OCIStmt, std::function<void(OCIStmt *)>>(
	    stmthp, [&](OCIStmt *stmt) { OCIHandleFree(stmt, OCI_HTYPE_STMT); });

	CheckError(OCIStmtPrepare(stmthp, errhp, (OraText *)query.c_str(), query.size(), OCI_NTV_SYNTAX, OCI_DEFAULT),
	           "OCIStmtPrepare");
	CheckError(OCIStmtExecute(svchp, stmthp, errhp, 0, 0, nullptr, nullptr, OCI_DESCRIBE_ONLY),
	           "OCIStmtExecute describe");

	ub4 param_count = 0;
	CheckError(OCIAttrGet(stmthp, OCI_HTYPE_STMT, &param_count, 0, OCI_ATTR_PARAM_COUNT, errhp),
	           "OCI_ATTR_PARAM_COUNT");

	OracleResult result;
	for (ub4 i = 1; i <= param_count; i++) {
		OCIParam *param = nullptr;
		CheckError(OCIParamGet(stmthp, OCI_HTYPE_STMT, errhp, (dvoid **)&param, i), "OCIParamGet");

		OraText *col_name;
		ub4 col_name_len = 0;
		CheckError(OCIAttrGet(param, OCI_DTYPE_PARAM, &col_name, &col_name_len, OCI_ATTR_NAME, errhp), "OCI_ATTR_NAME");
		result.columns.emplace_back((char *)col_name, col_name_len);
	}

	// Re-execute to fetch rows
	CheckError(OCIStmtExecute(svchp, stmthp, errhp, 0, 0, nullptr, nullptr, OCI_DEFAULT), "OCIStmtExecute fetch");

	// Bind all columns as strings for metadata discovery.
	std::vector<std::vector<char>> buffers(param_count);
	std::vector<ub2> lengths(param_count);
	std::vector<sb2> indicators(param_count);
	std::vector<OCIDefine *> defines(param_count, nullptr);

	const ub4 buffer_size = 4096;
	for (ub4 i = 0; i < param_count; i++) {
		buffers[i].resize(buffer_size);
		CheckError(OCIDefineByPos(stmthp, &defines[i], errhp, i + 1, buffers[i].data(), buffer_size, SQLT_STR,
		                          &indicators[i], &lengths[i], nullptr, OCI_DEFAULT),
		           "OCIDefineByPos");
	}

	while (true) {
		auto status = OCIStmtFetch2(stmthp, errhp, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT);
		if (status == OCI_NO_DATA) {
			break;
		}
		CheckError(status, "OCIStmtFetch2");

		std::vector<std::string> row;
		row.reserve(param_count);
		for (ub4 i = 0; i < param_count; i++) {
			if (indicators[i] == -1) {
				row.emplace_back();
			} else {
				row.emplace_back(buffers[i].data(), lengths[i]);
			}
		}
		result.rows.push_back(std::move(row));
	}

	return result;
}

std::string OracleResult::GetString(idx_t row, idx_t col) const {
	if (row >= rows.size() || col >= rows[row].size()) {
		throw InternalException("OracleResult index out of range");
	}
	return rows[row][col];
}

int64_t OracleResult::GetInt64(idx_t row, idx_t col) const {
	return std::stoll(GetString(row, col));
}

double OracleResult::GetDouble(idx_t row, idx_t col) const {
	return std::stod(GetString(row, col));
}

} // namespace duckdb
