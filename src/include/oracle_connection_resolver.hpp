#pragma once

#include "duckdb.hpp"
#include "oracle_settings.hpp"

namespace duckdb {

class OracleCatalogState;

struct OracleResolvedConnection {
	string connection_string;
	string wallet_path;
	OracleSettings settings;
	shared_ptr<OracleCatalogState> catalog_state;
};

OracleSettings GetOracleSettings(ClientContext &context, OracleCatalogState *state = nullptr);

OracleResolvedConnection ResolveOracleConnection(ClientContext &context, const string &connection_ref,
                                                 OracleCatalogState *state_hint = nullptr,
                                                 bool reject_bare_identifier = false,
                                                 const char *surface = "Oracle connection");

} // namespace duckdb
