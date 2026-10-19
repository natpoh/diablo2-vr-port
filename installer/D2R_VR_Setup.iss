; D2R VR - installer for the mod (Inno Setup 6). Built by build_installer.ps1,
; which stages installer\payload and passes /DModVersion.
;
; What it does:
;   - finds Diablo II: Resurrected (Steam, Battle.net or the folder you pick);
;   - checks D2RLoader is there (the mod runs under it; we do not ship it) and
;     switches on its allow_global_extensions;
;   - installs D2R VR as THE D2RLoader mod D2R-VR (shared\mod_layout.h): everything of ours is in <game>\mods\D2R-VR -
;       d2rloader\plugins\      d2rl-vrcam.dll, d2r_vr.ini (+ the defaults, the headset's and the depth's ini, Settings' window
;                               file), the skill-icon cache
;       d2rloader\d2r_vr_libs\  the optional weight and grip libraries (not among the plugins: D2RLoader would reject them)
;       d2rloader\logs\         the plugin's own logs, D2R VR Authentic Settings' launch log
;       reshade\               ReShade with its ini and presets, the fog/sky shader, our sky pictures, FlatVR's add-on
;       BodyWalkVR\            BodyWalk Portable (VR, when no BodyWalk is installed)
;       D2R-VR.mpq\modinfo.json, vrmod-manifest.json + launcher_icon.png (for the shared launcher), D2R_VR_LICENSE.txt
;     Outside that folder: D2R VR Authentic Settings beside D2R.exe (the launcher's START and Settings button name it), D2RLoader's own
;     d2rloader\config\d2rloader.toml (allow_global_extensions; default_mod = D2R-VR when no mod was the default) and BodyWalk's
;     files in %LOCALAPPDATA%\BodyWalkVR. So another VR mod (bearstonem's d2r-pcvr as mods\D2R-PCVR) sits beside it without one file
;     in the other's way; D2RLoader runs the one it is told (D2RLoader.exe -mod D2R-VR, D2R VR Authentic Settings' START). An earlier install
;     (0.169 and older: the plugin in <game>\d2rloader\plugins, ReShade beside D2R.exe, the libraries in d2rloader\d2r_vr_libs,
;     BodyWalk Portable in <game>\BodyWalkVR) is moved into the mod once, the player's settings and their backups with it;
;   - writes vrmod-manifest.json and launcher_icon.png into the mod's folder (tools\make_vrmod_manifest.py,
;     staged by build_installer.ps1): that is all the shared "D2R VR Launcher" needs to list and start the mod.
;     Nothing is written into another mod's folder (d2r-pcvr's mods\D2R-PCVR); the uninstaller removes only mods\D2R-VR;
;   - carries the launcher's own Setup (build_installer.ps1; installer\launcher\launcher_pin.txt) and, after the mod is in, runs it
;     silently as the original user when the launcher is missing or older (launcher_bundle.inc); the uninstaller leaves the launcher installed;
;   - ReShade: D2R VR Authentic Settings has the button (Install ReShade): it downloads the current ReShade with add-on support
;     from reshade.me, installs it headless, renames its dxgi.dll to ReShade64.dll - the
;     plain D2R.exe refuses an unsigned dxgi.dll, vrcam loads ReShade64.dll
;     itself under D2RLoader (vr\vrcam.cpp, LoadReShade) - and moves it into the mod's reshade folder.
;     A dxgi.dll that is next to D2R.exe already is the PLAYER'S OWN ReShade and is used as it is: Setup and the uninstaller never rename, move,
;     replace or delete it. D2R VR's effect and pictures (mods\D2R-VR\reshade) reach it through two search paths added to ITS ReShade.ini
;     (D2R_VR_Settings.exe --reshade-paths add, at the end of Setup; append-only, a copy kept as ReShade.ini.before_d2rvr; the uninstaller
;     takes them out again), and the plugin loads FlatVR's add-on from the mod's folder (mod_layout.inc PlaceReShade);
;   - VR only: BodyWalk. None installed: the BodyWalk Portable this Setup
;     carries (vjoy installer\make_bodywalk_portable.ps1; LITE on its first
;     start) goes into <game>\mods\D2R-VR\BodyWalkVR, with ViGEmBus and a
;     Start menu shortcut; running this Setup again refreshes that copy.
;     Installed: used as it is when it is at least 1.79 (its strike gestures no longer
;     go off on head turns or steps; 1.78 for the gesture zones in the headset and the
;     Input Source the game asks for, host API 9/10; 1.77 for the add-on's present
;     stamp); an older one (Steam's 1.74, an old standalone) is left
;     alone and the portable copy goes in beside it instead - D2R VR Authentic Settings
;     starts the newest BodyWalk it finds. Then
;     BodyWalk's Game Link plugin goes into BodyWalk's plugins (the old D2R
;     Bridge plugin out), the game's Game Link manifest into BodyWalk's
;     game_link folder (the mod's actions are in BodyWalk's Mapping from its
;     first start), BodyWalk's settings are set for the mod
;     (D2R_VR_Settings.exe --setup-bodywalk), and the FlatVR depth addon goes
;     into the mod's reshade folder;
;   - writes the chosen way to play into d2r_vr.ini ([mode] platform).

#ifndef ModVersion
  #define ModVersion "0.0"
#endif
; D2R VR as a D2RLoader mod (shared/mod_layout.h: the same names): ModName / ModDir (the folder id, never changes),
; ModDisplay / ModTagline (what players see: installer, Installed apps, shortcuts, modinfo.json's description, the launcher's card)
#include "mod_names.inc"
; the installer's file name: D2R_VR_Setup_v<version>.exe, unless build_installer.ps1 -OutputName says another (a test build)
#ifndef OutputName
  #define OutputName "D2R_VR_Setup_v" + ModVersion
#endif
; where build_installer.ps1 stages what the installer packs (installer\tests\mod_layout_test.iss: a scratch folder laid out the same)
#define PayloadDir "payload"
#define MinBodyWalkMajor 1
#define MinBodyWalkMinor 79
#define BodyWalkDownload "https://bodywalkvr.com/api/download/latest?product=bodywalk"
#define D2RLoaderSite "https://d2rloader.net"
; The one D2RLoader the mod is made for, fetched only when the player asks (InstallD2RLoader).
; The SHA-256 is the one d2rloader.net publishes on its download page. On 2026-10-13 its author
; repacked the 1.3.1 ZIP under the same name (the compression and the order of the files; D2RCore.dll,
; D2RLoader.exe and d2rloader.mpq are byte for byte the same), so the page lists a new hash as
; current and the old one as historical: both are accepted, any other file is not.
#define D2RLoaderZipUrl "https://d2rloader.net/downloads/D2RLoader-1.3.1-beta.zip"
#define D2RLoaderZip "D2RLoader-1.3.1-beta.zip"
#define D2RLoaderSha256 "f7bf0287398890b09bcc68b576327e727304bd88f7be80160f9676e8bd6871e2"
#define D2RLoaderSha256Historical "9286c6b5bff7f1043658411dbea305faa9698b6a455e6441668fc6d02c22aad7"
; Per user and writable: BodyWalk writes config.json, imgui.ini and plugins\ beside its exe.
; BodyWalk Portable is part of the mod, in the mod's folder (an earlier install's, beside D2R.exe, is moved there). It writes nothing
; beside itself (1.74 on): settings, configs and plugins go to %LOCALAPPDATA%\BodyWalkVR.
#define PortableDir "{app}\" + ModDir + "\BodyWalkVR"
#define ViGEmSetup "ViGEmBus_1.22.0_x64_x86_arm64.exe"

