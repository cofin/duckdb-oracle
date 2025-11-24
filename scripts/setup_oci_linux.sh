#!/bin/bash
set -e

# Oracle Instant Client Version (full client, not Basic Lite)
OCI_VER_MAJOR=23
OCI_VER_FULL=23.6.0.24.10
# Base URL for Oracle Instant Client (Linux x64). If Oracle rotates the path, we fall back to latest links.
BASE_URL="https://download.oracle.com/otn_software/linux/instantclient/2360000"
LATEST_BASE_URL="https://download.oracle.com/otn_software/linux/instantclient"

# Detect architecture
ARCH=$(uname -m)
case "$ARCH" in
    x86_64)
        OCI_ARCH="linux.x64"
        LIBAIO_ARCH="x86_64-linux-gnu"
        ;;
    aarch64|arm64)
        OCI_ARCH="linux.arm64"
        LIBAIO_ARCH="aarch64-linux-gnu"
        ;;
    *)
        echo "Error: Unsupported architecture: $ARCH"
        exit 1
        ;;
esac

echo "Detected architecture: $ARCH (using $OCI_ARCH)"

INSTALL_DIR=$PWD/oracle_sdk
mkdir -p "$INSTALL_DIR"

SKIP_DOWNLOAD=0
OCI_HOME=""

# 1. Check if ORACLE_HOME is set and valid
if [ -n "${ORACLE_HOME:-}" ] && [ -f "$ORACLE_HOME/libclntsh.so" ] && [ -f "$ORACLE_HOME/sdk/include/oci.h" ]; then
    echo "Using existing system ORACLE_HOME: $ORACLE_HOME"
    OCI_HOME="$ORACLE_HOME"
    SKIP_DOWNLOAD=1
else
    # 2. Check if we have a valid local install
    # We look for instantclient_* directory inside INSTALL_DIR
    EXISTING_HOME=$(find "$INSTALL_DIR" -maxdepth 1 -name "instantclient_*" | head -n 1)
    if [ -n "$EXISTING_HOME" ] && [ -f "$EXISTING_HOME/libclntsh.so" ] && [ -f "$EXISTING_HOME/sdk/include/oci.h" ]; then
         echo "Found valid cached Oracle SDK at: $EXISTING_HOME"
         OCI_HOME="$EXISTING_HOME"
         SKIP_DOWNLOAD=1
    fi
fi

