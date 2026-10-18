// D2R VR as the D2RLoader mod D2R-VR: where its files are and what Setup / the uninstaller do about them (shared\mod_layout.h: the same names).
// Included in [Code] by D2R_VR_Setup.iss and by installer\tests\mod_layout_test.iss (installer\tests\mod_layout_test.py runs it for real in a
// scratch folder). Needs mod_names.inc. Everything here works inside <game>\mods\D2R-VR, <game>\d2rloader and the global plugin files an
// earlier D2R VR left; it never writes into another mod's folder (d2r-pcvr as mods\D2R-PCVR).

// ---------------------------------------------------------------- the mod's folders (shared/mod_layout.h)
// <game>\mods\D2R-VR\ with d2rloader\plugins\ and reshade\.
function ModRoot(GameDir: String): String;
begin
  Result := GameDir + '\{#ModDir}\';
end;
function ModPlugins(GameDir: String): String;
begin
  Result := ModRoot(GameDir) + 'd2rloader\plugins\';
end;
function ModReShade(GameDir: String): String;
begin
  Result := ModRoot(GameDir) + 'reshade\';
end;
// The optional weight-physics and grip libraries: d2rloader\d2r_vr_libs, NOT among the plugins (D2RLoader takes a DLL there for a plugin and logs
// "2 plugins rejected": 0.168.0). The plugin looks in <its folder>\..\d2r_vr_libs first, which for the mod is this one.
function ModLibs(GameDir: String): String;
begin
  Result := ModRoot(GameDir) + 'd2rloader\d2r_vr_libs\';
end;

// A file or folder moved where it goes, unless something is there already (what is there wins).
procedure MoveInto(FromPath, ToPath: String);
begin
  if not (FileExists(FromPath) or DirExists(FromPath)) then Exit;
  if FileExists(ToPath) or DirExists(ToPath) then Exit;
  ForceDirectories(ExtractFileDir(ToPath));
  if RenameFile(FromPath, ToPath) then
    Log('D2R VR: moved ' + FromPath + ' -> ' + ToPath)
  else
    Log('D2R VR: could not move ' + FromPath + ' -> ' + ToPath);
end;

// One library into the mod's d2r_vr_libs, from the folder it was in. Not there yet: moved. There already (what is there wins): a copy among the
// PLUGINS (InPlugins) is removed - D2RLoader would take it for a plugin and log "plugin rejected" - a copy in another d2r_vr_libs folder is left.
procedure MoveLibInto(FromDir, Name, ToDir: String; InPlugins: Boolean);
begin
  if not FileExists(FromDir + Name) then Exit;
  if FileExists(ToDir + Name) then
  begin
    if InPlugins and DeleteFile(FromDir + Name) then
      Log('D2R VR: ' + FromDir + Name + ' removed (the mod has its own in d2r_vr_libs; among the plugins D2RLoader rejects a library)');
    Exit;
  end;
  MoveInto(FromDir + Name, ToDir + Name);
end;

// 0.168.0 put the libraries among the plugins - the mod's own plugins folder too, in a test build of the mod: out of there into d2r_vr_libs.
procedure MoveLibsIntoMod(GameDir: String);
begin
  MoveLibInto(ModPlugins(GameDir), 'd2r_vr_weight.dll', ModLibs(GameDir), True);
  MoveLibInto(ModPlugins(GameDir), 'd2r_vr_grip.dll', ModLibs(GameDir), True);
end;

// Every FILE of FromDir that FindFirst's Mask names moves into ToDir (what is there wins), except the extensions in SkipExts (',.dll,.exe,' style, lower case).
// Folders are never touched: d2r-pcvr leaves folders of the same family (d2r_vr_icons, d2r_vr_potions) in the plugins folder.
procedure MoveFilesMatching(FromDir, Mask, ToDir, SkipExts: String);
var
  FR: TFindRec;
  Names: TStringList;
  I: Integer;
