#define DUCKDB_EXTENSION_MAIN

#include "oracle_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/main/config.hpp"
#include "oracle_functions.hpp"
#include "oracle_settings.hpp"

namespace duckdb {

void OracleExtension::Load(ExtensionLoader &loader) {
	RegisterOracleFunctions(loader);
	auto &db = loader.GetDatabaseInstance();
	auto &config = DBConfig::GetConfig(db);
	RegisterOracleExtensionOptions(config);
}

std::string OracleExtension::Name() {
	return "oracle";
}

std::string OracleExtension::Version() const {
#ifdef EXT_VERSION_ORACLE
	return EXT_VERSION_ORACLE;
#else
	return "";
#endif
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
