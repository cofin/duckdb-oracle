#!/bin/bash
set -e

# Oracle Instant Client Version
# macOS ARM64 uses version 23.3.0.23.09 (latest available as of Nov 2024)
OCI_VER_MAJOR=23
OCI_VER_FULL=23.3.0.23.09
OCI_VER_SHORT=233023
BASE_URL="https://download.oracle.com/otn_software/mac/instantclient/${OCI_VER_SHORT}"

# Detect architecture - only ARM64 is supported (Apple Silicon M1/M2/M3)
ARCH=$(uname -m)
case "$ARCH" in
    arm64)
        OCI_ARCH="arm64"
        BASIC_FILE="instantclient-basic-macos.${OCI_ARCH}-${OCI_VER_FULL}-2.dmg"
        SDK_FILE="instantclient-sdk-macos.${OCI_ARCH}-${OCI_VER_FULL}.dmg"
        ;;
    x86_64)
        echo "Error: Intel macOS (x86_64) is no longer supported"
        echo "Apple Silicon (ARM64) is required for Oracle Instant Client"
        exit 1
        ;;
    *)
        echo "Error: Unsupported architecture: $ARCH"
        exit 1
        ;;
esac

echo "Detected architecture: $ARCH (using macOS $OCI_ARCH)"

INSTALL_DIR=$PWD/oracle_sdk
rm -rf "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR"

echo "Downloading Oracle Instant Client ${OCI_VER_FULL} for macOS ${OCI_ARCH}..."
curl -L -o basic.dmg "${BASE_URL}/${BASIC_FILE}" || {
    echo "Failed to download Basic package"
    exit 1
}
curl -L -o sdk.dmg "${BASE_URL}/${SDK_FILE}" || {
    echo "Failed to download SDK package"
    exit 1
}

echo "Mounting DMG files..."
# Use -plist to parse mount point reliably, or simple awk if we assume standard output format with tabs
# hdiutil standard output is: /dev/diskXsY <tab> TYPE <tab> /Volumes/Mount Point
BASIC_MOUNT_INFO=$(hdiutil attach basic.dmg -nobrowse -noverify -noautoopen)
echo "Basic Mount Info: $BASIC_MOUNT_INFO"
BASIC_MOUNT=$(echo "$BASIC_MOUNT_INFO" | grep '/Volumes' | cut -f 3)

SDK_MOUNT_INFO=$(hdiutil attach sdk.dmg -nobrowse -noverify -noautoopen)
echo "SDK Mount Info: $SDK_MOUNT_INFO"
SDK_MOUNT=$(echo "$SDK_MOUNT_INFO" | grep '/Volumes' | cut -f 3)

echo "Basic Mount Point: '$BASIC_MOUNT'"
echo "SDK Mount Point: '$SDK_MOUNT'"

echo "Listing Basic Mount:"
ls -la "$BASIC_MOUNT" || echo "Failed to list Basic mount"

echo "Extracting files from DMG..."
# Copy the instantclient directory from mounted DMG
cp -R "${BASIC_MOUNT}"/instantclient_* "$INSTALL_DIR/" || {
    echo "Failed to copy Basic package"
    hdiutil detach "$BASIC_MOUNT" 2>/dev/null || true
    hdiutil detach "$SDK_MOUNT" 2>/dev/null || true
    exit 1
}

# Find the extracted directory
OCI_HOME=$(find "$INSTALL_DIR" -maxdepth 1 -name "instantclient_*" | head -n 1)

# Copy SDK files into the same directory
cp -R "${SDK_MOUNT}"/instantclient_*/sdk "$OCI_HOME/" || {
    echo "Failed to copy SDK package"
    hdiutil detach "$BASIC_MOUNT" 2>/dev/null || true
    hdiutil detach "$SDK_MOUNT" 2>/dev/null || true
    exit 1
}

echo "Unmounting DMG files..."
hdiutil detach "$BASIC_MOUNT"
hdiutil detach "$SDK_MOUNT"

# Clean up DMG files
rm -f basic.dmg sdk.dmg

echo "Oracle Home found at: $OCI_HOME"

# Create symlinks for .dylib files (macOS uses .dylib instead of .so)
if [ ! -f "$OCI_HOME/libclntsh.dylib" ] && [ -f "$OCI_HOME/libclntsh.dylib.23.1" ]; then
    ln -s "$OCI_HOME/libclntsh.dylib.23.1" "$OCI_HOME/libclntsh.dylib" || true
fi

# Export environment variables
export ORACLE_HOME="$OCI_HOME"
export DYLD_LIBRARY_PATH="$OCI_HOME:${DYLD_LIBRARY_PATH}"

# For GitHub Actions: add to PATH and environment
if [ -n "$GITHUB_PATH" ]; then
    echo "$OCI_HOME" >> "$GITHUB_PATH"
    echo "ORACLE_HOME=$OCI_HOME" >> "$GITHUB_ENV"
    echo "DYLD_LIBRARY_PATH=$OCI_HOME:\$DYLD_LIBRARY_PATH" >> "$GITHUB_ENV"
fi

# For local use: save to env file
echo "export ORACLE_HOME=\"$ORACLE_HOME\"" > "$INSTALL_DIR/env.sh"
echo "export DYLD_LIBRARY_PATH=\"$DYLD_LIBRARY_PATH\"" >> "$INSTALL_DIR/env.sh"

echo "OCI setup complete for macOS ARM64."
echo "ORACLE_HOME set to: $OCI_HOME"
