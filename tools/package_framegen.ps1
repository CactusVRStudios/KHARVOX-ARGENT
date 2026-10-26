param([Parameter(Mandatory=$true)][string]$Package)
$ErrorActionPreference='Stop'
$source=Join-Path (Split-Path -Parent $PSScriptRoot) 'runtimes/dlss-unlocked-0.4.0'
# Exact runtime already validated in KHARVOX CIRCLE. Never copy its logs or tools.
$approved=@{
 'version.dll'='7B3F90435BE4F94348BF8D128C84871C7F904C704F7690601A263277C327A7B2'
 'dlssg_sm86/backend/sm86_backend.dll'='30FFC6E09A347CCDE87ECC6FA54FD7818F5ED8A9353F5C31C2A90835C7F0CA9B'
 'dlssg_sm86/kernels/fg_kernels.dll'='1394C2D5D9CC8938BA3E52B043048D20D6D4C13A019C161B11A3DFD16343EC24'
 'dlssg_sm86/runtime/nvngx_dlssg.dll'='CADEE1AA6DB3907BABDC3285DA2AEBC4553D92B89C38BA3EAE8ABD8BBA1BEA72'
}
foreach($profile in @('rtx20-30','rtx40')){
 foreach($relative in $approved.Keys){
  $file=Join-Path (Join-Path $source $profile) $relative
  if(!(Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $approved[$relative]){throw "Unverified FrameGen runtime: $profile/$relative"}
  $destination=Join-Path (Join-Path (Join-Path $Package 'framegen') $profile) $relative
  New-Item -ItemType Directory -Force (Split-Path -Parent $destination) | Out-Null
  Copy-Item -LiteralPath $file -Destination $destination
 }
 $config=[IO.File]::ReadAllText((Join-Path (Join-Path $source $profile) 'dlssg_sm86.ini')).Replace('MaxGeneratedFrames=3','MaxGeneratedFrames=1').Replace('Level=2','Level=0')
 [IO.File]::WriteAllText((Join-Path (Join-Path (Join-Path $Package 'framegen') $profile) 'dlssg_sm86.ini'),$config,[Text.Encoding]::ASCII)
}
# Blackwell uses the signed NVIDIA runtime directly, without the SM86 proxy.
$nativeSource=Join-Path $source 'rtx40/dlssg_sm86/runtime/nvngx_dlssg.dll'
$nativeDirectory=Join-Path $Package 'framegen/rtx50'
New-Item -ItemType Directory -Force $nativeDirectory | Out-Null
Copy-Item -LiteralPath $nativeSource -Destination (Join-Path $nativeDirectory 'nvngx_dlssg.dll')
Write-Output 'PASS: bundled DLSS-G runtime hashes match CIRCLE; RTX50 native DLL included; fixed 2X profiles, no captured logs'
