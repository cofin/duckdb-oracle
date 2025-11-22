#!/bin/bash
set -e

# Oracle Instant Client Version
OCI_VER_MAJOR=23
OCI_VER_FULL=23.6.0.24.10
# Base URL for Oracle Instant Client (Linux x64)
BASE_URL="https://download.oracle.com/otn_software/linux/instantclient/2360000"

INSTALL_DIR=$PWD/oracle_sdk
mkdir -p $INSTALL_DIR

echo "Downloading Oracle Instant Client ${OCI_VER_FULL}..."
wget -q -O basic.zip "${BASE_URL}/instantclient-basic-linux.x64-${OCI_VER_FULL}.zip"
wget -q -O sdk.zip "${BASE_URL}/instantclient-sdk-linux.x64-${OCI_VER_FULL}.zip"

echo "Extracting..."
unzip -q basic.zip -d $INSTALL_DIR
unzip -q sdk.zip -d $INSTALL_DIR

# The zip extracts to a folder like 'instantclient_23_6'
# We need to find it and normalize or export path
OCI_HOME=$(find $INSTALL_DIR -maxdepth 1 -name "instantclient_*" | head -n 1)

echo "Oracle Home found at: $OCI_HOME"

# Symlink libclntsh.so (sometimes needed if only .so.version exists)
# 23c usually handles this, but safety check
if [ ! -f "$OCI_HOME/libclntsh.so" ]; then
    ln -s "$OCI_HOME/libclntsh.so.23.1" "$OCI_HOME/libclntsh.so" || true
fi

# Export for subsequent steps (this script runs in subshell in make, 
# so we rely on the Workflow env vars or strict paths)
echo "OCI setup complete."
