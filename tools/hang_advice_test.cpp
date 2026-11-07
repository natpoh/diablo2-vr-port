// [support] hang_advice (shared/hang_advice.h): the last game's graphics card stop read from the plugin's record and from D2RLoader's log, the
// render size's step down, the game's DLSS read and switched off in its Settings.json.
//   hang_advice_test   (exit code 0 = all passed)
#include "hang_advice.h"

#include <cstdio>

using namespace hangadvice;

static int g_failed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAILED line %d: %s\n", __LINE__, #c); ++g_failed; } } while (0)

int main() {
    // the plugin's record as vr/gpu_fault.cpp writes it (the head of a real one, 2026-10-31)
    const std::string hung =
        "==== 2026-10-31 15:56:16 - D2R VR - the D3D12 device was removed (the device's own fence went to its end: the runtime signals every fence "
        "when it removes the device) ====\r\n"
        "vrcam: gpu fault - the device's removal reason: 0x887A0006 DXGI_ERROR_DEVICE_HUNG (the GPU stopped answering: a hang the driver reset); "
        "328.8 s after the plugin loaded\r\n"
        "vrcam: gpu fault - auto-breadcrumbs asked: 0x00000000, first list 000000001CE923C0\r\n";
    Found f = FromGpuFault(hung);
    CHECK(f.reason == Reason::Hung && f.stamp == "2026-10-31 15:56:16" && !f.fromLoaderLog && Advise(f));
    // an old file with several games (before the rotation): the LAST one counts
    const std::string two = hung +
        "==== 2026-11-01 20:06:02 - D2R VR - the D3D12 device was removed (the device's own fence went to its end) ====\n"
        "vrcam: gpu fault - the device's removal reason: 0x887A0007 DXGI_ERROR_DEVICE_RESET (the driver reset the device); 151.8 s after\n";
    f = FromGpuFault(two);
    CHECK(f.reason == Reason::Reset && f.stamp == "2026-11-01 20:06:02");
    // a bad call is not the card: found, but no advice (Serg's own 2026-11-06 record)
    f = FromGpuFault("==== 2026-11-06 20:16:38 - D2R VR - the D3D12 device was removed (the device's own fence went to its end) ====\n"
                     "vrcam: gpu fault - the device's removal reason: 0x887A0001 DXGI_ERROR_INVALID_CALL (a call the runtime could not take); 66.7 s\n");
    CHECK(f.reason == Reason::BadCall && !Advise(f) && f.stamp == "2026-11-06 20:16:38");
    // removed, driver error, out of memory, an unknown code, no reason line
    CHECK(FromGpuFault("==== 1 - D2R VR - the D3D12 device was removed (x) ====\nremoval reason: 0x887A0005 X\n").reason == Reason::Removed);
    CHECK(FromGpuFault("==== 1 - D2R VR - the D3D12 device was removed (x) ====\nremoval reason: 0x887A0020 X\n").reason == Reason::Driver);
    CHECK(FromGpuFault("==== 1 - D2R VR - the D3D12 device was removed (x) ====\nremoval reason: 0x8007000E X\n").reason == Reason::Memory);
    CHECK(FromGpuFault("==== 1 - D2R VR - the D3D12 device was removed (x) ====\nremoval reason: 0x80004005 X\n").reason == Reason::Unknown);
    f = FromGpuFault("==== 2026-11-07 10:00:00 - D2R VR - the D3D12 device was removed (x) ====\n");
    CHECK(f.reason == Reason::Unknown && Advise(f) && f.stamp == "2026-11-07 10:00:00");
    // nothing / something else: nothing
    CHECK(FromGpuFault("").reason == Reason::None && !Advise(FromGpuFault("")));
    CHECK(FromGpuFault("vrcam: gpu fault - DRED on for the game's device\n").reason == Reason::None);
    CHECK(FromGpuFault("text - D2R VR - the D3D12 device was removed but not at a line's start\n").reason == Reason::None);

    // D2RLoader's log: the game's dx12 backend assertion box (as in his d2rloader.log of 2026-11-06)
    const std::string box =
        "[2026-11-06 20:16:38.191] [ERROR] \n"
        "+--------------------------------------------------------------------------------------------------+\n"
        "| ASSERTION - a game check failed                                                                  |\n"
        "| Message : BC_ASSERT: SUCCEEDED(hr)                                                               |\n"
        "| Action  : Continuing because assertion dialogs are disabled. The error is not fixed.             |\n"
        "| Scope   : mod:D2R-VR                                                                             |\n"
        "| Location: package_cache\\prism\\2afda49\\runtime\\src\\api\\dx12\\dx12_backend.cpp:5241                 |\n";
    f = FromLoaderLog("[info] D2RLoader 1.3.1\n" + box, "loader 20261106-201640");
    CHECK(f.reason == Reason::Unknown && f.fromLoaderLog && f.stamp == "loader 20261106-201640" && Advise(f));
    // another game check in dx12_backend (not the failed call), or the failed call elsewhere: not this
    CHECK(FromLoaderLog("| Message : BC_ASSERT: pTexture != nullptr |\n| Location: x\\dx12_backend.cpp:100 |\n", "s").reason == Reason::None);
    CHECK(FromLoaderLog("| Message : BC_ASSERT: SUCCEEDED(hr) |\n| Location: x\\texture_loader.cpp:9 |\n", "s").reason == Reason::None);
    CHECK(FromLoaderLog("", "s").reason == Reason::None);
    // a SUCCEEDED(hr) far above (another box) does not join a later dx12_backend line
    CHECK(FromLoaderLog("BC_ASSERT: SUCCEEDED(hr)" + std::string(2000, '.') + "| Location: dx12_backend.cpp:1 |", "s").reason == Reason::None);

    // the render size one step down
    CHECK(StepDown(4096) == 3584 && StepDown(3584) == 3072 && StepDown(3072) == 2880 && StepDown(2880) == 2560);
    CHECK(StepDown(2560) == 2240 && StepDown(2240) == 2048 && StepDown(2048) == 1792 && StepDown(1792) == 1536);
    CHECK(StepDown(1536) == 0 && StepDown(1024) == 0 && StepDown(0) == 0 && StepDown(-5) == 0);   // the bottom: no step offered
    CHECK(StepDown(2580) == 2240);   // within half a slider step of 2560 counts as 2560
    CHECK(StepDown(2700) == 2560);   // between steps: the next one below
    CHECK(StepDown(3200) == 3072 && StepDown(5000) == 4096);
    // the headset's recommendation (not square): the square of the same pixels
    CHECK(SquareOf(2496, 2688) >= 2589 && SquareOf(2496, 2688) <= 2591);
    CHECK(SquareOf(2048, 2048) == 2048 && SquareOf(0, 10) == 0);
    CHECK(StepDown(SquareOf(2496, 2688)) == 2240);   // Quest 3's recommendation -> 2240

    // the game's DLSS in its Settings.json (keys as the game writes them)
    const std::string json = "{\r\n\t\"Graphics Mode\": 0,\r\n\t\"NVIDIA DLSS\": 2,\r\n\t\"AutoMapMode\": 2\r\n}";
    CHECK(DlssValue(json) == 2);
    bool ok = false;
    const std::string off = WithDlssOff(json, &ok);
    CHECK(ok && DlssValue(off) == 0);
    CHECK(off == "{\r\n\t\"Graphics Mode\": 0,\r\n\t\"NVIDIA DLSS\": 0,\r\n\t\"AutoMapMode\": 2\r\n}");   // only that number changed
    CHECK(WithDlssOff(off, &ok) == off && ok);
    CHECK(DlssValue("{\"NVIDIA DLSS\":13}") == 13 && WithDlssOff("{\"NVIDIA DLSS\":13}", &ok) == "{\"NVIDIA DLSS\":0}");
    CHECK(DlssValue("{\"Graphics Mode\": 0}") == -1);
    CHECK(WithDlssOff("{\"Graphics Mode\": 0}", &ok) == "{\"Graphics Mode\": 0}" && !ok);
    CHECK(DlssValue("{\"NVIDIA DLSS\": \"x\"}") == -1 && WithDlssOff("{\"NVIDIA DLSS\": \"x\"}", &ok) == "{\"NVIDIA DLSS\": \"x\"}" && !ok);

    std::printf(g_failed ? "%d FAILED\n" : "all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
