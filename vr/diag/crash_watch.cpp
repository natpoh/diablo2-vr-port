// vr/diag/crash_watch.cpp - [support] crash_reports: at the plugin's load, D2R VR Crash Reporter (shared/d2r_vr_crash_watch.h, an optional
// closed program in d2rloader\d2r_vr_libs) is started to watch this game from outside; after a game that ended badly it offers to send a
// report to the BodyWalk server, and sends nothing unless the player presses Send (docs/crash_watchdog.md). Here only: the quiet event the
// reporter reads to tell a clean end, its start with an inherited handle of the game, and the event set on the clean ways out.
// Unticked (the default) or no reporter: nothing at all, as before.
#include "diag.h"
#include "../vrcam_internal.h"
#include "../../shared/d2r_vr_crash_watch.h"

#include <atomic>
#include <string>

namespace vrc {

namespace crashwatch {
namespace {
HANDLE g_quiet = nullptr;
std::atomic<bool> g_crashed{false};
}  // namespace

void Start(const char* version) {
    if (GetPrivateProfileIntW(L"support", L"crash_reports", 0, g_iniPath) != 1 || g_quiet) return;
    std::wstring plugins = g_iniPath;
    plugins.resize(plugins.find_last_of(L'\\') + 1);
    const std::wstring exe = ::crashwatch::ReporterPath(plugins);
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("vrcam: crash reports ([support] crash_reports) - ticked, but there is no D2R_VR_Crash_Reporter.exe in d2rloader\\d2r_vr_libs: "
            "nothing watches this game");
        return;
    }
    const DWORD pid = GetCurrentProcessId();
    g_quiet = CreateEventW(nullptr, TRUE, FALSE, ::crashwatch::QuietName(pid).c_str());
    HANDLE self = nullptr;   // the game, for the reporter to wait on: inherited, so a reused pid can never stand in for it
    if (!g_quiet || !DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &self,
                                     SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0)) {
        Log("vrcam: crash reports - could not make the quiet event or the game's handle: nothing watches this game");
        return;
    }
    const int platform = GetPrivateProfileIntW(L"mode", L"platform", 1, g_iniPath);
    wchar_t mode[48];
    swprintf_s(mode, L"%ls F%d", platform ? L"VR" : L"flat", GetPrivateProfileIntW(L"mode", L"vr_mode", 1, g_iniPath));
    wchar_t ver[48];
    swprintf_s(ver, L"%hs", version);
    std::wstring cmd = L"\"" + exe + L"\" --watch " + std::to_wstring(pid) + L" --handle " + std::to_wstring((unsigned long long)(uintptr_t)self) +
                       L" --logs \"" + plugins + L"..\\logs\" --ini \"" + std::wstring(g_iniPath) + L"\" --version " + ver + L" --mode \"" + mode + L"\"";
    // Only the game's handle goes to the child (none of the game's other inheritable handles)
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::string attrs(size, '\0');
    auto* list = (LPPROC_THREAD_ATTRIBUTE_LIST)attrs.data();
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    BOOL ok = InitializeProcThreadAttributeList(list, 1, 0, &size) &&
              UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &self, sizeof self, nullptr, nullptr);
    if (ok) {
        si.lpAttributeList = list;
        // Out of the game's job if it may (a launcher's job that ends its children with the game would end the watcher too)
        const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | DETACHED_PROCESS | BELOW_NORMAL_PRIORITY_CLASS;
        ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, flags | CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr, &si.StartupInfo, &pi) ||
             CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si.StartupInfo, &pi);
        DeleteProcThreadAttributeList(list);
    }
    const DWORD err = ok ? 0 : GetLastError();
    CloseHandle(self);
    if (!ok) {
        LogF("vrcam: crash reports - D2R_VR_Crash_Reporter.exe could not be started (error %lu): nothing watches this game", err);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    LogF("vrcam: crash reports ([support] crash_reports) - D2R VR Crash Reporter watches this game (its pid %lu); if the game ends badly it "
         "offers a report, sent only on its Send button. Its verdicts: d2rloader\\logs\\d2r_vr_crash_watch.log", pi.dwProcessId);
}

// The plugin's unload, and the process ending through ExitProcess (DllMain below): a clean end - unless crash.cpp saw the game die.
void Quiet() {
    if (g_quiet && !g_crashed.load()) SetEvent(g_quiet);
}
void Crashed() { g_crashed.store(true); }
}  // namespace crashwatch

}  // namespace vrc

// DLL_PROCESS_DETACH with lpReserved set: the process is ending through ExitProcess (a quit; a crash is ended by TerminateProcess and
// never gets here). Without lpReserved: the plugin unloaded (D2RLoaderUnloadPlugin has set it already).
extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) vrc::crashwatch::Quiet();
    return TRUE;
}
