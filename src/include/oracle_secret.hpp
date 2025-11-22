#pragma once

#include "duckdb.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/parser/parsed_data/create_secret_info.hpp"

namespace duckdb {

//! Oracle secret parameters structure
struct OracleSecretParameters {
	string host = "localhost";
	idx_t port = 1521;
	string service;      // Oracle service name (required)
	string database;     // Alias for service
	string user;         // Oracle username (required)
	string password;     // Oracle password (required)
	string wallet_path;  // Optional: path to Oracle Wallet
};

//! Parse Oracle secret from CreateSecretInput
OracleSecretParameters ParseOracleSecret(const CreateSecretInput &input);

//! Validate Oracle secret parameters
void ValidateOracleSecret(const OracleSecretParameters &params);

//! Build Oracle connection string from secret parameters
//! Returns EZConnect format: user/password@host:port/service
string BuildConnectionStringFromSecret(const KeyValueSecret &secret);

//! Create secret function for Oracle secrets
unique_ptr<BaseSecret> CreateOracleSecretFromConfig(ClientContext &context, CreateSecretInput &input);

} // namespace duckdb
