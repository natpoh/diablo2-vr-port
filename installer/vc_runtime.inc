// The Microsoft Visual C++ 2015-2022 runtime (x64) that D2RLoader needs (docs/plan_no_reshade.md, 2026-10-26): D2RLoader.exe and D2RCore.dll
// import MSVCP140, VCRUNTIME140(_1) and MSVCP140_ATOMIC_WAIT; without them D2RLoader says only "could not start its loader. Reinstall the
// complete D2RLoader package". Included by D2R_VR_Setup.iss; installer\tests\vc_runtime_test.iss runs VcRuntimeMissing on fake folders.

// The runtime's DLLs missing from SysDir (the 64-bit System32), comma-separated; '' when all are there. msvcp140_atomic_wait.dll came with
// the 2019 16.10+ runtime: an older 2015-2019 install has the rest and still fails.
function VcRuntimeMissing(SysDir: String): String;
var
  Names: array[0..3] of String;
  I: Integer;
begin
  Names[0] := 'vcruntime140.dll';
  Names[1] := 'vcruntime140_1.dll';
  Names[2] := 'msvcp140.dll';
  Names[3] := 'msvcp140_atomic_wait.dll';
  Result := '';
  for I := 0 to 3 do
    if not FileExists(AddBackslash(SysDir) + Names[I]) then
    begin
      if Result <> '' then Result := Result + ', ';
      Result := Result + Names[I];
    end;
end;
