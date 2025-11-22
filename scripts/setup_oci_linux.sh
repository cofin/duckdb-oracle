#!/bin/bash
set -e

# Oracle Instant Client Version
OCI_VER_MAJOR=23
OCI_VER_FULL=23.6.0.24.10
# Base URL for Oracle Instant Client (Linux x64)
BASE_URL="https://download.oracle.com/otn_software/linux/instantclient/2360000"

INSTALL_DIR=$PWD/oracle_sdk
# Ensure a clean install location to avoid unzip prompts on CI reruns
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"

echo "Downloading Oracle Instant Client ${OCI_VER_FULL}..."
wget -q -O basic.zip "${BASE_URL}/instantclient-basic-linux.x64-${OCI_VER_FULL}.zip"
wget -q -O sdk.zip "${BASE_URL}/instantclient-sdk-linux.x64-${OCI_VER_FULL}.zip"

echo "Extracting..."
unzip -oq basic.zip -d "$INSTALL_DIR"
unzip -oq sdk.zip -d "$INSTALL_DIR"

# The zip extracts to a folder like 'instantclient_23_6'
# We need to find it and normalize or export path
OCI_HOME=$(find $INSTALL_DIR -maxdepth 1 -name "instantclient_*" | head -n 1)

echo "Oracle Home found at: $OCI_HOME"

# Ensure libaio is available on Ubuntu 24.04 where libaio1 -> libaio1t64 transition removed the compat symlink
if ! ldconfig -p | grep -q "libaio.so.1"; then
    echo "Installing libaio runtime dependency..."
    sudo apt-get update -y >/dev/null
    sudo apt-get install -y libaio1t64 || sudo apt-get install -y libaio1
    if [ -f /usr/lib/x86_64-linux-gnu/libaio.so.1t64 ] && [ ! -f /usr/lib/x86_64-linux-gnu/libaio.so.1 ]; then
        sudo ln -sf /usr/lib/x86_64-linux-gnu/libaio.so.1t64 /usr/lib/x86_64-linux-gnu/libaio.so.1
    fi
fi

# Symlink libclntsh.so (sometimes needed if only .so.version exists)
# 23c usually handles this, but safety check
if [ ! -f "$OCI_HOME/libclntsh.so" ]; then
    ln -s "$OCI_HOME/libclntsh.so.23.1" "$OCI_HOME/libclntsh.so" || true
fi

# Export for subsequent steps (callers should `source` this script if they want env vars in-shell)
export ORACLE_HOME="$OCI_HOME"
export LD_LIBRARY_PATH="$OCI_HOME:${LD_LIBRARY_PATH}"
echo "export ORACLE_HOME=\"$ORACLE_HOME\"" > "$INSTALL_DIR/env.sh"
echo "export LD_LIBRARY_PATH=\"$LD_LIBRARY_PATH\"" >> "$INSTALL_DIR/env.sh"

echo "OCI setup complete."
