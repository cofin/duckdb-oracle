#include "oracle_connection.hpp"
#include "duckdb/common/string_util.hpp"
#include <cstring>
#include <functional>
#include <cstdio>

namespace duckdb {

// Static helper for error checking (duplicated from manager, but we need it here for query exec)
static void CheckOCIError(sword status, OCIError *errhp, const std::string &msg) {
	if (status == OCI_SUCCESS || status == OCI_SUCCESS_WITH_INFO) {
		return;
	}
	text errbuf[512];
	sb4 errcode = 0;
	if (errhp) {
		OCIErrorGet((dvoid *)errhp, (ub4)1, (text *)NULL, &errcode, errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR);
		throw IOException(msg + ": " + std::string((char *)errbuf));
	}
	throw IOException(msg + ": (No Error Handle)");
}

OracleConnection::OracleConnection() {
}

OracleConnection::~OracleConnection() {
	// Handle destructor releases connection back to pool automatically
}

void OracleConnection::Connect(const std::string &connection_string) {
	if (conn_handle) {
		return;
	}
	// Use default settings for catalog connections
	OracleSettings settings;
	conn_handle = OracleConnectionManager::Instance().Acquire(connection_string, settings);
}

bool OracleConnection::IsConnected() const {
	return conn_handle != nullptr;
}

OracleResult OracleConnection::Query(const std::string &query) {
	if (!conn_handle) {
		throw IOException("OracleConnection::Query called before Connect");
	}

	auto ctx = conn_handle->Get();

	OCIStmt *stmthp = nullptr;
	CheckOCIError(OCIHandleAlloc(ctx->envhp, (dvoid **)&stmthp, OCI_HTYPE_STMT, 0, nullptr), ctx->errhp,
	              "OCIHandleAlloc stmthp");

	auto cleanup_stmt = std::unique_ptr<OCIStmt, std::function<void(OCIStmt *)>>(
	    stmthp, [&](OCIStmt *stmt) { OCIHandleFree(stmt, OCI_HTYPE_STMT); });

	CheckOCIError(
	    OCIStmtPrepare(stmthp, ctx->errhp, (OraText *)query.c_str(), query.size(), OCI_NTV_SYNTAX, OCI_DEFAULT),
	    ctx->errhp, "OCIStmtPrepare");

	CheckOCIError(OCIStmtExecute(ctx->svchp, stmthp, ctx->errhp, 0, 0, nullptr, nullptr, OCI_DESCRIBE_ONLY), ctx->errhp,
	              "OCIStmtExecute describe");

	ub4 param_count = 0;
	CheckOCIError(OCIAttrGet(stmthp, OCI_HTYPE_STMT, &param_count, 0, OCI_ATTR_PARAM_COUNT, ctx->errhp), ctx->errhp,
	              "OCI_ATTR_PARAM_COUNT");

	OracleResult result;
	for (ub4 i = 1; i <= param_count; i++) {
		OCIParam *param = nullptr;
		CheckOCIError(OCIParamGet(stmthp, OCI_HTYPE_STMT, ctx->errhp, (dvoid **)&param, i), ctx->errhp, "OCIParamGet");

		OraText *col_name;
		ub4 col_name_len = 0;
		CheckOCIError(OCIAttrGet(param, OCI_DTYPE_PARAM, &col_name, &col_name_len, OCI_ATTR_NAME, ctx->errhp),
		              ctx->errhp, "OCI_ATTR_NAME");
		result.columns.emplace_back((char *)col_name, col_name_len);
	}

	// Re-execute to fetch rows
	CheckOCIError(OCIStmtExecute(ctx->svchp, stmthp, ctx->errhp, 0, 0, nullptr, nullptr, OCI_DEFAULT), ctx->errhp,
	              "OCIStmtExecute fetch");

	// Bind all columns as strings
	std::vector<std::vector<char>> buffers(param_count);
	std::vector<ub2> lengths(param_count);
	std::vector<sb2> indicators(param_count);
	std::vector<OCIDefine *> defines(param_count, nullptr);

	const ub4 buffer_size = 4096;
	for (ub4 i = 0; i < param_count; i++) {
		buffers[i].resize(buffer_size);
		CheckOCIError(OCIDefineByPos(stmthp, &defines[i], ctx->errhp, i + 1, buffers[i].data(), buffer_size, SQLT_STR,
		                             &indicators[i], &lengths[i], nullptr, OCI_DEFAULT),
		              ctx->errhp, "OCIDefineByPos");
	}

	idx_t rows = 0;
	while (true) {
		auto status = OCIStmtFetch2(stmthp, ctx->errhp, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT);
		if (status == OCI_NO_DATA) {
			break;
		}
		CheckOCIError(status, ctx->errhp, "OCIStmtFetch2");
		rows++;

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
