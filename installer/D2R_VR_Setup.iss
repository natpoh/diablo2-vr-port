; D2R VR - installer for the mod (Inno Setup 6). Built by build_installer.ps1,
; which stages installer\payload and passes /DModVersion.
;
; What it does:
;   - finds Diablo II: Resurrected (Steam, Battle.net or the folder you pick);
;   - checks D2RLoader is there (the mod runs under it; we do not ship it) and
;     switches on its allow_global_extensions;
;   - puts the mod in <game>\d2rloader\plugins, the fog/sky shader and our sky
;     pictures in <game>\reshade-shaders;
;   - ReShade: downloads the current ReShade with add-on support from reshade.me,
;     installs it headless, then renames its dxgi.dll to ReShade64.dll - the
;     plain D2R.exe refuses an unsigned dxgi.dll, vrcam loads ReShade64.dll
;     itself under D2RLoader (vr\vrcam.cpp, LoadReShade);
;   - VR only: BodyWalk. None installed: the BodyWalk Portable this Setup
;     carries (vjoy installer\make_bodywalk_portable.ps1; LITE on its first
;     start) goes into <game>\BodyWalkVR, with ViGEmBus and a
;     Start menu shortcut; running this Setup again refreshes that copy.
;     Installed: used as it is - at least 1.74 is wanted, an older standalone
;     one is updated on a yes. Then
;     the D2R Bridge plugin goes into BodyWalk's plugins and BodyWalk's
;     settings are set for the mod (D2R_VR_Settings.exe --setup-bodywalk), and
;     the FlatVR depth addon goes beside the game;
;   - writes the chosen way to play into d2r_vr.ini ([mode] platform).

#ifndef ModVersion
  #define ModVersion "0.0"
#endif
#define MinBodyWalkMajor 1
#define MinBodyWalkMinor 74
#define BodyWalkDownload "https://bodywalkvr.com/api/download/latest?product=bodywalk"
#define D2RLoaderSite "https://d2rloader.net"
; The one D2RLoader the mod is made for, fetched only when the player asks (InstallD2RLoader);
; the SHA-256 is the one d2rloader.net publishes on its download page.
#define D2RLoaderZipUrl "https://d2rloader.net/downloads/D2RLoader-1.3.1-beta.zip"
#define D2RLoaderZip "D2RLoader-1.3.1-beta.zip"
#define D2RLoaderSha256 "9286c6b5bff7f1043658411dbea305faa9698b6a455e6441668fc6d02c22aad7"
; Per user and writable: BodyWalk writes config.json, imgui.ini and plugins\ beside its exe.
; BodyWalk Portable is part of the mod, in the game's folder. It writes nothing
; beside itself (1.74 on): settings, configs and plugins go to %LOCALAPPDATA%\BodyWalkVR.
#define PortableDir "{app}\BodyWalkVR"
#define ViGEmSetup "ViGEmBus_1.22.0_x64_x86_arm64.exe"

