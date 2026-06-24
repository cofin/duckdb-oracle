cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0079 NEW)

# Set extension name here
set(TARGET_NAME oracle)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# OCI Library Detection
if(DEFINED ENV{ORACLE_HOME})
    set(ORACLE_HOME $ENV{ORACLE_HOME})
    message(STATUS "Found ORACLE_HOME: ${ORACLE_HOME}")
else()
    # Check for local CI install
    file(GLOB LOCAL_OCI_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/oracle_sdk/instantclient_*")
    if(LOCAL_OCI_DIRS)
        list(GET LOCAL_OCI_DIRS 0 ORACLE_HOME)
        message(STATUS "Found local CI ORACLE_HOME: ${ORACLE_HOME}")
    else()
        # Fallback to a common location (e.g., Linux Instant Client RPM install)
        set(ORACLE_HOME "/usr/share/oracle/instantclient_23_26")
        message(STATUS "ORACLE_HOME not set, using default: ${ORACLE_HOME}")
    endif()
endif()

find_library(OCI_LIBRARY NAMES clntsh oci HINTS ${ORACLE_HOME} ${ORACLE_HOME}/lib ${ORACLE_HOME}/sdk/lib/msvc)
find_library(OCICORE_LIBRARY NAMES clntshcore HINTS ${ORACLE_HOME} ${ORACLE_HOME}/lib)
find_library(NNZ_LIBRARY NAMES nnz HINTS ${ORACLE_HOME} ${ORACLE_HOME}/lib)
find_path(OCI_INCLUDE_DIR NAMES oci.h HINTS ${ORACLE_HOME}/sdk/include ${ORACLE_HOME}/include)

if (NOT OCI_LIBRARY OR NOT OCI_INCLUDE_DIR)
    message(FATAL_ERROR "OCI library or include headers not found. Please set ORACLE_HOME environment variable.")
endif()

# Additional Oracle libraries (optional but recommended for full functionality)
if (OCICORE_LIBRARY)
    message(STATUS "Found libclntshcore: ${OCICORE_LIBRARY}")
endif()
if (NNZ_LIBRARY)
    message(STATUS "Found libnnz: ${NNZ_LIBRARY}")
endif()

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES ${OCI_INCLUDE_DIR})
check_cxx_source_compiles("
#include <oci.h>
#ifndef SQLT_JSON
#error SQLT_JSON missing
#endif
#ifndef SQLT_VEC
#error SQLT_VEC missing
#endif
#ifndef SQLT_BOL
#error SQLT_BOL missing
#endif
int main() {
    return SQLT_JSON == 0 || SQLT_VEC == 0 || SQLT_BOL == 0;
}
" ORACLE_OCI_HAS_NATIVE_JSON_VECTOR_TYPES)
unset(CMAKE_REQUIRED_INCLUDES)
if (NOT ORACLE_OCI_HAS_NATIVE_JSON_VECTOR_TYPES)
    message(FATAL_ERROR "Oracle Instant Client SDK headers must define SQLT_JSON, SQLT_VEC, and SQLT_BOL. Install Oracle Instant Client SDK 23.6 or newer; the setup scripts target the current 23.26 line.")
endif()

include_directories(src/include)

set(EXTENSION_SOURCES
    src/oracle_extension.cpp
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

# Link OCI in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} ${OCI_LIBRARY})
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${OCI_LIBRARY})

# Link additional Oracle libraries if found (required for ARM64 and some configurations)
if (OCICORE_LIBRARY)
    target_link_libraries(${EXTENSION_NAME} ${OCICORE_LIBRARY})
    target_link_libraries(${LOADABLE_EXTENSION_NAME} ${OCICORE_LIBRARY})
endif()
if (NNZ_LIBRARY)
    target_link_libraries(${EXTENSION_NAME} ${NNZ_LIBRARY})
    target_link_libraries(${LOADABLE_EXTENSION_NAME} ${NNZ_LIBRARY})
endif()

target_include_directories(${EXTENSION_NAME} PRIVATE ${OCI_INCLUDE_DIR})
target_include_directories(${LOADABLE_EXTENSION_NAME} PRIVATE ${OCI_INCLUDE_DIR})

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
