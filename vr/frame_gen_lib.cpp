// frame_gen_lib.cpp - see frame_gen_lib.h. After bearstonem/d2r-pcvr v0.3.25 (MIT, steamvr/transport.cpp's FrameGen*): the layer named for this process only, the
// panels' images listed below it, the control API asked live; in our words.

#include "frame_gen_lib.h"

#include <psapi.h>
#include <openxr/openxr_loader_negotiation.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace framegenlib {

namespace {

LogFn g_log = nullptr;
void Log(const char* s) { if (g_log) g_log(s); }
void LogLine(const std::string& s) { Log(("vrcam: openxr - " + s).c_str()); }

std::mutex g_mutex;
framegen::Options g_wanted;              // the ini's now
bool g_bothEyes = true;
framegen::Start g_started = framegen::Start::NotAsked;
bool g_asked = false;                    // the layer was put to xrCreateInstance this start
std::wstring g_folder;                   // ...\d2r_vr_libs\ofxr
framegen::SnapshotFn g_snapshot = nullptr;
framegen::RequestFn g_request = nullptr;
framegen::ControlSettingsV1 g_sent{};    // what the layer was last asked for
bool g_haveSent = false;
std::string g_lastLine;
PFN_xrEnumerateSwapchainImages g_runtimeEnumerate = nullptr;   // the runtime's own, below the layers (null: the loader's)
bool g_eyeChecked = false;
// after bearstonem's v0.3.27: a new session when it is switched on (once each time), or once after a live request failed; Virtual Desktop's own halving watched
bool g_rebuildArmed = false;             // switched on while the game runs: the next state that wants it makes the session again, once
bool g_retry = false;                    // a live request asked it to run: a failure makes the session again, once
bool g_newSession = false;               // taken by xr.cpp (TakeNewSession)
bool g_vdxr = false;                     // the runtime is Virtual Desktop's
framegen::HalvingWatch g_halving;

bool FileThere(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring FolderOf(const wchar_t* iniPath) {
    std::wstring dir = iniPath ? iniPath : L"";
    const size_t slash = dir.find_last_of(L"\\/");
    dir = slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
    return dir + L"..\\d2r_vr_libs\\" + framegen::kFolder;
}

bool Installed() { return !g_folder.empty() && FileThere(g_folder + L"\\" + framegen::kManifestFile) && FileThere(g_folder + L"\\" + framegen::kLibraryFile); }

bool Listed() {
    uint32_t n = 0;
    if (XR_FAILED(xrEnumerateApiLayerProperties(0, &n, nullptr)) || !n) return false;
    std::vector<XrApiLayerProperties> layers(n, {XR_TYPE_API_LAYER_PROPERTIES});
    if (XR_FAILED(xrEnumerateApiLayerProperties(n, &n, layers.data()))) return false;
    for (uint32_t i = 0; i < n && i < layers.size(); ++i)
        if (!std::strcmp(layers[i].layerName, framegen::kLayerName)) return true;
    return false;
}

// The runtime under the layers: the one loaded module that offers the runtime's negotiation and is no layer (SteamVR's vrclient_x64.dll, VDXR's); its
// xrEnumerateSwapchainImages for this instance.
PFN_xrEnumerateSwapchainImages RuntimeEnumerate(XrInstance instance) {
    HMODULE mods[1024];
    DWORD bytes = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &bytes)) return nullptr;
    HMODULE runtime = nullptr;
    int found = 0;
    for (DWORD i = 0; i < bytes / sizeof(HMODULE) && i < 1024; ++i) {
        if (!GetProcAddress(mods[i], "xrNegotiateLoaderRuntimeInterface") || GetProcAddress(mods[i], "xrNegotiateLoaderApiLayerInterface")) continue;
        runtime = mods[i];
        ++found;
    }
    if (found != 1) return nullptr;   // none, or more than one: not sure which is under us
    using NegotiateFn = XrResult(XRAPI_PTR*)(const XrNegotiateLoaderInfo*, XrNegotiateRuntimeRequest*);
    const NegotiateFn negotiate = reinterpret_cast<NegotiateFn>(GetProcAddress(runtime, "xrNegotiateLoaderRuntimeInterface"));
    XrNegotiateLoaderInfo li{};
    li.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
    li.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
    li.structSize = sizeof li;
    li.minInterfaceVersion = 1;
    li.maxInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    li.minApiVersion = XR_MAKE_VERSION(1, 0, 0);
    li.maxApiVersion = XR_MAKE_VERSION(1, 0x3ff, 0xfff);
    XrNegotiateRuntimeRequest rr{};
    rr.structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST;
    rr.structVersion = XR_RUNTIME_INFO_STRUCT_VERSION;
    rr.structSize = sizeof rr;
    PFN_xrEnumerateSwapchainImages fn = nullptr;
    if (!negotiate || XR_FAILED(negotiate(&li, &rr)) || !rr.getInstanceProcAddr ||
        XR_FAILED(rr.getInstanceProcAddr(instance, "xrEnumerateSwapchainImages", reinterpret_cast<PFN_xrVoidFunction*>(&fn))))
        return nullptr;
    return fn;
}