[Setup]
AppId={{8C1F3A52-6D0B-4E47-9B7E-2D5A0C9E4F31}
AppName={#ModDisplay}
AppVersion={#ModVersion}
AppPublisher=natpoh
AppPublisherURL=https://bodywalkvr.com
; The mod's own icon (tools/gen_icon.py) on the setup exe, in the wizard and in
; Installed apps; the wizard pictures are that icon on white, 100% and 200%.
SetupIconFile=..\tools\settings\d2r_vr.ico
WizardSmallImageFile=wizard_small.bmp,wizard_small_2x.bmp
UninstallDisplayIcon={app}\D2R_VR_Settings.exe
DefaultDirName={code:DefaultGameDir}
AppendDefaultDirName=no
DirExistsWarning=no
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
; The folder page always shows, an update too: the game may have moved, or a PC may hold several copies of it (one for
; online play, one for offline) - the player picks which one gets the mod. Inno's default (auto) skipped it on an update.
DisableDirPage=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=Output
OutputBaseFilename={#OutputName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#ModDisplay} {#ModVersion}

[Messages]
SelectDirLabel3=Setup will install D2R VR into the folder of Diablo II: Resurrected - the one with D2R.exe and D2RLoader.exe. If you have more than one copy of the game, pick the one you want the mod in. D2R VR is a D2RLoader mod: it goes into its own folder there, mods\D2R-VR ({#SettingsDisplay} stays next to D2R.exe).
SelectDirBrowseLabel=If this is not your game's folder, click Browse.

[Files]
; D2R VR Authentic Settings beside D2R.exe (the only file of the mod outside its folder: the launcher's START and Settings button name it there)
Source: "payload\plugins\D2R_VR_Settings.exe"; DestDir: "{app}"; Flags: ignoreversion
; The mod's own files, all in <game>\mods\D2R-VR: the plugin and its ini, the libraries, the shader, the sky, FlatVR's add-on, the licence,
; and the shared VR mod launcher's vrmod-manifest.json with its icon (mod_files.inc; installer\tests\mod_layout_test.iss includes the same lines)
#include "mod_files.inc"
; The shared VR mod launcher's own Setup, carried inside (build_installer.ps1 -LauncherSetup; no line in a build that carries none), unpacked into
; {tmp} only when it is needed (launcher_bundle.inc)
#include "launcher_bundle_files.inc"
; BodyWalk Game Link (vjoy plugins/game_link): the game talks to BodyWalk through it (vr/link.cpp).
Source: "payload\bodywalk\game_link.dll"; DestDir: "{localappdata}\BodyWalkVR\plugins\game_link"; Flags: ignoreversion; Check: IsVR
; The game's manifest as Game Link keeps it (D2R_VR_Settings.exe --game-link-manifest): the
; "D2R: ..." actions are in BodyWalk's Mapping from its first start, before the game ran.
Source: "payload\bodywalk\d2r.txt"; DestDir: "{localappdata}\BodyWalkVR\game_link"; Flags: ignoreversion; Check: IsVR
; The mod's BodyWalk profile (listed as "<profile_name> (mod)": "0165 (mod)" from 0.165, "D2VR (mod)" before),
; beside the plugin as BodyWalk looks for it:
; BodyWalk applies it on its own terms (a profile the player changed is never
; overwritten without asking).
Source: "payload\bodywalk\profile.json"; DestDir: "{localappdata}\BodyWalkVR\plugins\game_link"; Flags: ignoreversion; Check: IsVR
; BodyWalk Portable, only when no BodyWalk is installed. LITE is forced on its
; first start only: refreshing the copy keeps the player's own choice.
Source: "payload\bodywalk_portable\*"; DestDir: "{#PortableDir}"; Excludes: "force_lite_mode.txt,imgui.ini"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: UsePortable
Source: "payload\bodywalk_portable\imgui.ini"; DestDir: "{#PortableDir}"; Flags: onlyifdoesntexist; Check: UsePortable
Source: "payload\bodywalk_portable\force_lite_mode.txt"; DestDir: "{#PortableDir}"; Flags: ignoreversion; Check: PortableFresh

[UninstallDelete]
; the portable BodyWalk goes with the mod (it is in the mod's folder now, which RemoveMod removes whole; this covers an uninstall log written
; before the move); its settings in %LOCALAPPDATA%\BodyWalkVR are shared with any BodyWalk and stay
Type: filesandordirs; Name: "{#PortableDir}"; Check: UsePortable

[InstallDelete]
; (the earlier names of the settings program's shortcuts are in mod_shortcuts.inc)
; the D2R Bridge plugin, BodyWalk's side up to 0.157: Game Link does its work now, and the
; two together would register every action twice (gone again after BodyWalk is closed,
; SetBodyWalkSettings, if it was running now)
Type: filesandordirs; Name: "{localappdata}\BodyWalkVR\plugins\d2r_bridge"

; the rest of what Setup and the uninstaller take away of the mod (mod_delete.inc: its own [InstallDelete] and [UninstallDelete])
#include "mod_delete.inc"

; the settings program's and the game's shortcuts, the desktop task, and the earlier names of those shortcuts (mod_shortcuts.inc: its own [Tasks], [Icons], [InstallDelete],
; [UninstallDelete]; installer\tests\mod_layout_test.iss includes the same lines)
#include "mod_shortcuts.inc"

[Icons]
Name: "{autoprograms}\BodyWalk VR"; Filename: "{#PortableDir}\BodyWalkVR.exe"; WorkingDir: "{#PortableDir}"; Check: UsePortable


[Run]
; shellexec: started the way Explorer starts it - a player got "CreateProcess failed; code 740,
; the requested operation requires elevation" from the plain CreateProcess (0.147, 2026-10-08)
Filename: "{app}\D2R_VR_Settings.exe"; WorkingDir: "{app}"; Description: "Open {#SettingsDisplay}"; Flags: postinstall nowait skipifsilent shellexec
; The shared VR mod launcher, when the player has it (per user, installed on its own): offered, never ticked - Setup does not need it
Filename: "{code:LauncherExePath}"; Description: "Open D2R VR Launcher"; Flags: postinstall nowait skipifsilent shellexec unchecked; Check: LauncherInstalled

[Code]
var
  ModePage: TInputOptionWizardPage;
  DownloadPage: TDownloadWizardPage;
  ReShadeSetup: String;      // a downloaded ReShade installer to run, or ''
  BodyWalkSetup: String;     // a downloaded BodyWalk installer to run, or ''
  BodyWalkPortable: Boolean; // no BodyWalk installed: the portable copy goes in
  BodyWalkFresh: Boolean;    // ... and there was none of it yet: LITE on its first start
  BodyWalkDir: String;       // where BodyWalk is (found, or after installing it)
  BodyWalkSteam: Boolean;    // the Steam build (it updates itself; no installer of ours)

function IsVR: Boolean;
begin
  Result := ModePage.Values[1];
end;

function UsePortable: Boolean;
begin
  Result := IsVR and BodyWalkPortable;
end;

// BodyWalk Portable of ours is there already: in the mod's folder, or (0.169 and older) beside D2R.exe, where MovePortableIntoMod finds it. Either way
// it is not "fresh": the player's own LITE / PRO choice stays.
function PortableThere: Boolean;
begin
  Result := FileExists(ExpandConstant('{#PortableDir}') + '\BodyWalkVR.exe') or FileExists(WizardDirValue + '\BodyWalkVR\BodyWalkVR.exe');
end;

function PortableFresh: Boolean;
begin
  Result := UsePortable and BodyWalkFresh;
end;

function PlatformValue(Param: String): String;
begin
  if IsVR then Result := '1' else Result := '0';
end;

// ---------------------------------------------------------------- the game

function UninstallLocation(Root: Integer; Key: String): String;
begin
  Result := '';
  if not RegQueryStringValue(Root, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + Key, 'InstallLocation', Result) then
    Result := '';
end;

function IsGameDir(Dir: String): Boolean;
begin
  Result := (Dir <> '') and FileExists(AddBackslash(Dir) + 'D2R.exe');
end;

// Steam (app 2536520), Battle.net, the usual folders.
function DefaultGameDir(Param: String): String;
var
  Candidates: array of String;
  I: Integer;
begin
  SetArrayLength(Candidates, 7);
  Candidates[0] := UninstallLocation(HKLM64, 'Steam App 2536520');
  Candidates[1] := UninstallLocation(HKLM32, 'Steam App 2536520');
  Candidates[2] := UninstallLocation(HKLM32, 'Diablo II Resurrected');
  Candidates[3] := UninstallLocation(HKLM64, 'Diablo II Resurrected');
  Candidates[4] := ExpandConstant('{commonpf32}\Diablo II Resurrected');
  Candidates[5] := ExpandConstant('{commonpf32}\Steam\steamapps\common\Diablo II Resurrected');
  Candidates[6] := ExpandConstant('{commonpf64}\Diablo II Resurrected');
  Result := ExpandConstant('{commonpf32}\Diablo II Resurrected');
  for I := 0 to GetArrayLength(Candidates) - 1 do
    if IsGameDir(Candidates[I]) then
    begin
      Result := RemoveBackslashUnlessRoot(Candidates[I]);
      Exit;
    end;
end;

// ---------------------------------------------------------------- the D2RLoader mod D2R-VR
// Its folders, MoveEarlierInstall, modinfo.json, default_mod, allow_global_extensions, the uninstaller's RemoveMod and the launcher
// lookup are in mod_layout.inc - the same file installer\tests\mod_layout_test.iss runs for real in a scratch folder.
#include "mod_layout.inc"
// The shared "D2R VR Launcher": installed silently, as the original user, when it is missing or older than the one this Setup carries
// (BundleLauncher after the mod is in; launcher_bundle.inc - installer\tests\mod_layout_test.iss runs it for real with a stand-in).
#include "launcher_bundle.inc"

// D2RLoader 1.3.1 into the game's folder, on the player's click: downloaded from
// d2rloader.net (checked against the SHA-256 it publishes, the current or the historical one),
// unpacked by PowerShell to a folder of its own, and the contents of the folder
// D2RLoader.exe is in copied beside D2R.exe. A player found the download refused
// by Edge and Chrome (2026-10-08). True when D2RLoader.exe is there afterwards.
function InstallD2RLoader(GameDir: String): Boolean;
var
  Zip, Unpack, Script, Ps, Sum: String;
  Code: Integer;
begin
  Result := False;
  DownloadPage.Clear;
  // No hash given to the page (it takes one): the file is checked below against the two published ones.
  DownloadPage.Add('{#D2RLoaderZipUrl}', '{#D2RLoaderZip}', '');
  DownloadPage.Show;
  try
    try
      DownloadPage.Download;
    except
      MsgBox('D2RLoader could not be downloaded from d2rloader.net: ' + GetExceptionMessage + #13#10#13#10 +
             'Try again, or download it from the site yourself.', mbError, MB_OK);
      Exit;
    end;
  finally
    DownloadPage.Hide;
  end;
  Zip := ExpandConstant('{tmp}\{#D2RLoaderZip}');
  try
    Sum := GetSHA256OfFile(Zip);
  except
    Sum := '';
  end;
  if not (SameText(Sum, '{#D2RLoaderSha256}') or SameText(Sum, '{#D2RLoaderSha256Historical}')) then
  begin
    DeleteFile(Zip);
    MsgBox('The file downloaded from d2rloader.net is not the D2RLoader 1.3.1 ZIP the site publishes (its SHA-256 differs), ' +
           'so nothing was installed.' + #13#10#13#10 + 'Try again, or download D2RLoader from the site yourself.', mbError, MB_OK);
    Exit;
  end;
  Unpack := ExpandConstant('{tmp}\d2rloader_unpack');
  Script := ExpandConstant('{tmp}\unpack_d2rloader.ps1');
  SaveStringToFile(Script,
    'param($Zip, $Unpack, $Game)' + #13#10 +
    '$ErrorActionPreference = ''Stop''' + #13#10 +
    'Expand-Archive -LiteralPath $Zip -DestinationPath $Unpack -Force' + #13#10 +
    '$e = Get-ChildItem -LiteralPath $Unpack -Recurse -Filter ''D2RLoader.exe'' | Select-Object -First 1' + #13#10 +
    'if (-not $e) { exit 3 }' + #13#10 +
    'Get-ChildItem -LiteralPath $e.DirectoryName | Copy-Item -Destination $Game -Recurse -Force' + #13#10 +
    'exit 0' + #13#10, False);
  Ps := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
  if not Exec(Ps, '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + Script + '" -Zip "' + Zip + '" -Unpack "' + Unpack +
              '" -Game "' + GameDir + '"', '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
  begin
    MsgBox('D2RLoader could not be unpacked into the game''s folder (code ' + IntToStr(Code) + ').', mbError, MB_OK);
    Exit;
  end;
  if not FileExists(GameDir + '\D2RLoader.exe') then
  begin
    MsgBox('D2RLoader was unpacked, but D2RLoader.exe is not in the game''s folder: an antivirus may have removed it ' +
           '(Windows Security > Protection history).', mbError, MB_OK);
    Exit;
  end;
  EnableGlobalExtensions(GameDir);
  Result := True;
end;

// ---------------------------------------------------------------- versions

// '1.74', '1.74.0.0' -> major, minor; False if it does not read as one.
function ParseVersion(V: String; var Major, Minor: Integer): Boolean;
var
  P: Integer;
  Rest: String;
begin
  Result := False;
  V := Trim(V);
  P := Pos('.', V);
  if P = 0 then Exit;
  Major := StrToIntDef(Copy(V, 1, P - 1), -1);
  Rest := Copy(V, P + 1, Length(V));
  P := Pos('.', Rest);
  if P > 0 then Rest := Copy(Rest, 1, P - 1);
  Minor := StrToIntDef(Rest, -1);
  Result := (Major >= 0) and (Minor >= 0);
end;

function AtLeastMin(V: String): Boolean;
var
  Major, Minor: Integer;
begin
  Result := ParseVersion(V, Major, Minor) and
            ((Major > {#MinBodyWalkMajor}) or ((Major = {#MinBodyWalkMajor}) and (Minor >= {#MinBodyWalkMinor})));
end;

// ---------------------------------------------------------------- BodyWalk

// The uninstall entry: the current AppId, the old name, either view, either hive.
function BodyWalkEntry(var Dir, Version: String): Boolean;
var
  Keys: array of String;
  Roots: array of Integer;
  I, J: Integer;
  Base: String;
begin
  Result := False;
  SetArrayLength(Keys, 2);
  Keys[0] := '{5E9E3C51-4043-4245-8B24-817C647DE553}_is1';
  Keys[1] := 'BodyWalkVR_is1';
  SetArrayLength(Roots, 4);
  Roots[0] := HKLM64; Roots[1] := HKLM32; Roots[2] := HKCU64; Roots[3] := HKCU32;
  for I := 0 to 1 do
    for J := 0 to 3 do
    begin
      Base := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + Keys[I];
      if RegQueryStringValue(Roots[J], Base, 'InstallLocation', Dir) and FileExists(AddBackslash(Dir) + 'BodyWalkVR.exe') then
      begin
        Dir := RemoveBackslashUnlessRoot(Dir);
        if not RegQueryStringValue(Roots[J], Base, 'DisplayVersion', Version) then Version := '';
        Result := True;
        Exit;
      end;
    end;
end;

// Where BodyWalk is and which version: the exe's own version block (1.74 on),
// else the uninstall entry's. Steam: its own entry, version from the exe only.
function FindBodyWalk(var Version: String): Boolean;
var
  ExeVersion, SteamDir: String;
begin
  BodyWalkSteam := False;
  Result := BodyWalkEntry(BodyWalkDir, Version);
  if not Result then
  begin
    SteamDir := UninstallLocation(HKLM64, 'Steam App 4711120');
    if SteamDir = '' then SteamDir := UninstallLocation(HKLM32, 'Steam App 4711120');
    if (SteamDir <> '') and FileExists(AddBackslash(SteamDir) + 'BodyWalkVR.exe') then
    begin
      BodyWalkDir := RemoveBackslashUnlessRoot(SteamDir);
      BodyWalkSteam := True;
      Version := '';
      Result := True;
    end;
  end;
  // None installed: the portable copy we carry (or put in on an earlier run).
  BodyWalkPortable := not Result;
  if BodyWalkPortable then
  begin
    BodyWalkDir := ExpandConstant('{#PortableDir}');
    BodyWalkFresh := not PortableThere;
    Version := '';
    Exit;
  end;
  if Result and GetVersionNumbersString(BodyWalkDir + '\BodyWalkVR.exe', ExeVersion) and (ExeVersion <> '0.0.0.0') then
    Version := ExeVersion;
end;

function BodyWalkRunning: Boolean;
var
  Code: Integer;
begin
  Result := Exec(ExpandConstant('{cmd}'), '/C tasklist /FI "IMAGENAME eq BodyWalkVR.exe" /NH | find /I "BodyWalkVR.exe" >NUL',
                 '', SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0);
end;

// BodyWalk's settings for the mod: its user settings (standalone and Steam)
// and the defaults a fresh install starts from.
procedure SetBodyWalkSettings;
var
  Args: String;
  Code: Integer;
begin
  while BodyWalkRunning do
    if MsgBox('BodyWalk is running. Close it so Setup can set it up for D2R VR (Universal tracking output, FlatVR, the Game Link plugin), then click OK.' + #13#10#13#10 +
              'Cancel skips this: you can set it by hand later.', mbInformation, MB_OKCANCEL) = IDCANCEL then
      Exit;
  // the old D2R Bridge plugin, if BodyWalk held it when [InstallDelete] ran
  DelTree(ExpandConstant('{localappdata}\BodyWalkVR\plugins\d2r_bridge'), True, True, True);
  Args := '--setup-bodywalk' +
          ' "' + ExpandConstant('{localappdata}\BodyWalkVR\usersettings.json') + '"' +
          ' "' + ExpandConstant('{localappdata}\BodyWalkVR\usersettings_steam.json') + '"' +
          ' "' + BodyWalkDir + '\settings.json"' +
          ' "' + BodyWalkDir + '\steam_default_settings.json"';
  if not Exec(ExpandConstant('{app}\D2R_VR_Settings.exe'), Args, '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
    MsgBox('BodyWalk''s settings could not all be set (code ' + IntToStr(Code) + '). In BodyWalk switch on Universal tracking output ' +
           '(Startup tab) and FlatVR, and make sure the Game Link plugin is on.', mbError, MB_OK);
end;

// ViGEmBus: BodyWalk's virtual Xbox pad: walking, sticks and buttons reach the game through it. The
// BodyWalk installer runs it; for the portable copy we do, once.
procedure InstallViGEm;
var
  Code: Integer;
begin
  if RegKeyExists(HKLM64, 'SYSTEM\CurrentControlSet\Services\ViGEmBus') then Exit;
  if not Exec(ExpandConstant('{#PortableDir}\drivers\vgem\{#ViGEmSetup}'), '/passive /norestart', '', SW_SHOW, ewWaitUntilTerminated, Code) or
     ((Code <> 0) and (Code <> 3010)) then
    MsgBox('The ViGEmBus driver (the virtual Xbox controller) did not install (code ' + IntToStr(Code) + '). ' +
           'BodyWalk''s Status tab offers it again.', mbError, MB_OK);
end;

// ---------------------------------------------------------------- ReShade

// The current ReShade with add-on support, as BodyWalk finds it
// (services\flat_vr\reshade_fetcher.cpp): its name on reshade.me's front page.
function ReShadeName: String;
var
  Page: String;
  Text: AnsiString;
  S: String;
  P, E: Integer;
begin
  Result := '';
  try
    DownloadTemporaryFile('https://reshade.me', 'reshade_home.html', '', nil);
  except
    Exit;
  end;
  Page := ExpandConstant('{tmp}\reshade_home.html');
  if not LoadStringFromFile(Page, Text) then Exit;
  S := String(Text);
  P := Pos('ReShade_Setup_', S);
  while P > 0 do
  begin
    E := Pos('_Addon.exe', Copy(S, P, 40));
    if E > 0 then
    begin
      Result := Copy(S, P, E - 1 + Length('_Addon.exe'));
      Exit;
    end;
    S := Copy(S, P + 1, Length(S));
    P := Pos('ReShade_Setup_', S);
  end;
end;

// ReShade in the mod's reshade folder (PlaceReShade): in mod_layout.inc, which installer\tests\mod_layout_test.iss runs for real.
// (It is called after the files are in, CurStepChanged; ReShadeSetup above is the host's variable it reads.)

// ---------------------------------------------------------------- d2r_vr.ini
// An update never wipes a tuned d2r_vr.ini (DecideIni on the Ready page, ApplyIni after the files are in): ini_choice.inc, which
// installer\tests\mod_layout_test.iss runs for real. Needs PlatformValue (above) and ModVersion.
#include "ini_choice.inc"

// ---------------------------------------------------------------- wizard

procedure InitializeWizard;
begin
  ModePage := CreateInputOptionPage(wpWelcome, 'How you play', 'On the monitor, or in a VR headset?',
    'VR needs a headset and BodyWalk with FlatVR: if you do not have BodyWalk, Setup puts in BodyWalk Portable ' +
    '(the free LITE version), and sets it up for the game. Flat, on the monitor, needs neither.', True, False);
  ModePage.Add('Flat: on the monitor, mouse and keyboard');
  ModePage.Add('VR: in the headset, with BodyWalk and FlatVR');
  ModePage.Values[1] := True;
  DownloadPage := CreateDownloadPage('Downloading', 'Setup is downloading what D2R VR needs.', nil);
  // An update offers the folder of the last install; if the game is no longer there (moved), the folder the game's
  // own installer names now (Steam, Battle.net) is offered instead.
  if not IsGameDir(WizardForm.DirEdit.Text) then
    WizardForm.DirEdit.Text := DefaultGameDir('');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir, Version, Name: String;
  Code: Integer;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    Dir := WizardDirValue;
    if not IsGameDir(Dir) then
    begin
      MsgBox('There is no D2R.exe in this folder. Pick the folder Diablo II: Resurrected is installed in.', mbError, MB_OK);
      Result := False;
      Exit;
    end;
    if not FileExists(Dir + '\D2RLoader.exe') then
    begin
      // Three buttons: Setup fetches it (only on this click), the site, or back.
      case TaskDialogMsgBox('D2RLoader is not installed in this game folder',
             'D2R VR runs only under D2RLoader 1.3.1. Setup can download it from d2rloader.net, check it against the ' +
             'SHA-256 the site publishes and unpack it into the game''s folder, beside D2R.exe.',
             mbConfirmation, MB_YESNOCANCEL, ['Download and install D2RLoader 1.3.1', 'Open d2rloader.net', 'Cancel'], 0) of
        IDYES:
          if InstallD2RLoader(Dir) then
            MsgBox('D2RLoader 1.3.1 is installed in the game''s folder. Setup goes on with D2R VR.', mbInformation, MB_OK)
          else
          begin
            Result := False;
            Exit;
          end;
        IDNO:
          begin
            ShellExec('open', '{#D2RLoaderSite}', '', '', SW_SHOWNORMAL, ewNoWait, Code);
            Result := False;
            Exit;
          end;
      else
        begin
          Result := False;
          Exit;
        end;
      end;
    end;
  end;

  if CurPageID = wpReady then
  begin
    Dir := WizardDirValue;
    DecideIni(Dir);
    ReShadeSetup := '';
    BodyWalkSetup := '';
    BodyWalkPortable := False;
    BodyWalkFresh := False;
    DownloadPage.Clear;
    // ReShade: no longer installed here - D2R VR Authentic Settings has the button (Home > Status >
    // Install ReShade), after D2RLoader: its headless setup quit with code 1 for a player
    // whose folder had no D2RLoader.exe yet (0.144, 2026-10-08).
    if IsVR then
    begin
      // none installed: FindBodyWalk says no and points at the portable copy. One older than
      // D2R VR needs stays as it is and the portable copy goes in too: an old FlatVR with the
      // add-on this Setup brings put a player's eyes out of line (1.76 with 0.153, 2026-10-10).
      if FindBodyWalk(Version) and not AtLeastMin(Version) then
      begin
        if Version = '' then Name := 'of an unknown version' else Name := Version;
        if BodyWalkSteam then Name := 'from Steam is ' + Name else Name := 'is ' + Name;
        MsgBox('Your BodyWalk ' + Name + ', older than D2R VR needs ({#MinBodyWalkMajor}.{#MinBodyWalkMinor}). ' +
               'Setup puts BodyWalk Portable {#MinBodyWalkMajor}.{#MinBodyWalkMinor} in D2R VR''s own folder (mods\D2R-VR\BodyWalkVR), and {#SettingsDisplay} starts that one. ' +
               'Your BodyWalk stays as it is.', mbInformation, MB_OK);
        BodyWalkPortable := True;
        BodyWalkDir := ExpandConstant('{#PortableDir}');
        BodyWalkFresh := not PortableThere;
      end;
    end;
    if (ReShadeSetup <> '') or (BodyWalkSetup <> '') then
    begin
      DownloadPage.Show;
      try
        try
          DownloadPage.Download;
        except
          if DownloadPage.AbortedByUser then
            Result := False
          else
          begin
            SuppressibleMsgBox('Download failed: ' + AddPeriod(GetExceptionMessage), mbCriticalError, MB_OK, IDOK);
            Result := False;
          end;
        end;
      finally
        DownloadPage.Hide;
      end;
    end;
  end;
end;

// [sky] arcane_sky: the Arcane Sanctuary's sky pictures out of the player's own game - D2R_VR_Settings reads
// the game's archives (read only) and writes reshade-shaders\Textures\D2R_Arcane into the mod's ReShade folder
// (mods\D2R-VR\reshade) and the level's visual files without the game's flat star cards into mods\D2R-VR\D2R-VR.mpq\data
// (after bearstonem/d2r-pcvr, whose installer makes them too). Once here, after ReShade, the mod's modinfo.json and the ini;
// the program makes them again at its start when they are missing or the game has changed. All of it is inside the mod, so
// the uninstaller's RemoveMod takes it with the rest. Whatever comes back, Setup goes on.
procedure PrepareAssets(Dir: String);
var
  Code: Integer;
begin
  WizardForm.StatusLabel.Caption := 'Making the Arcane Sanctuary sky from your game...';
  if Exec(Dir + '\D2R_VR_Settings.exe', '--prepare-assets', Dir, SW_HIDE, ewWaitUntilTerminated, Code) then
    Log('D2R_VR_Settings --prepare-assets: ' + IntToStr(Code) + ' (0 made, 1 could not, 2 no ReShade folder)')
  else
    Log('D2R_VR_Settings --prepare-assets did not start: ' + SysErrorMessage(Code));
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Dir, Version, Args: String;
  Code: Integer;
begin
  if CurStep = ssInstall then
  begin
    MoveEarlierInstall(ExpandConstant('{app}'));   // before the files go in: the player's settings into the mod first
    MoveLibsIntoMod(ExpandConstant('{app}'));      // ... and the libraries a 0.168 build of the mod left among its plugins, into d2r_vr_libs
    MovePortableIntoMod(ExpandConstant('{app}'));  // ... and BodyWalk Portable from beside D2R.exe
    Exit;
  end;
  if CurStep <> ssPostInstall then Exit;
  Dir := ExpandConstant('{app}');
  EnableGlobalExtensions(Dir);
  WriteModInfo(Dir);
  SetDefaultMod(Dir);
  PlaceReShade(Dir);
  ApplyIni(Dir);
  PrepareAssets(Dir);   // [sky] arcane_sky: the Arcane Sanctuary's pictures, made from the player's own game into the mod (after ApplyIni: the mod's ini is there)
  BundleLauncher;   // the mod is in: the launcher, when it is missing or older (never blocks, never asks; also in a silent run)
  if not IsVR then Exit;
  if UsePortable then
    InstallViGEm
  else if BodyWalkSetup <> '' then
  begin
    // an update of an installed BodyWalk keeps the player's own LITE/PRO choice
    Args := '/SILENT /SUPPRESSMSGBOXES /NORESTART';
    if not Exec(BodyWalkSetup, Args, '', SW_SHOW, ewWaitUntilTerminated, Code) or (Code <> 0) then
      MsgBox('BodyWalk''s installer did not finish (code ' + IntToStr(Code) + '). Install it from bodywalkvr.com, then run this Setup again.', mbError, MB_OK);
  end;
  if UsePortable or FindBodyWalk(Version) then
    SetBodyWalkSettings
  else
    MsgBox('BodyWalk was not found after installing. Install it from bodywalkvr.com and run this Setup again to set it up.', mbError, MB_OK);
end;

// ---------------------------------------------------------------- uninstall
//
// Everything the mod put in the game goes. ReShade only when this Setup
// installed it (d2r_vr_reshade_ours.txt): a ReShade the player had before stays.
// The game's own files, D2RLoader and the player's screenshots are never touched.
// Nor is another VR mod's folder (d2r-pcvr as mods\D2R-PCVR): only mods\D2R-VR goes (RemoveMod in mod_layout.inc).
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep <> usUninstall then Exit;
  // the mod's folder goes whole (its ReShade, ini, presets, logs, the settings, the launcher's manifest and icon) and the
  // default_mod it set; a ReShade of the player's own beside the game is not in it
  RemoveMod(ExpandConstant('{app}'));
end;
