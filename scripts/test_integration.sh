#!/bin/bash
#
# Copyright 2025 DuckDB-Oracle Extension Authors
#
# Script to run integration tests against an Oracle Database container.
# Follows Google Shell Style Guide.

set -euo pipefail

# Constants
readonly DEFAULT_ORACLE_IMAGE="gvenzl/oracle-free:23-slim"
readonly CONTAINER_NAME="duckdb-oracle-test-db"
readonly DB_PASSWORD="password"
readonly DB_USER="duckdb_test"
readonly DB_USER_PWD="duckdb_test"
readonly SETUP_DIR="$(pwd)/test/integration/init_sql"

# Detect container runtime
if command -v podman >/dev/null 2>&1; then
  readonly RUNTIME="podman"
elif command -v docker >/dev/null 2>&1; then
  readonly RUNTIME="docker"
else
  echo "Error: Neither docker nor podman found." >&2
  exit 1
fi

#######################################
# Print error message to stderr.
# Arguments:
#   Message string
#######################################
err() {
  echo "[$(date +'%Y-%m-%dT%H:%M:%S%z')]: $*" >&2
}

#######################################
# Find a free port in a given range.
# Arguments:
#   Start port (default 1521)
#   End port (default 1550)
# Returns:
#   Available port number to stdout
#######################################
get_free_port() {
  local start_port="${1:-1521}"
  local end_port="${2:-1550}"
  
  for ((port=start_port; port<=end_port; port++)); do
    if ! nc -z localhost "$port" 2>/dev/null; then
      echo "$port"
      return 0
    fi
  done
  
  err "No free port found between $start_port and $end_port"
  return 1
}

#######################################
# Cleanup container on exit.
#######################################
cleanup() {
  local exit_code=$?
  if [[ $exit_code -ne 0 ]]; then
    echo "Test failed. Container logs:"
    ${RUNTIME} logs "${CONTAINER_NAME}" || true
  fi
  echo "Cleaning up container ${CONTAINER_NAME}..."
  ${RUNTIME} stop "${CONTAINER_NAME}" >/dev/null 2>&1 || true
  ${RUNTIME} rm "${CONTAINER_NAME}" >/dev/null 2>&1 || true
  rm -f test/sql/test_integration_temp.test
}

#######################################
# Wait for Oracle to be ready.
# Arguments:
#   Container name
#######################################
wait_for_db() {
  local container="$1"
  echo "Waiting for Oracle Database to be ready..."
  
  # Loop until the logs show the database is ready
  # gvenzl images print "DATABASE IS READY TO USE!"
  local retries=300
  local count=0
  
  set +o pipefail
  while [[ "${count}" -lt "${retries}" ]]; do
    if ${RUNTIME} logs "${container}" 2>&1 | grep -q "DATABASE IS READY TO USE!"; then
      echo "Oracle Database is ready."
      set -o pipefail
      return 0
    fi
    
    # Check if container is still running
    if [[ -z "$(${RUNTIME} ps -q -f name="${container}")" ]]; then
       echo "Container died unexpectedly."
       set -o pipefail
       return 1
    fi

    sleep 2
    ((++count))
    echo -n "$count "
  done
  set -o pipefail
  
  echo "" # Newline
  err "Timeout waiting for database to start."
  return 1
}

#######################################
# Main execution.
#######################################
main() {
  local oracle_image="${1:-${DEFAULT_ORACLE_IMAGE}}"
  
  trap cleanup EXIT

  echo "Finding available port..."
  # Start looking from 11521 to avoid standard 1521 conflicts
  local db_port
  db_port=$(get_free_port 11521 11600)
  echo "Using port: ${db_port}"

  echo "Starting Oracle container using ${RUNTIME}..."
  echo "Image: ${oracle_image}"
  
  # Start container
  # We map the setup scripts to /container-entrypoint-initdb.d which gvenzl images run on startup
  ${RUNTIME} run -d \
    --name "${CONTAINER_NAME}" \
    -p "${db_port}:1521" \
    -e ORACLE_PASSWORD="${DB_PASSWORD}" \
    -e APP_USER="${DB_USER}" \
    -e APP_USER_PASSWORD="${DB_USER_PWD}" \
    -v "${SETUP_DIR}:/container-entrypoint-initdb.d" \
    "${oracle_image}"

  wait_for_db "${CONTAINER_NAME}"

  echo "Running DuckDB integration tests..."
  
  # Set environment variables for the test
  export ORACLE_CONNECTION_STRING="${DB_USER}/${DB_USER_PWD}@//localhost:${db_port}/FREEPDB1"
  
  # Run the tests (assuming 'make test' or specific test runner)
  # For now, we just verify connection via the manual script or run specific tests
  # Ideally, we pass a flag to the test runner to run integration tests
  
  # Example: Verify connection with a simple DuckDB query
  # We need to ensure the extension is built first
  echo "Building extension..."
  make release
  
  echo "Building unittest runner..."
  cmake --build build/release --target unittest
  
  # Create a temporary test script (SQLLogicTest format) inside test directory
  # unittest runner expects files in test/ directory usually
  cat <<EOF > test/sql/test_integration_temp.test
# name: test_connection
# description: Integration test
# group: [oracle_integration]

statement ok
LOAD 'build/release/extension/oracle/oracle.duckdb_extension';

statement ok
SELECT oracle_attach_wallet('/container-entrypoint-initdb.d');

# Verify connection and basic query
query I
SELECT * FROM oracle_query('${ORACLE_CONNECTION_STRING}', 'SELECT 1 FROM DUAL');
----
1

# Verify Scan
query IIII
SELECT id, val_varchar, val_number, val_date FROM oracle_scan('${ORACLE_CONNECTION_STRING}', 'DUCKDB_TEST', 'TEST_TYPES');
----
1	test	123.45	2025-11-22 00:00:00	2025-11-22 00:00:00

# Verify Vector (as string)
query I
SELECT val_vector FROM oracle_scan('${ORACLE_CONNECTION_STRING}', 'DUCKDB_TEST', 'TEST_TYPES');
----
[1.1,2.2,3.3]

EOF

  echo "Executing test query using unittest..."
  ./build/release/test/unittest "test/sql/test_integration_temp.test"
  
  echo "Integration tests completed successfully."
}

main "$@"
