$Vora = ".\build\Debug\Vora.exe"
$Pass = 0
$Fail = 0

Write-Host "=== Vora Examples (VM mode) ==="

Get-ChildItem examples/*.va | ForEach-Object {
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