// Sends the settings (caller holds g_mutex). live: while it runs (not as the instance starts) - a failure to run is then retried with a new session, once.
void Send(bool live = true) {
    if (!g_request) return;
    const framegen::ControlSettingsV1 c = framegen::Control(g_wanted, g_bothEyes);
    if (g_haveSent && std::memcmp(&c, &g_sent, sizeof c) == 0) return;
    const bool saved = g_request(&c) != 0;
    g_sent = c;
    g_haveSent = true;
    g_retry = live && c.enabled;
    char b[220];
    std::snprintf(b, sizeof b, "frame generation: OFXR Bridge asked to %s%s", c.enabled ? ("run " + framegen::Describe(c.backend, c.preset, c.scale)).c_str() : "stand aside",
                  saved ? "" : " (it did not save it to its ofxr_bridge.ini)");
    LogLine(b);
}

}  // namespace

void SetLog(LogFn fn) { g_log = fn; }

void Configure(const framegen::Options& o, bool bothEyes) {
    std::lock_guard<std::mutex> l(g_mutex);
    if (o.On() && !g_wanted.On()) g_rebuildArmed = true;   // switched on: if this instance has no bridge, a new session brings it (Tick)
    if (!o.On()) g_newSession = false;
    g_wanted = o;
    g_bothEyes = bothEyes;
}

const char* Offer(const wchar_t* iniPath) {
    std::lock_guard<std::mutex> l(g_mutex);
    g_asked = false;
    g_folder = FolderOf(iniPath);
    if (!g_wanted.On()) { g_started = framegen::Start::NotAsked; return nullptr; }
    wchar_t full[MAX_PATH * 2];
    if (GetFullPathNameW(g_folder.c_str(), MAX_PATH * 2, full, nullptr)) g_folder = full;
    if (!Installed()) {
        g_started = framegen::Start::Missing;
        LogLine("frame generation: on ([openxr] frame_generation), but OFXR Bridge is not installed (no layer in d2rloader\\d2r_vr_libs\\ofxr: D2R VR Settings > Home "
                "installs it) - starting without it");
        return nullptr;
    }
    // named for this process only, before anything already in the variable
    wchar_t value[32768] = L"";
    const DWORD n = GetEnvironmentVariableW(L"XR_API_LAYER_PATH", value, 32768);
    std::wstring path = g_folder;
    if (n > 0 && n < 32768) {
        const std::wstring had(value, n);
        if (had.find(g_folder) == std::wstring::npos) path += L";" + had;
        else path = had;
    }
    SetEnvironmentVariableW(L"XR_API_LAYER_PATH", path.c_str());
    if (!Listed()) {
        g_started = framegen::Start::Failed;
        LogLine("frame generation: the OpenXR loader does not list OFXR Bridge from d2rloader\\d2r_vr_libs\\ofxr - starting without it");
        return nullptr;
    }
    g_asked = true;
    char b[200];
    std::snprintf(b, sizeof b, "frame generation: OFXR Bridge %ls asked for (%s, for this game only) - the game draws every other headset frame, the bridge makes the rest",
                  framegen::kVersion, framegen::kLayerName);
    LogLine(b);
    return framegen::kLayerName;
}

