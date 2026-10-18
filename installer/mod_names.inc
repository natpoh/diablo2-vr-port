; The names of D2R VR as a D2RLoader mod (shared\mod_layout.h: the same). Included at the top of D2R_VR_Setup.iss and of
; installer\tests\mod_layout_test.iss, so both run the same ones.
;
; ModName / ModDir: the mod's folder under <game>\mods and D2RLoader's -mod name - an id, it never changes.
; ModDisplay / ModTagline: what players see (installer, Installed apps, shortcuts, modinfo.json's description, the launcher's card);
; bearstonem's d2r-pcvr is "D2R VR Unleashed" in mods\D2R-PCVR.
#define ModName "D2R-VR"
#define ModDir "mods\D2R-VR"
#define ModDisplay "D2R VR Authentic"
#define ModTagline "Diablo II as it is, in VR: the game's own rules, your body and hands."
; SettingsDisplay: what players call the settings program (D2R_VR_Settings.exe, beside D2R.exe): the mod's OWN settings. The Start menu entry and the desktop shortcut are named
; so (mod_shortcuts.inc), so is "Open ..." on Setup's last page and the program's window (tools\settings\settings.cpp kAppName). A display name only: the file, the ini, the ids stay.
#define SettingsDisplay "D2R VR Authentic Settings"
