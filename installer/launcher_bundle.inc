// D2R VR Authentic's Setup carries the shared "D2R VR Launcher" and installs it silently when it is missing or older, then goes on with its own mod
// (the contract: the launcher repository's docs/INSTALLER_CONTRACT.md; ours: docs/launcher_authentic.md). Included in [Code] by D2R_VR_Setup.iss and by
// installer\tests\mod_layout_test.iss, which runs it for real with a stand-in for the carried Setup.
//
// The host defines, BEFORE this include:
//   LauncherSetupFile      the carried file, D2R_VR_Launcher_Setup_<version>.exe (the unmodified Setup the launcher's repository publishes)
//   LauncherSetupVersion   its version, x.y.z (both given by build_installer.ps1 -LauncherSetup ... or none: a build that carries no launcher)
//   LauncherRegKey         (optional) the HKCU key the launcher's Setup writes, default "Software\D2R VR Launcher"; a test names a throw-away key
// and in [Files] the line of launcher_bundle_files.inc (the carried file, dontcopy: it is unpacked on demand into {tmp}).
//
// What it does, in this order, after the mod's files are in (BundleLauncher):
//   1. Detect: HKCU\<key> values Version and InstallDir of the user who STARTED Setup (our Setup is elevated, but the launcher is per user: a
//      program started as the original user reads the original user's hive), and <InstallDir>\D2RVRLauncher.exe must exist.
//   2. Compare: dotted numbers, up to four parts, a missing part is 0 (0.2 = 0.2.0.0). Only a launcher that is missing, whose exe is missing or
//      whose version is older than the carried one gets the carried Setup; the same or a newer one is left alone (never a downgrade).
//   3. Install: the carried Setup as the original user: /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /TASKS=   (no desktop shortcut). Its exit code is
//      logged and nothing else; nothing here ever stops or fails the mod's install.
// The uninstaller does nothing about the launcher: it stays (the other mod, or the player, may use it).

#ifndef LauncherRegKey
  #define LauncherRegKey "Software\D2R VR Launcher"
#endif

var
  LauncherDecision: String;    // what was decided and why, for the log and for the tests: not carried / missing / exe missing / older / current / newer
  LauncherRan: Boolean;        // the carried Setup was started
  LauncherExitCode: Integer;   // ... and this was its exit code (-1: not started)

function LauncherCarriedVersion: String;
begin
#ifdef LauncherSetupVersion
  Result := '{#LauncherSetupVersion}';
#else
  Result := '';
#endif
end;

// The next dotted number off S ("2-beta.1" gives 2: the leading digits; nothing readable gives 0).
function LauncherVersionPart(var S: String): Integer;
var
  P, I: Integer;
  Part: String;
begin
  P := Pos('.', S);
  if P = 0 then
  begin
    Part := S;
    S := '';
  end
  else
  begin
    Part := Copy(S, 1, P - 1);
    S := Copy(S, P + 1, Length(S));
  end;
  Result := 0;
  for I := 1 to Length(Part) do
  begin
    if (Part[I] < '0') or (Part[I] > '9') or (I > 9) then Break;
    Result := Result * 10 + (Ord(Part[I]) - Ord('0'));
  end;
end;

// -1 when A is older than B, 0 when they are the same, 1 when A is newer. Up to four parts; a missing part is 0.
function LauncherCompareVersions(A, B: String): Integer;
var
  I, X, Y: Integer;
begin
  A := Trim(A);
  B := Trim(B);
  Result := 0;
  for I := 1 to 4 do
  begin
    X := LauncherVersionPart(A);
    Y := LauncherVersionPart(B);
    if X < Y then begin Result := -1; Exit; end;
    if X > Y then begin Result := 1; Exit; end;
  end;
end;

// Version and the exe of the launcher installed for the user who started Setup. A tiny batch file is run AS the original user (reg.exe, and
// "if exist" on the folder, in one console code page: a path with letters outside ASCII survives), and tells in ASCII: version=<x.y.z or empty>
// and exe=<1/0>. True when it could be asked. Where the original user's process cannot be started, the registry of Setup's own user is read
// (the same person unless Setup was started from another account).
function LauncherInstalledState(var Version: String; var ExeThere: Boolean): Boolean;
var
  Script, Answer, Line, Dir: String;
  Lines: TArrayOfString;
  Code, I: Integer;