begin
  Names := TStringList.Create;
  try
    if FindFirst(FromDir + Mask, FR) then
    try
      repeat
        if (FR.Attributes and FILE_ATTRIBUTE_DIRECTORY) = 0 then
          if Pos(',' + LowerCase(ExtractFileExt(FR.Name)) + ',', ',' + SkipExts + ',') = 0 then Names.Add(FR.Name);
      until not FindNext(FR);
    finally
      FindClose(FR);
    end;
    for I := 0 to Names.Count - 1 do
      MoveInto(FromDir + Names[I], ToDir + Names[I]);
  finally
    Names.Free;
  end;
end;

// BodyWalk Portable of an earlier install (0.169 and older: beside D2R.exe) into the mod's folder. Ours when its portable.txt is there.
procedure MovePortableIntoMod(GameDir: String);
begin
  if FileExists(GameDir + '\BodyWalkVR\portable.txt') and FileExists(GameDir + '\BodyWalkVR\BodyWalkVR.exe') then
    MoveInto(GameDir + '\BodyWalkVR', ModRoot(GameDir) + 'BodyWalkVR');
end;

// An install before the mod layout (0.167 and older): the plugin in <game>\d2rloader\plugins, ReShade, the shader, the sky
// and FlatVR's add-on beside D2R.exe. Moved into the mod once, the player's settings with it. Not d2r-pcvr's (up to 0.3.15
// it used the same file names; its install record says so): those are left alone, and so is everything of d2r-pcvr as a mod of its own.
procedure MoveEarlierInstall(GameDir: String);
var
  Old, P, R: String;
  Names: TArrayOfString;
  I: Integer;
