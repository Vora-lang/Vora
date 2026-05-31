# run_tests.ps1 — Vora test suite runner (PowerShell)
# Usage: .\tests\run_tests.ps1

$ErrorActionPreference = "Continue"
$Vora = ".\build\Debug\Vora.exe"
$Pass = 0
$Fail = 0
$Errors = @()

Write-Host "============================================"
Write-Host "  Vora Test Suite"
Write-Host "============================================"
Write-Host ""

Get-ChildItem -Path tests/lexer, tests/parser, tests/runtime, tests/interpreter -Filter *.va | ForEach-Object {
    $file = $_.FullName
    $name = $_.FullName -replace [regex]::Escape("$PWD\"), ""
    Write-Host ("  {0,-45} " -f $name) -NoNewline

    $output = & $Vora $file 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "PASS" -ForegroundColor Green
        $Pass++
    } else {
        Write-Host "FAIL" -ForegroundColor Red
        $Fail++
        $Errors += "  $name : $output"
    }
}

Write-Host ""
Write-Host "============================================"
Write-Host "  Results: $Pass passed, $Fail failed"
Write-Host "============================================"

if ($Fail -gt 0) {
    Write-Host ""
    Write-Host "Failures:" -ForegroundColor Red
    $Errors | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    exit 1
}
