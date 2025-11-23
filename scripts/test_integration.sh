#!/bin/bash
#
# Copyright 2025 DuckDB-Oracle Extension Authors
#
# Script to run integration tests against an Oracle Database container.
# Follows Google Shell Style Guide.
#
# Usage:
#   ./scripts/test_integration.sh [--keep-container] [--no-cleanup]
#
# Options:
#   --keep-container  Keep container running after tests (for debugging)
#   --no-cleanup      Alias for --keep-container
#
# Environment Variables:
#   ORACLE_IMAGE      Oracle container image (default: gvenzl/oracle-free:23-slim)
#   SKIP_BUILD        Skip build step if set to 1
#   CI                Set to true in CI environments (auto-cleanup disabled)

set -euo pipefail

# Constants
readonly DEFAULT_ORACLE_IMAGE="gvenzl/oracle-free:23-slim"
readonly CONTAINER_NAME="duckdb-oracle-test-db"
readonly DB_PASSWORD="password"
readonly DB_USER="duckdb_test"
readonly DB_USER_PWD="duckdb_test"
readonly SETUP_DIR="$(pwd)/test/integration/init_sql"

# Flags
CLEANUP_ENABLED=1

# Parse command line arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
    --keep-container|--no-cleanup)
      CLEANUP_ENABLED=0
      shift
      ;;
    --help|-h)
      echo "Usage: $0 [--keep-container] [--no-cleanup]"
      echo ""
      echo "Options:"
      echo "  --keep-container  Keep container running after tests"
      echo "  --no-cleanup      Alias for --keep-container"
      echo ""
      echo "Environment Variables:"
      echo "  ORACLE_IMAGE      Oracle container image"
      echo "  SKIP_BUILD        Skip build if set to 1"
      echo "  CI                Set to true in CI environments"
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      echo "Use --help for usage information" >&2
      exit 1
      ;;
  esac
done

# In CI, default to cleanup unless explicitly disabled
if [[ "${CI:-false}" == "true" && "$CLEANUP_ENABLED" -eq 1 ]]; then
  echo "Running in CI mode with cleanup enabled"
fi

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

  if [[ "$CLEANUP_ENABLED" -eq 0 ]]; then
    echo ""
    echo "Container cleanup disabled (--keep-container flag)"
    echo "Container '${CONTAINER_NAME}' is still running on port ${db_port:-unknown}"
    echo "Connection string: ${DB_USER}/${DB_USER_PWD}@//localhost:${db_port:-11521}/FREEPDB1"
    echo ""
    echo "To stop and remove manually:"
    echo "  ${RUNTIME} stop ${CONTAINER_NAME}"
    echo "  ${RUNTIME} rm ${CONTAINER_NAME}"
    return 0
  fi

  if [[ $exit_code -ne 0 ]]; then
    echo "Test failed (exit code: $exit_code). Container logs:"
    ${RUNTIME} logs "${CONTAINER_NAME}" 2>&1 | tail -n 100 || true
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
  local oracle_image="${1:-${ORACLE_IMAGE:-${DEFAULT_ORACLE_IMAGE}}}"
  
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

  # Set environment variables for integration tests
  # Tests use oracle_env() helper to read these
  export ORACLE_HOST="localhost"
  export ORACLE_PORT="${db_port}"
  export ORACLE_SERVICE="FREEPDB1"
  export ORACLE_USER="${DB_USER}"
  export ORACLE_PASSWORD="${DB_USER_PWD}"

  # Legacy connection string format (for simple smoke tests)
  export ORACLE_CONNECTION_STRING="${DB_USER}/${DB_USER_PWD}@//localhost:${db_port}/FREEPDB1"
  
  # Run the tests (assuming 'make test' or specific test runner)
  # For now, we just verify connection via the manual script or run specific tests
  # Ideally, we pass a flag to the test runner to run integration tests
  
  # Example: Verify connection with a simple DuckDB query
  # We need to ensure the extension is built first
  if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    echo "Building extension..."
    make release

    echo "Building unittest runner..."
    cmake --build build/release --target unittest
  else
    echo "SKIP_BUILD=1 set; assuming build artifacts already present."
  fi
  
  # Run integration tests from test/integration directory
  if [[ -d "test/integration" && $(find test/integration -name "*.test" | wc -l) -gt 0 ]]; then
    echo "Running integration test suite from test/integration/..."
    ./build/release/test/unittest "test/integration/*"
    echo "Integration tests completed successfully."
  else
    echo "No integration tests found in test/integration/"
    echo "Creating basic smoke test to verify Oracle connection..."

    # Create a minimal smoke test to verify the integration script works
    cat <<EOF > test/sql/test_integration_temp.test
# name: test_oracle_integration_smoke
# description: Basic Oracle integration smoke test
# group: [oracle_integration]

# Verify basic connection and query
query I
SELECT * FROM oracle_query('${ORACLE_CONNECTION_STRING}', 'SELECT 1 FROM DUAL');
----
1

EOF

    echo "Executing smoke test..."
    ./build/release/test/unittest "test/sql/test_integration_temp.test"
    echo "Smoke test completed successfully."
  fi
}

main "$@"
