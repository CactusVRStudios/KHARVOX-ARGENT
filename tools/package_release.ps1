param(
    [Parameter(Mandatory=$true)][ValidatePattern('^ARGENT-(Alpha-Test-r[0-9]{3}|Beta-[0-9]+\.[0-9]+(?:\.[0-9]+)?|[0-9]+\.[0-9]+(?:\.[0-9]+)?[a-z]?)$')][string]$ReleaseName,
    [switch]$IncludeSymbols,
    [switch]$PerformanceDiagnostics,
    [switch]$Clean = $true,
    [string]$Build = (Join-Path $PSScriptRoot '..\build-clean\Release'),
    [string]$CrtDirectory = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!$Clean -or $PerformanceDiagnostics -or $IncludeSymbols) { throw 'Releases must be clean, without continuous diagnostics or symbols.' }
$cache = Join-Path (Split-Path -Parent $Build) 'CMakeCache.txt'
if (!(Test-Path -LiteralPath $cache) -or !(Select-String -LiteralPath $cache -Pattern '^ARGENT_CLEAN_RELEASE:BOOL=ON$' -Quiet)) { throw 'Release requires an ARGENT_CLEAN_RELEASE=ON build.' }
$releases = Join-Path $root 'releases'
$package = Join-Path $releases $ReleaseName
$archive = "$package.zip"
if ((Test-Path -LiteralPath $package) -or (Test-Path -LiteralPath $archive)) { throw 'Release already exists; choose a new revision. No files overwritten.' }
$crtFiles = @('concrt140.dll','msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll','msvcp140_atomic_wait.dll','msvcp140_codecvt_ids.dll','vccorlib140.dll','vcruntime140.dll','vcruntime140_1.dll','vcruntime140_threads.dll')
foreach ($name in $crtFiles) { if (!(Test-Path -LiteralPath (Join-Path $CrtDirectory $name))) { throw "Missing VC++ runtime: $name" } }
& (Join-Path $PSScriptRoot 'package_runtime.ps1') -Package $package -Build $Build -Clean
foreach ($name in $crtFiles) { Copy-Item -LiteralPath (Join-Path $CrtDirectory $name) -Destination $package }
if (Test-Path -LiteralPath (Join-Path $package 'assets/launcher.ini')) { throw 'Personal launcher settings in release staging' }
$controls = Join-Path $package 'assets/argent_controls.cfg'
if (!(Select-String -LiteralPath $controls -Pattern '^snap_turn_angle ' -Quiet)) { Add-Content -LiteralPath $controls -Value 'snap_turn_angle 45' -Encoding ASCII }
# The launcher includes this small identity file with user-requested captures.
[ordered]@{
    release=$ReleaseName
    cleanRelease=$true
    extendedLoggingAvailable=$true
    performanceDiagnostics=$false
    builtUtc=[DateTime]::UtcNow.ToString('o')
    supportedGameSha256='69dc13e88d1c19133ead7950dc64ebcbd4a5a3f6bd6f9c336ebffe56df6a1c11'
    experimentalStorePeTimestamp='0x69bc663d'
    experimentalStoreImageSize='0x74f1000'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'build-info.json') -Encoding UTF8
$forbidden = @(Get-ChildItem -LiteralPath $package -Recurse | Where-Object {
    $_.Name -match '^(docs|licenses|tools|symbols|logs|captures)$|^README|^START[-_ ]HERE' -or
    (!$_.PSIsContainer -and $_.Extension -in @('.md','.pdb','.py','.ps1','.cmd'))
})
if ($forbidden.Count) { throw ('Non-runtime content in release: ' + ($forbidden.Name -join ', ')) }
# Keep audit metadata outside the playable package and ZIP.
$manifestDirectory = Join-Path $releases 'manifests'
New-Item -ItemType Directory -Force $manifestDirectory | Out-Null
$manifest = @(Get-ChildItem -LiteralPath $package -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{name=$_.FullName.Substring($package.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $manifestDirectory "$ReleaseName.json") -Encoding UTF8
Compress-Archive -LiteralPath $package -DestinationPath $archive -CompressionLevel Optimal
Get-Item -LiteralPath $package,$archive | Select-Object FullName,Length
