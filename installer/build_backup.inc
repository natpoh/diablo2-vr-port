// D2R VR Setup: a backup of the installed build before an update installs over it (D2R VR Settings > Home > Build backups restores it;
// shared/build_backup_rule.h says what is in it). Included in [Code] by D2R_VR_Setup.iss and by installer\tests\build_backup_test.iss.
// Needs ModDir (mod_names.inc) and, in [Files], this Setup's own settings program as a dontcopy entry named D2R_VR_Settings_backup.exe:
// the NEW program does the copying (D2R_VR_Settings.exe --backup-build <game> update), from {tmp} - the installed one may be too old to know it.
// [support] build_backup_before_update in the installed d2r_vr.ini: 1 (or no key) = the box starts ticked, 0 = unticked; [support]
// build_backup_keep: how many automatic backups the program keeps (it prunes, not Setup).

function BuildIniPath(GameDir: String): String;
begin
  Result := AddBackslash(GameDir) + '{#ModDir}\d2rloader\plugins\d2r_vr.ini';
end;

// A build of the mod is installed in this game folder (an update, not a first install).
function BuildThere(GameDir: String): Boolean;
begin
  Result := FileExists(AddBackslash(GameDir) + '{#ModDir}\d2rloader\plugins\d2rl-vrcam.dll');
end;

// The box's start: ticked unless the player's ini says build_backup_before_update=0.
function BuildBackupDefault(GameDir: String): Boolean;
begin
  Result := GetIniInt('support', 'build_backup_before_update', 1, 0, 1, BuildIniPath(GameDir)) <> 0;
end;

// The installed build backed up by this Setup's settings program. Its exit code: 0 made, 1 failed, 2 no build there, 3 not enough free
// space; -1 it did not start. Never raises.
function BackupInstalledBuild(GameDir: String): Integer;
var
  Exe: String;
  Code: Integer;
begin
  Result := -1;
  try
    ExtractTemporaryFile('D2R_VR_Settings_backup.exe');
    Exe := ExpandConstant('{tmp}\D2R_VR_Settings_backup.exe');
    if Exec(Exe, '--backup-build "' + RemoveBackslash(GameDir) + '" update', ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, Code) then
      Result := Code
    else
      Log('D2R VR: the build backup did not start: ' + SysErrorMessage(Code));
    DeleteFile(Exe);
  except
    Log('D2R VR: the build backup could not run: ' + GetExceptionMessage);
  end;
  Log('D2R VR: build backup before the update: exit code ' + IntToStr(Result) + ' (0 made, 1 failed, 2 no build there, 3 not enough space; ' +
      'the program''s line is in ' + AddBackslash(GameDir) + '{#ModDir}\d2rloader\logs\d2r_vr_settings.log)');
end;

function BuildBackupFailText(Code: Integer): String;
begin
  case Code of
    1: Result := 'a file could not be copied - the reason is in mods\D2R-VR\d2rloader\logs\d2r_vr_settings.log';
    3: Result := 'there is not enough free space on the game''s drive (about 250 MB is needed)';
  else
    Result := 'the settings program could not run (code ' + IntToStr(Code) + ')';
  end;
end;