if [ "$SKIP_DOWNLOAD" -eq 0 ]; then
    echo "No valid Oracle SDK found. Downloading..."
    
    # Ensure a clean install location to avoid unzip prompts on CI reruns
    # But only clean subdirs, don't remove INSTALL_DIR itself
    rm -rf "$INSTALL_DIR/instantclient_*" "$INSTALL_DIR"/*.zip

    echo "Downloading Oracle Instant Client ${OCI_VER_FULL} for ${OCI_ARCH}..."
    wget -q -O "$INSTALL_DIR/basic.zip" "${BASE_URL}/instantclient-basic-${OCI_ARCH}-${OCI_VER_FULL}.zip" || \
      wget -q -O "$INSTALL_DIR/basic.zip" "${LATEST_BASE_URL}/instantclient-basic-${OCI_ARCH}.zip"
    wget -q -O "$INSTALL_DIR/sdk.zip" "${BASE_URL}/instantclient-sdk-${OCI_ARCH}-${OCI_VER_FULL}.zip" || \
      wget -q -O "$INSTALL_DIR/sdk.zip" "${LATEST_BASE_URL}/instantclient-sdk-${OCI_ARCH}.zip"

    echo "Extracting..."
    unzip -oq "$INSTALL_DIR/basic.zip" -d "$INSTALL_DIR"
    unzip -oq "$INSTALL_DIR/sdk.zip" -d "$INSTALL_DIR"

    # The zip extracts to a folder like 'instantclient_23_6'
    OCI_HOME=$(find "$INSTALL_DIR" -maxdepth 1 -name "instantclient_*" | head -n 1)
    echo "Oracle Home installed at: $OCI_HOME"
    
    # Cleanup zips
    rm -f "$INSTALL_DIR/basic.zip" "$INSTALL_DIR/sdk.zip"
fi

echo "Oracle Home: $OCI_HOME"

# Ensure libaio is available on Ubuntu 24.04 (libaio1 -> libaio1t64) and other runners.
# Avoid sudo; CI containers typically run as root.
if ! [ -f /usr/lib/${LIBAIO_ARCH}/libaio.so.1 ] && ! [ -f /lib/${LIBAIO_ARCH}/libaio.so.1 ] && ! [ -f /usr/lib64/libaio.so.1 ]; then
  echo "Checking for libaio runtime dependency..."

  SUDO=""
  CAN_INSTALL=0
  
  if [ "$EUID" -eq 0 ]; then
    CAN_INSTALL=1
  elif command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
    SUDO="sudo"
    CAN_INSTALL=1
  else
    echo "Warning: Running as non-root and sudo unavailable/requires password. Skipping system dependency installation (libaio)."
  fi

  if [ "$CAN_INSTALL" -eq 1 ]; then
    echo "Installing libaio..."
    # Try various package managers
    if command -v apt-get >/dev/null 2>&1; then
        $SUDO apt-get update -y >/dev/null
        $SUDO apt-get install -y --no-install-recommends libaio1t64 || $SUDO apt-get install -y --no-install-recommends libaio-dev || true
    elif command -v yum >/dev/null 2>&1; then
        $SUDO yum install -y libaio || true
    elif command -v dnf >/dev/null 2>&1; then
        $SUDO dnf install -y libaio || true
    elif command -v apk >/dev/null 2>&1; then
        apk add --no-cache libaio || true
    else
        echo "Warning: no supported package manager found; continuing without installing libaio."
    fi

    # Handle Ubuntu 24.04 libaio1t64 -> libaio.so.1 symlink
    if [ -f /usr/lib/${LIBAIO_ARCH}/libaio.so.1t64 ] && [ ! -f /usr/lib/${LIBAIO_ARCH}/libaio.so.1 ]; then
        $SUDO ln -sf /usr/lib/${LIBAIO_ARCH}/libaio.so.1t64 /usr/lib/${LIBAIO_ARCH}/libaio.so.1
    fi
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

# For GitHub Actions: add to PATH and environment
if [ -n "$GITHUB_PATH" ]; then
    echo "$OCI_HOME" >> "$GITHUB_PATH"
    echo "ORACLE_HOME=$OCI_HOME" >> "$GITHUB_ENV"
    echo "LD_LIBRARY_PATH=$OCI_HOME:\$LD_LIBRARY_PATH" >> "$GITHUB_ENV"
fi

# For local use: save to env file
echo "export ORACLE_HOME=\"$ORACLE_HOME\"" > "$INSTALL_DIR/env.sh"
echo "export LD_LIBRARY_PATH=\"$LD_LIBRARY_PATH\"" >> "$INSTALL_DIR/env.sh"

# Try to register with ldconfig when permitted (helps local builds and Docker)
if command -v ldconfig >/dev/null 2>&1; then
  SUDO=""
  CAN_CONFIG=0
  if [ "$EUID" -eq 0 ]; then
    CAN_CONFIG=1
  elif command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
    SUDO="sudo"
    CAN_CONFIG=1
  fi

  if [ "$CAN_CONFIG" -eq 1 ]; then
      if [ -d "/etc/ld.so.conf.d" ]; then
          if [ -n "$SUDO" ]; then
             echo "$OCI_HOME" | $SUDO tee /etc/ld.so.conf.d/oracle-instantclient.conf > /dev/null || true
          else
             echo "$OCI_HOME" > /etc/ld.so.conf.d/oracle-instantclient.conf || true
          fi
          $SUDO ldconfig || true
      fi
  fi
fi

echo "OCI setup complete."