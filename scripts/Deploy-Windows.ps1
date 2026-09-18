# Deploy the current MSYS2 UCRT64 build, including transitive third-party DLLs.
param(
    [string]$BuildDir = "$PSScriptRoot\..\build",
    [string]$OutputDir = "$PSScriptRoot\..\dist\EzConn-video-fix",
    [string]$QtBin = 'C:\msys64\ucrt64\bin'
)
$ErrorActionPreference = 'Stop'
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$QtBin = (Resolve-Path -LiteralPath $QtBin).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
$previousPath = $env:PATH
try {
    $env:PATH = "$QtBin;$previousPath"
    foreach ($name in @('EasyConn.exe', 'EasyConnProbe.exe')) {
        $source = Join-Path $BuildDir $name
        if (-not (Test-Path -LiteralPath $source)) { throw "Missing executable: $source" }
        if ($BuildDir -ne $OutputDir) { Copy-Item -LiteralPath $source -Destination $OutputDir -Force }
    }
    & "$QtBin\windeployqt.exe" --no-translations --compiler-runtime --dir $OutputDir "$OutputDir\EasyConn.exe" "$OutputDir\EasyConnProbe.exe"
    if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }
    # windeployqt does not include all MSYS2 dependencies (ICU, PCRE, zlib, etc.).
    $queue = [Collections.Generic.Queue[string]]::new()
    Get-ChildItem -LiteralPath $OutputDir -Recurse -File | Where-Object Extension -in '.exe','.dll' | ForEach-Object { $queue.Enqueue($_.FullName) }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    while ($queue.Count -gt 0) {
        $binary = $queue.Dequeue()
        if (-not $seen.Add($binary)) { continue }
        $imports = & "$QtBin\objdump.exe" -p $binary
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $binary" }
        foreach ($line in $imports) {
            if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
            $name = $Matches[1]
            if ($name -match '^(api-ms-|ext-ms-)') { continue }
            $source = Join-Path $QtBin $name
            $target = Join-Path $OutputDir $name
            if (Test-Path -LiteralPath $source) {
                if (-not $seen.Contains($target)) {
                    Copy-Item -LiteralPath $source -Destination $target -Force
                    $queue.Enqueue($target)
                }
            } elseif (-not (Test-Path -LiteralPath $target) -and -not (Test-Path -LiteralPath "$env:SystemRoot\System32\$name")) {
                throw "Unresolved dependency $name in $binary"
            }
        }
    }
    @('[Paths]', 'Prefix=.', 'Plugins=.') | Set-Content -LiteralPath "$OutputDir\qt.conf" -Encoding ASCII
    if ($BuildDir -ne $OutputDir -and (Test-Path -LiteralPath "$BuildDir\legacy")) {
        Copy-Item -LiteralPath "$BuildDir\legacy" -Destination $OutputDir -Recurse -Force
    }
    Write-Output "Deployment complete: $OutputDir ($($seen.Count) binaries inspected)"
} finally { $env:PATH = $previousPath }
