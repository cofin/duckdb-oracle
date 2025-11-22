SHELL := /usr/bin/env bash
MAKEFLAGS += --no-builtin-rules
PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=oracle
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

ORACLE_IMAGE ?= gvenzl/oracle-free:23-slim

.PHONY: configure_ci integration help

configure_ci:
	./scripts/setup_oci_linux.sh



# Build (release) then run integration tests against containerized Oracle.
integration: release
	SKIP_BUILD=1 ORACLE_IMAGE=$(ORACLE_IMAGE) ./scripts/test_integration.sh

help:
	@printf "Available targets:\n"
	@printf "  release          Build the extension in release mode (from ci tools)\n"
	@printf "  debug            Build the extension in debug mode (from ci tools)\n"
	@printf "  test             Run SQL tests (from ci tools)\n"
	@printf "  integration      Run containerized Oracle integration tests (uses ORACLE_IMAGE=%s)\n" "$(ORACLE_IMAGE)"
	@printf "  configure_ci     Install OCI prerequisites for CI/local env\n"
