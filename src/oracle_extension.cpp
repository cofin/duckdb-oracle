#define DUCKDB_EXTENSION_MAIN

#include "oracle_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include <oci.h>
#include <iostream>

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>

#ifndef SQLT_JSON
#define SQLT_JSON 119
#endif

#ifndef SQLT_VEC
#define SQLT_VEC 127
#endif

namespace duckdb {

struct OracleContext {
    OCIEnv *envhp = nullptr;
    OCISvcCtx *svchp = nullptr;
    OCIStmt *stmthp = nullptr;
    OCIError *errhp = nullptr;

    ~OracleContext() {
        if (stmthp) OCIHandleFree(stmthp, OCI_HTYPE_STMT);
        if (svchp) {
             if (errhp) OCILogoff(svchp, errhp);
             else OCIHandleFree(svchp, OCI_HTYPE_SVCCTX); // Fallback
        }
        if (envhp) OCIHandleFree(envhp, OCI_HTYPE_ENV);
        if (errhp) OCIHandleFree(errhp, OCI_HTYPE_ERROR);
    }
};

struct OracleBindData : public FunctionData {
    std::shared_ptr<OracleContext> ctx;
    string query;
    vector<ub2> oci_types;
    vector<ub4> oci_sizes;

    OracleBindData() : ctx(std::make_shared<OracleContext>()) {}

    unique_ptr<FunctionData> Copy() const override {
        auto copy = make_uniq<OracleBindData>();
        copy->ctx = ctx;
        copy->query = query;
        copy->oci_types = oci_types;
        copy->oci_sizes = oci_sizes;
        return std::move(copy);
    }

    bool Equals(const FunctionData &other) const override {
        auto &other_bind_data = (const OracleBindData &)other;
        return query == other_bind_data.query;
    }
};

static void CheckOCIError(sword status, OCIError *errhp, const string &msg) {
    if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
        text errbuf[512];
        sb4 errcode = 0;
        if (errhp) {
            OCIErrorGet((dvoid *)errhp, (ub4)1, (text *)NULL, &errcode, errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR);
            throw IOException(msg + ": " + string((char *)errbuf));
        } else {
            throw IOException(msg + ": (No Error Handle)");
        }
    }
}

