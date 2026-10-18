; The mod's files in <game>\mods\D2R-VR: the lines of [Files] that go there. Included INSIDE [Files] by D2R_VR_Setup.iss (PayloadDir = payload,
; staged by build_installer.ps1) and by installer\tests\mod_layout_test.iss (PayloadDir = a scratch folder laid out the same), so the test
; installs through the very lines the real Setup has. Needs IsVR (the host's Check function), ModDir (mod_names.inc) and PayloadDir.
;
; Nothing here is written outside <game>\mods\D2R-VR: another VR mod (d2r-pcvr as mods\D2R-PCVR) is never touched. The uninstaller
; removes the folder whole (RemoveMod in mod_layout.inc), the launcher's manifest and icon with it.
#ifndef PayloadDir
  #error PayloadDir must be defined before mod_files.inc
#endif
Source: "{#PayloadDir}\plugins\d2rl-vrcam.dll"; DestDir: "{app}\{#ModDir}\d2rloader\plugins"; Flags: ignoreversion
; MIT, with the third-party notices MinHook's and ReShade's licences ask for in a binary (build_installer.ps1 stages the repository's LICENSE here)
Source: "{#PayloadDir}\license\D2R_VR_LICENSE.txt"; DestDir: "{app}\{#ModDir}"; Flags: ignoreversion
; The optional weight-physics and grip libraries ([weight] hands_model / grip_model), built apart from this repository (C:\wsl\weight_physics;
; binaries only). In d2rloader\d2r_vr_libs - NOT among the plugins, where D2RLoader took them for plugins and logged "2 plugins rejected" (0.168.0).
; The plugin looks there first (vr\weight_lib.cpp, vr\grip_lib.cpp: <the plugin's folder>\..\d2r_vr_libs, beside the plugin still works) and D2R VR
; Settings the same way from d2r_vr.ini's folder (LibFileFound); for the D2RLoader mod the plugin's folder is mods\D2R-VR\d2rloader\plugins, so
; the libraries are in mods\D2R-VR\d2rloader\d2r_vr_libs. Without them the plugin keeps its own way of moving and holding, and the two ticks in
; Settings are greyed out.
Source: "{#PayloadDir}\plugins\d2r_vr_weight.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\plugins\d2r_vr_grip.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
; The player's own settings stay through an update (IniChoice in [Code]); the
; shipped defaults go beside them as d2r_vr.default.ini, which tells the next
; update whether the player changed anything.
Source: "{#PayloadDir}\plugins\d2r_vr.ini"; DestDir: "{app}\{#ModDir}\d2rloader\plugins"; Flags: onlyifdoesntexist
Source: "{#PayloadDir}\plugins\d2r_vr.ini"; DestDir: "{app}\{#ModDir}\d2rloader\plugins"; DestName: "d2r_vr.default.ini"; Flags: ignoreversion
Source: "{#PayloadDir}\shaders\D2R_DepthFog.fx"; DestDir: "{app}\{#ModDir}\reshade\reshade-shaders\Shaders"; Flags: ignoreversion
Source: "{#PayloadDir}\sky\*.png"; DestDir: "{app}\{#ModDir}\reshade\reshade-shaders\Textures\D2R_Sky_ours"; Flags: ignoreversion
; The shared VR mod launcher's descriptor of this mod and the picture it names (tools\make_vrmod_manifest.py, from installer\launcher):
; the launcher finds the mod by <game>\mods\*\vrmod-manifest.json. It is not needed to play, and nothing else reads it.
Source: "{#PayloadDir}\launcher\vrmod-manifest.json"; DestDir: "{app}\{#ModDir}"; Flags: ignoreversion
Source: "{#PayloadDir}\launcher\launcher_icon.png"; DestDir: "{app}\{#ModDir}"; Flags: ignoreversion
; VR: the headset side.
; FlatVR's add-on is ReShade's: it goes where the mod's ReShade looks for add-ons (its base, the mod's reshade folder)
Source: "{#PayloadDir}\game\FlatVR_DepthProvider.addon64"; DestDir: "{app}\{#ModDir}\reshade"; Flags: ignoreversion; Check: IsVR
Source: "{#PayloadDir}\game\FlatVR_Keepalive.addonfx"; DestDir: "{app}\{#ModDir}\reshade"; Flags: ignoreversion; Check: IsVR
