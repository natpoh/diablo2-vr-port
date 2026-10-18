; What Setup takes away of the mod and of earlier installs, and what the uninstaller takes away of what the mod wrote while running. Included with its own
; section headers by D2R_VR_Setup.iss and by installer\tests\mod_layout_test.iss (so the test installs and uninstalls through the very lines the real
; Setup has). Needs ModDir (mod_names.inc). The mod's FOLDER is removed whole by the uninstaller (RemoveMod in mod_layout.inc); these lines are what
; Inno's own uninstall log would not know, and what an update clears away.

[InstallDelete]
; the licence file beside D2R.exe up to 0.169: it is in the mod's folder now (D2R_VR_LICENSE.txt, mod_files.inc)
Type: files; Name: "{app}\D2R_VR_LICENSE.txt"
; 0.168.0 put the weight and grip libraries among the plugins, where D2RLoader took them for plugins ("2 plugins rejected"); they live in
; d2rloader\d2r_vr_libs now - in the mod: mods\D2R-VR\d2rloader\d2r_vr_libs. What is left of those copies in the GLOBAL plugins folder goes (an
; earlier D2R VR's were moved into the mod before this, MoveEarlierInstall; the mod's own plugins folder is cleaned there too, MoveLibsIntoMod)
Type: files; Name: "{app}\d2rloader\plugins\d2r_vr_weight.dll"
Type: files; Name: "{app}\d2rloader\plugins\d2r_vr_grip.dll"
; the settings program lived beside the mod until 2026-10-05; it sits next to D2R.exe now
Type: files; Name: "{app}\d2rloader\plugins\d2r_vr_settings.exe"
; the lowercase name before 2026-10-06: Windows keeps an existing file's case, so
; it goes first and D2R_VR_Settings.exe comes in under its own name. Only these EXACT names: the player's own copies and backups of the program
; (D2R_VR_Settings.exe.before_...) and of the ini (d2r_vr.ini.before_..., moved into the mod with the ini) are never matched by a wildcard here.
Type: files; Name: "{app}\d2r_vr_settings.exe"

[UninstallDelete]
; the mod's own files that it wrote while running: its settings
; (d2r_vr.ini, D2R_VR_Settings' window file) with their backups, logs and dumps; FlatVR's add-on log
Type: files; Name: "{app}\{#ModDir}\d2rloader\plugins\d2r_vr*"
Type: files; Name: "{app}\{#ModDir}\reshade\FlatVR_DepthProvider.log"