static unique_ptr<FunctionData> OracleBindInternal(ClientContext &context, string connection_string, string query,
                                                        vector<LogicalType> &return_types, vector<string> &names,
                                                        OracleBindData *bind_data_ptr = nullptr) {
    auto result = bind_data_ptr ? unique_ptr<OracleBindData>(bind_data_ptr) : make_uniq<OracleBindData>();
    result->query = query;
    auto &ctx = result->ctx;

    if (OCIEnvCreate(&ctx->envhp, OCI_DEFAULT, nullptr, nullptr, nullptr, nullptr, 0, nullptr) != OCI_SUCCESS) {
        throw IOException("Failed to create OCI environment.");
    }

    if (OCIHandleAlloc(ctx->envhp, (dvoid **)&ctx->errhp, OCI_HTYPE_ERROR, 0, nullptr) != OCI_SUCCESS) {
        throw IOException("Failed to allocate OCI error handle.");
    }

    if (OCIHandleAlloc(ctx->envhp, (dvoid **)&ctx->svchp, OCI_HTYPE_SVCCTX, 0, nullptr) != OCI_SUCCESS) {
        throw IOException("Failed to allocate OCI service context handle.");
    }

    sword status = OCILogon(ctx->envhp, ctx->errhp, &ctx->svchp, (OraText *)"", 0, (OraText *)"", 0, (OraText *)connection_string.c_str(), connection_string.size());
    CheckOCIError(status, ctx->errhp, "Failed to connect to Oracle");

    if (OCIHandleAlloc(ctx->envhp, (dvoid **)&ctx->stmthp, OCI_HTYPE_STMT, 0, nullptr) != OCI_SUCCESS) {
        throw IOException("Failed to allocate OCI statement handle.");
    }

    status = OCIStmtPrepare(ctx->stmthp, ctx->errhp, (OraText *)result->query.c_str(), result->query.size(), OCI_NTV_SYNTAX, OCI_DEFAULT);
    CheckOCIError(status, ctx->errhp, "Failed to prepare OCI statement");

    status = OCIStmtExecute(ctx->svchp, ctx->stmthp, ctx->errhp, 0, 0, nullptr, nullptr, OCI_DESCRIBE_ONLY);
    CheckOCIError(status, ctx->errhp, "Failed to execute OCI statement (Describe)");

    ub4 param_count;
    OCIAttrGet(ctx->stmthp, OCI_HTYPE_STMT, &param_count, 0, OCI_ATTR_PARAM_COUNT, ctx->errhp);

    for (ub4 i = 1; i <= param_count; i++) {
        OCIParam *param;
        OCIParamGet(ctx->stmthp, OCI_HTYPE_STMT, ctx->errhp, (dvoid **)&param, i);

        ub2 data_type;
        OCIAttrGet(param, OCI_DTYPE_PARAM, &data_type, 0, OCI_ATTR_DATA_TYPE, ctx->errhp);

        OraText *col_name;
        ub4 col_name_len;
        OCIAttrGet(param, OCI_DTYPE_PARAM, &col_name, &col_name_len, OCI_ATTR_NAME, ctx->errhp);

        names.emplace_back((char *)col_name, col_name_len);
        result->oci_types.push_back(data_type);
        
        ub2 precision = 0;
        sb1 scale = 0;
        OCIAttrGet(param, OCI_DTYPE_PARAM, &precision, 0, OCI_ATTR_PRECISION, ctx->errhp);
        OCIAttrGet(param, OCI_DTYPE_PARAM, &scale, 0, OCI_ATTR_SCALE, ctx->errhp);

        ub4 char_len = 0;
        OCIAttrGet(param, OCI_DTYPE_PARAM, &char_len, 0, OCI_ATTR_CHAR_SIZE, ctx->errhp);
        result->oci_sizes.push_back(char_len > 0 ? char_len : 4000); // Default buffer size

        switch (data_type) {
        case SQLT_CHR:
        case SQLT_AFC:
        case SQLT_VCS:
        case SQLT_AVC:
             return_types.push_back(LogicalType::VARCHAR);
             break;
        case SQLT_NUM:
        case SQLT_VNU:
            if (scale == 0) {
                 if (precision > 18) {
                    return_types.push_back(LogicalType::DECIMAL(38, 0)); 
                 } else {
                    return_types.push_back(LogicalType::BIGINT);
                 }
            } else {
                return_types.push_back(LogicalType::DOUBLE);
            }
            break;
        case SQLT_INT:
        case SQLT_UIN:
            return_types.push_back(LogicalType::BIGINT);
            break;
        case SQLT_FLT:
        case SQLT_BFLOAT:
        case SQLT_BDOUBLE:
        case SQLT_IBFLOAT:
        case SQLT_IBDOUBLE:
            return_types.push_back(LogicalType::DOUBLE);
            break;
        case SQLT_DAT:
        case SQLT_ODT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            return_types.push_back(LogicalType::TIMESTAMP);
            break;
        case SQLT_CLOB:
        case SQLT_BLOB:
        case SQLT_BIN:
        case SQLT_LBI:
        case SQLT_LNG:
        case SQLT_LVC:
             return_types.push_back(LogicalType::BLOB);
             break;
        case SQLT_JSON:
             return_types.push_back(LogicalType::VARCHAR); // Fetch JSON as string
             break;
        case SQLT_VEC:
             return_types.push_back(LogicalType::VARCHAR);
             break;
        default:
            return_types.push_back(LogicalType::VARCHAR);
            break;
        }
    }

    // Re-execute for fetching
    status = OCIStmtExecute(ctx->svchp, ctx->stmthp, ctx->errhp, 0, 0, nullptr, nullptr, OCI_DEFAULT);
     if (status != OCI_SUCCESS && status != OCI_SUCCESS_WITH_INFO) {
        CheckOCIError(status, ctx->errhp, "Failed to execute OCI statement");
    }

    return std::move(result);
}

static unique_ptr<FunctionData> OracleScanBind(ClientContext &context, TableFunctionBindInput &input,
                                                    vector<LogicalType> &return_types, vector<string> &names) {
    auto connection_string = input.inputs[0].GetValue<string>();
    auto schema_name = input.inputs[1].GetValue<string>();
    auto table_name = input.inputs[2].GetValue<string>();
    string query = "SELECT * FROM " + schema_name + "." + table_name;
    return OracleBindInternal(context, connection_string, query, return_types, names);
}

static unique_ptr<FunctionData> OracleQueryBind(ClientContext &context, TableFunctionBindInput &input,
                                                     vector<LogicalType> &return_types, vector<string> &names) {
    auto connection_string = input.inputs[0].GetValue<string>();
    auto query = input.inputs[1].GetValue<string>();
    return OracleBindInternal(context, connection_string, query, return_types, names);
}

