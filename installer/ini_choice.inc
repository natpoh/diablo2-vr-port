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
  IniWasThere: Boolean;  // a d2r_vr.ini was there before this Setup (an update): the one-time moves of the author's settings only then

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

// Kept settings from 0.203.2: quick blow is the default in 0.204. Serg 2026-11-05: "в принципе играется нормально с таким ударом, это хорошая
// замена директ хит давай поставим его галочкой ... по умолчанию"; Direct hit stays as the player has it ("директ хит должен по прежнему быть
// включен для двуручного меча и топоров": with quick blow on, By the rules is only a two-handed swing's further monsters). Once, at the first
// update that has this (its mark [support] quick_blow_0204, one of the shipped defaults, comes in with the other new keys right after this -
// missing = not done yet): [gameplay] quick_blow=0 (0.203.2's test default) becomes 1 (a missing key comes in as 1 anyway). A choice made after that stays.
procedure MoveToQuickBlow(Ini: String);
begin
  if GetIniString('support', 'quick_blow_0204', '<none>', Ini) <> '<none>' then Exit;
  if IniValue(GetIniString('gameplay', 'quick_blow', '<none>', Ini)) = '0' then
  begin
    SetIniString('gameplay', 'quick_blow', '1', Ini);
    Log('d2r_vr.ini: [gameplay] quick_blow=0 from an older version - on by default since 0.204: 1 now');
  end;
end;

// Kept settings from 0.203 - 0.205: the headset session ended at quit ([openxr] end_session_on_exit) shipped 0 there and 1 since 0.206 - the
// Virtual Desktop exit-crash fix acts only with it (Serg 2026-11-06: "вылет на вд тоже включи пусть люди тестят"). A kept ini keeps its 0, so
// 0.206 never reached the players who had it: crash batch 2026-11-07, rows 280 / 281 on 0.206.0 (and 282 / 284 on 0.205), one VD player, the
// same exit crash each time after Exit, end_session_on_exit=0 in all four. Once, at the first update that has this (its mark [support]
// exit_end_0206, one of the shipped defaults, comes in with the other new keys right after this - missing = not done yet): 0 becomes 1. A 0
// chosen after that stays.
procedure MoveToExitEnd(Ini: String);
begin
  if GetIniString('support', 'exit_end_0206', '<none>', Ini) <> '<none>' then Exit;
  if IniValue(GetIniString('openxr', 'end_session_on_exit', '<none>', Ini)) = '0' then
  begin
    SetIniString('openxr', 'end_session_on_exit', '1', Ini);
    Log('d2r_vr.ini: [openxr] end_session_on_exit=0 from an older version - on by default since 0.206 (the Virtual Desktop exit crash): 1 now');
  end;
end;

// Kept settings from 0.208 / 0.209.0: the monitor's one eye ([render] monitor_eye) and the hands over the Gestures tab ([openxr]
// hands_over_panels) are in every quality preset since 0.209.1 (Serg 2026-11-09: "на графике нормал по умолчанию должно быть включено
// monitor_eye=1 а так же при записи движений чтобы слой рук был поверх меню на всех режимах"); 0.208 / 0.209.0 shipped hands_over_panels=0. Once,
// at the first update that has this (its mark [support] monitor_hands_0209, one of the shipped defaults, comes in with the other new keys right
// after this - missing = not done yet): either at 0 becomes 1. A 0 chosen after that stays.
procedure MoveToMonitorEyeHands(Ini: String);
begin
  if GetIniString('support', 'monitor_hands_0209', '<none>', Ini) <> '<none>' then Exit;
  if IniValue(GetIniString('render', 'monitor_eye', '<none>', Ini)) = '0' then
  begin
    SetIniString('render', 'monitor_eye', '1', Ini);
    Log('d2r_vr.ini: [render] monitor_eye=0 from an older version - in every preset since 0.209.1: 1 now');
  end;
  if IniValue(GetIniString('openxr', 'hands_over_panels', '<none>', Ini)) = '0' then
  begin
    SetIniString('openxr', 'hands_over_panels', '1', Ini);
    Log('d2r_vr.ini: [openxr] hands_over_panels=0 from an older version - in every preset since 0.209.1: 1 now');
  end;
end;

