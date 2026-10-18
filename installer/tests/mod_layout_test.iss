; Runs installer\mod_layout.inc, installer\ini_choice.inc and the [Files] lines of installer\mod_files.inc for real - Setup and the uninstaller of a
; stand-in for D2R VR Authentic - in a scratch folder. Compiled and driven by installer\tests\mod_layout_test.py
; (MOD_LAYOUT_TEST_DIR = the scratch folder); never meant for players. What it leaves out of the real Setup: the wizard, ReShade's download,
; BodyWalk, the shortcuts. The [Code] hooks (CurStepChanged, CurUninstallStepChanged) call the real procedures in the real order.

#define Root GetEnv("MOD_LAYOUT_TEST_DIR")
#if Root == ""
  #error MOD_LAYOUT_TEST_DIR is not set
#endif
#define ModVersion "test"
#include "..\mod_names.inc"
#define PayloadDir Root + "\payload"
; the carried launcher Setup: a stand-in (tests\fake_launcher_setup.cpp), named like the real file, when the test says so; none otherwise
#define LSF GetEnv("MOD_LAYOUT_TEST_LAUNCHER_FILE")
#if LSF != ""
  #define LauncherSetupFile LSF
  #define LauncherSetupVersion GetEnv("MOD_LAYOUT_TEST_LAUNCHER_VERSION")
#endif
; ... and a throw-away registry key for what the launcher's Setup writes: never the real one (HKCU\Software\D2R VR Launcher)
#define LauncherRegKey GetEnv("MOD_LAYOUT_TEST_LAUNCHER_KEY")
#if (LauncherRegKey == "") || (LowerCase(LauncherRegKey) == "software\d2r vr launcher")
  #error MOD_LAYOUT_TEST_LAUNCHER_KEY must name a throw-away key, not the launcher's own
#endif

[Setup]
AppId={{7B3D9A10-4C2E-4E6B-8F51-0A9D3C6E2B71}
AppName=D2R VR mod-layout test
AppVersion=1
DefaultDirName={#Root}\game
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
DisableFinishedPage=yes
DisableWelcomePage=yes
CreateUninstallRegKey=no
PrivilegesRequired=lowest
OutputDir={#Root}\out
OutputBaseFilename=mod_layout_test_setup
Compression=none

[Files]
; D2R VR Settings beside D2R.exe, as the real Setup has it
Source: "{#PayloadDir}\plugins\D2R_VR_Settings.exe"; DestDir: "{app}"; Flags: ignoreversion
#include "..\mod_files.inc"
#include "..\launcher_bundle_files.inc"

; the real [InstallDelete] / [UninstallDelete] lines of the mod (they carry their own section headers)
#include "..\mod_delete.inc"

; the settings program's shortcuts, their earlier names and the desktop task - the real lines, in folders of the scratch folder (never the real Start menu or desktop):
; the stand-in installs with the user's rights, so {autoprograms} / {autodesktop} are the user's places
#define ShellPrograms Root + "\shell\start_user"
#define ShellDesktop Root + "\shell\desktop_user"
#define OldProgramsAll Root + "\shell\start_all"
#define OldProgramsUser Root + "\shell\start_user"
#define OldDesktopAll Root + "\shell\desktop_all"
#define OldDesktopUser Root + "\shell\desktop_user"
#include "..\mod_shortcuts.inc"

[Code]
var
  ReShadeSetup: String;      // (as in the real Setup: never set)

// VR or flat: the test says (the real Setup asks on its first page)
function IsVR: Boolean;
begin
  Result := GetEnv('MOD_LAYOUT_TEST_VR') <> '0';
end;

function PlatformValue(Param: String): String;
begin
  if IsVR then Result := '1' else Result := '0';
end;

#include "..\mod_layout.inc"
#include "..\launcher_bundle.inc"
#include "..\ini_choice.inc"

function InitializeSetup: Boolean;
begin
  Result := True;
  // the Ready page's work (the question of a changed ini is answered by SuppressibleMsgBox's default in a silent run)
  DecideIni(ExpandConstant('{#Root}\game'));
  SaveStringToFile(ExpandConstant('{#Root}\ready.txt'), 'take_new=' + IntToStr(Ord(IniTakeNew)) + ' changed=' + IntToStr(Ord(IniChanged)) + #13#10, False);
  // what the finish page's "Open D2R VR Launcher" would find (read-only: the registry value the launcher's Setup writes, or its default folder)
  SaveStringToFile(ExpandConstant('{#Root}\launcher.txt'), 'exe=' + LauncherExePath('') + ' installed=' + IntToStr(Ord(LauncherInstalled)) + #13#10, False);
end;

// the real CurStepChanged, to the end of the mod's part
procedure CurStepChanged(CurStep: TSetupStep);
var
  Dir: String;
begin
  if CurStep = ssInstall then
  begin
    MoveEarlierInstall(ExpandConstant('{app}'));   // before the files go in: the player's settings into the mod first
    MoveLibsIntoMod(ExpandConstant('{app}'));      // ... and the libraries a 0.168 build of the mod left among its plugins, into d2r_vr_libs
    MovePortableIntoMod(ExpandConstant('{app}'));  // ... and BodyWalk Portable from beside D2R.exe
    // (what the move itself did to the player's ini, before ApplyIni touches it)
    if FileExists(ModPlugins(ExpandConstant('{app}')) + 'd2r_vr.ini') then
      SaveStringToFile(ExpandConstant('{#Root}\moved.txt'), 'ini_sha=' + GetSHA256OfFile(ModPlugins(ExpandConstant('{app}')) + 'd2r_vr.ini') + #13#10, False);
    Exit;
  end;
  if CurStep <> ssPostInstall then Exit;
  Dir := ExpandConstant('{app}');
  EnableGlobalExtensions(Dir);
  WriteModInfo(Dir);
  SetDefaultMod(Dir);
  PlaceReShade(Dir);
  ApplyIni(Dir);
  // the launcher, as the real Setup does it right after the mod is in. The stand-in is told by a file beside it (in {tmp}) what to leave behind, as
  // the real launcher's Setup leaves its registry values and its exe; the test reads back what was decided and how the stand-in was started.
  if GetEnv('MOD_LAYOUT_TEST_LAUNCHER_SIM') = '1' then
    SaveStringToFile(ExpandConstant('{tmp}\launcher_standin.cfg'), 'key={#LauncherRegKey}' + #13#10 + 'version=' + LauncherCarriedVersion + #13#10 +
                     'dir=' + GetEnv('MOD_LAYOUT_TEST_LAUNCHER_DIR') + #13#10, False);
  BundleLauncher;
  SaveStringToFile(ExpandConstant('{#Root}\launcher_result.txt'), 'decision=' + LauncherDecision + ' ran=' + IntToStr(Ord(LauncherRan)) + ' exit=' + IntToStr(LauncherExitCode) + #13#10, False);
  if FileExists(ExpandConstant('{tmp}\launcher_standin_record.txt')) then FileCopy(ExpandConstant('{tmp}\launcher_standin_record.txt'), ExpandConstant('{#Root}\launcher_record.txt'), False);
end;

// the real CurUninstallStepChanged
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep <> usUninstall then Exit;
  RemoveMod(ExpandConstant('{app}'));
end;
