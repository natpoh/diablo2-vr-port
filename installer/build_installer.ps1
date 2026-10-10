# Builds D2R_VR_Setup_v<version>.exe: stages what the installer packs into
# installer\payload (gitignored), then compiles D2R_VR_Setup.iss with Inno Setup 6.
#
#   powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1
#
# The mod's own files come from build\Release (build them first), the sky
# pictures from reshade\sky (OUR skies only - never extracted\, that is
# Blizzard's art), the FlatVR addon from BodyWalk's Release_main build.
#
#   -Addon <file>     another FlatVR addon than Release_main's (one the bundled BodyWalk knows)
#   -Portable <dir>   a BodyWalk Portable folder kept from an earlier build, instead of
#                     making one from Release_main (which may hold a BodyWalk not released yet)
param(
    [string]$Addon = "C:\wsl\vjoy\Release_main\flatvr\reshade_addon\FlatVR_DepthProvider.addon64",
    [string]$Portable = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$payload = Join-Path $PSScriptRoot "payload"
$addon = $Addon
$keepalive = "C:\wsl\vjoy\cpp_src\flatvr\reshade_depth_addon\FlatVR_Keepalive.addonfx"
$iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"

if (Test-Path $payload) { Remove-Item $payload -Recurse -Force }
New-Item -ItemType Directory -Force "$payload\plugins", "$payload\shaders", "$payload\sky", "$payload\bodywalk", "$payload\game" | Out-Null

foreach ($f in "d2rl-vrcam.dll", "D2R_VR_Settings.exe") { Copy-Item (Join-Path $root "build\Release\$f") "$payload\plugins\" }
Copy-Item (Join-Path $root "d2r_vr.ini") "$payload\plugins\"
Copy-Item (Join-Path $root "build\Release\d2r_bridge.dll") "$payload\bodywalk\"
Copy-Item (Join-Path $root "bodywalk_bridge\profile.json") "$payload\bodywalk\"
Copy-Item (Join-Path $root "reshade\D2R_DepthFog.fx") "$payload\shaders\"
Get-ChildItem (Join-Path $root "reshade\sky") -Filter "D2R_Sky*.png" | Copy-Item -Destination "$payload\sky\"
# The cave ceilings' pictures d2r_vr.ini names, beside the skies: our OpenAI drawings
# (tools/casc_extract/gen_ceiling_from_ref.py), kept in extracted\ - never in the repository -
# or, for the ones finished in the game (*_smooth), the game's D2R_Sky_ours. Without them
# ReShade said "errors loading some effects" and the caves had no ceiling (a player, 0.153).
$ceilFrom = @((Join-Path $root "extracted"), "D:\SteamLibrary\steamapps\common\Diablo II Resurrected\reshade-shaders\Textures\D2R_Sky_ours")
$ceilNames = Select-String -Path (Join-Path $root "d2r_vr.ini") -Pattern 'D2R_Ceiling_[A-Za-z0-9_]+\.png' -AllMatches |
    ForEach-Object { $_.Matches } | ForEach-Object { $_.Value } | Sort-Object -Unique
foreach ($n in $ceilNames) {
    $src = $ceilFrom | ForEach-Object { Join-Path $_ $n } | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $src) { throw "ceiling picture not found: $n (looked in $($ceilFrom -join ', '))" }
    Copy-Item $src "$payload\sky\"
}
# the flat crosshair, beside the skies (vrcam [input] crosshair; tools/gen_crosshair.py)
Copy-Item (Join-Path $root "reshade\sky\D2R_Crosshair.png") "$payload\sky\"
Copy-Item $addon "$payload\game\FlatVR_DepthProvider.addon64"
# The keepalive effect BodyWalk writes into a game: vjoy's copy when there is one, else
# ours beside this script (installer\FlatVR_Keepalive.addonfx) - never the live game,
# which a clean test empties.
if (Test-Path $keepalive) { Copy-Item $keepalive "$payload\game\" }
else { Copy-Item (Join-Path $PSScriptRoot "FlatVR_Keepalive.addonfx") "$payload\game\" }

# BodyWalk Portable for players without BodyWalk: built by vjoy's own script
# from Release_main (build BodyWalk first), the same folder as the site's zip.
if ($Portable) { Copy-Item $Portable "$payload\bodywalk_portable" -Recurse }
else {
    & "C:\wsl\vjoy\installer\make_bodywalk_portable.ps1"
    if ($LASTEXITCODE -ne 0) { throw "make_bodywalk_portable.ps1 failed ($LASTEXITCODE)" }
    Copy-Item "C:\wsl\vjoy\installer\Output\BodyWalkVR_Portable" "$payload\bodywalk_portable" -Recurse
}

# The version shown and in the file name: vrcam's own (g_info_version).
$m = Select-String -Path (Join-Path $root "vr\vrcam.cpp") -Pattern 'g_info_version\[\] = "([0-9.]+)"'
$version = $m.Matches[0].Groups[1].Value
Write-Host "D2R VR $version"
& $iscc "/DModVersion=$version" (Join-Path $PSScriptRoot "D2R_VR_Setup.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }
