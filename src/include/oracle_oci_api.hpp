#pragma once

#include <cstddef>
#include <cstdint>

namespace duckdb {

typedef unsigned char ub1;
typedef unsigned short ub2;
typedef unsigned int ub4;
typedef uint64_t ub8;
typedef signed char sb1;
typedef signed short sb2;
typedef signed int sb4;
typedef int64_t sb8;
typedef int sword;
typedef unsigned int uword;
typedef void dvoid;
typedef unsigned char OraText;
typedef unsigned char text;

struct OCIEnv;
struct OCIError;
struct OCISvcCtx;
struct OCIStmt;
struct OCIServer;
struct OCISession;
struct OCIDefine;
struct OCIBind;
struct OCIParam;

struct OCITime {
	ub1 OCITimeHH;
	ub1 OCITimeMI;
	ub1 OCITimeSS;
};

struct OCIDate {
	sb2 OCIDateYYYY;
	ub1 OCIDateMM;
	ub1 OCIDateDD;
	OCITime OCIDateTime;
};

inline void OCIDateSetDate(OCIDate *date, sb2 year, ub1 month, ub1 day) {
	date->OCIDateYYYY = year;
	date->OCIDateMM = month;
	date->OCIDateDD = day;
}

inline void OCIDateSetTime(OCIDate *date, ub1 hour, ub1 min, ub1 sec) {
	date->OCIDateTime.OCITimeHH = hour;
	date->OCIDateTime.OCITimeMI = min;
	date->OCIDateTime.OCITimeSS = sec;
}

// OCI Return Codes
constexpr sword OCI_SUCCESS = 0;
constexpr sword OCI_SUCCESS_WITH_INFO = 1;
constexpr sword OCI_NO_DATA = 100;
constexpr sword OCI_ERROR = -1;
constexpr sword OCI_INVALID_HANDLE = -2;

// OCI Modes and Flags
constexpr ub4 OCI_DEFAULT = 0x00000000;
constexpr ub4 OCI_THREADED = 0x00000001;
constexpr ub4 OCI_OBJECT = 0x00000002;
constexpr ub4 OCI_NTV_SYNTAX = 1;
constexpr ub2 OCI_FETCH_NEXT = 0x02;
constexpr ub4 OCI_DESCRIBE_ONLY = 0x00000010;
constexpr ub4 OCI_COMMIT_ON_SUCCESS = 0x00000020;
constexpr ub4 OCI_CRED_RDBMS = 1;
constexpr ub4 OCI_CRED_EXT = 2;
constexpr ub4 OCI_SERVER_NOT_CONNECTED = 0;
constexpr ub4 OCI_SERVER_NORMAL = 1;

// OCI Statement Types
constexpr ub2 OCI_STMT_SELECT = 1;
constexpr ub2 OCI_STMT_UPDATE = 2;
constexpr ub2 OCI_STMT_DELETE = 3;
constexpr ub2 OCI_STMT_INSERT = 4;
constexpr ub2 OCI_STMT_CREATE = 5;
constexpr ub2 OCI_STMT_DROP = 6;
constexpr ub2 OCI_STMT_ALTER = 7;
constexpr ub2 OCI_STMT_MERGE = 16;

// OCI Handle and Descriptor Types
constexpr ub4 OCI_HTYPE_ENV = 1;
constexpr ub4 OCI_HTYPE_ERROR = 2;
constexpr ub4 OCI_HTYPE_SVCCTX = 3;
constexpr ub4 OCI_HTYPE_STMT = 4;
constexpr ub4 OCI_HTYPE_BIND = 5;
constexpr ub4 OCI_HTYPE_DEFINE = 6;
constexpr ub4 OCI_HTYPE_DESCRIBE = 7;
constexpr ub4 OCI_HTYPE_SERVER = 8;
constexpr ub4 OCI_HTYPE_SESSION = 9;
constexpr ub4 OCI_DTYPE_PARAM = 53;

// OCI Attribute Constants
constexpr ub4 OCI_ATTR_DATA_SIZE = 1;
constexpr ub4 OCI_ATTR_DATA_TYPE = 2;
constexpr ub4 OCI_ATTR_NAME = 4;
constexpr ub4 OCI_ATTR_PRECISION = 5;
constexpr ub4 OCI_ATTR_SCALE = 6;
constexpr ub4 OCI_ATTR_SERVER = 6;
constexpr ub4 OCI_ATTR_SESSION = 7;
constexpr ub4 OCI_ATTR_TYPE_NAME = 8;
constexpr ub4 OCI_ATTR_SCHEMA_NAME = 9;
constexpr ub4 OCI_ATTR_PREFETCH_ROWS = 11;
constexpr ub4 OCI_ATTR_PREFETCH_MEMORY = 13;
constexpr ub4 OCI_ATTR_PARAM_COUNT = 18;
constexpr ub4 OCI_ATTR_USERNAME = 22;
constexpr ub4 OCI_ATTR_PASSWORD = 23;
constexpr ub4 OCI_ATTR_STMT_TYPE = 24;
constexpr ub4 OCI_ATTR_SERVER_STATUS = 143;
constexpr ub4 OCI_ATTR_STMTCACHESIZE = 176;
constexpr ub4 OCI_ATTR_CONN_TIMEOUT = 177;
constexpr ub4 OCI_ATTR_ROWS_FETCHED = 197;
constexpr ub4 OCI_ATTR_CHAR_SIZE = 286;
constexpr ub4 OCI_ATTR_UB8_ROW_COUNT = 457;
constexpr ub4 OCI_ATTR_CALL_TIMEOUT = 531;
constexpr ub4 OCI_ATTR_VECTOR_DATA_FORMAT = 696;
#ifndef OCI_ATTR_VECTOR_DATA_FORMAT
#define OCI_ATTR_VECTOR_DATA_FORMAT 696
#endif

// OCI SQLT Data Type Codes
constexpr ub2 SQLT_CHR = 1;
constexpr ub2 SQLT_NUM = 2;
constexpr ub2 SQLT_INT = 3;
constexpr ub2 SQLT_FLT = 4;
constexpr ub2 SQLT_STR = 5;
constexpr ub2 SQLT_VNU = 6;
constexpr ub2 SQLT_LNG = 8;
constexpr ub2 SQLT_VCS = 9;
constexpr ub2 SQLT_DAT = 12;
constexpr ub2 SQLT_BFLOAT = 21;
constexpr ub2 SQLT_BDOUBLE = 22;
constexpr ub2 SQLT_BIN = 23;
constexpr ub2 SQLT_LBI = 24;
constexpr ub2 SQLT_UIN = 68;
constexpr ub2 SQLT_LVC = 94;
constexpr ub2 SQLT_AFC = 96;
constexpr ub2 SQLT_AVC = 97;
constexpr ub2 SQLT_IBFLOAT = 100;
constexpr ub2 SQLT_IBDOUBLE = 101;
constexpr ub2 SQLT_RDD = 104;
constexpr ub2 SQLT_NTY = 108;
constexpr ub2 SQLT_REF = 110;
constexpr ub2 SQLT_CLOB = 112;
constexpr ub2 SQLT_BLOB = 113;
constexpr ub2 SQLT_BFILEE = 114;
constexpr ub2 SQLT_BFILE = 114;
constexpr ub2 SQLT_JSON = 119;
constexpr ub2 SQLT_VEC = 127;
constexpr ub2 SQLT_ODT = 156;
constexpr ub2 SQLT_TIMESTAMP = 187;
constexpr ub2 SQLT_TIMESTAMP_TZ = 188;
constexpr ub2 SQLT_INTERVAL_YM = 189;
constexpr ub2 SQLT_INTERVAL_DS = 190;
constexpr ub2 SQLT_TIMESTAMP_LTZ = 232;
constexpr ub2 SQLT_BOL = 252;

//! Ensure the Oracle Instant Client shared library (libclntsh / oci.dll) is dynamically loaded.
//! Throws IOException with installation instructions if the library or required symbols cannot be resolved.
void EnsureOCILoaded();

//! Return true if the Oracle Instant Client shared library has already been loaded.
bool IsOCILoaded() noexcept;

// Dynamically dispatched OCI C API entrypoints
sword OCIEnvCreate(OCIEnv **envp, ub4 mode, dvoid *ctxp, dvoid *(*malocfp)(dvoid *ctxp, size_t size),
                   dvoid *(*ralocfp)(dvoid *ctxp, dvoid *memptr, size_t newsize),
                   void (*mfreefp)(dvoid *ctxp, dvoid *memptr), size_t xtramem_sz, dvoid **usrmempp);
sword OCIHandleAlloc(const dvoid *parenth, dvoid **hndlpp, const ub4 type, const size_t xtramem_sz, dvoid **usrmempp);
sword OCIHandleFree(dvoid *hndlp, const ub4 type);
sword OCIDescriptorFree(dvoid *descp, const ub4 type);
sword OCIAttrSet(dvoid *trgthndlp, ub4 trghndltyp, dvoid *attributep, ub4 size, ub4 attrtype, OCIError *errhp);
sword OCIAttrGet(const dvoid *trgthndlp, ub4 trghndltyp, dvoid *attributep, ub4 *sizep, ub4 attrtype, OCIError *errhp);
sword OCIServerAttach(OCIServer *srvhp, OCIError *errhp, const OraText *dblink, sb4 dblink_len, ub4 mode);
sword OCIServerDetach(OCIServer *srvhp, OCIError *errhp, ub4 mode);
sword OCISessionBegin(OCISvcCtx *svchp, OCIError *errhp, OCISession *usrhp, ub4 credt, ub4 mode);
sword OCISessionEnd(OCISvcCtx *svchp, OCIError *errhp, OCISession *usrhp, ub4 mode);
sword OCIPing(OCISvcCtx *svchp, OCIError *errhp, ub4 mode);
sword OCIStmtPrepare(OCIStmt *stmtp, OCIError *errhp, const OraText *stmt, ub4 stmt_len, ub4 language, ub4 mode);
sword OCIStmtExecute(OCISvcCtx *svchp, OCIStmt *stmtp, OCIError *errhp, ub4 iters, ub4 rowoff, const void *snap_in,
                     void *snap_out, ub4 mode);
sword OCIStmtFetch2(OCIStmt *stmtp, OCIError *errhp, ub4 nrows, ub2 orientation, sb4 scrollOffset, ub4 mode);
sword OCIParamGet(const dvoid *hndlp, ub4 htype, OCIError *errhp, dvoid **parmdpp, ub4 pos);
sword OCIDefineByPos(OCIStmt *stmtp, OCIDefine **defnp, OCIError *errhp, ub4 position, dvoid *valuep, sb4 value_sz,
                     ub2 dty, dvoid *indp, ub2 *rlenp, ub2 *rcodep, ub4 mode);
sword OCIDefineArrayOfStruct(OCIDefine *defnp, OCIError *errhp, ub4 pvskip, ub4 indskip, ub4 rlskip, ub4 rcskip);
sword OCIBindByPos(OCIStmt *stmtp, OCIBind **bindpp, OCIError *errhp, ub4 position, dvoid *valuep, sb4 value_sz,
                   ub2 dty, dvoid *indp, ub2 *alenp, ub2 *rcodep, ub4 maxarr_len, ub4 *curelep, ub4 mode);
sword OCIBindArrayOfStruct(OCIBind *bindp, OCIError *errhp, ub4 pvskip, ub4 indskip, ub4 alskip, ub4 rcskip);
sword OCITransCommit(OCISvcCtx *svchp, OCIError *errhp, ub4 flags);
sword OCITransRollback(OCISvcCtx *svchp, OCIError *errhp, ub4 flags);
sword OCIErrorGet(dvoid *hndlp, ub4 recordno, OraText *sqlstate, sb4 *errcodep, OraText *bufp, ub4 bufsiz, ub4 type);

} // namespace duckdb
