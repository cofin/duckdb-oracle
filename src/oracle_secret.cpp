#include "oracle_secret.hpp"
#include "duckdb/common/case_insensitive_map.hpp"
#include <sys/stat.h>

namespace duckdb {

namespace {
static bool PathIsDirectory(const string &path) {
	struct stat st {};
	if (stat(path.c_str(), &st) != 0) {
		return false;
	}
#ifdef _WIN32
	return (st.st_mode & _S_IFDIR) == _S_IFDIR;
#else
	return S_ISDIR(st.st_mode);
#endif
}
} // namespace

OracleSecretParameters ParseOracleSecret(const CreateSecretInput &input) {
	OracleSecretParameters params;

	// Parse host (optional, default: localhost)
	auto host_lookup = input.options.find("host");
	if (host_lookup != input.options.end()) {
		params.host = host_lookup->second.ToString();
		params.host_provided = true;
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
		// DATABASE is an alias for SERVICE
		if (params.service.empty()) {
			params.service = database_lookup->second.ToString();
		}
	}

	// Parse user (required unless wallet_path uses external password store)
	auto user_lookup = input.options.find("user");
	if (user_lookup != input.options.end()) {
		params.user = user_lookup->second.ToString();
	}

	// Parse password (required unless wallet_path uses external password store)
	auto password_lookup = input.options.find("password");
	if (password_lookup != input.options.end()) {
		params.password = password_lookup->second.ToString();
	}

	// Parse wallet_path (optional)
	auto wallet_lookup = input.options.find("wallet_path");
	if (wallet_lookup != input.options.end()) {
		params.wallet_path = wallet_lookup->second.ToString();
	}

	return params;
}

void ValidateOracleWalletPath(const string &wallet_path) {
	if (!wallet_path.empty() && !PathIsDirectory(wallet_path)) {
		throw InvalidInputException("WALLET_PATH does not exist or is not a directory: %s", wallet_path.c_str());
	}
}

void ValidateOracleSecret(const OracleSecretParameters &params) {
	if (params.service.empty()) {
		throw InvalidInputException("Oracle secret requires SERVICE or DATABASE parameter. "
		                            "Example: CREATE SECRET (TYPE oracle, HOST 'localhost', PORT 1521, "
		                            "SERVICE 'XEPDB1', USER 'scott', PASSWORD 'tiger')");
	}

	if (params.wallet_path.empty()) {
		if (params.user.empty()) {
			throw InvalidInputException("Oracle secret requires USER parameter. "
			                            "Example: CREATE SECRET (TYPE oracle, HOST 'localhost', PORT 1521, "
			                            "SERVICE 'XEPDB1', USER 'scott', PASSWORD 'tiger')");
		}
		if (params.password.empty()) {
			throw InvalidInputException("Oracle secret requires PASSWORD parameter. "
			                            "Example: CREATE SECRET (TYPE oracle, HOST 'localhost', PORT 1521, "
			                            "SERVICE 'XEPDB1', USER 'scott', PASSWORD 'tiger')");
		}
	} else if (params.user.empty() != params.password.empty()) {
		throw InvalidInputException("Oracle WALLET_PATH secret requires either both USER and PASSWORD or neither (for "
		                            "external password store)");
	}

	if (params.user.find('/') != string::npos) {
		throw InvalidInputException("Oracle secret USER cannot contain '/'");
	}

	if ((params.wallet_path.empty() || params.host_provided) && params.host.empty()) {
		throw InvalidInputException("Oracle secret HOST cannot be empty");
	}

	// Validate port range
	if (params.port == 0 || params.port > 65535) {
		throw InvalidInputException("Oracle secret: PORT must be between 1 and 65535, got %llu", params.port);
	}
	ValidateOracleWalletPath(params.wallet_path);
}

string BuildConnectionStringFromSecret(const KeyValueSecret &secret) {
	// Extract parameters from secret
	OracleSecretParameters params;

	Value val;
	if (secret.TryGetValue("host", val)) {
		params.host = val.ToString();
		params.host_provided = true;
	}
	if (secret.TryGetValue("port", val)) {
		auto port_val = val.GetValue<int64_t>();
		if (port_val <= 0 || port_val > 65535) {
			throw InvalidInputException("Oracle secret: PORT must be between 1 and 65535, got %lld", port_val);
		}
		params.port = static_cast<idx_t>(port_val);
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

	string creds = (params.user.empty() && params.password.empty()) ? "/" : (params.user + "/" + params.password);
	if (!params.wallet_path.empty() && !params.host_provided) {
		return creds + "@" + params.service;
	}
	return creds + "@" + params.host + ":" + std::to_string(params.port) + "/" + params.service;
}

string GetWalletPathFromSecret(const KeyValueSecret &secret) {
	Value val;
	if (secret.TryGetValue("wallet_path", val)) {
		return val.ToString();
	}
	return "";
}

unique_ptr<BaseSecret> CreateOracleSecretFromConfig(ClientContext &context, CreateSecretInput &input) {
	// Parse and validate parameters
	auto params = ParseOracleSecret(input);
	ValidateOracleSecret(params);

	// Create a KeyValueSecret to store the parameters
	auto secret = make_uniq<KeyValueSecret>(input.scope, input.type, input.provider, input.name);

	// Store parameters in the secret
	if (params.host_provided || params.wallet_path.empty()) {
		secret->secret_map["host"] = Value(params.host);
	}
	secret->secret_map["port"] = Value::BIGINT(static_cast<int64_t>(params.port));
	secret->secret_map["service"] = Value(params.service);
	secret->secret_map["user"] = Value(params.user);
	secret->secret_map["password"] = Value(params.password);

	if (!params.wallet_path.empty()) {
		secret->secret_map["wallet_path"] = Value(params.wallet_path);
	}

	// Mark password as sensitive (will be redacted in output)
	secret->redact_keys.insert("password");

	return secret;
}

} // namespace duckdb
