// tools/crash_watch_test.cpp - shared/d2r_vr_crash_watch.h, the contract with D2R VR Crash Reporter ([support] crash_reports /
// send_logs, docs/crash_watchdog.md): the quiet event's name the reporter opens, where the plugin and Settings look for the reporter,
// which report folders Settings offers again.
#include "d2r_vr_crash_watch.h"

#include <cstdio>

static int g_fail = 0, g_pass = 0;
static void Check(bool ok, const char* what) {
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("FAIL: %s\n", what); }
}

int main() {
    Check(crashwatch::QuietName(26840) == L"Local\\D2RVR_Quiet_26840", "the quiet event's name (the reporter opens exactly this)");
    Check(crashwatch::ReporterPath(L"C:\\G\\mods\\D2R-VR\\d2rloader\\plugins\\") ==
              L"C:\\G\\mods\\D2R-VR\\d2rloader\\plugins\\..\\d2r_vr_libs\\D2R_VR_Crash_Reporter.exe",
          "the reporter beside the libraries");
    Check(crashwatch::Pending("pending\r\n") && crashwatch::Pending("pending"), "a pending report is offered again");
    Check(!crashwatch::Pending("sent 4201\r\n") && !crashwatch::Pending("declined\r\n") && !crashwatch::Pending(""),
          "a sent, declined or unknown one never");
    Check(crashwatch::D2RVR_CRASH_ABI == 1 && crashwatch::kExitNotAllowed == 3, "the interface version and the 'not allowed' exit");
    std::printf("crash_watch_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
