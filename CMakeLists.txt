cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0079 NEW)

# Set extension name here
set(TARGET_NAME oracle)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)

set(EXTENSION_SOURCES
    src/oracle_extension.cpp
    src/oracle_oci_api.cpp
    src/oracle_connection.cpp
    src/oracle_connection_resolver.cpp
    src/oracle_connection_manager.cpp
    src/oracle_debug_stats.cpp
    src/oracle_secret.cpp
    src/oracle_scan.cpp
    src/oracle_pushdown.cpp
    src/oracle_functions.cpp
    src/oracle_settings.cpp
    src/oracle_type_conversion.cpp
    src/oracle_type_registry.cpp
    src/storage/oracle_catalog.cpp
    src/storage/oracle_schema_entry.cpp
    src/storage/oracle_table_entry.cpp
    src/storage/oracle_storage_extension.cpp
    src/storage/oracle_transaction.cpp
    src/storage/oracle_transaction_manager.cpp
    src/storage/oracle_write.cpp
    src/storage/oracle_insert.cpp
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

if(CMAKE_DL_LIBS)
    target_link_libraries(${EXTENSION_NAME} ${CMAKE_DL_LIBS})
    target_link_libraries(${LOADABLE_EXTENSION_NAME} ${CMAKE_DL_LIBS})
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
