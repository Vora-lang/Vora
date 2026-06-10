[Console]::InputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
chcp 65001 > $null

param(
    [Parameter(HelpMessage="Target architecture")]
    [ValidateSet("x64", "x86", "arm64")]
    [string]$Architecture = "x64",

    [Parameter(HelpMessage="Build configuration")]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(HelpMessage="Generate installer package after build (.msi for Release)")]
    [switch]$Package
)

$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "==== Vora Build System ====" -ForegroundColor Cyan
Write-Host "  Architecture : $Architecture"         -ForegroundColor Gray
Write-Host "  Configuration: $Config"               -ForegroundColor Gray
if ($Package) {
    Write-Host "  Package      : yes"               -ForegroundColor Gray
}
Write-Host ""

$PresetName = "windows-$Architecture-$Config".ToLower()

# ----------------------------------------
# Clean old build
# ----------------------------------------

Write-Host "[1/5] Cleaning old build..." -ForegroundColor Yellow

$buildDir = "build\$PresetName"
if (Test-Path $buildDir) {
    Remove-Item $buildDir -Recurse -Force
}

# ----------------------------------------
# Configure CMake (via presets)
# ----------------------------------------

Write-Host "[2/5] Configuring CMake (preset: $PresetName)..." -ForegroundColor Yellow

cmake --preset $PresetName

# ----------------------------------------
# Build project
# ----------------------------------------

Write-Host "[3/5] Building project..." -ForegroundColor Yellow

cmake --build --preset $PresetName --config $Config

# ----------------------------------------
# Package (if requested)
# ----------------------------------------

if ($Package) {
    if ($Config -eq "Release") {
        Write-Host "[4/5] Generating package..." -ForegroundColor Yellow
        cmake --build $buildDir --target package --config $Config

        Write-Host ""
        Write-Host "Packages:" -ForegroundColor Cyan
        Get-ChildItem $buildDir\vora-*.* 2>$null | ForEach-Object {
            Write-Host "  $($_.Name)" -ForegroundColor Green
        }
    } else {
        Write-Host "[4/5] Package skipped — only available for Release builds" -ForegroundColor Yellow
    }
} else {
    Write-Host "[4/5] Package skipped — use -Package to generate installer" -ForegroundColor Yellow
}

# ----------------------------------------
# Run executable
# ----------------------------------------

Write-Host "[5/5] Running Vora..." -ForegroundColor Yellow
Write-Host ""

$exePath = "$buildDir\$Config\Vora.exe"

if (Test-Path $exePath) {
    & $exePath @args
    Write-Host ""
    Write-Host "==== Build Success ====" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "Executable not found: $exePath" -ForegroundColor Red
}

# Show available presets hint
Write-Host ""
Write-Host "Available presets: cmake --list-presets" -ForegroundColor Gray
Write-Host "Usage  : .\build.ps1 -Architecture arm64 -Config Release -Package" -ForegroundColor Gray