void Attach(XrInstance instance, bool withLayer) {
    std::lock_guard<std::mutex> l(g_mutex);
    g_snapshot = nullptr;
    g_request = nullptr;
    g_haveSent = false;
    g_runtimeEnumerate = nullptr;
    g_eyeChecked = false;
    g_lastLine.clear();
    if (!g_asked) return;
    if (!withLayer) {
        g_started = framegen::Start::Failed;
        LogLine("frame generation: the instance failed with OFXR Bridge and was made without it - no frames made");
        return;
    }
    const HMODULE layer = GetModuleHandleW(framegen::kLibraryFile);
    const auto version = layer ? reinterpret_cast<framegen::VersionFn>(GetProcAddress(layer, framegen::kVersionExport)) : nullptr;
    g_snapshot = layer ? reinterpret_cast<framegen::SnapshotFn>(GetProcAddress(layer, framegen::kSnapshotExport)) : nullptr;
    g_request = layer ? reinterpret_cast<framegen::RequestFn>(GetProcAddress(layer, framegen::kRequestExport)) : nullptr;
    if (!version || !g_snapshot || !g_request || version() < framegen::kApiVersion) {
        g_snapshot = nullptr;
        g_request = nullptr;
        g_started = framegen::Start::NoControl;
        LogLine("frame generation: OFXR Bridge runs, without a control API this mod knows - its own ofxr_bridge.ini decides");
    } else {
        g_started = framegen::Start::Loaded;
        Send(false);
    }
    g_runtimeEnumerate = RuntimeEnumerate(instance);
    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    g_vdxr = instance && XR_SUCCEEDED(xrGetInstanceProperties(instance, &ip)) && std::strstr(ip.runtimeName, "VirtualDesktop") != nullptr;
    g_halving = framegen::HalvingWatch{};
    LogLine(g_runtimeEnumerate ? "frame generation: the panels' images are listed by the runtime itself, below OFXR Bridge - it takes over the eyes only"
                               : "frame generation: the runtime under OFXR Bridge was not found - the panels go through the bridge too (SteamVR may run out of swapchains)");
}

XrResult PanelImages(XrSwapchain chain, uint32_t capacity, uint32_t* count, XrSwapchainImageBaseHeader* images) {
    PFN_xrEnumerateSwapchainImages fn;
    {
        std::lock_guard<std::mutex> l(g_mutex);
        fn = g_runtimeEnumerate;
    }
    return fn ? fn(chain, capacity, count, images) : xrEnumerateSwapchainImages(chain, capacity, count, images);
}

void CheckEye(XrSwapchain chain, const std::vector<XrSwapchainImageD3D12KHR>& passed) {
    std::lock_guard<std::mutex> l(g_mutex);
    if (!g_runtimeEnumerate || g_eyeChecked) return;
    g_eyeChecked = true;
    uint32_t n = 0;
    bool same = XR_SUCCEEDED(g_runtimeEnumerate(chain, 0, &n, nullptr)) && n == passed.size();
    std::vector<XrSwapchainImageD3D12KHR> own(n, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    same = same && XR_SUCCEEDED(g_runtimeEnumerate(chain, n, &n, reinterpret_cast<XrSwapchainImageBaseHeader*>(own.data())));
    for (uint32_t i = 0; same && i < n; ++i) same = own[i].texture == passed[i].texture;
    if (!same) {
        g_runtimeEnumerate = nullptr;
        LogLine("frame generation: the runtime's own list of an eye's images differs from the bridge's - the panels go through OFXR Bridge after all");
    }
}

void Tick(float predictedHz) {
    std::lock_guard<std::mutex> l(g_mutex);
    if (g_started == framegen::Start::NotAsked && !g_wanted.On()) return;
    Send();
    framegen::ControlSnapshotV1 s{};
    const bool have = g_snapshot && g_snapshot(&s) != 0;
    if (g_vdxr && predictedHz > 0.0f) g_halving.Note(predictedHz);
    framegen::Readout r = framegen::Classify(g_started, Installed(), g_wanted, have ? &s : nullptr, g_bothEyes);
    r.runtimeHalves = r.state == framegen::State::Active && g_halving.halved;
    // Switched on while running, or a failed live request: the game is restarted for it, no session made again. The author, 2026-10-25: every
    // live start through a new session (3ad8b330) left the headset black; started with the game it works ("needs restart in this version").
    if (framegen::LoadsWithNewSession(r) && g_rebuildArmed) {
        g_rebuildArmed = false;
        LogLine("frame generation: switched on while the game runs - restart the game to load OFXR Bridge (it starts with the game in this version)");
    } else if (framegen::RetriesWithNewSession(r) && g_retry) {
        g_retry = false;
        char b[200];
        std::snprintf(b, sizeof b, "frame generation: OFXR Bridge failed a live request (0x%08X) - restart the game to set it up afresh",
                      static_cast<unsigned>(r.lastError));
        LogLine(b);
    } else if (r.state == framegen::State::Active) {
        g_retry = false;
    }
    const std::string line = framegen::Line(r);
    if (line != g_lastLine) {
        g_lastLine = line;
        LogLine(line);
    }
}

bool TakeNewSession() {
    std::lock_guard<std::mutex> l(g_mutex);
    const bool want = g_newSession;
    g_newSession = false;
    return want;
}

void Ended() {
    std::lock_guard<std::mutex> l(g_mutex);
    g_retry = false;
    g_halving = framegen::HalvingWatch{};
    g_snapshot = nullptr;
    g_request = nullptr;
    g_runtimeEnumerate = nullptr;
    g_haveSent = false;
    g_eyeChecked = false;
}

}  // namespace framegenlib
