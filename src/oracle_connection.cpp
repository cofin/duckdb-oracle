#include "oracle_connection.hpp"
#include "oracle_utils.hpp"
#include "duckdb/common/string_util.hpp"

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

OracleResult OracleConnection::Query(const std::string &query) {
	if (!conn_handle) {
		throw IOException("OracleConnection::Query called before Connect");
	}
	return QueryWithStringBinds(query, {});
}

OracleResult OracleConnection::QueryWithStringBinds(const std::string &query,
                                                    const std::vector<std::string> &bind_values) {
	if (!conn_handle) {
		throw IOException("OracleConnection::QueryWithStringBinds called before Connect");
	}

	auto ctx = conn_handle->Get();
	auto stmthp = AllocateOCIStatement(ctx->envhp, ctx->errhp, "OCIHandleAlloc stmthp");

	CheckOCIError(OCIStmtPrepare(stmthp.get(), ctx->errhp, reinterpret_cast<const OraText *>(query.c_str()),
	                             query.size(), OCI_NTV_SYNTAX, OCI_DEFAULT),
	              ctx->errhp, "OCIStmtPrepare");

	std::vector<OCIBind *> bind_handles(bind_values.size(), nullptr);
	std::vector<sb2> bind_indicators(bind_values.size(), 0);
	std::vector<std::vector<char>> bind_storage(bind_values.size());
	for (idx_t i = 0; i < bind_values.size(); i++) {
		const auto &bind_value = bind_values[i];
		bind_indicators[i] = bind_value.empty() ? static_cast<sb2>(-1) : static_cast<sb2>(0);
		bind_storage[i].assign(bind_value.begin(), bind_value.end());
		bind_storage[i].push_back('\0');
		sb4 bind_len = bind_value.empty() ? static_cast<sb4>(1) : static_cast<sb4>(bind_value.size());
		CheckOCIError(OCIBindByPos(stmthp.get(), &bind_handles[i], ctx->errhp, static_cast<ub4>(i + 1),
		                           bind_storage[i].data(), bind_len, SQLT_CHR, &bind_indicators[i], nullptr, nullptr, 0,
		                           nullptr, OCI_DEFAULT),
		              ctx->errhp, "OCIBindByPos metadata string bind " + std::to_string(i + 1));
	}

	ub4 prefetch_rows = 256;
	CheckOCIError(OCIAttrSet(stmthp.get(), OCI_HTYPE_STMT, &prefetch_rows, 0, OCI_ATTR_PREFETCH_ROWS, ctx->errhp),
	              ctx->errhp, "OCIAttrSet OCI_ATTR_PREFETCH_ROWS");

	CheckOCIError(OCIStmtExecute(ctx->svchp, stmthp.get(), ctx->errhp, 0, 0, nullptr, nullptr, OCI_DEFAULT), ctx->errhp,
	              "OCIStmtExecute fetch");

	ub4 param_count = 0;
	CheckOCIError(OCIAttrGet(stmthp.get(), OCI_HTYPE_STMT, &param_count, nullptr, OCI_ATTR_PARAM_COUNT, ctx->errhp),
	              ctx->errhp, "OCI_ATTR_PARAM_COUNT");

	OracleResult result;
	std::vector<std::vector<char>> buffers(param_count);
	std::vector<ub2> lengths(param_count);
	std::vector<sb2> indicators(param_count);
	std::vector<OCIDefine *> defines(param_count, nullptr);

	for (ub4 i = 1; i <= param_count; i++) {
		OCIParam *param_raw = nullptr;
		CheckOCIError(OCIParamGet(stmthp.get(), OCI_HTYPE_STMT, ctx->errhp, reinterpret_cast<dvoid **>(&param_raw), i),
		              ctx->errhp, "OCIParamGet");
		OCIDescriptorPtr<OCIParam> param(param_raw, OCIDescriptorFreeDeleter {OCI_DTYPE_PARAM});

		OraText *col_name = nullptr;
		ub4 col_name_len = 0;
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, reinterpret_cast<dvoid *>(&col_name), &col_name_len,
		                         OCI_ATTR_NAME, ctx->errhp),
		              ctx->errhp, "OCI_ATTR_NAME");
		result.columns.emplace_back(col_name ? reinterpret_cast<const char *>(col_name) : "", col_name_len);

		ub2 data_type = 0;
		ub2 data_size = 0;
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &data_type, nullptr, OCI_ATTR_DATA_TYPE, ctx->errhp),
		              ctx->errhp, "OCI_ATTR_DATA_TYPE");
		CheckOCIError(OCIAttrGet(param.get(), OCI_DTYPE_PARAM, &data_size, nullptr, OCI_ATTR_DATA_SIZE, ctx->errhp),
		              ctx->errhp, "OCI_ATTR_DATA_SIZE");

		ub4 buf_size = (data_type == SQLT_CLOB || data_type == SQLT_BLOB || data_type == SQLT_LNG)
		                   ? 65535
		                   : MaxValue<ub4>(static_cast<ub4>(data_size) * 4 + 1, 4096);
		buf_size = MinValue<ub4>(buf_size, 65535);

		auto col_idx = i - 1;
		buffers[col_idx].resize(buf_size);
		CheckOCIError(OCIDefineByPos(stmthp.get(), &defines[col_idx], ctx->errhp, i, buffers[col_idx].data(),
		                             static_cast<sb4>(buf_size), SQLT_STR, &indicators[col_idx], &lengths[col_idx],
		                             nullptr, OCI_DEFAULT),
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
			} else if (indicators[i] == -2 || indicators[i] > 0) {
				throw IOException("Oracle metadata query column '%s' exceeded fetch buffer size (%u bytes)",
				                  result.columns[i], static_cast<unsigned>(buffers[i].size()));
			} else {
				row.emplace_back(buffers[i].data(), lengths[i]);
			}
		}
		if (status == OCI_SUCCESS_WITH_INFO && OCIErrorCodeMatches(ctx->errhp, 1406)) {
			throw IOException("Oracle metadata query column value was truncated (ORA-01406)");
		}
		result.rows.push_back(std::move(row));
	}

	return result;
}

} // namespace duckdb
