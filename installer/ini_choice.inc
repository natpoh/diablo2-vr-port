// D2R VR's d2r_vr.ini through an install, an update and a switch of the way to play. Included in [Code] by D2R_VR_Setup.iss and by
// installer\tests\mod_layout_test.iss. Needs mod_layout.inc (ModPlugins: the ini is the mod's), the host's PlatformValue(Param) and
// the ModVersion define; DecideIni runs on the Ready page, ApplyIni after the files are in.

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
  IniChanged: Boolean;   // the player's file differs from the defaults the last Setup put in (or they cannot be compared)
  IniOldDeathRagdoll: String;   // [weight] death_ragdoll in the defaults the last Setup put in ('<none>': not there / no defaults)

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

// Kept settings still at a default that has changed since: a feature that came off in an earlier version and is on now goes on when the
// player's value is that old default (he never chose it). Serg 2026-11-03: death_ragdoll shipped 0 in 0.196 / 0.197, 1 since 0.198.
procedure TurnOnOldDefaults(Ini: String);
begin
  if (IniOldDeathRagdoll = '0') and (IniValue(GetIniString('weight', 'death_ragdoll', '<none>', Ini)) = '0') then
    SetIniString('weight', 'death_ragdoll', '1', Ini);
end;

// Kept settings with an unfinished test still on from an older version: the raw tests that ship off are turned off once, at the first
// update that has this. Its mark, [support] raw_tests_off_0201, is one of the shipped defaults: a first install has it from the start, an
// update adds it with the other new keys right after this (missing = not done yet). A test the player ticks again after that stays on.
// Serg 2026-11-04: denails75r's crashes came with world_contact, world_contact_hands and own_body on from an old install.
procedure TurnOffRawTest(Ini, Sec, Key: String);
begin
  if IniValue(GetIniString(Sec, Key, '<none>', Ini)) = '1' then
  begin
    SetIniString(Sec, Key, '0', Ini);
    Log('d2r_vr.ini: [' + Sec + '] ' + Key + ' was on from an older version - a test that ships off: off now');
  end;
end;

procedure TurnOffRawTests(Ini: String);
begin
  if GetIniString('support', 'raw_tests_off_0201', '<none>', Ini) <> '<none>' then Exit;
  TurnOffRawTest(Ini, 'weight', 'world_contact');
  TurnOffRawTest(Ini, 'weight', 'world_contact_hands');
  TurnOffRawTest(Ini, 'weight', 'own_body');
end;

// Kept settings from the ReShade days: since 0.203 D2R VR uses no ReShade (the plugin's own hooks and effect runtime only). Serg 2026-11-05: "those
// who had ReShade switch to the new way by themselves". Once, at the first update that has this (its mark [support] no_reshade_0203, one of the
// shipped defaults, comes in with the other new keys right after this - missing = not done yet): [render] no_reshade=0 becomes 1 (the plugin
// ignores it now; an older plugin put back for co-op goes the native way too), and [render] native_effects=0 becomes 1 - with ReShade gone, 0
// would leave no sky, fog or ceiling at all. The player's own dxgi.dll ReShade is not touched. A native_effects=0 chosen after that stays.
procedure MoveOffReShadeKey(Ini, Sec, Key, Why: String);
begin
  if IniValue(GetIniString(Sec, Key, '<none>', Ini)) = '0' then
  begin
    SetIniString(Sec, Key, '1', Ini);
    Log('d2r_vr.ini: [' + Sec + '] ' + Key + '=0 from an older version - ' + Why + ': 1 now');
  end;
end;

procedure MoveOffReShade(Ini: String);
begin
  if GetIniString('support', 'no_reshade_0203', '<none>', Ini) <> '<none>' then Exit;
  MoveOffReShadeKey(Ini, 'render', 'no_reshade', 'D2R VR uses no ReShade since 0.203');
  MoveOffReShadeKey(Ini, 'render', 'native_effects', 'without ReShade the sky, the fog and the ceiling are D2R VR''s own effect');
end;

procedure DecideIni(Dir: String);
var
  Ini, Def, V: String;
  Secs, Keys, Vals: TStringList;
  I: Integer;
  Changed: Boolean;
begin
  IniTakeNew := False;
  IniChanged := False;
  IniOldDeathRagdoll := '<none>';
  Ini := ModPlugins(Dir) + 'd2r_vr.ini';
  Def := ModPlugins(Dir) + 'd2r_vr.default.ini';
  if not FileExists(Ini) then Exit;   // first install: the defaults go in as they are
  if FileExists(Def) then IniOldDeathRagdoll := IniValue(GetIniString('weight', 'death_ragdoll', '<none>', Def));   // (before the new defaults overwrite it)
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
  IniChanged := Changed;
  if not Changed then
    IniTakeNew := True
  else
    IniTakeNew := SuppressibleMsgBox('You have changed D2R VR''s settings (d2r_vr.ini).' + #13#10#13#10 +
      'D2R VR {#ModVersion} comes with new default settings that turn its features on (weapon weight, hit by touch, the bow drawn with both hands, ' +
      'the life and mana on your wrists, the belt potions, native OpenXR...).' + #13#10#13#10 +
      'Yes - keep your settings. Only the settings that are new in this version are added; the features stay as your file has them (off, if it came from an older version).' + #13#10 +
      'No - take this version''s default settings: the features come on, and your changes are replaced (your old file is kept as d2r_vr.ini.before-{#ModVersion}).',
      mbConfirmation, MB_YESNO, IDYES) = IDNO;
end;

procedure ApplyIni(Dir: String);
var
  Ini, Def: String;
  Secs, Keys, Vals: TStringList;
  I: Integer;
begin
  Ini := ModPlugins(Dir) + 'd2r_vr.ini';
  Def := ModPlugins(Dir) + 'd2r_vr.default.ini';
  if IniTakeNew then
  begin
    // taking the defaults replaces the player's changes: the file they chose to replace is kept beside it
    if IniChanged and FileExists(Ini) then FileCopy(Ini, Ini + '.before-{#ModVersion}', False);
    FileCopy(Def, Ini, False);
  end
  else
  begin
    TurnOffRawTests(Ini);   // (before the new keys go in: its own mark is one of them)
    MoveOffReShade(Ini);    // (the same: its mark [support] no_reshade_0203 is one of them)
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
    TurnOnOldDefaults(Ini);
  end;
  SetIniString('mode', 'platform', PlatformValue(''), Ini);
end;