// Kept settings at 0.208 - 0.209's Medium distance: the Medium preset's models' radius was 210, since the Draw distance slider came back it is
// 200 - the slider's stop 2, its default (Serg 2026-11-09: "по умолчанию поставим как сейчас 2", "комнаты 200 дистанция"). A kept ini at 210
// would read Custom in both the Quality preset and the slider though nothing was changed. Once, at the first update that has this (its mark
// [support] model_radius_200, one of the shipped defaults, comes in with the other new keys right after this - missing = not done yet):
// [render] model_radius=210 with rings=2 becomes 200 (the fog is full at 140 either way: nothing seen changes). Any other radius stays.
procedure MoveToRadius200(Ini: String);
begin
  if GetIniString('support', 'model_radius_200', '<none>', Ini) <> '<none>' then Exit;
  if (IniValue(GetIniString('render', 'model_radius', '<none>', Ini)) = '210') and (IniValue(GetIniString('render', 'rings', '<none>', Ini)) = '2') then
  begin
    SetIniString('render', 'model_radius', '200', Ini);
    Log('d2r_vr.ini: [render] model_radius=210 from an older version - the Medium preset''s distance is 200 since the Draw distance slider: 200 now');
  end;
end;

// Kept settings from before 0.209.10: [fog] game_fog_before_fx was 0 (shipped off, and with it "Native one pass" was skipped). Since then
// it ships on (Serg 2026-11-09, "С‚СѓРјР°РЅ РґРѕ СЌС„С„РµРєС‚РѕРІ": a torch's flame and smoke were wiped by the fog behind them). Once, at the first update
// that has this (its mark [support] fog_before_fx_0209, one of the shipped defaults, comes in with the other new keys right after this -
// missing = not done yet), a kept 0 becomes 1; a 0 set after that update stays.
procedure MoveToFogBeforeFx(Ini: String);
begin
  if GetIniString('support', 'fog_before_fx_0209', '<none>', Ini) <> '<none>' then Exit;
  if IniValue(GetIniString('fog', 'game_fog_before_fx', '<none>', Ini)) = '0' then
  begin
    SetIniString('fog', 'game_fog_before_fx', '1', Ini);
    Log('d2r_vr.ini: [fog] game_fog_before_fx=0 from an older version - the fog goes before smoke and fire since 0.209.10: 1 now');
  end;
end;

// Kept settings from before 0.209.8: the game's fog read the old fog's distances ([fog] start / end / caves / caves_start / caves_end /
// caves_strength, and [render] game_drawn_flat for the flat game); since then it has keys of its own (Serg 2026-11-09: "у каждого свои
// ползунки и свой блок"). Once, at the first update that has this (its mark [support] game_fog_keys_0209, one of the shipped defaults, comes in
// with the other new keys right after this - missing = not done yet), each game-fog key the ini does not have yet takes the shared one's
// value, so the fog stays as the player had it; the new keys of the shipped defaults (the Medium preset's) fill only what is still missing.
procedure MoveToGameFogKey(Ini, Sec, Key, FromSec, From: String);
var
  V: String;
begin
  if GetIniString(Sec, Key, '<none>', Ini) <> '<none>' then Exit;
  V := GetIniString(FromSec, From, '<none>', Ini);
  if V = '<none>' then Exit;
  SetIniString(Sec, Key, IniValue(V), Ini);
  Log('d2r_vr.ini: [' + Sec + '] ' + Key + '=' + IniValue(V) + ' from [' + FromSec + '] ' + From + ' - the game''s fog has its own keys since 0.209.8');
end;

procedure MoveToGameFogKeys(Ini: String);
begin
  if GetIniString('support', 'game_fog_keys_0209', '<none>', Ini) <> '<none>' then Exit;
  MoveToGameFogKey(Ini, 'fog', 'game_fog_start', 'fog', 'start');
  MoveToGameFogKey(Ini, 'fog', 'game_fog_end', 'fog', 'end');
  MoveToGameFogKey(Ini, 'fog', 'game_fog_caves', 'fog', 'caves');
  MoveToGameFogKey(Ini, 'fog', 'game_fog_caves_start', 'fog', 'caves_start');
  MoveToGameFogKey(Ini, 'fog', 'game_fog_caves_end', 'fog', 'caves_end');
  MoveToGameFogKey(Ini, 'fog', 'game_fog_caves_strength', 'fog', 'caves_strength');
  MoveToGameFogKey(Ini, 'fog', 'game_fog_flat', 'render', 'game_drawn_flat');
end;

