param(
    [Parameter(HelpMessage="Target architecture")]
    [ValidateSet("x64", "x86", "arm64")]
    [string]$Architecture = "x64",

    [Parameter(HelpMessage="Build configuration")]
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",

    [Parameter(HelpMessage="Generate installer package after build (.msi for Release)")]
    [switch]$Package,

    [Parameter(HelpMessage="Force clean build (delete build directory before configuring)")]
    [switch]$Clean,

    [Parameter(HelpMessage="Parallel build jobs (default: 2x CPU cores, min 4)")]
    [int]$Jobs = 0
)

# UTF-8 encoding setup (must come after param() for cross-platform pwsh compat)
[Console]::InputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
if (Get-Command chcp -ErrorAction SilentlyContinue) {
    chcp 65001 > $null
}

$ErrorActionPreference = "Stop"

# Parallel jobs: -Jobs N override, else 2x CPU cores, fallback 4
if ($Jobs -le 0) {
    $cpuCores = try { [int]$env:NUMBER_OF_PROCESSORS } catch { 0 }
    if ($cpuCores -le 0) { $cpuCores = 4 }
    $Jobs = [Math]::Max(4, $cpuCores * 2)
}

# Read project version from CMakeLists.txt (used for MSI filtering + summary)
$versionMatch = [regex]::Match((Get-Content "$PSScriptRoot\CMakeLists.txt" -Raw), 'project\(Vora VERSION (\d+\.\d+\.\d+)\)')
$projectVersion = if ($versionMatch.Success) { $versionMatch.Groups[1].Value } else { "0.0.0" }

# Interactive mode -- when run without arguments, ask the user
$isInteractive = (-not $PSBoundParameters.ContainsKey('Architecture')) -and
                 (-not $PSBoundParameters.ContainsKey('Config')) -and
                 (-not $Package.IsPresent) -and
                 (-not $Clean.IsPresent)

if ($isInteractive) {
    Write-Host ""
    Write-Host "==== Vora Build (Interactive) ====" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Target architecture:" -ForegroundColor Yellow
    Write-Host "  [1] x64   (default)"
    Write-Host "  [2] x86"
    Write-Host "  [3] arm64"
    $choice = Read-Host "Choice [1-3]"
    switch ($choice) {
        "2" { $Architecture = "x86" }
        "3" { $Architecture = "arm64" }
        default { $Architecture = "x64" }
    }
    Write-Host ""
    Write-Host "Build configuration:" -ForegroundColor Yellow
    Write-Host "  [1] Debug   (default)"
    Write-Host "  [2] Release"
    $choice = Read-Host "Choice [1-2]"
    switch ($choice) {
        "2" { $Config = "Release" }
        default { $Config = "Debug" }
    }
    if ($Config -eq "Release") {
        Write-Host ""
        Write-Host "Generate .msi installer?" -ForegroundColor Yellow
        Write-Host "  [1] No   (default)"
        Write-Host "  [2] Yes"
        $choice = Read-Host "Choice [1-2]"
        if ($choice -eq "2") { $Package = $true }
    }
    Write-Host ""
}

Write-Host ""
Write-Host "==== Vora Build System ====" -ForegroundColor Cyan
Write-Host "  Architecture : $Architecture"         -ForegroundColor Gray
Write-Host "  Configuration: $Config"               -ForegroundColor Gray
Write-Host "  Jobs         : $Jobs"                  -ForegroundColor Gray
if ($Clean) {
    Write-Host "  Clean build  : yes"               -ForegroundColor Gray
}
if ($Package) {
    Write-Host "  Package      : yes"               -ForegroundColor Gray
}
Write-Host ""

$PresetName = "windows-$Architecture-$Config".ToLower()
$buildDir = "build\$PresetName"

# ----------------------------------------
# Clean old build (only with -Clean)
# ----------------------------------------

