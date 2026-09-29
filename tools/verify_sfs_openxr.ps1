param([string]$Runtime = 'D:\KHARVOX ARGENT\logs\simulator-runtime.json')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$log = Join-Path $root ('logs\sfs-xr-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.log')
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = Join-Path $root 'build\Release\ArgentSfsXrProbe.exe'
$start.WorkingDirectory = $root
$start.Arguments = '"' + (Join-Path $root 'build\sfs_pair.vert.spv') + '" "' + (Join-Path $root 'build\sfs_pair.frag.spv') + '"'
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.EnvironmentVariables['XR_RUNTIME_JSON'] = (Resolve-Path -LiteralPath $Runtime).Path
$start.EnvironmentVariables['SIMXR_VK_NO_TIMELINE'] = '1'
$start.EnvironmentVariables['ARGENT_LOG'] = $log
$process = [System.Diagnostics.Process]::Start($start)
if (-not $process.WaitForExit(30000)) { $process.Kill(); throw "SFS/XR probe timed out: $log" }
if ($process.ExitCode -ne 0) { throw "SFS/XR probe exit $($process.ExitCode): $log" }
$content = Get-Content -LiteralPath $log -Raw
if ($content -match 'STEREO_\w*(FAILED|REJECTED)' -or $content -notmatch 'STEREO_PROJECTION_SUBMITTED serial=1 arrayLayers=2' -or $content -notmatch 'XR_SHUTDOWN') {
    throw "SFS/XR projection validation failed: $log"
}
Write-Output "PASS: GPU SFS pair submitted as OpenXR stereo projection; runtime log: $log"
