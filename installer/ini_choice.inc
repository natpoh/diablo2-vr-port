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
  IniChanged := False;
  Ini := ModPlugins(Dir) + 'd2r_vr.ini';
  Def := ModPlugins(Dir) + 'd2r_vr.default.ini';
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
