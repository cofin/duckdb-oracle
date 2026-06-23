#pragma once

#include "duckdb.hpp"

namespace duckdb {

//! Register Oracle scalar functions, table functions, and secret handlers.
void RegisterOracleFunctions(ExtensionLoader &loader);

} // namespace duckdb
