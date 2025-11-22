cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0079 NEW)

# Set extension name here
set(TARGET_NAME oracle)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# DuckDB's extension distribution supports vcpkg. As such, dependencies can be added in ./vcpkg.json and then
# used in cmake with find_package. Feel free to remove or replace with other dependencies.
# Note that it should also be removed from vcpkg.json to prevent needlessly installing it..
find_package(OpenSSL REQUIRED)

# OCI Library Detection
if(DEFINED ENV{ORACLE_HOME})
    set(ORACLE_HOME $ENV{ORACLE_HOME})
    message(STATUS "Found ORACLE_HOME: ${ORACLE_HOME}")
else()
    # Fallback to a common location (e.g., Linux Instant Client RPM install)
    set(ORACLE_HOME "/usr/share/oracle/instantclient_23_26")
    message(STATUS "ORACLE_HOME not set, using default: ${ORACLE_HOME}")
endif()

find_library(OCI_LIBRARY NAMES clntsh HINTS ${ORACLE_HOME} ${ORACLE_HOME}/lib)
find_path(OCI_INCLUDE_DIR NAMES oci.h HINTS ${ORACLE_HOME}/sdk/include ${ORACLE_HOME}/include)

if (NOT OCI_LIBRARY OR NOT OCI_INCLUDE_DIR)
    message(FATAL_ERROR "OCI library or include headers not found. Please set ORACLE_HOME environment variable.")
endif()

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)

set(EXTENSION_SOURCES src/oracle_extension.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Link OpenSSL in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(${LOADABLE_EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto)

# Link OCI in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} ${OCI_LIBRARY})
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${OCI_LIBRARY})

target_include_directories(${EXTENSION_NAME} PRIVATE ${OCI_INCLUDE_DIR})
target_include_directories(${LOADABLE_EXTENSION_NAME} PRIVATE ${OCI_INCLUDE_DIR})

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
