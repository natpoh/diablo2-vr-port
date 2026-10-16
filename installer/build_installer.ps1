# Builds D2R_VR_Setup_v<version>.exe: stages what the installer packs into
# installer\payload (gitignored), then compiles D2R_VR_Setup.iss with Inno Setup 6.
#
#   powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1
#
# The mod's own files come from build\Release (build them first), the sky
# pictures from reshade\sky (OUR skies only - never extracted\, that is
# Blizzard's art), the FlatVR addon from BodyWalk's Release_main build, BodyWalk's
# Game Link plugin from its standalone build (vjoy plugins/game_link).
#
#   -Addon <file>     another FlatVR addon than Release_main's (one the bundled BodyWalk knows)
#   -GameLink <file>  another game_link.dll than the standalone build's
#   -Portable <dir>   a BodyWalk Portable folder kept from an earlier build, instead of
#                     making one from Release_main (which may hold a BodyWalk not released yet)
#   -SkipProfileCheck do not run tools\profile_sanitize.py on the staged BodyWalk profile (it needs Python)
param(
    [string]$Addon = "C:\wsl\vjoy\Release_main\flatvr\reshade_addon\FlatVR_DepthProvider.addon64",
    [string]$GameLink = "C:\wsl\vjoy\cpp_src\build_standalone\bin\Release\game_link.dll",
    [string]$Portable = "",
    [switch]$SkipProfileCheck
)
$ErrorActionPreference = "Stop"
# The BodyWalk profile this installer ships. BodyWalk lists it as "<Name> (mod)" and offers it again whenever
# profile.json changes; its cloud copy (CloudGameId / CloudProfileId) is how the author improves it without a
# new mod. 0.165 and later are bound to the profile "0165" (cloud profile 56); the older installers keep "D2VR"
# (cloud profile 53), so BodyWalk lists both on a PC ("0165 (mod)" and "D2VR (mod)") and each build puts its
# own first. To bind a later version to another profile, put its Name and CloudProfileId in the one line below.
# The line holds what installer\bodywalk_profile.json holds ("profile_name", "cloud_game_id", "cloud_profile_id");
# while they agree, the staged profile.json is that file byte for byte. When they differ, only those three
# values of the staged copy change (the repository's file stays as it is).
$BodyWalkProfile = @{ Name = "0165"; CloudGameId = 43; CloudProfileId = 56 }
$root = Split-Path $PSScriptRoot -Parent
$payload = Join-Path $PSScriptRoot "payload"
$addon = $Addon
$keepalive = "C:\wsl\vjoy\cpp_src\flatvr\reshade_depth_addon\FlatVR_Keepalive.addonfx"
$iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"

if (Test-Path $payload) { Remove-Item $payload -Recurse -Force }
New-Item -ItemType Directory -Force "$payload\plugins", "$payload\shaders", "$payload\sky", "$payload\bodywalk", "$payload\game" | Out-Null

