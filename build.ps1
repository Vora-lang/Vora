$ErrorActionPreference = "Stop"

Write-Host "==== Nyra Build System ===="

$buildDir = "build"

if (!(Test-Path $buildDir)) {
    Write-Host "[1/4] Creating build directory..."
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

Write-Host "[2/4] Configuring CMake..."
cmake -S . -B $buildDir

Write-Host "[3/4] Building project..."
cmake --build $buildDir

Write-Host "[4/4] Running Nyra..."

$exePath = "$buildDir/Debug/Nyra.exe"

if (Test-Path $exePath) {
    & $exePath
}
else {
    Write-Host "Executable not found: $exePath"
}
