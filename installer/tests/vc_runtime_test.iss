; Runs installer\vc_runtime.inc's VcRuntimeMissing on fake System32 folders under VC_RUNTIME_TEST_DIR: all four DLLs, none, and an older 2015-2019
; runtime without msvcp140_atomic_wait.dll. Writes "PASS n" / "FAIL ..." lines to <dir>\result.txt; never meant for players.
;   set VC_RUNTIME_TEST_DIR=<scratch>   ISCC installer\tests\vc_runtime_test.iss   <scratch>\out\vc_runtime_test_setup.exe /VERYSILENT /SUPPRESSMSGBOXES

#define Root GetEnv("VC_RUNTIME_TEST_DIR")
#if Root == ""
  #error VC_RUNTIME_TEST_DIR is not set
#endif

[Setup]
AppId={{2F6C1E0B-93D4-4B7A-A1E5-7C0D5B8E9F31}
AppName=D2R VR vc runtime test
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
OutputBaseFilename=vc_runtime_test_setup
Compression=none

[Code]
#include "..\vc_runtime.inc"

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

procedure Touch(Dir, Name: String);
begin
  ForceDirectories(Dir);
  SaveStringToFile(AddBackslash(Dir) + Name, 'x', False);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  All, Old, None: String;
  N: Integer;
begin
  if CurStep <> ssPostInstall then Exit;
  All := '{#Root}\sys_all';
  Old := '{#Root}\sys_2019_old';
  None := '{#Root}\sys_none';
  Touch(All, 'vcruntime140.dll'); Touch(All, 'vcruntime140_1.dll'); Touch(All, 'msvcp140.dll'); Touch(All, 'msvcp140_atomic_wait.dll');
  Touch(Old, 'vcruntime140.dll'); Touch(Old, 'vcruntime140_1.dll'); Touch(Old, 'msvcp140.dll');
  ForceDirectories(None);
  Expect('all four there', VcRuntimeMissing(All), '');
  Expect('an older runtime without the atomic wait DLL', VcRuntimeMissing(Old), 'msvcp140_atomic_wait.dll');
  Expect('none there', VcRuntimeMissing(None), 'vcruntime140.dll, vcruntime140_1.dll, msvcp140.dll, msvcp140_atomic_wait.dll');
  Expect('a folder without a trailing backslash or with one', VcRuntimeMissing(All + '\'), '');
  N := GetArrayLength(Lines);
  SetArrayLength(Lines, N + 1);
  if Fails = 0 then Lines[N] := 'PASS ' + IntToStr(N) else Lines[N] := 'FAIL ' + IntToStr(Fails) + ' of ' + IntToStr(N);
  SaveStringsToFile('{#Root}\result.txt', Lines, False);
end;
