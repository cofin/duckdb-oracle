#define DUCKDB_EXTENSION_MAIN

#include "oracle_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include <oci.h>

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>

namespace duckdb {

struct OracleBindData : public FunctionData {
    OCIEnv *envhp;
    OCISvcCtx *svchp;
    OCIStmt *stmthp;
    OCIError *errhp;
    string query;

    ~OracleBindData() {
        if (stmthp) {
            OCIHandleFree(stmthp, OCI_HTYPE_STMT);
        }
        if (svchp) {
            OCILogoff(svchp, errhp);
        }
        if (envhp) {
            OCIHandleFree(envhp, OCI_HTYPE_ENV);
        }
        if (errhp) {
            OCIHandleFree(errhp, OCI_HTYPE_ERROR);
        }
    }

    unique_ptr<FunctionData> Copy() const override {
        auto copy = make_uniq<OracleBindData>();
        copy->query = query;
        return std::move(copy);
    }

    bool Equals(const FunctionData &other) const override {
        auto &other_bind_data = (const OracleBindData &)other;
        return query == other_bind_data.query;
    }
};

static std::unique_ptr<FunctionData> OracleQueryBind(ClientContext &context, TableFunctionBindInput &input,
                                                     std::vector<LogicalType> &return_types, std::vector<string> &names) {
    auto result = make_uniq<OracleBindData>();

    auto connection_string = input.inputs[0].GetValue<string>();
    result->query = input.inputs[1].GetValue<string>();

    if (OCIEnvCreate(&result->envhp, OCI_DEFAULT, nullptr, nullptr, nullptr, nullptr, 0, nullptr) != OCI_SUCCESS) {
        throw IOException("Failed to create OCI environment.");
    }

    if (OCIHandleAlloc(result->envhp, (dvoid **)&result->errhp, OCI_HTYPE_ERROR, 0, nullptr) != OCI_SUCCESS) {
        throw IOException("Failed to allocate OCI error handle.");
    }

    if (OCIHandleAlloc(result->envhp, (dvoid **)&result->svchp, OCI_HTYPE_SVCCTX, 0, nullptr) != OCI_SUCCESS) {
        throw IOException("Failed to allocate OCI service context handle.");
    }

    if (OCILogon(result->envhp, result->errhp, &result->svchp, (OraText *)"", 0, (OraText *)"", 0, (OraText *)connection_string.c_str(), connection_string.size()) != OCI_SUCCESS) {
        throw IOException("Failed to connect to Oracle.");
    }

    if (OCIHandleAlloc(result->envhp, (dvoid **)&result->stmthp, OCI_HTYPE_STMT, 0, nullptr) != OCI_SUCCESS) {
        throw IOException("Failed to allocate OCI statement handle.");
    }

    if (OCIStmtPrepare(result->stmthp, result->errhp, (OraText *)result->query.c_str(), result->query.size(), OCI_NTV_SYNTAX, OCI_DEFAULT) != OCI_SUCCESS) {
        throw IOException("Failed to prepare OCI statement.");
    }

    if (OCIStmtExecute(result->svchp, result->stmthp, result->errhp, 0, 0, nullptr, nullptr, OCI_DEFAULT) != OCI_SUCCESS) {
        throw IOException("Failed to execute OCI statement.");
    }

    ub4 param_count;
    if (OCIAttrGet(result->stmthp, OCI_HTYPE_STMT, &param_count, 0, OCI_ATTR_PARAM_COUNT, result->errhp) != OCI_SUCCESS) {
        throw IOException("Failed to get parameter count.");
    }

    for (ub4 i = 1; i <= param_count; i++) {
        OCIParam *param;
        if (OCIParamGet(result->stmthp, OCI_HTYPE_STMT, result->errhp, (dvoid **)&param, i) != OCI_SUCCESS) {
            throw IOException("Failed to get parameter handle.");
        }

        ub2 data_type;
        if (OCIAttrGet(param, OCI_DTYPE_PARAM, &data_type, 0, OCI_ATTR_DATA_TYPE, result->errhp) != OCI_SUCCESS) {
            throw IOException("Failed to get data type.");
        }

        OraText *col_name;
        ub4 col_name_len;
        if (OCIAttrGet(param, OCI_DTYPE_PARAM, &col_name, &col_name_len, OCI_ATTR_NAME, result->errhp) != OCI_SUCCESS) {
            throw IOException("Failed to get column name.");
        }

        names.emplace_back((char *)col_name, col_name_len);

        switch (data_type) {
        case SQLT_CHR:
        case SQLT_AFC:
            return_types.push_back(LogicalType::VARCHAR);
            break;
        case SQLT_NUM:
            return_types.push_back(LogicalType::DOUBLE);
            break;
        case SQLT_DATE:
            return_types.push_back(LogicalType::TIMESTAMP);
            break;
        default:
            throw IOException("Unsupported data type.");
        }
    }

    return std::move(result);
}

static void OracleQueryFunction(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
    auto &bind_data = (OracleBindData &)*data.bind_data;
    OCIDefine *defnp = nullptr;
    sword status;
    idx_t row_count = 0;

    for (ub4 i = 0; i < output.ColumnCount(); i++) {
        switch (output.GetTypes()[i].id()) {
        case LogicalTypeId::VARCHAR: {
            auto col_data = FlatVector::GetData<string_t>(output.data[i]);
            if (OCIDefineByPos(bind_data.stmthp, &defnp, bind_data.errhp, i + 1, col_data, STANDARD_VECTOR_SIZE, SQLT_CHR, nullptr, nullptr, nullptr, OCI_DEFAULT) != OCI_SUCCESS) {
                throw IOException("Failed to define column.");
            }
            break;
        }
        case LogicalTypeId::DOUBLE: {
            auto col_data = FlatVector::GetData<double>(output.data[i]);
            if (OCIDefineByPos(bind_data.stmthp, &defnp, bind_data.errhp, i + 1, col_data, sizeof(double), SQLT_FLT, nullptr, nullptr, nullptr, OCI_DEFAULT) != OCI_SUCCESS) {
                throw IOException("Failed to define column.");
            }
            break;
        }
        case LogicalTypeId::TIMESTAMP: {
            auto col_data = FlatVector::GetData<timestamp_t>(output.data[i]);
            if (OCIDefineByPos(bind_data.stmthp, &defnp, bind_data.errhp, i + 1, col_data, sizeof(timestamp_t), SQLT_TIMESTAMP, nullptr, nullptr, nullptr, OCI_DEFAULT) != OCI_SUCCESS) {
                throw IOException("Failed to define column.");
            }
            break;
        }
        default:
            throw IOException("Unsupported data type.");
        }
    }

    while ((status = OCIStmtFetch2(bind_data.stmthp, bind_data.errhp, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT)) == OCI_SUCCESS) {
        row_count++;
        if (row_count == STANDARD_VECTOR_SIZE) {
            output.SetCardinality(row_count);
            return;
        }
    }

    if (status != OCI_NO_DATA) {
        throw IOException("Failed to fetch data.");
    }

    output.SetCardinality(row_count);
}

static void LoadInternal(ExtensionLoader &loader) {
    auto oracle_query_function = TableFunction("oracle_query", {LogicalType::VARCHAR, LogicalType::VARCHAR},
                                               OracleQueryFunction, OracleQueryBind);
    loader.RegisterFunction(oracle_query_function);
}

void OracleExtension::Load(ExtensionLoader &loader) {
    LoadInternal(loader);
}

extern "C" {

DUCKDB_EXTENSION_API void oracle_init(duckdb::DatabaseInstance &db) {
	duckdb::DuckDB db_wrapper(db);
	db_wrapper.LoadExtension<duckdb::OracleExtension>();
}

DUCKDB_EXTENSION_API const char *oracle_version() {
	return duckdb::DuckDB::LibraryVersion();
}

} // extern "C"
