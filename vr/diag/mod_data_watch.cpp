// vr/diag/mod_data_watch.cpp - [mods] restore_mod_data (shared/mod_data_check.h): D2R VR's own files in mods\D2R-VR\D2R-VR.mpq looked at
// once at the game's start, on a thread of its own. barti4033 (0.218.3, 2026-11-17/18): the game sky's dome not shown at any start - D2RMM,
// its output mod named D2R-VR, deletes that folder whole at every build, the domes, the ceilings, the canoe, the world's patches and
// Setup's act skies with it, until D2R VR Settings was opened again. One log line always says what is there or gone (and when the
// folder's modinfo.json is not Setup's, who likely rebuilt it). With the key on and something gone: D2R_VR_Settings.exe (beside D2R.exe)
// is run with --restore-mod-data, no window, below normal priority - Setup's files put back, its makers again - and its exit and a
// second look said in the log. The game draws the dome as soon as its files are there (vr/sky: looked for again every 3 s); the
// presets (canoe, patches) when their area is loaded next. Nothing of the game's is touched; nothing in play waits for this.
// restore_mod_data=0: the look and its line only, as before (the player opens Settings).
#include "diag.h"
#include "../vrcam_internal.h"
#include "../../shared/mod_data_check.h"

#include <cwctype>
#include <string>

namespace vrc {
namespace moddatawatch {
namespace {
HANDLE g_stop = nullptr, g_thread = nullptr;
std::wstring g_modRoot, g_game;   // <game>\mods\D2R-VR\ and <game>\ (each with its slash); empty: not the mod's layout

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    return s;
}

// The plugin's ini is <game>\mods\D2R-VR\d2rloader\plugins\d2r_vr.ini in the mod's layout: its root and the game's folder from it
bool Roots(const std::wstring& ini) {
    const std::wstring tail = L"\\mods\\d2r-vr\\d2rloader\\plugins\\d2r_vr.ini";
    const std::wstring low = Lower(ini);
    if (low.size() <= tail.size() || low.compare(low.size() - tail.size(), tail.size(), tail) != 0) return false;
    g_game = ini.substr(0, ini.size() - tail.size() + 1);
    g_modRoot = ini.substr(0, ini.size() - tail.size() + 13);   // "\mods\D2R-VR\" (13 characters) after the game's folder
    return true;
}

DWORD WINAPI Run(void*) {
    const moddata::Check c = moddata::Look(g_modRoot);
    LogF("vrcam: mod data ([mods] restore_mod_data): %s", moddata::Line(c).c_str());
    if (!c.Gone()) return 0;
    if (GetPrivateProfileIntW(L"mods", L"restore_mod_data", 1, g_iniPath) == 0) {
        Log("vrcam: mod data - not made again here ([mods] restore_mod_data=0): open D2R VR Settings once, it makes them; the game's sky dome "
            "comes back when they are there");
        return 0;
    }
    const std::wstring exe = g_game + L"D2R_VR_Settings.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("vrcam: mod data - D2R_VR_Settings.exe is not beside D2R.exe: nothing made again (reinstall D2R VR)");
        return 0;
    }
    std::wstring cmd = L"\"" + exe + L"\" --restore-mod-data";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    const ULONGLONG t0 = GetTickCount64();
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                        g_game.c_str(), &si, &pi)) {
        LogF("vrcam: mod data - D2R_VR_Settings.exe --restore-mod-data could not be started (error %lu): open D2R VR Settings once", GetLastError());
        return 0;
    }
    CloseHandle(pi.hThread);
    Log("vrcam: mod data - D2R_VR_Settings.exe --restore-mod-data started in the background (its lines in d2r_vr_settings.log)");
    HANDLE both[2] = {pi.hProcess, g_stop};
    const DWORD w = WaitForMultipleObjects(2, both, FALSE, 5 * 60 * 1000);
    if (w != WAIT_OBJECT_0) {   // the game closing (or 5 minutes): it finishes on its own
        CloseHandle(pi.hProcess);
        if (w == WAIT_TIMEOUT) Log("vrcam: mod data - D2R_VR_Settings.exe still busy after 5 minutes: left to finish on its own");
        return 0;
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    const moddata::Check after = moddata::Look(g_modRoot);
    LogF("vrcam: mod data - D2R_VR_Settings.exe done in %.1f s (exit %lu: %s); now %s", (GetTickCount64() - t0) / 1000.0, code,
         code == 0 ? "all made" : code == 1 ? "some still gone" : code == 2 ? "not the mod" : code == 3 ? "another run busy" : "?",
         moddata::Line(after).c_str());
    return 0;
}
}  // namespace

void Start() {
    if (g_thread || !Roots(g_iniPath)) return;
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, &Run, nullptr, 0, nullptr);
    if (g_thread) SetThreadPriority(g_thread, THREAD_PRIORITY_BELOW_NORMAL);
}

void Stop() {
    if (!g_thread) return;
    SetEvent(g_stop);
    WaitForSingleObject(g_thread, 3000);
    CloseHandle(g_thread);
    CloseHandle(g_stop);
    g_thread = g_stop = nullptr;
}
}  // namespace moddatawatch
}  // namespace vrc
