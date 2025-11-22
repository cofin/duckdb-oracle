#!/bin/bash
set -e

# Oracle Instant Client Version (full client, not Basic Lite)
OCI_VER_MAJOR=23
OCI_VER_FULL=23.6.0.24.10
# Base URL for Oracle Instant Client (Linux x64). If Oracle rotates the path, we fall back to latest links.
BASE_URL="https://download.oracle.com/otn_software/linux/instantclient/2360000"
LATEST_BASE_URL="https://download.oracle.com/otn_software/linux/instantclient"

INSTALL_DIR=$PWD/oracle_sdk
# Ensure a clean install location to avoid unzip prompts on CI reruns
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"

echo "Downloading Oracle Instant Client ${OCI_VER_FULL}"...
wget -q -O basic.zip "${BASE_URL}/instantclient-basic-linux.x64-${OCI_VER_FULL}.zip" || \
  wget -q -O basic.zip "${LATEST_BASE_URL}/instantclient-basic-linux.x64.zip"
wget -q -O sdk.zip "${BASE_URL}/instantclient-sdk-linux.x64-${OCI_VER_FULL}.zip" || \
  wget -q -O sdk.zip "${LATEST_BASE_URL}/instantclient-sdk-linux.x64.zip"

echo "Extracting..."
unzip -oq basic.zip -d "$INSTALL_DIR"
unzip -oq sdk.zip -d "$INSTALL_DIR"

# The zip extracts to a folder like 'instantclient_23_6'
# We need to find it and normalize or export path
OCI_HOME=$(find $INSTALL_DIR -maxdepth 1 -name "instantclient_*" | head -n 1)

echo "Oracle Home found at: $OCI_HOME"

# Ensure libaio is available on Ubuntu 24.04 (libaio1 -> libaio1t64) and other runners.
# Avoid sudo; CI containers typically run as root.
if ! [ -f /usr/lib/x86_64-linux-gnu/libaio.so.1 ] && ! [ -f /lib/x86_64-linux-gnu/libaio.so.1 ]; then
  echo "Installing libaio runtime dependency..."
  
  SUDO=""
  if [ "$EUID" -ne 0 ] && command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
  fi

  if command -v apt-get >/dev/null 2>&1; then
    $SUDO apt-get update -y >/dev/null
    $SUDO apt-get install -y --no-install-recommends libaio1t64 || $SUDO apt-get install -y --no-install-recommends libaio-dev || true
  elif command -v apk >/dev/null 2>&1; then
        apk add --no-cache libaio || true
  else
        echo "Warning: no supported package manager found; continuing without installing libaio."
  fi
    if [ -f /usr/lib/x86_64-linux-gnu/libaio.so.1t64 ] && [ ! -f /usr/lib/x86_64-linux-gnu/libaio.so.1 ]; then
        $SUDO ln -sf /usr/lib/x86_64-linux-gnu/libaio.so.1t64 /usr/lib/x86_64-linux-gnu/libaio.so.1
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

# Try to register with ldconfig when permitted
if command -v ldconfig >/dev/null 2>&1; then
  SUDO=""
  if [ "$EUID" -ne 0 ] && command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
  fi
  
  if [ -n "$SUDO" ]; then
     echo "$OCI_HOME" | $SUDO tee /etc/ld.so.conf.d/oracle-instantclient.conf > /dev/null || true
  else
     echo "$OCI_HOME" > /etc/ld.so.conf.d/oracle-instantclient.conf || true
  fi
  $SUDO ldconfig || true
fi

echo "OCI setup complete."