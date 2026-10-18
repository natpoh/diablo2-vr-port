; The shared VR mod launcher's own Setup, carried unmodified (build_installer.ps1 -LauncherSetup <file> -LauncherSetupSha256 <pin> checks the pin
; and stages it as payload\launcher_setup\). dontcopy: it is not installed anywhere - launcher_bundle.inc unpacks it into {tmp} on demand, runs it
; when the launcher is missing or older, and Setup removes {tmp} at its end. Without the define (a build that carries no launcher) there is no line.
; Included INSIDE [Files] by D2R_VR_Setup.iss and by installer\tests\mod_layout_test.iss (LauncherSetupFile, and PayloadDir as in mod_files.inc).
#ifdef LauncherSetupFile
Source: "{#PayloadDir}\launcher_setup\{#LauncherSetupFile}"; DestName: "{#LauncherSetupFile}"; Flags: dontcopy
#endif
