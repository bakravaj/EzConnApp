param(
    [switch]$Test,
    [switch]$Deploy
)
$ErrorActionPreference = 'Stop'
$toolBin = 'C:\msys64\ucrt64\bin'
foreach ($tool in @('cmake.exe', 'ctest.exe', 'ninja.exe', 'c++.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $toolBin $tool))) {
        throw "Required MSYS2 UCRT64 tool not found: $toolBin\$tool"
    }
}
$previousPath = $env:PATH
Push-Location (Join-Path $PSScriptRoot '..')
try {
    $env:PATH = "$toolBin;$previousPath"
    & "$toolBin\cmake.exe" --preset ucrt64-release
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
    & "$toolBin\cmake.exe" --build --preset ucrt64-release
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed' }
    if ($Test) {
        & "$toolBin\ctest.exe" --preset ucrt64-release
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    }
    if ($Deploy) { & "$PSScriptRoot\Deploy-Windows.ps1" -QtBin $toolBin }
} finally {
    $env:PATH = $previousPath
    Pop-Location
}
