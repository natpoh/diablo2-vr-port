; Runs installer\build_backup.inc as D2R VR Setup does before an update, on made-up game folders under BUILD_BACKUP_TEST_DIR, with a real
; D2R_VR_Settings.exe (BUILD_BACKUP_TEST_EXE: the build's own) carried as the dontcopy entry the real Setup has. Writes "ok ..." / "FAIL ..."
; lines and "PASS" / "FAILED n" at the end to <dir>\result.txt; never meant for players.
;   set BUILD_BACKUP_TEST_DIR=<scratch>  set BUILD_BACKUP_TEST_EXE=<build>\Release\D2R_VR_Settings.exe
;   ISCC installer\tests\build_backup_test.iss   <scratch>\out\build_backup_test_setup.exe /VERYSILENT /SUPPRESSMSGBOXES

#define Root GetEnv("BUILD_BACKUP_TEST_DIR")
#define SettingsExe GetEnv("BUILD_BACKUP_TEST_EXE")
#if Root == ""
  #error BUILD_BACKUP_TEST_DIR is not set
#endif
#if SettingsExe == ""
  #error BUILD_BACKUP_TEST_EXE is not set
#endif
#include "..\mod_names.inc"

[Setup]
AppId={{6A0E3C57-1B44-4F0E-9C2D-8E61B7A4D902}
AppName=D2R VR build backup test
AppVersion=1
DefaultDirName={#Root}\app
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
DisableWelcomePage=yes
CreateUninstallRegKey=no
Uninstallable=no
PrivilegesRequired=lowest
OutputDir={#Root}\out
OutputBaseFilename=build_backup_test_setup
Compression=none

[Files]
Source: "{#SettingsExe}"; DestName: "D2R_VR_Settings_backup.exe"; Flags: dontcopy

[Code]
#include "..\build_backup.inc"

var
  Lines: TArrayOfString;
  Fails: Integer;

procedure Expect(What, Got, Want: String);
var
  N: Integer;
begin
  N := GetArrayLength(Lines);
  SetArrayLength(Lines, N + 1);
  if Got = Want then Lines[N] := 'ok   ' + What
  else
  begin
    Lines[N] := 'FAIL ' + What + ': got "' + Got + '", want "' + Want + '"';
    Fails := Fails + 1;
  end;
end;

procedure Put(Path, Text: String);
begin
  ForceDirectories(ExtractFileDir(Path));
  SaveStringToFile(Path, Text, False);
end;

function B(V: Boolean): String;
begin
  if V then Result := 'yes' else Result := 'no';
end;

// The one folder under build_backups (its name), or '' when there is none, or '*' when there are several.
function OnlyBackup(Game: String): String;
var
  FR: TFindRec;
begin
  Result := '';
  if FindFirst(Game + '\{#ModDir}\build_backups\*', FR) then
  try
    repeat
      if ((FR.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0) and (FR.Name <> '.') and (FR.Name <> '..') then
        if Result = '' then Result := FR.Name else Result := '*';
    until not FindNext(FR);
  finally
    FindClose(FR);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Fresh, Old, Off, Name: String;
  N: Integer;
begin
  if CurStep <> ssPostInstall then Exit;
  // a first install: no build, nothing to back up
  Fresh := '{#Root}\fresh';
  Put(Fresh + '\D2R.exe', 'the game');
  Expect('no build: BuildThere', B(BuildThere(Fresh)), 'no');
  Expect('no build: exit code', IntToStr(BackupInstalledBuild(Fresh)), '2');
  Expect('no build: no backups folder', B(DirExists(Fresh + '\{#ModDir}\build_backups')), 'no');

  // an update over a build whose ini has no key: the box ticked, the backup made
  Old := '{#Root}\old';
  Put(Old + '\D2R.exe', 'the game');
  Put(Old + '\D2R_VR_Settings.exe', 'settings 0.219.2');
  Put(Old + '\{#ModDir}\d2rloader\plugins\d2rl-vrcam.dll', 'plugin 0.219.2');
  Put(Old + '\{#ModDir}\d2rloader\plugins\d2r_vr.ini', '[openxr]' + #13#10 + 'on=1' + #13#10);
  Put(Old + '\{#ModDir}\D2R-VR.mpq\modinfo.json', '{ }');
  Put(Old + '\{#ModDir}\d2rloader\logs\d2r-vr-vrcam.log', 'a log');
  Expect('update: BuildThere', B(BuildThere(Old)), 'yes');
  Expect('update, no key: ticked', B(BuildBackupDefault(Old)), 'yes');
  Expect('update: exit code', IntToStr(BackupInstalledBuild(Old)), '0');
  Name := OnlyBackup(Old);
  Expect('update: one backup, named _update', B((Length(Name) > 7) and (Copy(Name, Length(Name) - 6, 7) = '_update')), 'yes');
  Expect('update: its manifest', B(FileExists(Old + '\{#ModDir}\build_backups\' + Name + '\backup.txt')), 'yes');
  Expect('update: the ini in it', B(FileExists(Old + '\{#ModDir}\build_backups\' + Name + '\game\{#ModDir}\d2rloader\plugins\d2r_vr.ini')), 'yes');
  Expect('update: the settings program in it', B(FileExists(Old + '\{#ModDir}\build_backups\' + Name + '\game\D2R_VR_Settings.exe')), 'yes');
  Expect('update: no log in it', B(FileExists(Old + '\{#ModDir}\build_backups\' + Name + '\game\{#ModDir}\d2rloader\logs\d2r-vr-vrcam.log')), 'no');
  Expect('update: the program wrote its line', B(FileExists(Old + '\{#ModDir}\d2rloader\logs\d2r_vr_settings.log')), 'yes');
  Expect('update: the temporary program gone', B(FileExists(ExpandConstant('{tmp}\D2R_VR_Settings_backup.exe'))), 'no');

  // the player said no in Settings: the box starts unticked
  Off := '{#Root}\off';
  Put(Off + '\{#ModDir}\d2rloader\plugins\d2rl-vrcam.dll', 'plugin');
  Put(Off + '\{#ModDir}\d2rloader\plugins\d2r_vr.ini', '[support]' + #13#10 + 'build_backup_before_update=0' + #13#10);
  Expect('build_backup_before_update=0: unticked', B(BuildBackupDefault(Off)), 'no');
  Put(Off + '\{#ModDir}\d2rloader\plugins\d2r_vr.ini', '[support]' + #13#10 + 'build_backup_before_update=1' + #13#10);
  Expect('build_backup_before_update=1: ticked', B(BuildBackupDefault(Off)), 'yes');

  N := GetArrayLength(Lines);
  SetArrayLength(Lines, N + 1);
  if Fails = 0 then Lines[N] := 'PASS' else Lines[N] := 'FAILED ' + IntToStr(Fails);
  SaveStringsToFile('{#Root}\result.txt', Lines, False);
end;
