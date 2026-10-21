#define DUCKDB_EXTENSION_MAIN

#include "oracle_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/main/config.hpp"
#include "oracle_functions.hpp"
#include "oracle_settings.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
	RegisterOracleFunctions(loader);
	auto &db = loader.GetDatabaseInstance();
	auto &config = DBConfig::GetConfig(db);
	RegisterOracleExtensionOptions(config);
}

void OracleExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
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

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(oracle, loader) {
	duckdb::LoadInternal(loader);
}
}
