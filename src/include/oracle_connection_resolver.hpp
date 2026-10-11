#pragma once

#include "duckdb.hpp"
#include "oracle_settings.hpp"
#include <type_traits>

namespace duckdb {

class OracleCatalogState;

struct OracleResolvedConnection {
	string connection_string;
	string wallet_path;
	OracleSettings settings;
	shared_ptr<OracleCatalogState> catalog_state;
};

OracleSettings GetOracleSettings(ClientContext &context, OracleCatalogState *state = nullptr);

template <class T,
          typename std::enable_if<std::is_same<typename std::decay<T>::type, shared_ptr<OracleCatalogState>>::value,
                                  int>::type = 0>
OracleSettings GetOracleSettings(ClientContext &context, T &&state) {
	return GetOracleSettings(context, state.get());
}

OracleResolvedConnection ResolveOracleConnection(ClientContext &context, const string &connection_ref,
                                                 OracleCatalogState *state_hint = nullptr,
                                                 bool reject_bare_identifier = false,
                                                 const char *surface = "Oracle connection");

OracleResolvedConnection ResolveOracleConnectionWithSharedState(ClientContext &context, const string &connection_ref,
                                                                const shared_ptr<OracleCatalogState> &state_hint,
                                                                bool reject_bare_identifier = false,
                                                                const char *surface = "Oracle connection");

template <class T,
          typename std::enable_if<std::is_same<typename std::decay<T>::type, shared_ptr<OracleCatalogState>>::value,
                                  int>::type = 0>
OracleResolvedConnection ResolveOracleConnection(ClientContext &context, const string &connection_ref, T &&state_hint,
                                                 bool reject_bare_identifier = false,
                                                 const char *surface = "Oracle connection") {
	return ResolveOracleConnectionWithSharedState(context, connection_ref, state_hint, reject_bare_identifier, surface);
}

} // namespace duckdb
