param([string]$OutputDir = "$PSScriptRoot\..\build\legacy")
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$root = (Resolve-Path "$PSScriptRoot\..").Path
$lib = "$root\LegacyApp\EasyConn\Lib"
& "$env:SystemRoot\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:exe /platform:anycpu "/out:$OutputDir\EasyConn.LegacyHost.exe" "/reference:$lib\TeleAssistenza.dll" "$root\src\legacyhost\Program.cs"
if ($LASTEXITCODE -ne 0) { throw 'LegacyHost compilation failed' }
foreach ($name in @('TeleAssistenza.dll','Tool.dll','ICSharpCode.SharpZipLib.dll')) {
    Copy-Item -LiteralPath "$lib\$name" -Destination $OutputDir -Force
}