foreach ($f in "d2rl-vrcam.dll", "D2R_VR_Settings.exe") { Copy-Item (Join-Path $root "build\Release\$f") "$payload\plugins\" }
Copy-Item (Join-Path $root "d2r_vr.ini") "$payload\plugins\"
# BodyWalk's side: its Game Link plugin, the mod's BodyWalk profile beside it, and the game's
# Game Link manifest (the actions and Mapping tabs vrcam sends, from the same tables), so they
# are in BodyWalk's Mapping from its first start.
Copy-Item $GameLink "$payload\bodywalk\game_link.dll"
$profileSource = Join-Path $PSScriptRoot "bodywalk_profile.json"
$profileText = [System.IO.File]::ReadAllText($profileSource)
function Set-ProfileValue([string]$text, [string]$key, [string]$literal) {
    $rx = [regex]('("' + [regex]::Escape($key) + '"\s*:\s*)("(?:[^"\\]|\\.)*"|-?\d+)')
    if ($rx.Matches($text).Count -ne 1) { throw "bodywalk_profile.json must hold exactly one ""$key"" value" }
    return $rx.Replace($text, [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $m.Groups[1].Value + $literal }, 1)
}
if (-not $BodyWalkProfile.Name -or $BodyWalkProfile.CloudGameId -lt 0 -or $BodyWalkProfile.CloudProfileId -lt 0) { throw "BodyWalkProfile: a name and non-negative cloud ids are needed" }
$stagedProfile = $profileText
$stagedProfile = Set-ProfileValue $stagedProfile "profile_name" (ConvertTo-Json ([string]$BodyWalkProfile.Name) -Compress)
$stagedProfile = Set-ProfileValue $stagedProfile "cloud_game_id" ([string][int]$BodyWalkProfile.CloudGameId)
$stagedProfile = Set-ProfileValue $stagedProfile "cloud_profile_id" ([string][int]$BodyWalkProfile.CloudProfileId)
$profileAsIs = $stagedProfile -ceq $profileText
if ($profileAsIs) { Copy-Item $profileSource "$payload\bodywalk\profile.json" }
else { [System.IO.File]::WriteAllText("$payload\bodywalk\profile.json", $stagedProfile, (New-Object System.Text.UTF8Encoding($false))) }
Write-Host ("BodyWalk profile: '{0} (mod)', cloud game {1} profile {2} - installer\bodywalk_profile.json {3}" -f $BodyWalkProfile.Name,
    $BodyWalkProfile.CloudGameId, $BodyWalkProfile.CloudProfileId, $(if ($profileAsIs) { "as it is" } else { "with these three values" }))
# Nothing that names a device ships in the profile: no tracker serial, MAC or user folder at ANY depth (0.165.0 carried a tracker
# serial in a nested preset). tools\profile_sanitize.py --fix cleans installer\bodywalk_profile.json.
if (-not $SkipProfileCheck) {
    $py = Get-Command python, py -ErrorAction SilentlyContinue | Where-Object { $_.Source -notmatch 'WindowsApps' } | Select-Object -First 1
    if (-not $py) { throw "Python is needed to check the BodyWalk profile (tools\profile_sanitize.py): install it, or pass -SkipProfileCheck" }
    $sanitize = Join-Path $root "tools\profile_sanitize.py"
    & $py.Source -I $sanitize --selftest
    if ($LASTEXITCODE -ne 0) { throw "tools\profile_sanitize.py --selftest failed ($LASTEXITCODE)" }
    & $py.Source -I $sanitize --check "$payload\bodywalk\profile.json"
    if ($LASTEXITCODE -ne 0) { throw "the BodyWalk profile carries device identifiers: python tools\profile_sanitize.py --fix installer\bodywalk_profile.json" }
}
$manifest = Start-Process -FilePath (Join-Path $root "build\Release\D2R_VR_Settings.exe") -Wait -PassThru `
    -ArgumentList "--game-link-manifest", "`"$payload\bodywalk\d2r.txt`""
if ($manifest.ExitCode -ne 0 -or -not (Test-Path "$payload\bodywalk\d2r.txt")) { throw "the Game Link manifest was not written ($($manifest.ExitCode))" }
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

# Every built installer also lands in <main checkout>\releases (git-ignored), with its SHA-256 beside it -
# the same folder whichever worktree built it, so the file to upload is never searched for under .claude.
$common = (& git -C $root rev-parse --path-format=absolute --git-common-dir).Trim()
$releases = Join-Path (Split-Path $common -Parent) "releases"
New-Item -ItemType Directory -Force $releases | Out-Null
$setup = Join-Path $PSScriptRoot "Output\D2R_VR_Setup_v$version.exe"
Copy-Item $setup $releases -Force
$hash = (Get-FileHash $setup -Algorithm SHA256).Hash
Set-Content -Path (Join-Path $releases "D2R_VR_Setup_v$version.exe.sha256") -Value "$hash  D2R_VR_Setup_v$version.exe" -Encoding ascii
Write-Host "releases: $releases\D2R_VR_Setup_v$version.exe  SHA-256 $hash"
