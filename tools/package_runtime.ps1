param([string]$Build = (Join-Path $PSScriptRoot '..\build-clean\Release'), [string]$Package = (Join-Path $PSScriptRoot '..\runtime'), [switch]$Clean = $true)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force (Join-Path $Package 'assets') | Out-Null
if (!(Test-Path -LiteralPath (Join-Path $Package 'assets/argent_hud_group.cfg'))) { Copy-Item -LiteralPath (Join-Path $root 'assets/argent_hud_group.cfg') -Destination (Join-Path $Package 'assets/argent_hud_group.cfg') }
Copy-Item -LiteralPath (Join-Path $root 'assets\argent_vr.cfg') -Destination (Join-Path $Package 'assets\argent_vr.cfg') -Force
if (!(Test-Path -LiteralPath (Join-Path $Package 'assets\argent_hud.cfg'))) { Copy-Item -LiteralPath (Join-Path $root 'assets\argent_hud.cfg') -Destination (Join-Path $Package 'assets\argent_hud.cfg') }
if (!(Test-Path -LiteralPath (Join-Path $Package 'assets\argent_offhand_hud.cfg'))) { Copy-Item -LiteralPath (Join-Path $root 'assets\argent_offhand_hud.cfg') -Destination (Join-Path $Package 'assets\argent_offhand_hud.cfg') }
if (!(Test-Path -LiteralPath (Join-Path $Package 'assets\argent_compass.cfg'))) { Copy-Item -LiteralPath (Join-Path $root 'assets\argent_compass.cfg') -Destination (Join-Path $Package 'assets\argent_compass.cfg') }
if (!(Test-Path -LiteralPath (Join-Path $Package 'assets\argent_hud_artwork.cfg'))) { Copy-Item -LiteralPath (Join-Path $root 'assets\argent_hud_artwork.cfg') -Destination (Join-Path $Package 'assets\argent_hud_artwork.cfg') }
Copy-Item -LiteralPath (Join-Path $root 'assets\kharvox-argent-logo.png') -Destination (Join-Path $Package 'assets\kharvox-argent-logo.png') -Force
New-Item -ItemType Directory -Force (Join-Path $Package 'assets/3D') | Out-Null
foreach ($hand in @('DOOM_LEFT_HAND_FIST.glb','DOOM_RIGHT_HAND_FIST.glb','DOOM_LEFT_HAND_GUN.glb','DOOM_RIGHT_HAND_GUN.glb')) {
    Copy-Item -LiteralPath (Join-Path $root "assets/3D/$hand") -Destination (Join-Path $Package 'assets/3D') -Force
}
foreach ($config in @('hand_models.cfg','hand_models_calibration_default.cfg','weapon_pose_calibration_default.cfg','hand_pose_calibration_default.cfg')) {
    Copy-Item -LiteralPath (Join-Path $root "assets/$config") -Destination $Package -Force
}
foreach ($shader in @('HandPbr.vert.spv','HandPbr.frag.spv')) {
    Copy-Item -LiteralPath (Join-Path $root "src/hands/$shader") -Destination $Package -Force
}
New-Item -ItemType Directory -Force $Package | Out-Null
if (!$Clean) { New-Item -ItemType Directory -Force (Join-Path $Package 'licenses') | Out-Null }
if (!(Test-Path -LiteralPath (Join-Path $Package 'assets\argent_controls.cfg'))) { Copy-Item -LiteralPath (Join-Path $root 'assets\argent_controls.cfg') -Destination (Join-Path $Package 'assets\argent_controls.cfg') }
$files = @('Fsr1Easu.spv','Fsr1Rcas.spv','ArgentLauncher.exe','ArgentRuntimeProbe.exe','ArgentLayer.dll','ArgentLayer.json','openxr_loader.dll','KharvoxBhapticsBridge.exe','bhaptics_library.dll','KharvoxPsvr2Bridge.exe','psvr2_toolkit_capi_loader.dll')
if (!(Test-Path -LiteralPath (Join-Path $Package 'assets/argent_controls.cfg.support'))) {
    Copy-Item -LiteralPath (Join-Path $root 'assets/argent_controls.cfg.support') -Destination (Join-Path $Package 'assets/argent_controls.cfg.support')
}
foreach ($file in $files) { Copy-Item -LiteralPath (Join-Path $Build $file) -Destination $Package -Force }
if (!$Clean) {
New-Item -ItemType Directory -Force (Join-Path $Package 'docs') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $Package -Force
New-Item -ItemType Directory -Force (Join-Path $Package 'tools') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'docs\TESTER-GAMEPLAY.md') -Destination (Join-Path $Package 'docs') -Force
Copy-Item -LiteralPath (Join-Path $root 'docs/PERFORMANCE-ANALYSIS.md') -Destination (Join-Path $Package 'docs') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'verify_package_integrations.ps1') -Destination (Join-Path $Package 'tools') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'analyze_pipeline_spikes.py') -Destination (Join-Path $Package 'tools') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'analyze_performance_diagnostics.py') -Destination (Join-Path $Package 'tools') -Force
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'performance') -Filter '*.cmd' -File | Copy-Item -Destination (Join-Path $Package 'tools') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'xr-sync-baseline.cmd'),(Join-Path $PSScriptRoot 'xr-sync-early.cmd') -Destination (Join-Path $Package 'tools') -Force
}
if (!$Clean) {
Copy-Item -LiteralPath (Join-Path $root 'docs\KHARVOX-LICENSE.txt'),(Join-Path $root 'docs\THIRD_PARTY_NOTICES.md') -Destination (Join-Path $Package 'licenses') -Force
Copy-Item -LiteralPath (Join-Path $root 'third-party\openxr-sdk\LICENSE') -Destination (Join-Path $Package 'licenses\OpenXR-APACHE-2.0.txt') -Force
Copy-Item -LiteralPath (Join-Path $root 'third-party\vulkan-headers\LICENSE.md') -Destination (Join-Path $Package 'licenses\Vulkan-Headers-LICENSE.md') -Force
Copy-Item -LiteralPath (Join-Path $root 'third-party\psvr2-toolkit\LICENSE') -Destination (Join-Path $Package 'licenses\PSVR2-Toolkit-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $root 'third-party\fsr1\LICENSE') -Destination (Join-Path $Package 'licenses\FSR1-MIT.txt') -Force
Copy-Item -LiteralPath (Join-Path $root 'third-party\minhook\LICENSE.txt') -Destination (Join-Path $Package 'licenses\MinHook-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $root 'third-party/cgltf/LICENSE') -Destination (Join-Path $Package 'licenses/cgltf-MIT.txt') -Force
New-Item -ItemType Directory -Force (Join-Path $Package 'licenses/sfs-compiler') | Out-Null
Copy-Item -Path (Join-Path $root 'third-party/sfs-compiler-licenses/*.txt') -Destination (Join-Path $Package 'licenses/sfs-compiler') -Force
}
& (Join-Path $PSScriptRoot 'verify_package_integrations.ps1') -Package $Package
Copy-Item -LiteralPath (Join-Path $root "assets/argent-controller-bindings.png") -Destination (Join-Path $Package "assets/argent-controller-bindings.png") -Force
if (!$Clean) {
$manifest = Get-ChildItem -LiteralPath $Package -File -Recurse | Where-Object { $_.Name -ne 'manifest.json' -and $_.FullName -notmatch '\\(logs|captures)\\' -and $_.Name -notlike '*.request' } | ForEach-Object { [ordered]@{ name=$_.FullName.Substring((Get-Item -LiteralPath $Package).FullName.Length + 1); sha256=(Get-FileHash -LiteralPath $_.FullName).Hash } }
$manifest | ConvertTo-Json | Set-Content (Join-Path $Package 'manifest.json')
}
Write-Output "ARGENT runtime ready: $Package"


