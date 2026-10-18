; The shortcuts to D2R VR Authentic's settings program (D2R_VR_Settings.exe, beside D2R.exe), their earlier names, and what the uninstaller does about them. Included with its own
; section headers by D2R_VR_Setup.iss and by installer\tests\mod_layout_test.iss (so the test creates, replaces and removes real .lnk files through the very lines the real Setup
; has). Needs mod_names.inc (ModName, ModDisplay, ModTagline, SettingsDisplay).
;
; The program's display name is SettingsDisplay, "D2R VR Authentic Settings": it says it is THE MOD'S settings (the shared VR mod launcher, Setup's last page and the Start menu
; also have other things with "D2R VR" in the name). The FILE stays D2R_VR_Settings.exe; the shortcut's file name is the display name.
;
; Where: the Start menu and the desktop of all users (the admin Setup's {autoprograms} / {autodesktop}). The OLD names are removed from all four places - the all-users and the
; current user's Start menu and desktop - because earlier installers put theirs in either (a per-user or an all-users Setup), and an update must not leave them beside the new
; ones. The tests define the six places as folders of their scratch folder (ShellPrograms ... OldDesktopUser) before this file is included.
#ifndef ShellPrograms
  #define ShellPrograms "{autoprograms}"
  #define ShellDesktop "{autodesktop}"
  #define OldProgramsAll "{commonprograms}"
  #define OldProgramsUser "{userprograms}"
  #define OldDesktopAll "{commondesktop}"
  #define OldDesktopUser "{userdesktop}"
#endif

[InstallDelete]
; The settings program's shortcuts under their earlier names - gone when the new ones are made:
;   Start menu "D2R VR Settings" (0.1xx up to 0.169, and the 0.170 test builds);
;   desktop "D2R VR" (0.167 and older up to 0.169), "D2R VR Authentic" (0.170 test builds: it opened the settings program, the game's own shortcut is the Start menu's
;   "D2R VR Authentic"), "D2R VR Settings" (a player's or an even older install).
Type: files; Name: "{#OldProgramsAll}\D2R VR Settings.lnk"
Type: files; Name: "{#OldProgramsUser}\D2R VR Settings.lnk"
Type: files; Name: "{#OldDesktopAll}\D2R VR.lnk"
Type: files; Name: "{#OldDesktopUser}\D2R VR.lnk"
Type: files; Name: "{#OldDesktopAll}\D2R VR Authentic.lnk"
Type: files; Name: "{#OldDesktopUser}\D2R VR Authentic.lnk"
Type: files; Name: "{#OldDesktopAll}\D2R VR Settings.lnk"
Type: files; Name: "{#OldDesktopUser}\D2R VR Settings.lnk"

[UninstallDelete]
; Inno's own uninstall log removes the shortcuts this Setup made; these lines also take the earlier names (an install whose log never knew them, or a copy a player made
; by renaming) and the new ones in the other place (a per-user Setup after an all-users one), so none is left
Type: files; Name: "{#OldProgramsAll}\D2R VR Settings.lnk"
Type: files; Name: "{#OldProgramsUser}\D2R VR Settings.lnk"
Type: files; Name: "{#OldDesktopAll}\D2R VR.lnk"
Type: files; Name: "{#OldDesktopUser}\D2R VR.lnk"
Type: files; Name: "{#OldDesktopAll}\D2R VR Authentic.lnk"
Type: files; Name: "{#OldDesktopUser}\D2R VR Authentic.lnk"
Type: files; Name: "{#OldDesktopAll}\D2R VR Settings.lnk"
Type: files; Name: "{#OldDesktopUser}\D2R VR Settings.lnk"
Type: files; Name: "{#OldProgramsAll}\{#SettingsDisplay}.lnk"
Type: files; Name: "{#OldProgramsUser}\{#SettingsDisplay}.lnk"
Type: files; Name: "{#OldDesktopAll}\{#SettingsDisplay}.lnk"
Type: files; Name: "{#OldDesktopUser}\{#SettingsDisplay}.lnk"

; A shortcut on the desktop too, so nobody hunts for the game's folder (on by default).
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut to {#SettingsDisplay}"; GroupDescription: "Shortcuts:"

[Icons]
Name: "{#ShellPrograms}\{#SettingsDisplay}"; Filename: "{app}\D2R_VR_Settings.exe"; WorkingDir: "{app}"; Comment: "The settings of {#ModDisplay}"
; the game with this mod (D2RLoader's -mod): another VR mod's own shortcut names its mod the same way
Name: "{#ShellPrograms}\{#ModDisplay}"; Filename: "{app}\D2RLoader.exe"; Parameters: "-mod {#ModName}"; WorkingDir: "{app}"; IconFilename: "{app}\D2R_VR_Settings.exe"; Comment: "{#ModTagline}"
Name: "{#ShellDesktop}\{#SettingsDisplay}"; Filename: "{app}\D2R_VR_Settings.exe"; WorkingDir: "{app}"; Comment: "The settings of {#ModDisplay}"; Tasks: desktopicon