begin
  Old := GameDir + '\d2rloader\plugins\';
  if not FileExists(Old + 'd2rl-vrcam.dll') then Exit;
  if FileExists(GameDir + '\d2rloader\pcvr-install.json') then
  begin
    Log('D2R VR: d2rloader\plugins\d2rl-vrcam.dll is d2r-pcvr''s (its install record is there): left alone. D2RLoader loads a global plugin under every mod.');
    Exit;
  end;
  P := ModPlugins(GameDir);
  R := ModReShade(GameDir);
  SetArrayLength(Names, 7);
  Names[0] := 'd2r_vr.ini'; Names[1] := 'd2r_vr.default.ini'; Names[2] := 'd2r_vr_headset.ini'; Names[3] := 'd2r_vr_settings.ini';
  Names[4] := 'd2r_vr_depth.ini'; Names[5] := 'd2r_vr_reshade_ours.txt'; Names[6] := 'd2r_vr_skill_icons';
  for I := 0 to GetArrayLength(Names) - 1 do
    MoveInto(Old + Names[I], P + Names[I]);
  // and the rest that is ours by its name (d2r_vr*): the ini's backups (d2r_vr.ini.before-<version>, the player's own .before_<what>), the dumps
  // and traces the plugin wrote beside its ini. The libraries (.dll) and a program (.exe) are handled apart; folders stay (d2r-pcvr's d2r_vr_icons...).
  // Not ours by name, so left where they are: d2rl-vrcam.dll.<what> (backups of the plugin, D2RLoader does not load them) and the uitex_*.raw dumps.
  MoveFilesMatching(Old, 'd2r_vr*', P, '.dll,.exe');
  // the optional weight and grip libraries, wherever the earlier install had them: d2rloader\d2r_vr_libs (0.169, the newer copy first), or among the
  // plugins (0.168.0: the leftover is removed - D2RLoader rejects a library there)
  MoveLibInto(GameDir + '\d2rloader\d2r_vr_libs\', 'd2r_vr_weight.dll', ModLibs(GameDir), False);
  MoveLibInto(GameDir + '\d2rloader\d2r_vr_libs\', 'd2r_vr_grip.dll', ModLibs(GameDir), False);
  MoveLibInto(Old, 'd2r_vr_weight.dll', ModLibs(GameDir), True);
  MoveLibInto(Old, 'd2r_vr_grip.dll', ModLibs(GameDir), True);
  RemoveDir(GameDir + '\d2rloader\d2r_vr_libs');   // (only if it is empty now: a folder somebody else put things in is left)
  // the global copy goes: D2RLoader would load it without the mod too (the new one is the mod's)
  if DeleteFile(Old + 'd2rl-vrcam.dll') then Log('D2R VR: the global d2rloader\plugins\d2rl-vrcam.dll removed (the mod has its own)');
  // ReShade only when it was ours: a ReShade of the player's own stays beside the game
  if FileExists(P + 'd2r_vr_reshade_ours.txt') then
  begin
    MoveInto(GameDir + '\ReShade64.dll', R + 'ReShade64.dll');
    MoveInto(GameDir + '\ReShade.ini', R + 'ReShade.ini');
    MoveInto(GameDir + '\ReShadePreset.ini', R + 'ReShadePreset.ini');
    MoveFilesMatching(GameDir + '\', 'ReShade.ini.*', R, '');   // its ini's backups
    MoveFilesMatching(GameDir + '\', 'ReShade.log*', R, '');    // and its log: the next one is written there
  end;
  MoveInto(GameDir + '\FlatVR_DepthProvider.addon64', R + 'FlatVR_DepthProvider.addon64');
  MoveInto(GameDir + '\FlatVR_Keepalive.addonfx', R + 'FlatVR_Keepalive.addonfx');
  MoveFilesMatching(GameDir + '\', 'FlatVR_DepthProvider.*', R, '');   // the add-on's log and its backups (.addon64.dev, .bak_..: not loaded)
  MoveInto(GameDir + '\reshade-shaders\Shaders\D2R_DepthFog.fx', R + 'reshade-shaders\Shaders\D2R_DepthFog.fx');
  MoveInto(GameDir + '\reshade-shaders\Shaders\FlatVR_Keepalive.addonfx', R + 'reshade-shaders\Shaders\FlatVR_Keepalive.addonfx');
  MoveInto(GameDir + '\reshade-shaders\Textures\D2R_Sky_ours', R + 'reshade-shaders\Textures\D2R_Sky_ours');
  // the Arcane Sanctuary's pictures a build of the arcane-sky branch made in the game's own folder (stars.png, clouds.png, made.json): they are
  // the mod's now (Setup's PrepareAssets makes them there anyway; moving keeps the receipt of what we wrote)
  MoveInto(GameDir + '\reshade-shaders\Textures\D2R_Arcane', R + 'reshade-shaders\Textures\D2R_Arcane');
  // the game's reshade-shaders folders that are empty now (ReShade made them for us); a folder with anything of the player's in it stays
  RemoveDir(GameDir + '\reshade-shaders\Textures');
  RemoveDir(GameDir + '\reshade-shaders\Shaders');
  RemoveDir(GameDir + '\reshade-shaders');
end;

// ---------------------------------------------------------------- D2RLoader's side: modinfo.json, d2rloader.toml
// The mod's modinfo.json (D2R's own mod format: <mod>\<mod>.mpq\modinfo.json). savepath "../": the characters are the
// game's usual ones, not a mod's folder of their own.
procedure WriteModInfo(GameDir: String);
var
  Path: String;
begin
  Path := ModRoot(GameDir) + '{#ModName}.mpq\modinfo.json';
  if FileExists(Path) then Exit;
  ForceDirectories(ExtractFileDir(Path));
  // "name" is the mod's id (the folder, D2RLoader's -mod): the name players see goes in "description"
  SaveStringToFile(Path, '{ "name": "{#ModName}", "savepath": "../", "description": "{#ModDisplay} - {#StringChange(ModTagline, "'", "''")}" }' + #13#10, False);
end;

// default_mod = "D2R-VR" in d2rloader.toml when no mod was the default: D2RLoader.exe started by hand (or by BodyWalk) runs
// D2R VR as before; another mod's own launcher names its mod with -mod and wins. A default the player chose (d2r-pcvr's, say) stays.
procedure SetDefaultMod(GameDir: String);
var
  Path: String;
  Text: AnsiString;
  S: String;
begin
  Path := GameDir + '\d2rloader\config\d2rloader.toml';
  if not LoadStringFromFile(Path, Text) then Exit;
  S := String(Text);
  if StringChangeEx(S, 'default_mod = ""', 'default_mod = "{#ModName}"', True) > 0 then
    SaveStringToFile(Path, AnsiString(S), False);
end;
// ... and back to none when the mod goes; a default that is not ours stays.
procedure ClearDefaultMod(GameDir: String);
var
  Path: String;
  Text: AnsiString;
  S: String;
begin
  Path := GameDir + '\d2rloader\config\d2rloader.toml';
  if not LoadStringFromFile(Path, Text) then Exit;
  S := String(Text);
  if StringChangeEx(S, 'default_mod = "{#ModName}"', 'default_mod = ""', True) > 0 then
    SaveStringToFile(Path, AnsiString(S), False);
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

// ---------------------------------------------------------------- ReShade in the mod
// Needs the host's `ReShadeSetup: String` (a downloaded ReShade installer to run, or '': D2R VR Setup no longer downloads one).
// D2R VR's own ReShade is ReShade64.dll in the mod's reshade folder (vrcam loads it from there, that folder its base). A dxgi.dll beside the game is the
// PLAYER'S OWN ReShade: it is USED as it is - Setup never renames, moves, replaces or deletes it, and neither does the uninstaller (the old rule, "a dxgi.dll
// becomes ReShade64.dll or goes", is gone; [reshade] as_dxgi in an old ini means nothing now). What a dxgi.dll ReShade cannot find by itself are D2R VR's
// effect and pictures in mods\D2R-VR\reshade: its ReShade.ini gets two search paths for them (D2R_VR_Settings.exe --reshade-paths, shared/reshade_paths.h:
// append-only, once, a copy kept as ReShade.ini.before_d2rvr), and the uninstaller takes them out again.

// D2R VR's two folders in the search paths of the player's dxgi.dll ReShade. The program is beside D2R.exe already (the [Files] lines are done at ssPostInstall).
// Its exit code: 0 they are in, 1 could not write, 2 nothing to do (no dxgi.dll), 3 the game runs (the Home button does it later). Setup goes on whatever comes back.
procedure AddReShadePaths(GameDir: String);
var
  Code: Integer;
begin
  if not FileExists(GameDir + '\D2R_VR_Settings.exe') then
  begin
    Log('D2R VR: D2R_VR_Settings.exe is not beside the game: the search paths of the player''s ReShade are not added');
    Exit;
  end;
  if Exec(GameDir + '\D2R_VR_Settings.exe', '--reshade-paths add', GameDir, SW_HIDE, ewWaitUntilTerminated, Code) then
    Log('D2R VR: D2R_VR_Settings --reshade-paths add: ' + IntToStr(Code) + ' (0 they are in, 1 could not write, 2 nothing to do, 3 the game runs: Settings > Home has a button)')
  else
    Log('D2R VR: D2R_VR_Settings --reshade-paths add did not start: ' + SysErrorMessage(Code));
end;

procedure RemoveReShadePaths(GameDir: String);
var
  Code: Integer;
begin
  if not FileExists(GameDir + '\D2R_VR_Settings.exe') then Exit;
  if Exec(GameDir + '\D2R_VR_Settings.exe', '--reshade-paths remove', GameDir, SW_HIDE, ewWaitUntilTerminated, Code) then
    Log('D2R VR: D2R_VR_Settings --reshade-paths remove: ' + IntToStr(Code) + ' (0 out, 1 could not write)')
  else
    Log('D2R VR: D2R_VR_Settings --reshade-paths remove did not start: ' + SysErrorMessage(Code));
end;

procedure PlaceReShade(GameDir: String);
var
  Code: Integer;
  Raw: AnsiString;
  Ini: String;
  Target, R: String;
begin
  R := ModReShade(GameDir);
  ForceDirectories(R);
  // (only with no ReShade at all: ReShade's setup would write a dxgi.dll over the player's)
  if (ReShadeSetup <> '') and not FileExists(R + 'ReShade64.dll') and not FileExists(GameDir + '\dxgi.dll') then
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
    begin
      // ours: the uninstaller takes it away again (CurUninstallStepChanged)
      SaveStringToFile(ModPlugins(GameDir) + 'd2r_vr_reshade_ours.txt', 'ReShade was installed by D2R VR Setup.', False);
      // the one dxgi.dll Setup ever moves: the one ReShade's setup made JUST NOW (there was none before) - D2R.exe refuses an unsigned dxgi.dll, vrcam loads
      // ReShade64.dll from the mod's reshade folder
      if FileExists(GameDir + '\dxgi.dll') and not RenameFile(GameDir + '\dxgi.dll', R + 'ReShade64.dll') then
        MsgBox('Could not move dxgi.dll from the game''s folder to ' + R + 'ReShade64.dll - do it by hand, or D2R.exe will not start.', mbError, MB_OK);
    end;
    // its setup writes its ini beside D2RLoader.exe: it goes with ReShade into the mod (unless the mod has one)
    MoveInto(GameDir + '\ReShade.ini', R + 'ReShade.ini');
  end;
  // ReShade's headless setup writes its search paths as '...\Shaders\**\**', which
  // ReShade 6.8 itself cannot resolve (error 123 in ReShade.log): no effect loads,
  // so no fog and no sky. '...\**' is what it reads (2026-10-06).
  if LoadStringFromFile(R + 'ReShade.ini', Raw) then
  begin
    Ini := String(Raw);
    if StringChangeEx(Ini, '\**\**', '\**', True) > 0 then
      SaveStringToFile(R + 'ReShade.ini', AnsiString(Ini), False);
  end;
  // a dxgi.dll beside the game: the player's own ReShade, used as it is
  if FileExists(GameDir + '\dxgi.dll') then
  begin
    Log('D2R VR: ' + GameDir + '\dxgi.dll is the player''s own ReShade: left exactly as it is, and used (vrcam joins it)');
    AddReShadePaths(GameDir);
  end;
end;

// ---------------------------------------------------------------- the uninstaller
// The mod's folder goes whole: its plugin, ini, ReShade with its ini and presets, logs, the settings, and the shared VR mod launcher's
// manifest and icon (vrmod-manifest.json, launcher_icon.png) - and the default_mod it set. Nothing else: another VR mod's folder
// (mods\D2R-PCVR), the game's own files, D2RLoader and the player's screenshots and saves are not in it. A ReShade of the player's own
// beside the game was never moved into the mod (MoveEarlierInstall) and stays.
procedure RemoveMod(GameDir: String);
var
  Root: String;
begin
  RemoveReShadePaths(GameDir);   // D2R VR's two folders out of the player's own ReShade.ini again (its dxgi.dll is not touched); the program is still beside the game
  Root := RemoveBackslash(ModRoot(GameDir));
  // (only ever the folder named D2R-VR directly under a folder named mods)
  if (CompareText(ExtractFileName(Root), '{#ModName}') = 0) and (CompareText(ExtractFileName(ExtractFileDir(Root)), 'mods') = 0) then
  begin
    if DirExists(Root) and not DelTree(Root, True, True, True) then
      Log('D2R VR: ' + Root + ' could not be removed whole');
  end;
  ClearDefaultMod(GameDir);
end;

// ---------------------------------------------------------------- the shared VR mod launcher
// "D2R VR Launcher" (its own Setup, per user, no admin) writes HKCU\Software\D2R VR Launcher\InstallDir; without that, the folder it
// proposes. Setup only OFFERS to open it on its last page (unchecked); it does not need it and the launcher does not need Setup.
function LauncherExePath(Param: String): String;
var
  Dir: String;
begin
  Result := '';
  if RegQueryStringValue(HKCU, 'Software\D2R VR Launcher', 'InstallDir', Dir) and FileExists(AddBackslash(Dir) + 'D2RVRLauncher.exe') then
    Result := AddBackslash(Dir) + 'D2RVRLauncher.exe'
  else if FileExists(ExpandConstant('{localappdata}\Programs\D2R VR Launcher\D2RVRLauncher.exe')) then
    Result := ExpandConstant('{localappdata}\Programs\D2R VR Launcher\D2RVRLauncher.exe');
end;
function LauncherInstalled: Boolean;
begin
  Result := LauncherExePath('') <> '';
end;
