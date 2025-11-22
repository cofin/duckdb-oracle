# Oracle Instant Client Setup for Windows
# PowerShell script to download and configure Oracle Instant Client

$ErrorActionPreference = "Stop"

# Oracle Instant Client Version
$OCI_VER_FULL = "23.6.0.24.10"
$BASE_URL = "https://download.oracle.com/otn_software/nt/instantclient/2360000"

# Detect architecture
$ARCH = $env:PROCESSOR_ARCHITECTURE
if ($ARCH -eq "AMD64") {
    $OCI_ARCH = "x64"
    $BASIC_FILE = "instantclient-basic-windows.x64-${OCI_VER_FULL}.zip"
    $SDK_FILE = "instantclient-sdk-windows.x64-${OCI_VER_FULL}.zip"
} else {
    Write-Error "Unsupported architecture: $ARCH"
    exit 1
}

Write-Host "Detected architecture: $ARCH (using Windows $OCI_ARCH)"

$INSTALL_DIR = Join-Path $PWD "oracle_sdk"
if (Test-Path $INSTALL_DIR) {
    Remove-Item -Recurse -Force $INSTALL_DIR
}
New-Item -ItemType Directory -Path $INSTALL_DIR | Out-Null

Write-Host "Downloading Oracle Instant Client ${OCI_VER_FULL} for Windows ${OCI_ARCH}..."

# Download files
$BasicZip = Join-Path $PWD "basic.zip"
$SdkZip = Join-Path $PWD "sdk.zip"

try {
    Invoke-WebRequest -Uri "${BASE_URL}/${BASIC_FILE}" -OutFile $BasicZip
    Invoke-WebRequest -Uri "${BASE_URL}/${SDK_FILE}" -OutFile $SdkZip
} catch {
    Write-Error "Failed to download Oracle Instant Client: $_"
    exit 1
}

Write-Host "Extracting..."
Expand-Archive -Path $BasicZip -DestinationPath $INSTALL_DIR -Force
Expand-Archive -Path $SdkZip -DestinationPath $INSTALL_DIR -Force

# Clean up zip files
Remove-Item $BasicZip, $SdkZip

# Find the extracted directory
$OCI_HOME = Get-ChildItem -Path $INSTALL_DIR -Directory -Filter "instantclient_*" | Select-Object -First 1 -ExpandProperty FullName

if (-not $OCI_HOME) {
    Write-Error "Oracle Instant Client directory not found after extraction"
    exit 1
}

Write-Host "Oracle Home found at: $OCI_HOME"

# Set environment variables
$env:ORACLE_HOME = $OCI_HOME
$env:PATH = "$OCI_HOME;$env:PATH"

# For GitHub Actions: add to PATH and environment
if ($env:GITHUB_PATH) {
    Add-Content -Path $env:GITHUB_PATH -Value $OCI_HOME
    Add-Content -Path $env:GITHUB_ENV -Value "ORACLE_HOME=$OCI_HOME"
    Add-Content -Path $env:GITHUB_ENV -Value "PATH=$OCI_HOME;$env:PATH"
}

# Save to env file for local persistence
$EnvFile = Join-Path $INSTALL_DIR "env.ps1"
@"
`$env:ORACLE_HOME = "$OCI_HOME"
`$env:PATH = "$OCI_HOME;`$env:PATH"
"@ | Out-File -FilePath $EnvFile -Encoding UTF8

Write-Host "OCI setup complete for Windows x64."
Write-Host "ORACLE_HOME set to: $OCI_HOME"