// Kept settings from 0.209.7 / 0.209.8: [render] room_stitch (D2R VR Unleashed's room stitch, by Cal) came in at 0; since 0.209.9 it is on by
// default and floors_stay is gone (Serg 2026-11-09: "floors_stay отключи и удали", "оставляем код медведя включенным по умолчанию"). Once, at
// the first update that has this (its mark [support] room_stitch_on_0209, one of the shipped defaults, comes in with the other new keys right
// after this - missing = not done yet): room_stitch=0 becomes 1. A 0 chosen after that stays.
procedure MoveToRoomStitch(Ini: String);
begin
  if GetIniString('support', 'room_stitch_on_0209', '<none>', Ini) <> '<none>' then Exit;
  if IniValue(GetIniString('render', 'room_stitch', '<none>', Ini)) = '0' then
  begin
    SetIniString('render', 'room_stitch', '1', Ini);
    Log('d2r_vr.ini: [render] room_stitch=0 from an older version - on by default since 0.209.9: 1 now');
  end;
end;

// Kept settings from 0.208 - 0.213: Native one pass ([stereo] native_replay=1) was the default there; since 0.214 two passes are (Serg
// 2026-11-12: two passes by default, so that the game does not hang for people - one pass hung graphics cards inside the replayed right eye, the
// top crash of the week). A kept ini still at 1 would keep hanging. Once, at the first update that has this (its mark [support]
// two_passes_0214, one of the shipped defaults, comes in with the other new keys right after this - missing = not done yet): 1 becomes 0.
// One pass chosen again after that stays (for a processor too slow for two passes).
procedure MoveToTwoPasses(Ini: String);
begin
  if GetIniString('support', 'two_passes_0214', '<none>', Ini) <> '<none>' then Exit;
  if IniValue(GetIniString('stereo', 'native_replay', '<none>', Ini)) = '1' then
  begin
    SetIniString('stereo', 'native_replay', '0', Ini);
    Log('d2r_vr.ini: [stereo] native_replay=1 from an older version - Native two passes is the default since 0.214 (one pass hung graphics cards): 0 now');
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
  IniOldDeathRagdoll := '<none>';
  Ini := ModPlugins(Dir) + 'd2r_vr.ini';
  Def := ModPlugins(Dir) + 'd2r_vr.default.ini';
  IniWasThere := FileExists(Ini);
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

// Kept settings from before Weapon against the walls (Serg 2026-11-13: one choice Off / Ours / Bear's, [weight] wall_contact_mode, instead of the
// world_contact tick): an ini without wall_contact_mode that has world_contact=1 gets wall_contact_mode=1 (Ours) - before the new keys go in, which
// would add it as 0 and leave the tick unread. No mark: once the key is there this does nothing.
procedure MoveToWallContactMode(Ini: String);
begin
  if GetIniString('weight', 'wall_contact_mode', '<none>', Ini) <> '<none>' then Exit;
  if IniValue(GetIniString('weight', 'world_contact', '<none>', Ini)) = '1' then
  begin
    SetIniString('weight', 'wall_contact_mode', '1', Ini);
    Log('d2r_vr.ini: [weight] world_contact=1 from an older version - Weapon against the walls: Ours (wall_contact_mode=1)');
  end;
end;

// Kept settings from before 0.216 (Serg 2026-11-13, after the ini review releases/analysis/ini_diff_0216.md: "players get his settings"): the 40
// values he plays with become every player's once - a kept (tuned) d2r_vr.ini gets each of them at the first update that has this (its mark
// [support] serg_settings_0216, one of the shipped defaults, comes in with the other new keys right after this - missing = not done yet). Only
// these keys: the personal sections, the places (map_big_*, panel_grab_*), the sky radius, the window and render sizes, sun_shadows and
// hand_inventory_front are never touched. A value changed again after that stays.
procedure MoveKey0216(Sec, Key, Val, Ini: String);
var
  Was: String;
begin
  Was := GetIniString(Sec, Key, '<none>', Ini);
  if IniValue(Was) = Val then Exit;
  SetIniString(Sec, Key, Val, Ini);
  Log('d2r_vr.ini: [' + Sec + '] ' + Key + '=' + Was + ' -> ' + Val + ' (0.216: the settings the author plays with, once)');
end;

