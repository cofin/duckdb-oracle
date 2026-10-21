SHELL := /usr/bin/env bash
MAKEFLAGS += --no-builtin-rules
PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=oracle
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Prefer fast builds if tools are available
ifneq ($(shell command -v ninja 2>/dev/null),)
GEN ?= ninja
endif

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

ORACLE_IMAGE ?= gvenzl/oracle-free:23-slim

.PHONY: configure_ci configure_oci tidy-check integration help clean-all ensure-libaio lint bump-version bump-prerelease bump-duckdb test-unit

# Detect OS for Oracle Instant Client runtime setup (integration tests only)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    OCI_SETUP_SCRIPT := ./scripts/setup_oci_linux.sh
endif

# Helper function to ensure libaio is available (needed for Oracle Instant Client runtime in integration tests)
define ensure_libaio
	@SUDO=""; \
	if [ "$$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1; then \
		SUDO="sudo"; \
	fi; \
	if ! [ -f /usr/lib/x86_64-linux-gnu/libaio.so.1 ] && ! [ -f /lib/x86_64-linux-gnu/libaio.so.1 ] && ! [ -f /usr/lib64/libaio.so.1 ] && ! [ -f /usr/lib/aarch64-linux-gnu/libaio.so.1 ] && ! [ -f /lib/aarch64-linux-gnu/libaio.so.1 ]; then \
		echo "libaio.so.1 not found - installing..."; \
		if command -v apt-get >/dev/null 2>&1; then \
			$$SUDO apt-get update -qq && ($$SUDO apt-get install -y --no-install-recommends libaio1t64 || $$SUDO apt-get install -y --no-install-recommends libaio1 || $$SUDO apt-get install -y --no-install-recommends libaio-dev); \
		elif command -v yum >/dev/null 2>&1; then \
			$$SUDO yum install -y libaio; \
		elif command -v dnf >/dev/null 2>&1; then \
			$$SUDO dnf install -y libaio; \
		elif command -v apk >/dev/null 2>&1; then \
			$$SUDO apk add --no-cache libaio; \
		else \
			echo "ERROR: No supported package manager found (apt-get, yum, dnf, apk)"; \
			exit 1; \
		fi; \
	fi; \
	if [ -f /usr/lib/x86_64-linux-gnu/libaio.so.1t64 ] && ! [ -f /usr/lib/x86_64-linux-gnu/libaio.so.1 ]; then \
		$$SUDO ln -sf /usr/lib/x86_64-linux-gnu/libaio.so.1t64 /usr/lib/x86_64-linux-gnu/libaio.so.1 || true; \
	fi; \
	if [ -f /usr/lib/aarch64-linux-gnu/libaio.so.1t64 ] && ! [ -f /usr/lib/aarch64-linux-gnu/libaio.so.1 ]; then \
		$$SUDO ln -sf /usr/lib/aarch64-linux-gnu/libaio.so.1t64 /usr/lib/aarch64-linux-gnu/libaio.so.1 || true; \
	fi
endef

configure_oci:
	@echo "Running Oracle Instant Client runtime setup..."
	$(OCI_SETUP_SCRIPT)
ifeq ($(UNAME_S),Linux)
	$(call ensure_libaio)
endif
	@echo "configure_oci complete"

# Run only unit tests (integration tests require an Oracle container).
# Unit tests and builds require zero external OCI SDK or runtime libraries.
TESTS_BASE_DIRECTORY = test/unit_tests/
test-unit: test
ensure-libaio:
ifeq ($(UNAME_S),Linux)
	$(call ensure_libaio)
endif

# Build (release), install Instant Client runtime, then run integration tests against containerized Oracle.
# Runs both unit tests (test/unit_tests/) and integration tests (test/integration_tests/)
integration: configure_oci release
	SKIP_BUILD=1 ORACLE_IMAGE=$(ORACLE_IMAGE) ./scripts/test_integration.sh

lint: format-check tidy-check

# --- Versioning -------------------------------------------------------------
# NOTE: `release`/`debug`/`test` are the BUILD targets from extension-ci-tools,
# so version targets are `bump-`prefixed (unlike the Python litestar projects
# where `make release` is free to mean "bump version").
#
# Extension version (own version) via bump-my-version. description.yml is the
# source of truth; auto-tag.yml tags it on push to main.
#   make bump-version bump=patch | minor | major
bump-version:
	uvx bump-my-version bump $(bump)
	@echo "Extension version is now: $$(grep -E '^  version:' description.yml | awk '{print $$2}')"

#   make bump-prerelease version=0.3.0-alpha.1
bump-prerelease:
	@if [ -z "$(version)" ]; then echo "Usage: make bump-prerelease version=X.Y.Z-alpha.N"; exit 1; fi
	uvx bump-my-version bump --new-version $(version) pre

# DuckDB upstream version (submodules + CI refs + compat row + ext patch).
#   make bump-duckdb VERSION=v1.5.4
#   make bump-duckdb VERSION=v1.5.4 ARGS=--dry-run
bump-duckdb:
	@if [ -z "$(VERSION)" ]; then echo "Usage: make bump-duckdb VERSION=vX.Y.Z [ARGS=--dry-run]"; exit 1; fi
	./scripts/bump_duckdb_version.sh $(VERSION) $(ARGS)

help:
	@printf "Available targets:\n"
	@printf "  release          Build the extension in release mode (from ci tools)\n"
	@printf "  debug            Build the extension in debug mode (from ci tools)\n"
	@printf "  test             Run unit tests only (smoke tests, no Oracle container required)\n"
	@printf "  integration      Run full test suite with Oracle container (uses ORACLE_IMAGE=%s)\n" "$(ORACLE_IMAGE)"
	@printf "  lint             Run format-check and tidy-check\n"
	@printf "  bump-version     Bump extension version (bump-my-version): make bump-version bump=patch\n"
	@printf "  bump-prerelease  Start a pre-release: make bump-prerelease version=0.3.0-alpha.1\n"
	@printf "  bump-duckdb      Bump targeted DuckDB version: make bump-duckdb VERSION=v1.5.4 [ARGS=--dry-run]\n"
	@printf "  configure_ci     Install OCI prerequisites for CI/local env\n"
	@printf "  clean-all        Remove all build directories to allow switching generators (e.g., Ninja)\n"

clean-all:
	rm -rf build/release build/debug build/relassert build/reldebug build/ninja-release build/extension_configuration build/tidy
