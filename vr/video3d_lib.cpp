// video3d_lib.cpp - see video3d_lib.h.

#include "video3d_lib.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <string>

namespace video3dlib {

namespace {

constexpr wchar_t kFile[] = L"d2r_vr_video3d.dll";

LogFn g_log = nullptr;
std::wstring g_path;
Status g_status = Status::NotLooked;
HMODULE g_module = nullptr;
D2rv3NameFn g_name = nullptr;
D2rv3StartFn g_start = nullptr;
D2rv3RecordFn g_record = nullptr;
D2rv3VoidFn g_submitted = nullptr, g_reset = nullptr, g_stop = nullptr;
ULONGLONG g_lastTry = 0;
int g_toldMissing = -1;
void* g_startedFor = nullptr;   // the device Start succeeded for
void* g_failedFor = nullptr;    // ... or failed for (not tried again)

void Say(const char* s) { if (g_log) g_log(s); }

void Anchor() {}

std::wstring PathOfFile() {
    if (!g_path.empty()) return g_path;
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Anchor, &mod)) return kFile;
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(mod, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    if (!n || n >= sizeof buf / sizeof buf[0]) return kFile;
    std::wstring p(buf, n);
    const size_t slash = p.find_last_of(L"\\/");
    const std::wstring dir = slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1);
    const std::wstring libs = dir + L"..\\d2r_vr_libs\\" + kFile;   // (out of d2rloader\plugins, where D2RLoader takes any DLL for a plugin)
    if (GetFileAttributesW(libs.c_str()) != INVALID_FILE_ATTRIBUTES) return libs;
    return dir + kFile;
}

}  // namespace

void SetLog(LogFn fn) { g_log = fn; }
void SetPath(const wchar_t* path) { g_path = path ? path : L""; }
Status GetStatus() { return g_status; }
const char* Name() { return g_status == Status::Loaded && g_name ? g_name() : ""; }

bool Ensure() {
    if (g_status == Status::Loaded) return true;
    const ULONGLONG now = GetTickCount64();
    if (g_lastTry && now - g_lastTry < 2000) return false;
    g_lastTry = now;
    const std::wstring path = PathOfFile();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (g_toldMissing != 0) {
            g_toldMissing = 0;
            Say("vrcam: video3d - there is no d2r_vr_video3d.dll in d2rloader\\d2r_vr_libs (or beside the plugin): [cinema] video_3d does nothing, the films stay flat");
        }
        g_status = Status::Missing;
        return false;
    }
    g_toldMissing = 1;
    // (its own dependencies - onnxruntime.dll, DirectML.dll - from its folder, not the game's)
    HMODULE mod = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        char b[160];
        snprintf(b, sizeof b, "vrcam: video3d - d2r_vr_video3d.dll could not be loaded (error %lu): the films stay flat", GetLastError());
        Say(b);
        g_status = Status::BrokenExports;
        return false;
    }
    const D2rv3AbiFn abi = (D2rv3AbiFn)GetProcAddress(mod, "D2rv3Abi");
    g_name = (D2rv3NameFn)GetProcAddress(mod, "D2rv3Name");
    g_start = (D2rv3StartFn)GetProcAddress(mod, "D2rv3Start");
    g_record = (D2rv3RecordFn)GetProcAddress(mod, "D2rv3Record");
    g_submitted = (D2rv3VoidFn)GetProcAddress(mod, "D2rv3Submitted");
    g_reset = (D2rv3VoidFn)GetProcAddress(mod, "D2rv3Reset");
    g_stop = (D2rv3VoidFn)GetProcAddress(mod, "D2rv3Stop");
    if (!abi || !g_name || !g_start || !g_record || !g_submitted || !g_reset || !g_stop) {
        Say("vrcam: video3d - d2r_vr_video3d.dll lacks the D2rv3* exports: the films stay flat");
        FreeLibrary(mod);
        g_status = Status::BrokenExports;
        return false;
    }
    if (abi() != D2RV3_ABI_VERSION) {
        char b[160];
        snprintf(b, sizeof b, "vrcam: video3d - d2r_vr_video3d.dll speaks interface version %d, the mod needs %d: not used", (int)abi(), (int)D2RV3_ABI_VERSION);
        Say(b);
        FreeLibrary(mod);
        g_status = Status::BadAbi;
        return false;
    }
    g_module = mod;
    g_status = Status::Loaded;
    char b[200];
    snprintf(b, sizeof b, "vrcam: video3d - library loaded: %s", g_name());
    Say(b);
    return true;
}

bool Start(void* device, void* queue) {
    if (!device || !queue || !Ensure()) return false;
    if (g_startedFor == device) return true;
    if (g_failedFor == device) return false;
    if (g_startedFor) { g_stop(); g_startedFor = nullptr; }
    if (g_start(device, queue, g_log) == 1) { g_startedFor = device; return true; }
    g_failedFor = device;
    Say("vrcam: video3d - the library could not start on this device: the films stay flat");
    return false;
}

int Record(void* list, const D2rv3Frame* frame) { return g_startedFor ? g_record(list, frame) : -1; }
void Submitted() { if (g_startedFor) g_submitted(); }
void Reset() { if (g_startedFor) g_reset(); }
void Stop() {
    if (g_startedFor) g_stop();
    g_startedFor = nullptr;
    g_failedFor = nullptr;
}

}  // namespace video3dlib
