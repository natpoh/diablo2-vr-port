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
; D2R VR Settings' language files (lang\<code>.txt; the program beside D2R.exe finds them here). A language file a player made and put
; here stays: Setup writes only its own files.
Source: "{#PayloadDir}\lang\*.txt"; DestDir: "{app}\{#ModDir}\lang"; Flags: ignoreversion skipifsourcedoesntexist
; The optional weight-physics and grip libraries ([weight] hands_model / grip_model), built apart from this repository (C:\wsl\weight_physics;
; binaries only). In d2rloader\d2r_vr_libs - NOT among the plugins, where D2RLoader took them for plugins and logged "2 plugins rejected" (0.168.0).
; The plugin looks there first (vr\weight_lib.cpp, vr\grip_lib.cpp: <the plugin's folder>\..\d2r_vr_libs, beside the plugin still works) and D2R VR
; Settings the same way from d2r_vr.ini's folder (LibFileFound); for the D2RLoader mod the plugin's folder is mods\D2R-VR\d2rloader\plugins, so
; the libraries are in mods\D2R-VR\d2rloader\d2r_vr_libs. Without them the plugin keeps its own way of moving and holding, and the two ticks in
; Settings are greyed out.
Source: "{#PayloadDir}\plugins\d2r_vr_weight.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\plugins\d2r_vr_grip.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\plugins\d2r_vr_gait.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
; BodyWalk's gesture engine in the game ([gestures] engine_in_game; vr\gesture_engine_lib.cpp looks in d2r_vr_libs, then beside the plugin): optional
Source: "{#PayloadDir}\plugins\d2r_vr_gestures.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
; BodyWalk's walk in place in the game ([walk_in_place]; vr\walk_engine_lib.cpp looks in d2r_vr_libs, then beside the plugin): optional
Source: "{#PayloadDir}\plugins\d2r_vr_walk.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
; D2R VR Crash Reporter ([support] crash_reports / send_logs, shared\d2r_vr_crash_watch.h; built in the private vjoy repository, a binary only).
; Beside the libraries; nothing runs it unless the player ticks one of the two (both off as shipped). Without it the two ticks are greyed out.
Source: "{#PayloadDir}\plugins\D2R_VR_Crash_Reporter.exe"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
; The 3D cinematics' runtime ([cinema] video_3d; vr\video3d\depth_net.cpp loads them from d2r_vr_libs, then beside the plugin): optional, binaries only.
; ONNX Runtime and DirectML, loaded by their full paths from there at run time (Windows 11 has older ones in System32; nothing links against
; them), the ZipDepth network (MIT) and the licences. Without them the videos stay flat.
Source: "{#PayloadDir}\video3d\onnxruntime.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\video3d\DirectML.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\video3d\zipdepth_base_512x288.onnx"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\video3d\ZipDepth-MIT.txt"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\video3d\onnxruntime-MIT.txt"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\video3d\DirectML-LICENSE.txt"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs"; Flags: ignoreversion skipifsourcedoesntexist
; The voice gestures' listener ([gestures] voice): d2r_vr_gestures.dll starts voice\d2r_vr_voice.exe from its own folder (closed, the private vjoy
; build) - Vosk with its MinGW runtime, ONNX Runtime, openWakeWord's two feature models, a speech model per language in models\ and the licences
; (installer\voice_licenses). Optional, binaries only: without the folder the Gestures tab says the listener is missing and no word casts.
Source: "{#PayloadDir}\voice\d2r_vr_voice.exe"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs\voice"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\voice\libvosk.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs\voice"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\voice\libstdc++-6.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs\voice"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\voice\libgcc_s_seh-1.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs\voice"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\voice\libwinpthread-1.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs\voice"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\voice\onnxruntime.dll"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs\voice"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\voice\melspectrogram.onnx"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs\voice"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\voice\embedding_model.onnx"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs\voice"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\voice\*.txt"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs\voice"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#PayloadDir}\voice\models\*"; DestDir: "{app}\{#ModDir}\d2rloader\d2r_vr_libs\voice\models"; Flags: ignoreversion recursesubdirs createallsubdirs skipifsourcedoesntexist
; The player's own settings stay through an update (IniChoice in [Code]); the
; shipped defaults go beside them as d2r_vr.default.ini, which tells the next
; update whether the player changed anything.
Source: "{#PayloadDir}\plugins\d2r_vr.ini"; DestDir: "{app}\{#ModDir}\d2rloader\plugins"; Flags: onlyifdoesntexist
Source: "{#PayloadDir}\plugins\d2r_vr.ini"; DestDir: "{app}\{#ModDir}\d2rloader\plugins"; DestName: "d2r_vr.default.ini"; Flags: ignoreversion
Source: "{#PayloadDir}\shaders\D2R_DepthFog.fx"; DestDir: "{app}\{#ModDir}\reshade\reshade-shaders\Shaders"; Flags: ignoreversion
; ... and the file it includes (D2R VR Unleashed's fog ease, MPL 2.0): without it the effect does not build
Source: "{#PayloadDir}\shaders\D2R_FogEase_Unleashed.fxh"; DestDir: "{app}\{#ModDir}\reshade\reshade-shaders\Shaders"; Flags: ignoreversion
Source: "{#PayloadDir}\shaders\D2R_Campfire_Unleashed.fxh"; DestDir: "{app}\{#ModDir}\reshade\reshade-shaders\Shaders"; Flags: ignoreversion
Source: "{#PayloadDir}\shaders\D2R_Frontend_Unleashed.fxh"; DestDir: "{app}\{#ModDir}\reshade\reshade-shaders\Shaders"; Flags: ignoreversion
Source: "{#PayloadDir}\sky\*.png"; DestDir: "{app}\{#ModDir}\reshade\reshade-shaders\Textures\D2R_Sky_ours"; Flags: ignoreversion
; The game sky dome's painted act skies ([sky] game_sky), where its units (made by D2R VR Settings, tools\settings\dome_assets.cpp) name them
Source: "{#PayloadDir}\skydome\*.dds"; DestDir: "{app}\{#ModDir}\D2R-VR.mpq\data\hd\env\texture\d2r_vr\sky"; Flags: ignoreversion skipifsourcedoesntexist
; ... and the same pictures beside the .mpq, its tree under shipped_data: D2RMM with its output mod named D2R-VR deletes D2R-VR.mpq whole at
; every build (barti4033, 0.218.3: no dome at any start). D2R VR Settings puts a missing one back from here (shared/mod_data_check.h)
Source: "{#PayloadDir}\skydome\*.dds"; DestDir: "{app}\{#ModDir}\shipped_data\data\hd\env\texture\d2r_vr\sky"; Flags: ignoreversion skipifsourcedoesntexist
; The shared VR mod launcher's descriptor of this mod and the picture it names (tools\make_vrmod_manifest.py, from installer\launcher):
; the launcher finds the mod by <game>\mods\*\vrmod-manifest.json. It is not needed to play, and nothing else reads it.
Source: "{#PayloadDir}\launcher\vrmod-manifest.json"; DestDir: "{app}\{#ModDir}"; Flags: ignoreversion
Source: "{#PayloadDir}\launcher\launcher_icon.png"; DestDir: "{app}\{#ModDir}"; Flags: ignoreversion
; VR: the headset side.
; (FlatVR's ReShade add-on and its keepalive effect: not shipped since 0.203 - no ReShade; the plugin hands FlatVR its picture itself, vr/flatvr_feed.h)
