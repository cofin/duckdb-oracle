#include "oracle_connection_resolver.hpp"
#include "oracle_catalog_state.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/main/client_context.hpp"

namespace duckdb {

namespace {

static bool HasDirectConnectionStringShape(const string &connection_ref) {
	auto slash_pos = connection_ref.find('/');
	auto at_pos = connection_ref.rfind('@');
	if (slash_pos == string::npos || at_pos == string::npos || slash_pos >= at_pos) {
		return false;
	}
	if (at_pos + 1 >= connection_ref.size()) {
		return false;
	}
	if (slash_pos == 0) {
		return at_pos == 1;
	}
	return at_pos > slash_pos + 1;
}

} // namespace

OracleSettings GetOracleSettings(ClientContext &context, OracleCatalogState *state) {
	const OracleSettings defaults;
	OracleSettings settings = state ? state->settings : defaults;

	auto should_apply = [&](const string &name, SettingScope scope, bool state_differs_from_default,
	                        bool global_differs_from_default) {
		if (!state) {
			return true;
		}
		if (scope == SettingScope::LOCAL) {
			return true;
		}
		if (state->settings.attach_overridden_keys.find(name) != state->settings.attach_overridden_keys.end()) {
			return false;
		}
		if (state_differs_from_default && !global_differs_from_default) {
			return false;
		}
		return true;
	};

	Value option_value;
	auto lookup = context.TryGetCurrentSetting("oracle_enable_pushdown", option_value);
	if (lookup) {
		auto val = option_value.GetValue<bool>();
		if (should_apply("oracle_enable_pushdown", lookup.GetScope(),
		                 settings.enable_pushdown != defaults.enable_pushdown, val != defaults.enable_pushdown)) {
			settings.enable_pushdown = val;
		}
	}
	lookup = context.TryGetCurrentSetting("oracle_prefetch_rows", option_value);
	if (lookup) {
		auto raw = ExtractSignedOptionInt(option_value, "oracle_prefetch_rows", 1, MAX_ORACLE_PREFETCH_ROWS);
		auto val = OracleValidatedSettingValue(raw, "oracle_prefetch_rows", 1, MAX_ORACLE_PREFETCH_ROWS);
		if (should_apply("oracle_prefetch_rows", lookup.GetScope(), settings.prefetch_rows != defaults.prefetch_rows,
		                 val != defaults.prefetch_rows)) {
			settings.prefetch_rows = val;
		}
	}
	lookup = context.TryGetCurrentSetting("oracle_prefetch_memory", option_value);
	if (lookup) {
		auto raw = ExtractSignedOptionInt(option_value, "oracle_prefetch_memory", 0, MAX_ORACLE_PREFETCH_MEMORY);
		auto val = OracleValidatedSettingValue(raw, "oracle_prefetch_memory", 0, MAX_ORACLE_PREFETCH_MEMORY);
		if (should_apply("oracle_prefetch_memory", lookup.GetScope(),
		                 settings.prefetch_memory != defaults.prefetch_memory, val != defaults.prefetch_memory)) {
			settings.prefetch_memory = val;
		}
	}
	lookup = context.TryGetCurrentSetting("oracle_array_size", option_value);
	if (lookup) {
		auto raw = ExtractSignedOptionInt(option_value, "oracle_array_size", 1, STANDARD_VECTOR_SIZE);
		auto val = OracleValidatedSettingValue(raw, "oracle_array_size", 1, STANDARD_VECTOR_SIZE);
		if (should_apply("oracle_array_size", lookup.GetScope(), settings.array_size != defaults.array_size,
		                 val != defaults.array_size)) {
			settings.array_size = val;
		}
	}
	lookup = context.TryGetCurrentSetting("oracle_connection_cache", option_value);
	if (lookup) {
		auto val = option_value.GetValue<bool>();
		if (should_apply("oracle_connection_cache", lookup.GetScope(),
		                 settings.connection_cache != defaults.connection_cache, val != defaults.connection_cache)) {
			settings.connection_cache = val;
		}
	}
	lookup = context.TryGetCurrentSetting("oracle_connection_limit", option_value);
	if (lookup) {
		auto raw = ExtractSignedOptionInt(option_value, "oracle_connection_limit", 1, MAX_ORACLE_CONNECTION_LIMIT);
		auto val = OracleValidatedSettingValue(raw, "oracle_connection_limit", 1, MAX_ORACLE_CONNECTION_LIMIT);
		if (should_apply("oracle_connection_limit", lookup.GetScope(),
		                 settings.connection_limit != defaults.connection_limit, val != defaults.connection_limit)) {
			settings.connection_limit = val;
		}
	}
	lookup = context.TryGetCurrentSetting("oracle_debug_show_queries", option_value);
	if (lookup) {
		auto val = option_value.GetValue<bool>();
		if (should_apply("oracle_debug_show_queries", lookup.GetScope(),
		                 settings.debug_show_queries != defaults.debug_show_queries,
		                 val != defaults.debug_show_queries)) {
			settings.debug_show_queries = val;
		}
	}
	lookup = context.TryGetCurrentSetting("oracle_call_timeout_ms", option_value);
	if (lookup) {
		auto raw = ExtractSignedOptionInt(option_value, "oracle_call_timeout_ms", 0, MAX_ORACLE_CALL_TIMEOUT_MS);
		auto val = OracleValidatedSettingValue(raw, "oracle_call_timeout_ms", 0, MAX_ORACLE_CALL_TIMEOUT_MS);
		if (should_apply("oracle_call_timeout_ms", lookup.GetScope(),
		                 settings.call_timeout_ms != defaults.call_timeout_ms, val != defaults.call_timeout_ms)) {
			settings.call_timeout_ms = val;
		}
	}
	lookup = context.TryGetCurrentSetting("oracle_stmt_cache_size", option_value);
	if (lookup) {
		auto raw = ExtractSignedOptionInt(option_value, "oracle_stmt_cache_size", 0, MAX_ORACLE_STMT_CACHE_SIZE);
		auto val = OracleValidatedSettingValue(raw, "oracle_stmt_cache_size", 0, MAX_ORACLE_STMT_CACHE_SIZE);
		if (should_apply("oracle_stmt_cache_size", lookup.GetScope(),
		                 settings.stmt_cache_size != defaults.stmt_cache_size, val != defaults.stmt_cache_size)) {
			settings.stmt_cache_size = val;
		}
	}
	lookup = context.TryGetCurrentSetting("oracle_lazy_schema_loading", option_value);
	if (lookup) {
		auto val = option_value.GetValue<bool>();
		if (should_apply("oracle_lazy_schema_loading", lookup.GetScope(),
		                 settings.lazy_schema_loading != defaults.lazy_schema_loading,
		                 val != defaults.lazy_schema_loading)) {
			settings.lazy_schema_loading = val;
		}
	}
	lookup = context.TryGetCurrentSetting("oracle_metadata_object_types", option_value);
	if (lookup) {
		auto val = option_value.ToString();
		if (should_apply("oracle_metadata_object_types", lookup.GetScope(),
		                 settings.metadata_object_types != defaults.metadata_object_types,
		                 val != defaults.metadata_object_types)) {
			settings.metadata_object_types = std::move(val);
		}
	}
	lookup = context.TryGetCurrentSetting("oracle_metadata_result_limit", option_value);
	if (lookup) {
		auto raw =
		    ExtractSignedOptionInt(option_value, "oracle_metadata_result_limit", 1, MAX_ORACLE_METADATA_RESULT_LIMIT);
		auto val = OracleValidatedMetadataResultLimit(raw);
		if (should_apply("oracle_metadata_result_limit", lookup.GetScope(),
		                 settings.metadata_result_limit != defaults.metadata_result_limit,
		                 val != defaults.metadata_result_limit)) {
			settings.metadata_result_limit = val;
		}
	}
	lookup = context.TryGetCurrentSetting("oracle_use_current_schema", option_value);
	if (lookup) {
		auto val = option_value.GetValue<bool>();
		if (should_apply("oracle_use_current_schema", lookup.GetScope(),
		                 settings.use_current_schema != defaults.use_current_schema,
		                 val != defaults.use_current_schema)) {
			settings.use_current_schema = val;
		}
	}
	lookup = context.TryGetCurrentSetting("oracle_enable_spatial_types", option_value);
	if (lookup) {
		auto val = option_value.GetValue<bool>();
		if (should_apply("oracle_enable_spatial_types", lookup.GetScope(),
		                 settings.enable_spatial_types != defaults.enable_spatial_types,
		                 val != defaults.enable_spatial_types)) {
			settings.enable_spatial_types = val;
		}
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
		if (!connection_ref.empty()) {
			auto by_alias = OracleCatalogState::LookupByAlias(connection_ref);
			if (by_alias && by_alias.get() == state_hint) {
				resolved.catalog_state = std::move(by_alias);
				return resolved;
			}
		}
		resolved.catalog_state = shared_ptr<OracleCatalogState>(state_hint, [](OracleCatalogState *) {});
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

	if (reject_bare_identifier && !connection_ref.empty() && !HasDirectConnectionStringShape(connection_ref)) {
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
