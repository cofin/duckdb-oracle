#include "oracle_connection_resolver.hpp"
#include "oracle_catalog_state.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/main/client_context.hpp"

namespace duckdb {

OracleSettings GetOracleSettings(ClientContext &context, OracleCatalogState *state) {
	OracleSettings settings;
	if (state) {
		settings = state->settings;
	}

	Value option_value;
	if (context.TryGetCurrentSetting("oracle_enable_pushdown", option_value)) {
		settings.enable_pushdown = option_value.GetValue<bool>();
	}
	if (context.TryGetCurrentSetting("oracle_prefetch_rows", option_value)) {
		auto val = option_value.GetValue<int64_t>();
		settings.prefetch_rows = MaxValue<idx_t>(1, static_cast<idx_t>(val));
	}
	if (context.TryGetCurrentSetting("oracle_prefetch_memory", option_value)) {
		auto val = option_value.GetValue<int64_t>();
		settings.prefetch_memory = val <= 0 ? 0 : static_cast<idx_t>(val);
	}
	if (context.TryGetCurrentSetting("oracle_array_size", option_value)) {
		auto val = option_value.GetValue<int64_t>();
		settings.array_size = MaxValue<idx_t>(1, static_cast<idx_t>(val));
	}
	if (context.TryGetCurrentSetting("oracle_connection_cache", option_value)) {
		settings.connection_cache = option_value.GetValue<bool>();
	}
	if (context.TryGetCurrentSetting("oracle_connection_limit", option_value)) {
		auto val = option_value.GetValue<int64_t>();
		settings.connection_limit = MaxValue<idx_t>(1, static_cast<idx_t>(val));
	}
	if (context.TryGetCurrentSetting("oracle_debug_show_queries", option_value)) {
		settings.debug_show_queries = option_value.GetValue<bool>();
	}
	if (context.TryGetCurrentSetting("oracle_lazy_schema_loading", option_value)) {
		settings.lazy_schema_loading = option_value.GetValue<bool>();
	}
	if (context.TryGetCurrentSetting("oracle_metadata_object_types", option_value)) {
		settings.metadata_object_types = option_value.ToString();
	}
	if (context.TryGetCurrentSetting("oracle_metadata_result_limit", option_value)) {
		auto val = option_value.GetValue<int64_t>();
		settings.metadata_result_limit = val <= 0 ? DEFAULT_ORACLE_METADATA_RESULT_LIMIT : static_cast<idx_t>(val);
	}
	if (context.TryGetCurrentSetting("oracle_use_current_schema", option_value)) {
		settings.use_current_schema = option_value.GetValue<bool>();
	}
	if (context.TryGetCurrentSetting("oracle_enable_spatial_types", option_value)) {
		settings.enable_spatial_types = option_value.GetValue<bool>();
	}
	return settings;
}

OracleResolvedConnection ResolveOracleConnection(ClientContext &context, const string &connection_ref,
                                                 OracleCatalogState *state_hint, bool reject_bare_identifier,
                                                 const char *surface) {
	OracleResolvedConnection resolved;
	if (state_hint) {
		resolved.connection_string = state_hint->connection_string;
		resolved.wallet_path = state_hint->wallet_path;
		resolved.settings = GetOracleSettings(context, state_hint);
		return resolved;
	}

	auto catalog_state = OracleCatalogState::LookupByAlias(connection_ref);
	if (catalog_state) {
		resolved.connection_string = catalog_state->connection_string;
		resolved.wallet_path = catalog_state->wallet_path;
		resolved.settings = GetOracleSettings(context, catalog_state.get());
		resolved.catalog_state = std::move(catalog_state);
		return resolved;
	}

	if (reject_bare_identifier && !connection_ref.empty() && connection_ref.find('@') == string::npos) {
		throw InvalidInputException(
		    "%s does not accept bare Oracle secret names or malformed direct connection strings without an '@'. Use "
		    "ATTACH ... (TYPE oracle, SECRET ...) AS an alias and pass that alias, or pass a full "
		    "user/password@connect_identifier connection string.",
		    surface ? surface : "Oracle connection");
	}

	resolved.connection_string = connection_ref;
	resolved.settings = GetOracleSettings(context);
	return resolved;
}

} // namespace duckdb