procedure MoveToSergSettings0216(Ini: String);
begin
  if not IniWasThere then Exit;   // a first install: the shipped defaults are these already
  if GetIniString('support', 'serg_settings_0216', '<none>', Ini) <> '<none>' then Exit;
  MoveKey0216('hud', 'swing_timer', '0', Ini);
  MoveKey0216('hud', 'map_click_big', '0', Ini);
  MoveKey0216('hud', 'map_stick_zoom', '0', Ini);
  MoveKey0216('hud', 'labels_own', '1', Ini);
  MoveKey0216('hud', 'npc_names_own', '1', Ini);
  MoveKey0216('hud', 'own_labels_drop_game', '1', Ini);
  MoveKey0216('hud', 'wrist_game_glow', '25', Ini);
  MoveKey0216('hud', 'wrist_game_glass', '0.1', Ini);
  MoveKey0216('hud', 'wrist_globe_in', '-0.06', Ini);
  MoveKey0216('hud', 'wrist_globe_scale', '1.46', Ini);
  MoveKey0216('render', 'ui_band_corner_icons', '1', Ini);
  MoveKey0216('render', 'shadows_once_a_pair', '1', Ini);
  MoveKey0216('render', 'foveated_strength', '3', Ini);
  MoveKey0216('render', 'monitor_eye_fit', '0', Ini);
  MoveKey0216('render', 'phantom_rects', '1', Ini);
  MoveKey0216('render', 'preset_game_options', '1', Ini);
  MoveKey0216('render', 'game_depth_fog', '1', Ini);
  MoveKey0216('render', 'game_fog_scan_safe', '0', Ini);
  MoveKey0216('table', 'full_width', '1', Ini);
  MoveKey0216('body', 'leap_camera_up_pct', '30', Ini);
  MoveKey0216('weight', 'leap_landing_scatter', '1', Ini);
  MoveKey0216('gameplay', 'leap_air_aim', '1', Ini);
  MoveKey0216('gameplay', 'move_during_skills', '1', Ini);
  MoveKey0216('gameplay', 'quick_blow_faster', '1', Ini);
  MoveKey0216('gameplay', 'flat_move_while_attacking', '2', Ini);
  MoveKey0216('gameplay', 'flat_attack_path', '1', Ini);
  MoveKey0216('gameplay', 'melee_touch_thrown', '0', Ini);
  MoveKey0216('hands', 'shoulder_draw_shield_stow', '1', Ini);
  MoveKey0216('ceiling', 'height_act1_barracks', '14.3', Ini);
  MoveKey0216('ceiling', 'relief_act1_barracks', '1', Ini);
  MoveKey0216('ceiling', 'height_act1_catacombs', '14.8', Ini);
  MoveKey0216('ceiling', 'relief_act1_catacombs', '1', Ini);
  MoveKey0216('autotune', 'enabled', '1', Ini);
  MoveKey0216('fog', 'game_fog_top_from_floor', '1', Ini);
  MoveKey0216('fog', 'caves', '0', Ini);
  MoveKey0216('fog', 'ease_in', '1', Ini);
  MoveKey0216('fog', 'game_fog_act1_dusk', '#050505', Ini);
  MoveKey0216('stereo', 'replay_fresh_slots', '1', Ini);
  MoveKey0216('mods', 'merge_data_mods', '0', Ini);
  MoveKey0216('fist', 'from_knuckle', '1', Ini);   // (new in 0.216: only a test build's 0 moves)
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
    MoveToQuickBlow(Ini);   // (the same: its mark [support] quick_blow_0204 is one of them)
    MoveToExitEnd(Ini);     // (the same: its mark [support] exit_end_0206 is one of them)
    MoveToMonitorEyeHands(Ini);   // (the same: its mark [support] monitor_hands_0209 is one of them)
    MoveToRadius200(Ini);   // (the same: its mark [support] model_radius_200 is one of them)
    MoveToGameFogKeys(Ini); // (the same: its mark [support] game_fog_keys_0209 is one of them)
    MoveToRoomStitch(Ini);  // (the same: its mark [support] room_stitch_on_0209 is one of them)
    MoveToFogBeforeFx(Ini); // (the same: its mark [support] fog_before_fx_0209 is one of them)
    MoveToTwoPasses(Ini);   // (the same: its mark [support] two_passes_0214 is one of them)
    MoveToWallContactMode(Ini);   // (the same: wall_contact_mode itself is one of them)
    MoveToSergSettings0216(Ini);  // (the same: its mark [support] serg_settings_0216 is one of them)
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
