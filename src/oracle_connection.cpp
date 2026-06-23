#include "oracle_connection.hpp"
#include "oracle_utils.hpp"
#include "duckdb/common/string_util.hpp"
#include <cstring>
#include <cstdio>

namespace duckdb {

OracleConnection::OracleConnection() {
}

OracleConnection::~OracleConnection() {
	// Handle destructor releases connection back to pool automatically
}

void OracleConnection::Connect(const std::string &connection_string, const std::string &wallet_path,
                               const OracleSettings &settings) {
	if (conn_handle) {
		return;
	}
	conn_handle = OracleConnectionManager::Instance().Acquire(connection_string, wallet_path, settings);
}

bool OracleConnection::IsConnected() const {
	return conn_handle != nullptr;
}

OracleResult OracleConnection::Query(const std::string &query) {
	if (!conn_handle) {
		throw IOException("OracleConnection::Query called before Connect");
	}

	auto ctx = conn_handle->Get();

	auto stmthp = AllocateOCIStatement(ctx->envhp, ctx->errhp, "OCIHandleAlloc stmthp");

	CheckOCIError(
	    OCIStmtPrepare(stmthp.get(), ctx->errhp, (OraText *)query.c_str(), query.size(), OCI_NTV_SYNTAX, OCI_DEFAULT),
	    ctx->errhp, "OCIStmtPrepare");

	CheckOCIError(OCIStmtExecute(ctx->svchp, stmthp.get(), ctx->errhp, 0, 0, nullptr, nullptr, OCI_DESCRIBE_ONLY),
	              ctx->errhp, "OCIStmtExecute describe");

	ub4 param_count = 0;
	CheckOCIError(OCIAttrGet(stmthp.get(), OCI_HTYPE_STMT, &param_count, 0, OCI_ATTR_PARAM_COUNT, ctx->errhp),
	              ctx->errhp, "OCI_ATTR_PARAM_COUNT");

	OracleResult result;
	for (ub4 i = 1; i <= param_count; i++) {
		OCIParam *param_raw = nullptr;
		CheckOCIError(OCIParamGet(stmthp.get(), OCI_HTYPE_STMT, ctx->errhp, (dvoid **)&param_raw, i), ctx->errhp,
		              "OCIParamGet");
		OCIDescriptorPtr<OCIParam> param(param_raw, OCIDescriptorFreeDeleter {OCI_DTYPE_PARAM});

		OraText *col_name;
		ub4 col_name_len = 0;
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &col_name, &col_name_len, OCI_ATTR_NAME, ctx->errhp),
		              ctx->errhp, "OCI_ATTR_NAME");
		result.columns.emplace_back((char *)col_name, col_name_len);
	}

	// Re-execute to fetch rows
	CheckOCIError(OCIStmtExecute(ctx->svchp, stmthp.get(), ctx->errhp, 0, 0, nullptr, nullptr, OCI_DEFAULT), ctx->errhp,
	              "OCIStmtExecute fetch");

	// Bind all columns as strings
	std::vector<std::vector<char>> buffers(param_count);
	std::vector<ub2> lengths(param_count);
	std::vector<sb2> indicators(param_count);
	std::vector<OCIDefine *> defines(param_count, nullptr);

	const ub4 buffer_size = 4096;
	for (ub4 i = 0; i < param_count; i++) {
		buffers[i].resize(buffer_size);
		CheckOCIError(OCIDefineByPos(stmthp.get(), &defines[i], ctx->errhp, i + 1, buffers[i].data(), buffer_size,
		                             SQLT_STR, &indicators[i], &lengths[i], nullptr, OCI_DEFAULT),
		              ctx->errhp, "OCIDefineByPos");
	}

	idx_t rows = 0;
	while (true) {
		auto status = OCIStmtFetch2(stmthp.get(), ctx->errhp, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT);
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

OracleResult OracleConnection::QueryWithStringBinds(const std::string &query,
                                                    const std::vector<std::string> &bind_values) {
	if (!conn_handle) {
		throw IOException("OracleConnection::QueryWithStringBinds called before Connect");
	}

	auto ctx = conn_handle->Get();
	auto stmthp = AllocateOCIStatement(ctx->envhp, ctx->errhp, "OCIHandleAlloc stmthp");

	CheckOCIError(
	    OCIStmtPrepare(stmthp.get(), ctx->errhp, (OraText *)query.c_str(), query.size(), OCI_NTV_SYNTAX, OCI_DEFAULT),
	    ctx->errhp, "OCIStmtPrepare");

	std::vector<OCIBind *> bind_handles(bind_values.size(), nullptr);
	std::vector<sb2> bind_indicators(bind_values.size(), 0);
	for (idx_t i = 0; i < bind_values.size(); i++) {
		auto &bind_value = bind_values[i];
		CheckOCIError(OCIBindByPos(stmthp.get(), &bind_handles[i], ctx->errhp, i + 1,
		                           const_cast<char *>(bind_value.c_str()), static_cast<sb4>(bind_value.size()),
		                           SQLT_CHR, &bind_indicators[i], nullptr, nullptr, 0, nullptr, OCI_DEFAULT),
		              ctx->errhp, "OCIBindByPos metadata string bind " + std::to_string(i + 1));
	}

	CheckOCIError(OCIStmtExecute(ctx->svchp, stmthp.get(), ctx->errhp, 0, 0, nullptr, nullptr, OCI_DESCRIBE_ONLY),
	              ctx->errhp, "OCIStmtExecute describe");

	ub4 param_count = 0;
	CheckOCIError(OCIAttrGet(stmthp.get(), OCI_HTYPE_STMT, &param_count, 0, OCI_ATTR_PARAM_COUNT, ctx->errhp),
	              ctx->errhp, "OCI_ATTR_PARAM_COUNT");

	OracleResult result;
	for (ub4 i = 1; i <= param_count; i++) {
		OCIParam *param_raw = nullptr;
		CheckOCIError(OCIParamGet(stmthp.get(), OCI_HTYPE_STMT, ctx->errhp, (dvoid **)&param_raw, i), ctx->errhp,
		              "OCIParamGet");
		OCIDescriptorPtr<OCIParam> param(param_raw, OCIDescriptorFreeDeleter {OCI_DTYPE_PARAM});

		OraText *col_name;
		ub4 col_name_len = 0;
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &col_name, &col_name_len, OCI_ATTR_NAME, ctx->errhp),
		              ctx->errhp, "OCI_ATTR_NAME");
		result.columns.emplace_back((char *)col_name, col_name_len);
	}

	CheckOCIError(OCIStmtExecute(ctx->svchp, stmthp.get(), ctx->errhp, 0, 0, nullptr, nullptr, OCI_DEFAULT), ctx->errhp,
	              "OCIStmtExecute fetch");

	std::vector<std::vector<char>> buffers(param_count);
	std::vector<ub2> lengths(param_count);
	std::vector<sb2> indicators(param_count);
	std::vector<OCIDefine *> defines(param_count, nullptr);

	const ub4 buffer_size = 4096;
	for (ub4 i = 0; i < param_count; i++) {
		buffers[i].resize(buffer_size);
		CheckOCIError(OCIDefineByPos(stmthp.get(), &defines[i], ctx->errhp, i + 1, buffers[i].data(), buffer_size,
		                             SQLT_STR, &indicators[i], &lengths[i], nullptr, OCI_DEFAULT),
		              ctx->errhp, "OCIDefineByPos");
	}

	while (true) {
		auto status = OCIStmtFetch2(stmthp.get(), ctx->errhp, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT);
		if (status == OCI_NO_DATA) {
			break;
		}
		CheckOCIError(status, ctx->errhp, "OCIStmtFetch2");

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

void OracleConnection::Commit() {
	if (!conn_handle) {
		throw IOException("OracleConnection::Commit called before Connect");
	}
	auto ctx = conn_handle->Get();
	CheckOCIError(OCITransCommit(ctx->svchp, ctx->errhp, OCI_DEFAULT), ctx->errhp, "OCITransCommit");
}

void OracleConnection::Rollback() {
	if (!conn_handle) {
		throw IOException("OracleConnection::Rollback called before Connect");
	}
	auto ctx = conn_handle->Get();
	CheckOCIError(OCITransRollback(ctx->svchp, ctx->errhp, OCI_DEFAULT), ctx->errhp, "OCITransRollback");
}

} // namespace duckdb
