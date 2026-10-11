#!/bin/bash
set -euo pipefail

# Oracle Instant Client Version
OCI_VER_MAJOR=23
OCI_VERSION="${OCI_VERSION:-23.26.2.0.0}"
OCI_VER_FULL="$OCI_VERSION"
OCI_VER_SHORT=2326200
BASE_URL="https://download.oracle.com/otn_software/mac/instantclient/${OCI_VER_SHORT}"

MOUNT_POINTS=()
cleanup_mounts() {
    for mp in "${MOUNT_POINTS[@]:-}"; do
        if [ -n "$mp" ]; then
            hdiutil detach "$mp" -quiet 2>/dev/null || true
        fi
    done
}
trap cleanup_mounts EXIT

# Detect architecture - only ARM64 is supported (Apple Silicon M1/M2/M3)
ARCH=$(uname -m)
case "$ARCH" in
    arm64)
        OCI_ARCH="arm64"
        BASIC_FILE="instantclient-basic-macos.${OCI_ARCH}-${OCI_VER_FULL}.dmg"
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

BASIC_DMG="$INSTALL_DIR/basic.dmg"
SDK_DMG="$INSTALL_DIR/sdk.dmg"

echo "Downloading Oracle Instant Client ${OCI_VER_FULL} for macOS ${OCI_ARCH}..."
curl -fL --retry 3 -o "$BASIC_DMG" "${BASE_URL}/${BASIC_FILE}" || {
    echo "Failed to download Basic package"
    exit 1
}
curl -fL --retry 3 -o "$SDK_DMG" "${BASE_URL}/${SDK_FILE}" || {
    echo "Failed to download SDK package"
    exit 1
}

echo "Mounting DMG files..."
# Use -plist to parse mount point reliably, or simple awk if we assume standard output format with tabs
# hdiutil standard output is: /dev/diskXsY <tab> TYPE <tab> /Volumes/Mount Point
BASIC_MOUNT_INFO=$(hdiutil attach "$BASIC_DMG" -nobrowse -noverify -noautoopen)
echo "Basic Mount Info: $BASIC_MOUNT_INFO"
BASIC_MOUNT=$(echo "$BASIC_MOUNT_INFO" | grep '/Volumes' | cut -f 3)
MOUNT_POINTS+=("$BASIC_MOUNT")

SDK_MOUNT_INFO=$(hdiutil attach "$SDK_DMG" -nobrowse -noverify -noautoopen)
echo "SDK Mount Info: $SDK_MOUNT_INFO"
SDK_MOUNT=$(echo "$SDK_MOUNT_INFO" | grep '/Volumes' | cut -f 3)
MOUNT_POINTS+=("$SDK_MOUNT")

echo "Basic Mount Point: '$BASIC_MOUNT'"
echo "SDK Mount Point: '$SDK_MOUNT'"

echo "Listing Basic Mount:"
ls -la "$BASIC_MOUNT" || echo "Failed to list Basic mount"

echo "Extracting files from DMG..."
# Copy files from the root of the mounted DMG (no instantclient_* subdirectory)
cp -R "${BASIC_MOUNT}"/* "$INSTALL_DIR/" || {
    echo "Failed to copy Basic package"
    exit 1
}

# Find the extracted directory
# For macOS DMG, files are extracted directly to INSTALL_DIR
OCI_HOME="$INSTALL_DIR"

# Copy SDK files into the same directory
cp -R "${SDK_MOUNT}"/sdk "$OCI_HOME/" || {
    echo "Failed to copy SDK package"
    exit 1
}

echo "Unmounting DMG files..."
hdiutil detach "$BASIC_MOUNT"
hdiutil detach "$SDK_MOUNT"
MOUNT_POINTS=()

# Clean up DMG files
rm -f "$BASIC_DMG" "$SDK_DMG"

echo "Oracle Home found at: $OCI_HOME"

# Create symlinks for .dylib files (macOS uses .dylib instead of .so)
if [ ! -f "$OCI_HOME/libclntsh.dylib" ] && [ -f "$OCI_HOME/libclntsh.dylib.23.1" ]; then
    ln -s "$OCI_HOME/libclntsh.dylib.23.1" "$OCI_HOME/libclntsh.dylib" || true
fi

# Export environment variables
export ORACLE_HOME="$OCI_HOME"
export DYLD_LIBRARY_PATH="${OCI_HOME}${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"

# For GitHub Actions: add to PATH and environment
if [ -n "${GITHUB_PATH:-}" ]; then
    echo "$OCI_HOME" >> "$GITHUB_PATH"
    echo "ORACLE_HOME=$OCI_HOME" >> "$GITHUB_ENV"
    echo "DYLD_LIBRARY_PATH=${DYLD_LIBRARY_PATH}" >> "$GITHUB_ENV"
fi

# For local use: save to env file
echo "export ORACLE_HOME=\"$ORACLE_HOME\"" > "$INSTALL_DIR/env.sh"
echo "export DYLD_LIBRARY_PATH=\"$DYLD_LIBRARY_PATH\"" >> "$INSTALL_DIR/env.sh"

echo "OCI setup complete for macOS ARM64."
echo "ORACLE_HOME set to: $OCI_HOME"
