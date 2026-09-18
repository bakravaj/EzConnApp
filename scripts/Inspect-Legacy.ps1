# Run with Windows PowerShell 5.1 (.NET Framework). No network or machine access.
param([string]$LegacyRoot = "$PSScriptRoot\..\LegacyApp\EasyConn",
      [string]$OutputRoot = "$PSScriptRoot\..\artifacts\legacy")
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$LegacyRoot = (Resolve-Path -LiteralPath $LegacyRoot).Path
$paths = @('EasyConn.exe', 'Lib\TeleAssistenza.dll', 'Lib\Tool.dll')
$before = @($paths | ForEach-Object { Get-FileHash -LiteralPath (Join-Path $LegacyRoot $_) -Algorithm SHA256 })
[void][Reflection.Assembly]::LoadFrom((Join-Path $LegacyRoot 'Lib\ICSharpCode.SharpZipLib.dll'))
[void][Reflection.Assembly]::LoadFrom((Join-Path $LegacyRoot 'Lib\MySql.Data.dll'))
[void][Reflection.Assembly]::LoadFrom((Join-Path $LegacyRoot 'Lib\Tool.dll'))
$assembly = [Reflection.Assembly]::LoadFrom((Join-Path $LegacyRoot 'Lib\TeleAssistenza.dll'))
$flags = [Reflection.BindingFlags]'Public,NonPublic,Instance,Static,DeclaredOnly'
$inventory = foreach ($type in ($assembly.GetTypes() | Sort-Object FullName)) {
    [ordered]@{
        name = $type.FullName
        public = $type.IsVisible
        constructors = @($type.GetConstructors($flags) | ForEach-Object { $_.ToString() })
        methods = @($type.GetMethods($flags) | ForEach-Object { $_.ToString() })
    }
}
$inventory | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $OutputRoot 'api-inventory.json')
$packetType = $assembly.GetType('Schnell.TeleAssistenza.Protocollo.formatoComando', $true)
function Hex([byte[]]$bytes) { [BitConverter]::ToString($bytes).Replace('-', ' ') }
$cases = @(
    @{name='protocol-version'; type=80; command=3; payload=[byte[]]@(); expected='50 04 42 41 03 57 53'},
    @{name='software-version-local'; type=80; command=14; payload=[byte[]]@(32); expected='50 05 42 41 0E 57 20 7F'},
    @{name='options'; type=80; command=38; payload=[byte[]]@(); expected='50 04 42 41 26 57 76'},
    @{name='medium-frame'; type=67; command=0; payload=[byte[]]@(0,127,128,255); expected='43 08 00 42 41 00 57 00 7F 80 FF 5C'},
    @{name='large-frame'; type=66; command=0; payload=[byte[]]@(0,127,128,255); expected='42 08 00 00 00 42 41 00 57 00 7F 80 FF 5C'}
)
$vectors = foreach ($case in $cases) {
    $packet = [Activator]::CreateInstance($packetType, [object[]]@([byte]$case.type,[byte]0,[byte]0,[byte]65,[byte]66,[byte]$case.command,[byte]87,[byte[]]@()))
    # Constructor does NOT calculate the size field. SetDati is mandatory.
    $packet.SetDati($case.payload)
    [byte[]]$wire = $packet.GetPacchettoInByte()
    if ((Hex $wire) -ne $case.expected) { throw "Unexpected encoding: $($case.name): $(Hex $wire)" }
    $parsed = [Activator]::CreateInstance($packetType)
    $parsed.SetPacchetto($wire)
    if ($parsed.GetComando() -ne $case.command -or (Hex $parsed.GetDati()) -ne (Hex $case.payload)) { throw "Roundtrip failed: $($case.name)" }
    [ordered]@{name=$case.name; type=$case.type; command=$case.command; payload=(Hex $case.payload); wire=(Hex $wire)}
}
$vectors | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $OutputRoot 'oracle-vectors.json')
$after = @($paths | ForEach-Object { Get-FileHash -LiteralPath (Join-Path $LegacyRoot $_) -Algorithm SHA256 })
for ($i = 0; $i -lt $before.Count; $i++) {
    if ($before[$i].Hash -ne $after[$i].Hash) { throw 'Original binary changed' }
}
$before | Select-Object Path,Hash | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $OutputRoot 'sha256.json')
Write-Output "PASS: $($vectors.Count) encoding/decoding vectors; original hashes unchanged."
