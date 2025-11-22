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

.PHONY: configure_ci tidy-check integration help clean-all

# Detect OS for Oracle Instant Client setup
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    OCI_SETUP_SCRIPT := ./scripts/setup_oci_linux.sh
endif
ifeq ($(UNAME_S),Darwin)
    OCI_SETUP_SCRIPT := ./scripts/setup_oci_macos.sh
endif
ifeq ($(OS),Windows_NT)
    OCI_SETUP_SCRIPT := powershell -ExecutionPolicy Bypass -File ./scripts/setup_oci_windows.ps1
endif

configure_ci:
	@echo "Running Oracle Instant Client setup..."
	$(OCI_SETUP_SCRIPT)
	@echo "Verifying libaio installation..."
	@if ! [ -f /usr/lib/x86_64-linux-gnu/libaio.so.1 ] && ! [ -f /lib/x86_64-linux-gnu/libaio.so.1 ] && ! [ -f /usr/lib64/libaio.so.1 ] && ! [ -f /usr/lib/aarch64-linux-gnu/libaio.so.1 ] && ! [ -f /lib/aarch64-linux-gnu/libaio.so.1 ]; then \
		echo "WARNING: libaio.so.1 not found. Tests may fail to run."; \
		echo "Attempting to install libaio..."; \
		if command -v apt-get >/dev/null 2>&1; then \
			apt-get update -y && (apt-get install -y --no-install-recommends libaio1t64 || apt-get install -y --no-install-recommends libaio1 || apt-get install -y --no-install-recommends libaio-dev); \
		fi; \
	fi
	@echo "Creating libaio symlink if needed (Ubuntu 24.04 libaio1t64)..."
	@if [ -f /usr/lib/x86_64-linux-gnu/libaio.so.1t64 ] && ! [ -f /usr/lib/x86_64-linux-gnu/libaio.so.1 ]; then \
		ln -sf /usr/lib/x86_64-linux-gnu/libaio.so.1t64 /usr/lib/x86_64-linux-gnu/libaio.so.1; \
		echo "Created symlink: /usr/lib/x86_64-linux-gnu/libaio.so.1 -> libaio.so.1t64"; \
	fi
	@if [ -f /usr/lib/aarch64-linux-gnu/libaio.so.1t64 ] && ! [ -f /usr/lib/aarch64-linux-gnu/libaio.so.1 ]; then \
		ln -sf /usr/lib/aarch64-linux-gnu/libaio.so.1t64 /usr/lib/aarch64-linux-gnu/libaio.so.1; \
		echo "Created symlink: /usr/lib/aarch64-linux-gnu/libaio.so.1 -> libaio.so.1t64"; \
	fi
	@echo "configure_ci complete"

tidy-check:
	$(OCI_SETUP_SCRIPT)
	export ORACLE_HOME=$$(find $$(pwd)/oracle_sdk -maxdepth 1 -name "instantclient_*" | head -n1) && \
	export LD_LIBRARY_PATH=$$ORACLE_HOME:$$LD_LIBRARY_PATH && \
	mkdir -p ./build/tidy && \
	cmake $(GENERATOR) $(BUILD_FLAGS) $(EXT_DEBUG_FLAGS) -DDISABLE_UNITY=1 -DCLANG_TIDY=1 -S $(DUCKDB_SRCDIR) -B build/tidy && \
	cp duckdb/.clang-tidy build/tidy/.clang-tidy && \
	cd build/tidy && python3 ../../duckdb/scripts/run-clang-tidy.py '$(PROJ_DIR)src/.*/' -header-filter '$(PROJ_DIR)src/.*/' -quiet ${TIDY_THREAD_PARAMETER} ${TIDY_BINARY_PARAMETER} ${TIDY_PERFORM_CHECKS}


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
	@printf "  clean-all        Remove all build directories to allow switching generators (e.g., Ninja)\n"

clean-all:
	rm -rf build/release build/debug build/relassert build/reldebug build/ninja-release build/extension_configuration
