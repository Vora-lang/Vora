# run_examples.ps1 — Vora examples runner (PowerShell)
# Usage:
#   .\tests\run_examples.ps1

$ErrorActionPreference = "Continue"

# Locate the Vora binary — preset paths (consistent with build.ps1)
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
$Vora = $null
foreach ($candidate in @(
        "$ProjectDir\build\windows-x64-debug\Debug\Vora.exe",
        "$ProjectDir\build\windows-x64-release\Release\Vora.exe",
        "$ProjectDir\build\windows-x86-debug\Debug\Vora.exe",
        "$ProjectDir\build\windows-x86-release\Release\Vora.exe",
        "$ProjectDir\build\windows-arm64-debug\Debug\Vora.exe",
        "$ProjectDir\build\windows-arm64-release\Release\Vora.exe"
    )) {
    if (Test-Path $candidate) {
        $Vora = $candidate
        break
    }
}
if (-not $Vora) {
    Write-Host "Error: Vora binary not found. Build the project first:" -ForegroundColor Red
    Write-Host "  .\build.ps1"
    exit 1
}

Write-Host "Using: $Vora"
Write-Host ""

$Pass = 0
$Fail = 0

Write-Host "=== Vora Examples ==="

Get-ChildItem "$ProjectDir\examples\*.va" | ForEach-Object {
    $name = $_.Name
    Write-Host "  $name " -NoNewline

    if ($name -eq '17-type-annotations.va') {
        $out = "42`n3.14`n100`ntest`n" | & $Vora $_.FullName 2>&1
    } else {
        $out = & $Vora $_.FullName 2>&1
    }

    if ($LASTEXITCODE -eq 0) {
        Write-Host "PASS" -ForegroundColor Green
        $Pass++
    } else {
        Write-Host "FAIL" -ForegroundColor Red
        $Fail++
        Write-Host "    $out"
    }
}

Write-Host ""
Write-Host "Results: $Pass passed, $Fail failed"
if ($Fail -gt 0) { exit 1 }
