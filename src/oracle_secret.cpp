#include "oracle_secret.hpp"
#include "duckdb/common/case_insensitive_map.hpp"
#include <cstdlib>

#ifdef _WIN32
static int setenv(const char *name, const char *value, int overwrite) {
	if (!overwrite && getenv(name) != nullptr) {
		return 0;
	}
	return _putenv_s(name, value);
}
#endif

namespace duckdb {

OracleSecretParameters ParseOracleSecret(const CreateSecretInput &input) {
	OracleSecretParameters params;

	// Parse host (optional, default: localhost)
	auto host_lookup = input.options.find("host");
	if (host_lookup != input.options.end()) {
		params.host = host_lookup->second.ToString();
	}

	// Parse port (optional, default: 1521)
	auto port_lookup = input.options.find("port");
	if (port_lookup != input.options.end()) {
		auto port_val = port_lookup->second.GetValue<int64_t>();
		if (port_val <= 0 || port_val > 65535) {
			throw InvalidInputException("Oracle secret: PORT must be between 1 and 65535, got %lld", port_val);
		}
		params.port = static_cast<idx_t>(port_val);
	}

	// Parse service/database (required - one of them)
	auto service_lookup = input.options.find("service");
	if (service_lookup != input.options.end()) {
		params.service = service_lookup->second.ToString();
	}

	auto database_lookup = input.options.find("database");
	if (database_lookup != input.options.end()) {
		params.database = database_lookup->second.ToString();
		// DATABASE is an alias for SERVICE
		if (params.service.empty()) {
			params.service = params.database;
		}
	}

	// Parse user (required)
	auto user_lookup = input.options.find("user");
	if (user_lookup != input.options.end()) {
		params.user = user_lookup->second.ToString();
	}

	// Parse password (required)
	auto password_lookup = input.options.find("password");
	if (password_lookup != input.options.end()) {
		params.password = password_lookup->second.ToString();
	}

	// Parse wallet_path (optional, for future use)
	auto wallet_lookup = input.options.find("wallet_path");
	if (wallet_lookup != input.options.end()) {
		params.wallet_path = wallet_lookup->second.ToString();
	}

	return params;
}

void ValidateOracleSecret(const OracleSecretParameters &params) {
	// Validate required parameters
	if (params.user.empty()) {
		throw InvalidInputException("Oracle secret requires USER parameter. "
		                            "Example: CREATE SECRET (TYPE oracle, HOST 'localhost', PORT 1521, "
		                            "SERVICE 'XEPDB1', USER 'scott', PASSWORD 'tiger')");
	}

	if (params.service.empty()) {
		throw InvalidInputException("Oracle secret requires SERVICE or DATABASE parameter. "
		                            "Example: CREATE SECRET (TYPE oracle, HOST 'localhost', PORT 1521, "
		                            "SERVICE 'XEPDB1', USER 'scott', PASSWORD 'tiger')");
	}

	if (params.password.empty()) {
		throw InvalidInputException("Oracle secret requires PASSWORD parameter. "
		                            "Example: CREATE SECRET (TYPE oracle, HOST 'localhost', PORT 1521, "
		                            "SERVICE 'XEPDB1', USER 'scott', PASSWORD 'tiger')");
	}

	// Validate port range
	if (params.port == 0 || params.port > 65535) {
		throw InvalidInputException("Oracle secret: PORT must be between 1 and 65535, got %llu", params.port);
	}
}

string BuildConnectionStringFromSecret(const KeyValueSecret &secret) {
	// Extract parameters from secret
	OracleSecretParameters params;

	Value val;
	if (secret.TryGetValue("host", val)) {
		params.host = val.ToString();
	}
	if (secret.TryGetValue("port", val)) {
		params.port = static_cast<idx_t>(val.GetValue<int64_t>());
	}
	if (secret.TryGetValue("service", val)) {
		params.service = val.ToString();
	} else if (secret.TryGetValue("database", val)) {
		params.service = val.ToString();
	}
	if (secret.TryGetValue("user", val)) {
		params.user = val.ToString();
	}
	if (secret.TryGetValue("password", val)) {
		params.password = val.ToString();
	}
	if (secret.TryGetValue("wallet_path", val)) {
		params.wallet_path = val.ToString();
	}

	// Validate before building connection string
	ValidateOracleSecret(params);

	// Handle wallet path if provided (set TNS_ADMIN environment variable)
	if (!params.wallet_path.empty()) {
		setenv("TNS_ADMIN", params.wallet_path.c_str(), 1);
	}

	// Build Oracle EZConnect format: user/password@host:port/service
	string connection_string = params.user;

	if (!params.password.empty()) {
		connection_string += "/" + params.password;
	}

	connection_string += "@" + params.host + ":" + std::to_string(params.port) + "/" + params.service;

	return connection_string;
}

unique_ptr<BaseSecret> CreateOracleSecretFromConfig(ClientContext &context, CreateSecretInput &input) {
	// Parse and validate parameters
	auto params = ParseOracleSecret(input);
	ValidateOracleSecret(params);

	// Create a KeyValueSecret to store the parameters
	auto secret = make_uniq<KeyValueSecret>(input.scope, input.type, input.provider, input.name);

	// Store all parameters in the secret
	secret->secret_map["host"] = Value(params.host);
	secret->secret_map["port"] = Value::BIGINT(params.port);
	secret->secret_map["service"] = Value(params.service);
	secret->secret_map["user"] = Value(params.user);
	secret->secret_map["password"] = Value(params.password);

	if (!params.wallet_path.empty()) {
		secret->secret_map["wallet_path"] = Value(params.wallet_path);
	}

	// Mark password as sensitive (will be redacted in output)
	secret->redact_keys.insert("password");

	return std::move(secret);
}

} // namespace duckdb