[Setup]
AppId={{8C1F3A52-6D0B-4E47-9B7E-2D5A0C9E4F31}
AppName=D2R VR
AppVersion={#ModVersion}
AppPublisher=BodyWalkVR
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
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=Output
OutputBaseFilename=D2R_VR_Setup_v{#ModVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=D2R VR {#ModVersion}

[Messages]
SelectDirLabel3=Setup will install D2R VR into the folder of Diablo II: Resurrected - the one with D2R.exe and D2RLoader.exe.
SelectDirBrowseLabel=If this is not your game's folder, click Browse.

[Files]
Source: "payload\plugins\d2rl-vrcam.dll"; DestDir: "{app}\d2rloader\plugins"; Flags: ignoreversion
Source: "payload\plugins\D2R_VR_Settings.exe"; DestDir: "{app}"; Flags: ignoreversion
; MIT, with the third-party notices MinHook's and ReShade's licences ask for in a binary
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "D2R_VR_LICENSE.txt"; Flags: ignoreversion
; The player's own settings stay through an update (IniChoice in [Code]); the
; shipped defaults go beside them as d2r_vr.default.ini, which tells the next
; update whether the player changed anything.
Source: "payload\plugins\d2r_vr.ini"; DestDir: "{app}\d2rloader\plugins"; Flags: onlyifdoesntexist
Source: "payload\plugins\d2r_vr.ini"; DestDir: "{app}\d2rloader\plugins"; DestName: "d2r_vr.default.ini"; Flags: ignoreversion
Source: "payload\shaders\D2R_DepthFog.fx"; DestDir: "{app}\reshade-shaders\Shaders"; Flags: ignoreversion
Source: "payload\sky\*.png"; DestDir: "{app}\reshade-shaders\Textures\D2R_Sky_ours"; Flags: ignoreversion
; VR: the headset side.
Source: "payload\game\FlatVR_DepthProvider.addon64"; DestDir: "{app}"; Flags: ignoreversion; Check: IsVR
Source: "payload\game\FlatVR_Keepalive.addonfx"; DestDir: "{app}"; Flags: ignoreversion; Check: IsVR
Source: "payload\bodywalk\d2r_bridge.dll"; DestDir: "{localappdata}\BodyWalkVR\plugins\d2r_bridge"; Flags: ignoreversion; Check: IsVR
; The mod's BodyWalk profile ("D2VR (mod)"): BodyWalk applies it on its own terms
; (a changed profile is never overwritten without asking - gui_tab_profiles_local.cpp).
Source: "payload\bodywalk\profile.json"; DestDir: "{localappdata}\BodyWalkVR\plugins\d2r_bridge"; Flags: ignoreversion; Check: IsVR
; BodyWalk Portable, only when no BodyWalk is installed. LITE is forced on its
; first start only: refreshing the copy keeps the player's own choice.
Source: "payload\bodywalk_portable\*"; DestDir: "{#PortableDir}"; Excludes: "force_lite_mode.txt,imgui.ini"; Flags: ignoreversion recursesubdirs createallsubdirs; Check: UsePortable
Source: "payload\bodywalk_portable\imgui.ini"; DestDir: "{#PortableDir}"; Flags: onlyifdoesntexist; Check: UsePortable
Source: "payload\bodywalk_portable\force_lite_mode.txt"; DestDir: "{#PortableDir}"; Flags: ignoreversion; Check: PortableFresh

[UninstallDelete]
; the portable BodyWalk goes with the mod; its settings in %LOCALAPPDATA%\BodyWalkVR
; are shared with any BodyWalk and stay
Type: filesandordirs; Name: "{#PortableDir}"; Check: UsePortable
; the mod's own files in the game that it wrote while running: its settings
; (d2r_vr.ini, D2R_VR_Settings' window file), logs and dumps; FlatVR's add-on log
Type: files; Name: "{app}\d2rloader\plugins\d2r_vr*"
Type: files; Name: "{app}\FlatVR_DepthProvider.log"

[InstallDelete]
; the settings program lived beside the mod until 2026-10-05; it sits next to D2R.exe now
Type: files; Name: "{app}\d2rloader\plugins\d2r_vr_settings.exe"
; the lowercase name before 2026-10-06: Windows keeps an existing file's case, so
; it goes first and D2R_VR_Settings.exe comes in under its own name
Type: files; Name: "{app}\d2r_vr_settings.exe"

; A shortcut on the desktop too, so nobody hunts for the game's folder (on by default).
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut to D2R VR Settings"; GroupDescription: "Shortcuts:"

[Icons]
Name: "{autoprograms}\D2R VR Settings"; Filename: "{app}\D2R_VR_Settings.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\D2R VR"; Filename: "{app}\D2R_VR_Settings.exe"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{autoprograms}\BodyWalk VR"; Filename: "{#PortableDir}\BodyWalkVR.exe"; WorkingDir: "{#PortableDir}"; Check: UsePortable


[Run]
; shellexec: started the way Explorer starts it - a player got "CreateProcess failed; code 740,
; the requested operation requires elevation" from the plain CreateProcess (0.147, 2026-10-08)
Filename: "{app}\D2R_VR_Settings.exe"; WorkingDir: "{app}"; Description: "Open D2R VR Settings"; Flags: postinstall nowait skipifsilent shellexec

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

// allow_global_extensions = true in <game>\d2rloader\config\d2rloader.toml:
// D2RLoader loads plugins from <game>\d2rloader\plugins only with it.
procedure EnableGlobalExtensions(GameDir: String);
var
  Path: String;
  Text: AnsiString;
  S: String;
begin
  Path := GameDir + '\d2rloader\config\d2rloader.toml';
  if not LoadStringFromFile(Path, Text) then Exit;
  S := String(Text);
  if Pos('allow_global_extensions = false', S) > 0 then
  begin
    StringChangeEx(S, 'allow_global_extensions = false', 'allow_global_extensions = true', True);
    SaveStringToFile(Path, AnsiString(S), False);
  end;
end;

// D2RLoader 1.3.1 into the game's folder, on the player's click: downloaded from
// d2rloader.net (the download page checks it against the published SHA-256),
// unpacked by PowerShell to a folder of its own, and the contents of the folder
// D2RLoader.exe is in copied beside D2R.exe. A player found the download refused
// by Edge and Chrome (2026-10-08). True when D2RLoader.exe is there afterwards.
function InstallD2RLoader(GameDir: String): Boolean;
var
  Zip, Unpack, Script, Ps: String;
  Code: Integer;
begin
  Result := False;
  DownloadPage.Clear;
  DownloadPage.Add('{#D2RLoaderZipUrl}', '{#D2RLoaderZip}', '{#D2RLoaderSha256}');
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
    BodyWalkFresh := not FileExists(BodyWalkDir + '\BodyWalkVR.exe');
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
    if MsgBox('BodyWalk is running. Close it so Setup can set it up for D2R VR (Universal tracking output, FlatVR, the D2R Bridge plugin), then click OK.' + #13#10#13#10 +
              'Cancel skips this: you can set it by hand later.', mbInformation, MB_OKCANCEL) = IDCANCEL then
      Exit;
  Args := '--setup-bodywalk' +
          ' "' + ExpandConstant('{localappdata}\BodyWalkVR\usersettings.json') + '"' +
          ' "' + ExpandConstant('{localappdata}\BodyWalkVR\usersettings_steam.json') + '"' +
          ' "' + BodyWalkDir + '\settings.json"' +
          ' "' + BodyWalkDir + '\steam_default_settings.json"';
  if not Exec(ExpandConstant('{app}\D2R_VR_Settings.exe'), Args, '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
    MsgBox('BodyWalk''s settings could not all be set (code ' + IntToStr(Code) + '). In BodyWalk switch on Universal tracking output ' +
           '(Startup tab) and FlatVR, and make sure the D2R Bridge plugin is on.', mbError, MB_OK);
end;

// ViGEmBus: BodyWalk's virtual Xbox pad, which the D2R Bridge drives. The
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

// ReShade as the mod wants it: ReShade64.dll, no dxgi.dll left beside the game.
procedure PlaceReShade(GameDir: String);
var
  Code: Integer;
  Raw: AnsiString;
  Ini: String;
  Target: String;
begin
  if (ReShadeSetup <> '') and not FileExists(GameDir + '\ReShade64.dll') then
  begin
    // ReShade's setup needs an exe that is there: without D2RLoader yet (not installed,
    // or skipped on the folder page) it quit with code 1 (a player, 0.144, 2026-10-08).
    // D2R.exe beside it gives the same dxgi.dll in the same folder.
    Target := GameDir + '\D2RLoader.exe';
    if not FileExists(Target) then Target := GameDir + '\D2R.exe';
    if not Exec(ReShadeSetup, '"' + Target + '" --api dxgi --headless', '', SW_SHOW, ewWaitUntilTerminated, Code) or (Code <> 0) then
      MsgBox('ReShade''s installer did not finish (code ' + IntToStr(Code) + '). Install ReShade with add-on support for D2RLoader.exe ' +
             '(DirectX 10/11/12) from reshade.me, then rename dxgi.dll in the game''s folder to ReShade64.dll.', mbError, MB_OK)
    else
      // ours: the uninstaller takes it away again (CurUninstallStepChanged)
      SaveStringToFile(GameDir + '\d2rloader\plugins\d2r_vr_reshade_ours.txt', 'ReShade was installed by D2R VR Setup.', False);
  end;
  // ReShade's headless setup writes its search paths as '...\Shaders\**\**', which
  // ReShade 6.8 itself cannot resolve (error 123 in ReShade.log): no effect loads,
  // so no fog and no sky. '...\**' is what it reads (2026-10-06).
  if LoadStringFromFile(GameDir + '\ReShade.ini', Raw) then
  begin
    Ini := String(Raw);
    if StringChangeEx(Ini, '\**\**', '\**', True) > 0 then
      SaveStringToFile(GameDir + '\ReShade.ini', AnsiString(Ini), False);
  end;
  if FileExists(GameDir + '\dxgi.dll') then
  begin
    if FileExists(GameDir + '\ReShade64.dll') then
      DeleteFile(GameDir + '\dxgi.dll')   // a ReShade the old way beside the one vrcam loads: one is enough
    else if not RenameFile(GameDir + '\dxgi.dll', GameDir + '\ReShade64.dll') then
      MsgBox('Could not rename dxgi.dll to ReShade64.dll in the game''s folder - do it by hand, or D2R.exe will not start.', mbError, MB_OK);
  end;
end;

// ---------------------------------------------------------------- d2r_vr.ini
//
// An update never wipes a tuned d2r_vr.ini. The defaults the last Setup put in
// (d2r_vr.default.ini) say whether the player changed anything: unchanged -> the
// new defaults go in silently; changed, or no defaults to compare with (before
// 0.138) -> asked: keep (the keys this version added go in with their defaults;
// vrcam reads a missing key as its default anyway) or take the new defaults.
// [mode] platform is the wizard's own choice and never counts as a change.

var
  IniTakeNew: Boolean;   // decided on the Ready page, done after the files are in

// A value as vrcam reads it: up to an inline ';' comment, trimmed.
function IniValue(S: String): String;
var
  P: Integer;
begin
  P := Pos(';', S);
  if P > 0 then S := Copy(S, 1, P - 1);
  Result := Trim(S);
end;

// The keys of an ini file in three parallel lists: section, key, value.
function ReadIniKeys(FileName: String; Secs, Keys, Vals: TStringList): Boolean;
var
  Lines: TArrayOfString;
  I, P: Integer;
  S, Sec: String;
begin
  Result := LoadStringsFromFile(FileName, Lines);
  if not Result then Exit;
  Sec := '';
  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    S := Trim(Lines[I]);
    if (S = '') or (S[1] = ';') or (S[1] = '#') then Continue;
    if (S[1] = '[') and (Pos(']', S) > 0) then
    begin
      Sec := Trim(Copy(S, 2, Pos(']', S) - 2));
      Continue;
    end;
    P := Pos('=', S);
    if P < 2 then Continue;
    Secs.Add(Sec);
    Keys.Add(Trim(Copy(S, 1, P - 1)));
    Vals.Add(IniValue(Copy(S, P + 1, Length(S))));
  end;
end;

procedure DecideIni(Dir: String);
var
  Ini, Def, V: String;
  Secs, Keys, Vals: TStringList;
  I: Integer;
  Changed: Boolean;
begin
  IniTakeNew := False;
  Ini := Dir + '\d2rloader\plugins\d2r_vr.ini';
  Def := Dir + '\d2rloader\plugins\d2r_vr.default.ini';
  if not FileExists(Ini) then Exit;   // first install: the defaults go in as they are
  Changed := True;                     // no defaults from an earlier Setup: cannot tell
  if FileExists(Def) then
  begin
    Secs := TStringList.Create;
    Keys := TStringList.Create;
    Vals := TStringList.Create;
    try
      if ReadIniKeys(Def, Secs, Keys, Vals) then
      begin
        Changed := False;
        for I := 0 to Secs.Count - 1 do
        begin
          if (CompareText(Secs[I], 'mode') = 0) and (CompareText(Keys[I], 'platform') = 0) then Continue;
          V := GetIniString(Secs[I], Keys[I], '<none>', Ini);
          if (V <> '<none>') and (IniValue(V) <> Vals[I]) then
          begin
            Changed := True;
            Break;
          end;
        end;
      end;
    finally
      Secs.Free;
      Keys.Free;
      Vals.Free;
    end;
  end;
  if not Changed then
    IniTakeNew := True
  else
    IniTakeNew := SuppressibleMsgBox('You have changed D2R VR''s settings (d2r_vr.ini).' + #13#10#13#10 +
      'Yes - keep your settings. What this version adds comes in with its defaults.' + #13#10 +
      'No - take this version''s default settings.', mbConfirmation, MB_YESNO, IDYES) = IDNO;
end;

procedure ApplyIni(Dir: String);
var
  Ini, Def: String;
  Secs, Keys, Vals: TStringList;
  I: Integer;
begin
  Ini := Dir + '\d2rloader\plugins\d2r_vr.ini';
  Def := Dir + '\d2rloader\plugins\d2r_vr.default.ini';
  if IniTakeNew then
    FileCopy(Def, Ini, False)
  else
  begin
    Secs := TStringList.Create;
    Keys := TStringList.Create;
    Vals := TStringList.Create;
    try
      if ReadIniKeys(Def, Secs, Keys, Vals) then
        for I := 0 to Secs.Count - 1 do
          if GetIniString(Secs[I], Keys[I], '<none>', Ini) = '<none>' then
            SetIniString(Secs[I], Keys[I], Vals[I], Ini);
    finally
      Secs.Free;
      Keys.Free;
      Vals.Free;
    end;
  end;
  SetIniString('mode', 'platform', PlatformValue(''), Ini);
end;

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
    // ReShade: no longer installed here - D2R VR Settings has the button (Home > Status >
    // Install ReShade), after D2RLoader: its headless setup quit with code 1 for a player
    // whose folder had no D2RLoader.exe yet (0.144, 2026-10-08).
    if IsVR then
    begin
      // none installed: FindBodyWalk says no and points at the portable copy
      if FindBodyWalk(Version) and not AtLeastMin(Version) then
      begin
        if BodyWalkSteam then
          MsgBox('Your BodyWalk (Steam) is older than {#MinBodyWalkMajor}.{#MinBodyWalkMinor} or does not say its version. ' +
                 'Let Steam update it before playing; Setup goes on.', mbInformation, MB_OK)
        else if MsgBox('Your BodyWalk is ' + Version + '; D2R VR needs {#MinBodyWalkMajor}.{#MinBodyWalkMinor} or newer. Download and install the update now?',
                       mbConfirmation, MB_YESNO) = IDYES then
        begin
          DownloadPage.Add('{#BodyWalkDownload}', 'BodyWalkVR_Setup.exe', '');
          BodyWalkSetup := ExpandConstant('{tmp}\BodyWalkVR_Setup.exe');
        end;
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

procedure CurStepChanged(CurStep: TSetupStep);
var
  Dir, Version, Args: String;
  Code: Integer;
begin
  if CurStep <> ssPostInstall then Exit;
  Dir := ExpandConstant('{app}');
  EnableGlobalExtensions(Dir);
  PlaceReShade(Dir);
  ApplyIni(Dir);
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
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Dir: String;
begin
  if CurUninstallStep <> usUninstall then Exit;
  Dir := ExpandConstant('{app}');
  if FileExists(Dir + '\d2rloader\plugins\d2r_vr_reshade_ours.txt') then
  begin
    DeleteFile(Dir + '\ReShade64.dll');
    DeleteFile(Dir + '\ReShade.ini');
    DeleteFile(Dir + '\ReShadePreset.ini');
    DeleteFile(Dir + '\ReShade.log');
    DelTree(Dir + '\reshade-shaders', True, True, True);
  end;
end;
