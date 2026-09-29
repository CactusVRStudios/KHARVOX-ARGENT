param([Parameter(Mandatory=$true)][string]$Package)
$ErrorActionPreference = 'Stop'
# Both integrations are mandatory in full releases and playable prototypes.
# Pin the approved SDK/loader, not the bridges which change with our source.
$approved = @{
    'bhaptics_library.dll' = '909124BAED4467AA0C85B1A25BC84065FFC65030F3199C13EA7A18403434CE52'
    'psvr2_toolkit_capi_loader.dll' = 'E366143796FBB90EDCB5CFB61AFB05C7BAA3E27EBAABE209BD7DF16A3ADCED16'
}
foreach ($name in @('KharvoxBhapticsBridge.exe','bhaptics_library.dll','KharvoxPsvr2Bridge.exe','psvr2_toolkit_capi_loader.dll')) {
    $path = Join-Path $Package $name
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required integration file missing: $name" }
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -lt 64 -or $bytes[0] -ne 77 -or $bytes[1] -ne 90) { throw "Invalid integration binary: $name" }
    $pe = [BitConverter]::ToInt32($bytes,60)
    if ($pe -lt 64 -or $pe -gt $bytes.Length-24 -or [BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x8664) {
        throw "Integration binary must be Windows x64: $name"
    }
    if ($approved.ContainsKey($name) -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $approved[$name]) {
        throw "Unapproved integration DLL (or test stub): $name. Verify SDK compatibility before updating the approved hash."
    }
}
Write-Output 'PASS: bHaptics SDK + bridge and PSVR2 Toolkit loader + bridge present; x64 binaries and approved DLL hashes verified'
