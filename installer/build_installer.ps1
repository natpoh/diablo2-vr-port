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
#   -WeightLibs <dir> where d2r_vr_weight.dll and d2r_vr_grip.dll are (the private weight_physics build)
#   -Portable <dir>   a BodyWalk Portable folder kept from an earlier build, instead of
#                     making one from Release_main (which may hold a BodyWalk not released yet)
#   -SkipProfileCheck do not run tools\profile_sanitize.py on the staged BodyWalk profile (it needs Python)
#
# The shared "D2R VR Launcher" lists the mod by the vrmod-manifest.json in <game>\mods\D2R-VR: this script makes it
# (tools\make_vrmod_manifest.py, from installer\launcher\vrmod-manifest.template.json; version = vrcam.cpp's g_info_version, checked
# against the launcher's schema) and stages it with launcher_icon.png as payload\launcher\ for the installer (installer\mod_files.inc).
# The staged plugin and Settings program must carry the D2R VR identity (VERSIONINFO: vr\vrcam.rc, tools\settings\settings.rc).
#
#   -BuildDir <dir>      where d2rl-vrcam.dll and D2R_VR_Settings.exe are built (default: build\Release)
#   -VersionLabel <v>    the version SHOWN by the installer and in the manifest, e.g. 0.167.0-launcher-test (must start with the
#                        plugin's version); the plugin's own version stays as it is
#   -OutputName <name>   the installer's file name without .exe (default D2R_VR_Setup_v<version>), e.g. D2R_VR_Authentic_Setup_launcher-test
#   -ReleasesDir <dir>   where the installer and its .sha256 are copied (default: <main checkout>\releases - a test build
#                        should name its own folder: an installer built with the default name would replace the release file there)
#   -CeilingFrom <dir>   a folder holding the cave-ceiling pictures d2r_vr.ini names (e.g. an earlier payload's sky\), looked in first
#   -SkyDomeFrom <dir>   a folder holding the game sky dome's painted act skies sky_act1..5_day_HDR.dds (tools/game_sky/make_sky_dome.py --act;
#                        default releases\inputs\sky_dome). [sky] game_sky draws them; D2R VR Settings makes the domes' units and plain
#                        textures itself (tools/settings/dome_assets.cpp), not these
#
# The shared "D2R VR Launcher" travels inside this installer (installer\launcher_bundle.inc; docs\launcher_authentic.md): Setup installs it silently,
# as the original user, when it is missing or older, then installs the mod. The launcher's Setup is carried UNMODIFIED, as its repository publishes it
# (D2R_VR_Launcher_Setup_<version>.exe), and PINNED: installer\launcher\launcher_pin.txt names its version, file name and SHA-256 (a different
# file stops the build). This is part of the normal build - a build that cannot carry the launcher FAILS:
#
#   (nothing)                    the file named in launcher_pin.txt is looked for in installer\launcher\cache\ (git-ignored); put it there once per release
#   -LauncherSetup <file>        another place for the file (or another version: then -LauncherSetupSha256 is needed too)
#   -LauncherSetupSha256 <hash>  another pin than launcher_pin.txt's
#   -NoLauncher                  build WITHOUT the launcher, on purpose (a test build): the build output says so. Never for a release.
#
param(
    [string]$Addon = "C:\wsl\vjoy\Release_main\flatvr\reshade_addon\FlatVR_DepthProvider.addon64",
    [string]$GameLink = "C:\wsl\vjoy\cpp_src\build_standalone\bin\Release\game_link.dll",
    [string]$Portable = "",
    [string]$WeightLibs = "C:\wsl\weight_physics\build\Release",
    [switch]$SkipProfileCheck,
    [string]$BuildDir = "",
    [string]$VersionLabel = "",
    [string]$OutputName = "",
    [string]$ReleasesDir = "",
    [string]$CeilingFrom = "",
    [string]$SkyDomeFrom = "C:\wsl\d2r_vr\releases\inputs\sky_dome",
    [string]$LauncherSetup = "",
    [string]$LauncherSetupSha256 = "",
    [switch]$NoLauncher
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

$build = if ($BuildDir) { $BuildDir } else { Join-Path $root "build\Release" }

# The version: vrcam's own (g_info_version) - the file name's and, unless -VersionLabel says another, the one shown.
$m = Select-String -Path (Join-Path $root "vr\vrcam.cpp") -Pattern 'g_info_version\[\] = "([0-9.]+)"'
$version = $m.Matches[0].Groups[1].Value
$shownVersion = if ($VersionLabel) { $VersionLabel } else { $version }
if (($shownVersion -split '-', 2)[0] -ne $version) { throw "-VersionLabel '$shownVersion' must start with the plugin's version $version" }
Write-Host "D2R VR $shownVersion"

# Who the two programs are, as the shared VR mod launcher (and a support log) reads it: the VERSIONINFO of the built plugin and Settings
# program (vr\vrcam.rc, tools\settings\settings.rc). A stale build without it stops here, before anything is staged.
foreach ($id in @(@("d2rl-vrcam.dll", "d2r-vr", "d2rl-vrcam.dll"), @("D2R_VR_Settings.exe", "d2r-vr-settings", "D2R_VR_Settings.exe"))) {
    $vi = (Get-Item (Join-Path $build $id[0])).VersionInfo
    if ($vi.ProductName -cne "D2R VR" -or $vi.InternalName -cne $id[1] -or $vi.OriginalFilename -cne $id[2] -or $vi.FileVersion -ne $version -or $vi.Comments -notmatch 'mod-id=d2r-vr ') {
        throw ("{0}: its VERSIONINFO is not D2R VR {1} (product '{2}', internal '{3}', original '{4}', version '{5}', comments '{6}') - rebuild it" -f
            $id[0], $version, $vi.ProductName, $vi.InternalName, $vi.OriginalFilename, $vi.FileVersion, $vi.Comments)
    }
}

# The launcher's Setup that travels inside: the pin (installer\launcher\launcher_pin.txt, or -LauncherSetupSha256), the name and the version INSIDE
# the file are checked first - a wrong or missing file stops the build before anything is staged. -NoLauncher builds without it, on purpose, and says so.
$ls = $null
$carriedLauncher = $null
if ($NoLauncher) {
    if ($LauncherSetup) { throw "-NoLauncher and -LauncherSetup cancel each other" }
    Write-Warning "NO LAUNCHER: -NoLauncher - this installer is built WITHOUT the D2R VR Launcher (it installs the mod only). Not for a release."
} else {
    $pinFile = Join-Path $PSScriptRoot "launcher\launcher_pin.txt"
    $pin = @{}
    if (Test-Path -LiteralPath $pinFile) {
        foreach ($line in Get-Content -LiteralPath $pinFile) {
            if ($line -match '^\s*(version|file|sha256)\s*=\s*(\S+)\s*$') { $pin[$Matches[1]] = $Matches[2] }
        }
    }
    $lsPath = $LauncherSetup
    if (-not $lsPath) {
        if (-not $pin.file) { throw "the launcher's Setup is part of the build: installer\launcher\launcher_pin.txt (version=, file=, sha256=) is missing or names no file. Pass -LauncherSetup <file> -LauncherSetupSha256 <pin>, or -NoLauncher for a test build without it." }
        $lsPath = Join-Path $PSScriptRoot "launcher\cache\$($pin.file)"
    }
    if (-not (Test-Path -LiteralPath $lsPath -PathType Leaf)) { throw "the launcher's Setup is not there: $lsPath - put the file (D2R_VR_Launcher_Setup_<version>.exe from the launcher's releases) in installer\launcher\cache\, or pass -LauncherSetup <file>; -NoLauncher builds without it (a test build, never a release)" }
    $ls = Get-Item -LiteralPath $lsPath
    $wantSha = $LauncherSetupSha256.Trim()
    if (-not $wantSha) {
        if ($pin.sha256 -and $pin.file -eq $ls.Name) { $wantSha = $pin.sha256 }
        else { throw "no pin for $($ls.Name): installer\launcher\launcher_pin.txt pins '$($pin.file)' - pass -LauncherSetupSha256 <the SHA-256 the launcher's release lists>" }
    }
    if ($ls.Name -notmatch '^D2R_VR_Launcher_Setup_(\d+(?:\.\d+){1,3})\.exe$') { throw "the launcher's Setup must be the unmodified D2R_VR_Launcher_Setup_<version>.exe (this one is '$($ls.Name)')" }
    $lsVersion = $Matches[1]
    $lsHash = (Get-FileHash -LiteralPath $ls.FullName -Algorithm SHA256).Hash
    if ($lsHash -ne $wantSha) { throw "the launcher's Setup: the SHA-256 of $($ls.Name) is $lsHash, not the pin $wantSha - not carried, nothing built" }
    $norm = { param($v) $p = @($v -split '[.,]' | ForEach-Object { [int]($_.Trim()) }); while ($p.Count -lt 4) { $p += 0 }; ($p[0..3] -join '.') }
    $fv = $ls.VersionInfo.FileVersion
    if (-not $fv -or (& $norm $fv) -ne (& $norm $lsVersion)) { throw "the launcher's Setup: $($ls.Name) says it is version '$fv' inside - a file renamed to another version is not carried" }
    if ($pin.version -and $pin.file -eq $ls.Name -and (& $norm $pin.version) -ne (& $norm $lsVersion)) { throw "installer\launcher\launcher_pin.txt: version=$($pin.version) does not fit its file=$($pin.file)" }
    $carriedLauncher = "$($ls.Name)  version $lsVersion  SHA-256 $lsHash  ($([math]::Round($ls.Length / 1MB, 1)) MB)"
}

if (Test-Path $payload) { Remove-Item $payload -Recurse -Force }
New-Item -ItemType Directory -Force "$payload\plugins", "$payload\shaders", "$payload\sky", "$payload\bodywalk", "$payload\game", "$payload\launcher", "$payload\license" | Out-Null
# MIT, with the third-party notices MinHook's and ReShade's licences ask for in a binary: it goes into the mod's folder (installer\mod_files.inc)
Copy-Item (Join-Path $root "LICENSE") "$payload\license\D2R_VR_LICENSE.txt"
# Settings' language files (lang\<code>.txt, en.txt the English to translate from, README.txt how) - into mods\D2R-VR\lang (mod_files.inc)
New-Item -ItemType Directory -Force "$payload\lang" | Out-Null
Get-ChildItem (Join-Path $root "lang") -File | Where-Object { $_.Extension -eq ".txt" } | Copy-Item -Destination "$payload\lang\"
# ... and how complete each is: keys missing, or translated from an English that has changed since (tools/lang_sync.py). A warning, never
# a failure - a missing text is shown in English.
$langPy = Get-Command python, py -ErrorAction SilentlyContinue | Where-Object { $_.Source -notmatch 'WindowsApps' } | Select-Object -First 1
if ($langPy) {
    & $langPy.Source -I (Join-Path $root "tools\lang_sync.py") check --exe (Join-Path $build "D2R_VR_Settings.exe") | ForEach-Object {
        if ($_ -match '^WARNING ') { Write-Warning ("languages: " + $_.Substring(8)) } else { Write-Host "languages: $_" }
    }
} else { Write-Warning "languages: no Python - lang\ not checked (tools\lang_sync.py check)" }

foreach ($f in "d2rl-vrcam.dll", "D2R_VR_Settings.exe") { Copy-Item (Join-Path $build $f) "$payload\plugins\" }
# no debug switch turned on reaches the players (tools/check_debug_defaults.py; 0.179 shipped the bow's aiming ray, 0.181 the log traces)
& python (Join-Path $root "tools\check_debug_defaults.py") (Join-Path $root "d2r_vr.ini")
if ($LASTEXITCODE -ne 0) { throw "d2r_vr.ini ships a debug switch turned on - set it back (python tools\check_debug_defaults.py d2r_vr.ini --fix) and build again" }
Copy-Item (Join-Path $root "d2r_vr.ini") "$payload\plugins\"
# the optional weight-physics and grip libraries (binaries only; their source is not in this repository)
foreach ($f in "d2r_vr_weight.dll", "d2r_vr_grip.dll", "d2r_vr_gait.dll") {   # gait: the other players' legs ([multiplayer] pose_share_legs)
    $src = Join-Path $WeightLibs $f
    if (Test-Path $src) { Copy-Item $src "$payload\plugins\" } else { Write-Warning "$f not found in $WeightLibs - the installer goes without it" }
}
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
$manifest = Start-Process -FilePath (Join-Path $build "D2R_VR_Settings.exe") -Wait -PassThru `
    -ArgumentList "--game-link-manifest", "`"$payload\bodywalk\d2r.txt`""
if ($manifest.ExitCode -ne 0 -or -not (Test-Path "$payload\bodywalk\d2r.txt")) { throw "the Game Link manifest was not written ($($manifest.ExitCode))" }
Copy-Item (Join-Path $root "reshade\D2R_DepthFog.fx") "$payload\shaders\"
Get-ChildItem (Join-Path $root "reshade\sky") -Filter "D2R_Sky*.png" | Copy-Item -Destination "$payload\sky\"
# The cave ceilings' pictures d2r_vr.ini names, beside the skies: our OpenAI drawings
# (tools/casc_extract/gen_ceiling_from_ref.py), kept in extracted\ - never in the repository -
# or, for the ones finished in the game (*_smooth), the game's D2R_Sky_ours. Without them
# ReShade said "errors loading some effects" and the caves had no ceiling (a player, 0.153).
$ceilFrom = @((Join-Path $root "extracted"), "D:\SteamLibrary\steamapps\common\Diablo II Resurrected\reshade-shaders\Textures\D2R_Sky_ours")
if ($CeilingFrom) { $ceilFrom = @($CeilingFrom) + $ceilFrom }
$ceilNames = Select-String -Path (Join-Path $root "d2r_vr.ini") -Pattern 'D2R_Ceiling_[A-Za-z0-9_]+\.png' -AllMatches |
    ForEach-Object { $_.Matches } | ForEach-Object { $_.Value } | Sort-Object -Unique
foreach ($n in $ceilNames) {
    $src = $ceilFrom | ForEach-Object { Join-Path $_ $n } | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $src) { throw "ceiling picture not found: $n (looked in $($ceilFrom -join ', '))" }
    Copy-Item $src "$payload\sky\"
}
# The game sky dome's painted act skies ([sky] game_sky, shipped on since 0.186): without them a fresh install had no dome picture
# (PC1 had them only copied by hand). Into the mod's D2R-VR.mpq\data (mod_files.inc).
New-Item -ItemType Directory -Force "$payload\skydome" | Out-Null
foreach ($act in 1..5) {
    $src = Join-Path $SkyDomeFrom "sky_act${act}_day_HDR.dds"
    if (-not (Test-Path $src)) { throw "the sky dome's act $act picture not found: $src (-SkyDomeFrom)" }
    Copy-Item $src "$payload\skydome\"
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

# The launcher's descriptor of the mod (docs\launcher_authentic.md: what Setup writes into <game>\mods\D2R-VR for it) and its icon, made, not
# hand-written: tools\make_vrmod_manifest.py fills the template with the version, lists the files Setup puts in the mod (x-mod-files: from
# installer\mod_files.inc against the payload staged ABOVE, so this comes last) and checks it against the launcher's schema.
$py = Get-Command python, py -ErrorAction SilentlyContinue | Where-Object { $_.Source -notmatch 'WindowsApps' } | Select-Object -First 1
if (-not $py) { throw "Python is needed to make the launcher's vrmod-manifest.json (tools\make_vrmod_manifest.py)" }
$maker = Join-Path $root "tools\make_vrmod_manifest.py"
& $py.Source -I $maker --selftest | Out-Null
if ($LASTEXITCODE -ne 0) { throw "tools\make_vrmod_manifest.py --selftest failed ($LASTEXITCODE)" }
$commit = (& git -C $root rev-parse --short HEAD).Trim()
if (& git -C $root status --porcelain --untracked-files=no) { $commit += "-dirty" }   # built from files not committed
$makerArgs = @("-I", $maker, "--out", "$payload\launcher", "--payload", $payload, "--commit", $commit, "--date", (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd"))
if ($VersionLabel) { $makerArgs += @("--version-label", $VersionLabel) }
& $py.Source @makerArgs
if ($LASTEXITCODE -ne 0) { throw "the launcher's vrmod-manifest.json was not made ($LASTEXITCODE)" }

$isccArgs = @("/DModVersion=$shownVersion")
if ($OutputName) { $isccArgs += "/DOutputName=$OutputName" }

# The launcher's Setup, carried (checked at the top, before anything is staged): staged and named to the compiler.
if ($ls) {
    New-Item -ItemType Directory -Force "$payload\launcher_setup" | Out-Null
    Copy-Item -LiteralPath $ls.FullName "$payload\launcher_setup\$($ls.Name)"
    $isccArgs += @("/DLauncherSetupFile=$($ls.Name)", "/DLauncherSetupVersion=$lsVersion")
    Write-Host "launcher: carried - $carriedLauncher"
}
$outName = if ($OutputName) { $OutputName } else { "D2R_VR_Setup_v$shownVersion" }
& $iscc @isccArgs (Join-Path $PSScriptRoot "D2R_VR_Setup.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }

# Every built installer also lands in a releases folder (git-ignored), with its SHA-256 beside it: <main checkout>\releases - the same
# folder whichever worktree built it, so the file to upload is never searched for under .claude - or -ReleasesDir.
if ($ReleasesDir) { $releases = $ReleasesDir }
else {
    $common = (& git -C $root rev-parse --path-format=absolute --git-common-dir).Trim()
    $releases = Join-Path (Split-Path $common -Parent) "releases"
}
New-Item -ItemType Directory -Force $releases | Out-Null
$setup = Join-Path $PSScriptRoot "Output\$outName.exe"
Copy-Item $setup $releases -Force
$hash = (Get-FileHash $setup -Algorithm SHA256).Hash
Set-Content -Path (Join-Path $releases "$outName.exe.sha256") -Value "$hash  $outName.exe" -Encoding ascii
Write-Host "releases: $releases\$outName.exe  SHA-256 $hash"
Write-Host $(if ($carriedLauncher) { "the installer carries the D2R VR Launcher: $carriedLauncher" } else { "the installer carries NO launcher (built with -NoLauncher)" })
