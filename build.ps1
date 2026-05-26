[Console]::InputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
chcp 65001 > $null

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "==== Vora Build System ====" -ForegroundColor Cyan
Write-Host ""

$buildDir = "build"

# ----------------------------------------
# Clean old build
# ----------------------------------------

Write-Host "[1/5] Cleaning old build..." -ForegroundColor Yellow

if (Test-Path $buildDir) {
    Remove-Item $buildDir -Recurse -Force
}

# ----------------------------------------
# Create build directory
# ----------------------------------------

Write-Host "[2/5] Creating build directory..." -ForegroundColor Yellow

New-Item -ItemType Directory -Path $buildDir | Out-Null

# ----------------------------------------
# Configure CMake
# ----------------------------------------

Write-Host "[3/5] Configuring CMake..." -ForegroundColor Yellow

cmake -S . -B $buildDir

# ----------------------------------------
# Build project
# ----------------------------------------

Write-Host "[4/5] Building project..." -ForegroundColor Yellow

cmake --build $buildDir

# ----------------------------------------
# Run executable
# ----------------------------------------

Write-Host "[5/5] Running Vora..." -ForegroundColor Yellow
Write-Host ""

$exePath = "$buildDir/Debug/Vora.exe"

if (Test-Path $exePath) {

    & $exePath

    Write-Host ""
    Write-Host "==== Build Success ====" -ForegroundColor Green
}
else {

    Write-Host ""
    Write-Host "Executable not found: $exePath" -ForegroundColor Red
}