if ($Clean) {
    Write-Host "[1/5] Cleaning old build..." -ForegroundColor Yellow
    if (Test-Path $buildDir) {
        Remove-Item $buildDir -Recurse -Force
    }
} else {
    Write-Host "[1/5] Using existing build directory (use -Clean for fresh build)..." -ForegroundColor Yellow
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

cmake --build --preset $PresetName --config $Config --parallel $Jobs

# ----------------------------------------
# Build LSP + DAP from Vora-LSP repo (Release only)
# ----------------------------------------

$lspRepo = "$PSScriptRoot\..\Vora-LSP"
if ($Package -and $Config -eq "Release") {
    Write-Host "[4/6] Building vora-lsp + vora-dap from Vora-LSP..." -ForegroundColor Yellow
    if (Test-Path "$lspRepo\CMakeLists.txt") {
        # Compute absolute paths BEFORE Push-Location (relative paths break inside it)
        $absBuildDir = "$PSScriptRoot\$buildDir"
        $releaseDir = "$absBuildDir\Release"
        Push-Location $lspRepo
        try {
            # Force reconfigure: remove stale cache so VORA_BUILD takes effect
            if (Test-Path "build\CMakeCache.txt") {
                Remove-Item "build\CMakeCache.txt" -Force
            }
            cmake -B build -DVORA_BUILD="$absBuildDir" 2>&1 | Out-Null
            cmake --build build --config Release --target vora-lsp vora-dap --parallel $Jobs
            if ($LASTEXITCODE -eq 0) {
                New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
                Copy-Item -Force "build\Release\vora-lsp.exe" "$releaseDir\" -ErrorAction SilentlyContinue
                Copy-Item -Force "build\Release\vora-dap.exe" "$releaseDir\" -ErrorAction SilentlyContinue
                Copy-Item -Force "vora-lsp.exe" "$releaseDir\" -ErrorAction SilentlyContinue
                Copy-Item -Force "vora-dap.exe" "$releaseDir\" -ErrorAction SilentlyContinue
                Write-Host "  vora-lsp + vora-dap rebuilt and staged" -ForegroundColor Green
            } else {
                Write-Host "  Warning: Vora-LSP build failed, using existing binaries if present" -ForegroundColor Yellow
            }
        } finally {
            Pop-Location
        }
    } else {
        Write-Host "  Vora-LSP repo not found at $lspRepo, using existing binaries" -ForegroundColor Yellow
    }
} else {
    Write-Host "[4/6] LSP/DAP rebuild skipped (requires -Package -Config Release)" -ForegroundColor Yellow
}

# ----------------------------------------
# Package (if requested)
# ----------------------------------------

if ($Package) {
    if ($Config -eq "Release") {
        Write-Host "[5/6] Generating MSI..." -ForegroundColor Yellow
        cmake --build $buildDir --target package --config $Config --parallel $Jobs
    } else {
        Write-Host "[5/6] Package skipped — only available for Release builds" -ForegroundColor Yellow
    }
} else {
    Write-Host "[5/6] Package skipped — use -Package to generate installer" -ForegroundColor Yellow
}

# ----------------------------------------
# Summary
# ----------------------------------------

Write-Host "[6/6] Build complete" -ForegroundColor Yellow
Write-Host ""

$artifacts = @(
    @{Path="$buildDir\$Config\Vora.exe";         Label="Vora"},
    @{Path="$buildDir\$Config\vora_lib.lib";    Label="Static lib"},
    @{Path="$buildDir\$Config\vora-lsp.exe";    Label="LSP server"},
    @{Path="$buildDir\$Config\vora-dap.exe";    Label="DAP debugger"}
)
if ($Package -and $Config -eq "Release") {
    $msiPattern = "vora-$projectVersion-*.msi"
    $msi = Get-ChildItem "$buildDir\$msiPattern" 2>$null | Sort-Object Name -Descending | Select-Object -First 1
    if ($msi) { $artifacts += @{Path=$msi.FullName; Label="Installer"} }
}

$found = $false
foreach ($a in $artifacts) {
    if (Test-Path $a.Path) {
        Write-Host "  $($a.Label) : $($a.Path)" -ForegroundColor Green
        $found = $true
    }
}

if ($found) {
    Write-Host ""
    Write-Host "==== Build Success ====" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "No artifacts found" -ForegroundColor Red
}

Write-Host ""
Write-Host "Run without arguments for interactive mode" -ForegroundColor Gray
Write-Host "Usage  : .\build.ps1 -Architecture arm64 -Config Release -Package -Clean" -ForegroundColor Gray
