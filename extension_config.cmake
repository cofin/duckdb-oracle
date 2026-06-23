# This file is included by DuckDB's build system. It specifies which extension to load

file(READ "${CMAKE_CURRENT_LIST_DIR}/description.yml" ORACLE_DESCRIPTION_YML)
string(REGEX MATCH "version:[ \t]*([0-9]+\\.[0-9]+\\.[0-9]+(-[A-Za-z0-9.]+)?)" _ORACLE_VERSION_MATCH "${ORACLE_DESCRIPTION_YML}")
if("${_ORACLE_VERSION_MATCH}" STREQUAL "")
    message(FATAL_ERROR "Could not read extension.version from description.yml")
endif()
set(ORACLE_EXTENSION_VERSION "${CMAKE_MATCH_1}")

# Extension from this repo
duckdb_extension_load(oracle
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
    EXTENSION_VERSION ${ORACLE_EXTENSION_VERSION}
)

# Any extra extensions that should be built
duckdb_extension_load(json)
