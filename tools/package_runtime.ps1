param([string]$Build = (Join-Path $PSScriptRoot '..\build-clean\Release'), [string]$Package = (Join-Path $PSScriptRoot '..\runtime'), [switch]$Clean = $true)
$ErrorActionPreference = 'Stop'
if (!$Clean) { throw 'Runtime packages must contain playable files and assets only.' }
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
if (!(Test-Path -LiteralPath (Join-Path $Package 'assets\argent_controls.cfg'))) { Copy-Item -LiteralPath (Join-Path $root 'assets\argent_controls.cfg') -Destination (Join-Path $Package 'assets\argent_controls.cfg') }
$files = @('Fsr1Easu.spv','Fsr1Rcas.spv','ArgentLauncher.exe','ArgentRuntimeProbe.exe','ArgentLayer.dll','ArgentLayer.json','openxr_loader.dll','KharvoxBhapticsBridge.exe','bhaptics_library.dll','KharvoxPsvr2Bridge.exe','psvr2_toolkit_capi_loader.dll')
if (!(Test-Path -LiteralPath (Join-Path $Package 'assets/argent_controls.cfg.support'))) {
    Copy-Item -LiteralPath (Join-Path $root 'assets/argent_controls.cfg.support') -Destination (Join-Path $Package 'assets/argent_controls.cfg.support')
}
foreach ($file in $files) { Copy-Item -LiteralPath (Join-Path $Build $file) -Destination $Package -Force }
& (Join-Path $PSScriptRoot 'verify_package_integrations.ps1') -Package $Package
Copy-Item -LiteralPath (Join-Path $root "assets/argent-controller-bindings.png") -Destination (Join-Path $Package "assets/argent-controller-bindings.png") -Force
Copy-Item -LiteralPath (Join-Path $root "assets/kofi-support.png") -Destination (Join-Path $Package "assets/kofi-support.png") -Force
Write-Output "ARGENT runtime ready: $Package"
& (Join-Path $PSScriptRoot 'package_framegen.ps1') -Package $Package
