// shared/d2r_vr_crash_watch.h - the contract between D2R VR and D2R VR Crash Reporter (D2R_VR_Crash_Reporter.exe), the optional, closed
// program that offers to send a report to the BodyWalk server after a game that ended badly ([support] crash_reports) and sends the
// Collect logs zip when the player asks ([support] send_logs). Design: docs/crash_watchdog.md.
//
// The reporter is not part of this repository and may be absent: then nothing here does anything, and Settings greys its two ticks out
// with a line that says why. It sits in d2rloader\d2r_vr_libs (the mod's: mods\D2R-VR\d2rloader\d2r_vr_libs), beside the weight library.
// It is a program, not a DLL: it must outlive the game it watches, and nothing closed runs inside the game.
//
// Its command line (every path absolute):
//   --abi                          exit code = the interface version it speaks (D2RVR_CRASH_ABI)
//   --watch <pid> --handle <h> --logs <the mod's d2rloader\logs> --ini <d2r_vr.ini> --version <v> [--mode <text>]
//       started by the plugin at its load (vr/diag/crash_watch.cpp) with an inherited SYNCHRONIZE handle <h> of the game. It waits for the
//       game; a game whose end is not clean (the quiet event below unset, and a crash / hang / lost graphics device file of the run, or an
//       error exit code) gets a report folder logs\crash_reports\<YYYYMMDD-HHMMSS>\ and a window that shows what would be sent and sends
//       it only on its Send button. It writes its verdicts to logs\d2r_vr_crash_watch.log. [support] crash_reports=0: it exits at once.
//   --offer <report folder> [--ini <d2r_vr.ini>]
//       the window for a report left unsent (Settings > Home, "Look at the report and send it")
//   --send-logs <zip> --logs <the mod's d2rloader\logs> --ini <d2r_vr.ini> [--version <v>]
//       Settings' "Send the logs to the BodyWalk server": a copy of the Collect logs zip, its texts scrubbed (user and PC name, character
//       names, addresses, e-mails, passwords taken out), pictures and videos left out, in the same window. [support] send_logs=0: exit 3.
//   exit codes: kExitDone, kExitBadArgs, kExitNotAllowed, kExitNothing
//
// A report folder holds state.txt: "pending" (offered, not sent - Settings offers it again), "sent <id>", "declined" (never offered again).
#pragma once

#include <cwchar>
#include <string>

namespace crashwatch {

constexpr int D2RVR_CRASH_ABI = 1;
constexpr const wchar_t* kReporterFile = L"D2R_VR_Crash_Reporter.exe";

enum Exit { kExitDone = 0, kExitBadArgs = 2, kExitNotAllowed = 3, kExitNothing = 4 };

// The event the plugin sets on its own clean way out (the unload, the process ending through ExitProcess with no crash of ours before):
// the reporter opens it before it waits, so it outlives the game.
inline std::wstring QuietName(unsigned long pid) {
    wchar_t b[64];
    swprintf(b, 64, L"Local\\D2RVR_Quiet_%lu", pid);
    return b;
}

// The reporter beside the plugin's folder (<plugins>\..\d2r_vr_libs\), as the weight library is looked for. `pluginsDir` ends in '\'.
inline std::wstring ReporterPath(const std::wstring& pluginsDir) { return pluginsDir + L"..\\d2r_vr_libs\\" + kReporterFile; }

// A report folder's state.txt says it waits for the player (not sent, not declined).
inline bool Pending(const std::string& state) {
    std::string s = state;
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s == "pending";
}

}  // namespace crashwatch
