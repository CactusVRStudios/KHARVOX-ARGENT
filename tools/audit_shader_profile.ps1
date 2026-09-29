param([string]$Profile = 'D:\DoomVR\vk3\Profiles\Doom Eternal')
$ErrorActionPreference = 'Stop'
$files = @(Get-ChildItem -LiteralPath (Join-Path $Profile 'ShaderSwap') -File | Sort-Object Name)
$result = foreach ($file in $files) {
    $source = Get-Content -LiteralPath $file.FullName -Raw
    [ordered]@{ name=$file.Name; sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash; stage=$file.Extension; multiview=($source -match 'gl_ViewIndex'); vk3dParams=($source -match 'Vk3DParams'); verifiedAgainstGame=$false }
}
[ordered]@{ profile=$Profile; shaderCount=$files.Count; iniSha256=(Get-FileHash -LiteralPath (Join-Path $Profile 'Vk3DVision.ini')).Hash; shaders=@($result) } | ConvertTo-Json -Depth 5