begin
  Version := '';
  ExeThere := False;
  Result := False;
  Script := ExpandConstant('{tmp}\d2rvr_launcher_state.cmd');
  Answer := ExpandConstant('{tmp}\d2rvr_launcher_state.txt');
  DeleteFile(Answer);
  if SaveStringToFile(Script,
       '@echo off' + #13#10 +
       'set "V="' + #13#10 +
       'set "E=0"' + #13#10 +
       'for /f "tokens=2*" %%a in (''reg query "HKCU\{#LauncherRegKey}" /v Version 2^>nul ^| findstr /c:"REG_SZ"'') do set "V=%%b"' + #13#10 +
       'for /f "tokens=2*" %%a in (''reg query "HKCU\{#LauncherRegKey}" /v InstallDir 2^>nul ^| findstr /c:"REG_SZ"'') do if exist "%%b\D2RVRLauncher.exe" set "E=1"' + #13#10 +
       '> "%~1" echo version=%V%' + #13#10 +
       '>> "%~1" echo exe=%E%' + #13#10, False) and
     ExecAsOriginalUser(ExpandConstant('{cmd}'), '/S /C ""' + Script + '" "' + Answer + '""', '', SW_HIDE, ewWaitUntilTerminated, Code) and
     LoadStringsFromFile(Answer, Lines) then
  begin
    for I := 0 to GetArrayLength(Lines) - 1 do
    begin
      Line := Trim(Lines[I]);
      if Pos('version=', Line) = 1 then Version := Trim(Copy(Line, 9, Length(Line)));
      if Line = 'exe=1' then ExeThere := True;
    end;
    Result := True;
  end
  else
  begin
    Log('D2R VR: could not ask the original user about the launcher - reading the registry of the user Setup runs as');
    if RegQueryStringValue(HKCU, '{#LauncherRegKey}', 'Version', Version) then Version := Trim(Version) else Version := '';
    ExeThere := RegQueryStringValue(HKCU, '{#LauncherRegKey}', 'InstallDir', Dir) and FileExists(AddBackslash(Dir) + 'D2RVRLauncher.exe');
    Result := True;
  end;
  DeleteFile(Script);
  DeleteFile(Answer);
end;

// The carried Setup, unpacked into {tmp} and run as the original user, silently. Nothing here raises: whatever goes wrong is logged.
procedure InstallLauncherSilently;
var
  Setup: String;
  Code: Integer;
begin
  LauncherRan := False;
  LauncherExitCode := -1;
#ifdef LauncherSetupFile
  try
    ExtractTemporaryFile('{#LauncherSetupFile}');
    Setup := ExpandConstant('{tmp}\{#LauncherSetupFile}');
    Log('D2R VR: installing the D2R VR Launcher {#LauncherSetupVersion} (' + LauncherDecision + ') as the original user: ' + Setup + ' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /TASKS=');
    if ExecAsOriginalUser(Setup, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /TASKS=', '', SW_HIDE, ewWaitUntilTerminated, Code) then
    begin
      LauncherRan := True;
      LauncherExitCode := Code;
      Log('D2R VR: the launcher''s Setup ended with exit code ' + IntToStr(Code) + ' (the mod''s install goes on whatever it is)');
    end
    else
      Log('D2R VR: the launcher''s Setup could not be started (' + SysErrorMessage(Code) + ') - the mod is installed without it');
    DeleteFile(Setup);
  except
    Log('D2R VR: the launcher could not be installed: ' + GetExceptionMessage + ' - the mod is installed without it');
  end;
#endif
end;

// The whole step, after the mod's files are in. Never raises, never asks.
procedure BundleLauncher;
var
  Carried, Installed: String;
  ExeThere: Boolean;
begin
  LauncherRan := False;
  LauncherExitCode := -1;
  Carried := LauncherCarriedVersion;
  if Carried = '' then
  begin
    LauncherDecision := 'not carried';
    Log('D2R VR: this Setup carries no D2R VR Launcher');
    Exit;
  end;
  try
    LauncherInstalledState(Installed, ExeThere);
    if Installed = '' then LauncherDecision := 'missing'
    else if not ExeThere then LauncherDecision := 'exe missing'
    else if LauncherCompareVersions(Installed, Carried) < 0 then LauncherDecision := 'older'
    else if LauncherCompareVersions(Installed, Carried) = 0 then LauncherDecision := 'current'
    else LauncherDecision := 'newer';
    Log('D2R VR: D2R VR Launcher installed: "' + Installed + '"' + ' exe ' + IntToStr(Ord(ExeThere)) + ', carried ' + Carried + ' -> ' + LauncherDecision);
    if (LauncherDecision = 'current') or (LauncherDecision = 'newer') then Exit;
    InstallLauncherSilently;
  except
    Log('D2R VR: the launcher step failed: ' + GetExceptionMessage + ' - the mod is installed without it');
  end;
end;
