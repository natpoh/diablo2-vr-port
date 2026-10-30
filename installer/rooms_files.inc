; D2R Rooms - the co-op plugin (github.com/clauscodes/d2r_mp, its plugin/ folder): the open rooms list in D2RLoader's Multiplayer window and
; co-op without router setup (its relay). Carried unmodified (build_installer.ps1 -RoomsPlugin <dll> [-RoomsIni <ini>], staged as payload\rooms\),
; installed only with the "rooms" task ticked. It is D2RLoader's own plugin for every mod, so it goes to the game's d2rloader\plugins\ (not the
; mod's folder) and stays when D2R VR is removed; its ini is the player's once written. Without the define (a build that carries no plugin) no line.
; Included INSIDE [Files] by D2R_VR_Setup.iss.
#ifdef RoomsPlugin
Source: "{#PayloadDir}\rooms\d2rl-d2r-rooms.dll"; DestDir: "{app}\d2rloader\plugins"; Flags: ignoreversion uninsneveruninstall; Tasks: rooms
  #ifdef RoomsIni
Source: "{#PayloadDir}\rooms\d2r-rooms.ini"; DestDir: "{app}\d2rloader\plugins"; Flags: onlyifdoesntexist uninsneveruninstall; Tasks: rooms
  #endif
#endif
