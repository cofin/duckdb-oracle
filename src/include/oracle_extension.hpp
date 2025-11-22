#pragma once

#include "duckdb.hpp"

namespace duckdb {

class OracleExtension : public Extension {
public:
	/**
	 * Register all Oracle extension functions with DuckDB.
	 */
	void Load(ExtensionLoader &loader) override;
	/**
	 * Extension identifier used by DuckDB.
	 */
	std::string Name() override;
	/**
	 * Semantic version of the extension binary.
	 */
	std::string Version() const override;
};

} // namespace duckdb
