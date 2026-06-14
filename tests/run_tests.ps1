# run_tests.ps1 — Vora test suite runner (PowerShell)
# Usage:
#   .\tests\run_tests.ps1

$ErrorActionPreference = "Continue"

# Locate the Vora binary — try common locations
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
$Vora = $null
foreach ($candidate in @(
        "$ProjectDir\build\Debug\Vora.exe",
        "$ProjectDir\build\Release\Vora.exe",
        "$ProjectDir\build\windows-x64-debug\Debug\Vora.exe",
        "$ProjectDir\build\windows-x64-release\Release\Vora.exe",
        "$ProjectDir\build\Vora.exe"
    )) {
    if (Test-Path $candidate) {
        $Vora = $candidate
        break
    }
}
if (-not $Vora) {
    Write-Host "Error: Vora binary not found. Build the project first:" -ForegroundColor Red
    Write-Host "  cmake -S . -B build && cmake --build build"
    exit 1
}

Write-Host "Using: $Vora"
Write-Host ""

$Pass = 0
$Fail = 0
$Errors = @()

Write-Host "============================================"
Write-Host "  Vora Test Suite"
Write-Host "============================================"
Write-Host ""

Get-ChildItem -Path "$ProjectDir\tests\lexer", "$ProjectDir\tests\parser", "$ProjectDir\tests\runtime", "$ProjectDir\tests\interpreter", "$ProjectDir\tests\formatter" -Filter *.va | ForEach-Object {
    $file = $_.FullName
    $name = $_.FullName -replace [regex]::Escape("$ProjectDir\tests\"), ""
    Write-Host ("  {0,-45} " -f $name) -NoNewline

    $output = & $Vora $file 2>&1
    if ($LASTEXITCODE -eq 0)
    {
        Write-Host "PASS" -ForegroundColor Green
        $Pass++
    } else
    {
        Write-Host "FAIL" -ForegroundColor Red
        $Fail++
        $Errors += "  $name : $output"
    }
}

Write-Host ""
Write-Host "============================================"
Write-Host "  Results: $Pass passed, $Fail failed"
Write-Host "============================================"

if ($Fail -gt 0)
{
    Write-Host ""
    Write-Host "Failures:" -ForegroundColor Red
    $Errors | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    exit 1
}