static void OracleQueryFunction(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
    auto &bind_data = (OracleBindData &)*data.bind_data;
    auto &ctx = bind_data.ctx;
    OCIDefine *defnp = nullptr;
    sword status;
    idx_t row_count = 0;

    vector<vector<char>> buffers(output.ColumnCount());
    vector<OCIDefine*> defines(output.ColumnCount(), nullptr);
    vector<sb2> indicators(output.ColumnCount());
    vector<ub2> return_lens(output.ColumnCount());
    
    for (idx_t col_idx = 0; col_idx < output.ColumnCount(); col_idx++) {
         ub4 size = 4000; // Default max
         if (col_idx < bind_data.oci_sizes.size() && bind_data.oci_sizes[col_idx] > 0) {
             size = bind_data.oci_sizes[col_idx] * 4; // UTF8 safety
         }
         
         buffers[col_idx].resize(size);
         
         ub2 type = SQLT_STR; 
         
         switch(output.GetTypes()[col_idx].id()) {
             case LogicalTypeId::BIGINT: type = SQLT_INT; size = sizeof(int64_t); buffers[col_idx].resize(size); break;
             case LogicalTypeId::DOUBLE: type = SQLT_FLT; size = sizeof(double); buffers[col_idx].resize(size); break;
             default: type = SQLT_STR; break;
         }
         
         OCIDefineByPos(ctx->stmthp, &defines[col_idx], ctx->errhp, col_idx + 1, 
                        buffers[col_idx].data(), size, type, 
                        &indicators[col_idx], &return_lens[col_idx], nullptr, OCI_DEFAULT);
    }
    
    while (row_count < STANDARD_VECTOR_SIZE) {
        status = OCIStmtFetch2(ctx->stmthp, ctx->errhp, 1, OCI_FETCH_NEXT, 0, OCI_DEFAULT);
        if (status == OCI_NO_DATA) break;
        
        for (idx_t col_idx = 0; col_idx < output.ColumnCount(); col_idx++) {
            if (indicators[col_idx] == -1) {
                FlatVector::SetNull(output.data[col_idx], row_count, true);
                continue;
            }
            
            switch(output.GetTypes()[col_idx].id()) {
                case LogicalTypeId::VARCHAR: 
                case LogicalTypeId::BLOB: {
                     string_t val((char*)buffers[col_idx].data(), return_lens[col_idx]);
                     FlatVector::GetData<string_t>(output.data[col_idx])[row_count] = StringVector::AddString(output.data[col_idx], val);
                     break;
                }
                case LogicalTypeId::BIGINT:
                    FlatVector::GetData<int64_t>(output.data[col_idx])[row_count] = *(int64_t*)buffers[col_idx].data();
                    break;
                case LogicalTypeId::DOUBLE:
                    FlatVector::GetData<double>(output.data[col_idx])[row_count] = *(double*)buffers[col_idx].data();
                    break;
                case LogicalTypeId::TIMESTAMP:
                     string_t val((char*)buffers[col_idx].data(), return_lens[col_idx]);
                     // Placeholder: Fetch as string for safety
                     break;
            }
        }
        row_count++;
    }
    output.SetCardinality(row_count);
}

static void OracleAttachWallet(DataChunk &args, ExpressionState &state, Vector &result) {
    auto wallet_path = args.data[0].GetValue(0).ToString();
    setenv("TNS_ADMIN", wallet_path.c_str(), 1);
    result.SetValue(0, Value("Wallet attached: " + wallet_path));
}

static void LoadInternal(ExtensionLoader &loader) {
    auto oracle_scan_func = TableFunction("oracle_scan", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
                                          OracleQueryFunction, OracleScanBind);
    loader.RegisterFunction(oracle_scan_func);

    auto oracle_query_func = TableFunction("oracle_query", {LogicalType::VARCHAR, LogicalType::VARCHAR},
                                           OracleQueryFunction, OracleQueryBind);
    loader.RegisterFunction(oracle_query_func);

    auto attach_wallet_func = ScalarFunction("oracle_attach_wallet", {LogicalType::VARCHAR}, LogicalType::VARCHAR, OracleAttachWallet);
    loader.RegisterFunction(attach_wallet_func);
}

void OracleExtension::Load(ExtensionLoader &loader) {
    LoadInternal(loader);
}

std::string OracleExtension::Name() {
    return "oracle";
}

std::string OracleExtension::Version() const {
    return "1.0.0";
}

extern "C" {

DUCKDB_EXTENSION_API void oracle_init(duckdb::DatabaseInstance &db) {
	duckdb::DuckDB db_wrapper(db);
	db_wrapper.LoadStaticExtension<duckdb::OracleExtension>();
}

DUCKDB_EXTENSION_API void oracle_duckdb_cpp_init(duckdb::DatabaseInstance &db) {
	oracle_init(db);
}

DUCKDB_EXTENSION_API const char *oracle_version() {
	return duckdb::DuckDB::LibraryVersion();
}

} // extern "C"
} // namespace duckdb
