// [openxr] frame_generation: vr/frame_gen_lib.cpp's Offer against the real (static) Khronos OpenXR loader, no headset and no instance: a folder of the test's own
// with a plugins folder and d2r_vr_libs\ofxr beside it. Checks: off - nothing asked, the variable untouched; on without the layer - nothing asked; on with our
// manifest (and a stand-in DLL: the loader reads only the manifest to list a layer) - XR_API_LAYER_PATH names the folder and the loader lists the layer, so it
// is asked for; an earlier XR_API_LAYER_PATH is kept after ours. Run: frame_gen_loader_test.exe <an empty scratch folder>

#include <cstdio>
#include <string>

#include "frame_gen_lib.h"

namespace {
int g_fails = 0;
std::string g_log;
#define EXPECT(c, ...)                                         \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)
void Log(const char* s) { g_log += s; g_log += "\n"; }
bool Write(const std::wstring& p, const std::string& text) {
    FILE* f = _wfopen(p.c_str(), L"wb");
    if (!f) return false;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return true;
}
std::wstring Var() {
    wchar_t b[32768] = L"";
    const DWORD n = GetEnvironmentVariableW(L"XR_API_LAYER_PATH", b, 32768);
    return n ? std::wstring(b, n) : std::wstring();
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { std::printf("usage: frame_gen_loader_test <empty scratch folder>\n"); return 2; }
    const std::wstring root = argv[1];
    const std::wstring plugins = root + L"\\d2rloader\\plugins", libs = root + L"\\d2rloader\\d2r_vr_libs", ofxr = libs + L"\\ofxr";
    CreateDirectoryW((root + L"\\d2rloader").c_str(), nullptr);
    CreateDirectoryW(plugins.c_str(), nullptr);
    CreateDirectoryW(libs.c_str(), nullptr);
    const std::wstring ini = plugins + L"\\d2r_vr.ini";
    framegenlib::SetLog(&Log);
    SetEnvironmentVariableW(L"XR_API_LAYER_PATH", L"C:\\elsewhere");

    framegenlib::Configure(framegen::Read(0, 1, 2), true);
    EXPECT(framegenlib::Offer(ini.c_str()) == nullptr && Var() == L"C:\\elsewhere", "off: nothing asked, the variable untouched");

    framegenlib::Configure(framegen::Read(1, 1, 2), true);
    EXPECT(framegenlib::Offer(ini.c_str()) == nullptr, "on, not installed: nothing asked");
    EXPECT(g_log.find("not installed") != std::string::npos, "and the log says so");

    CreateDirectoryW(ofxr.c_str(), nullptr);
    EXPECT(Write(ofxr + L"\\" + framegen::kManifestFile, framegen::Manifest()) && Write(ofxr + L"\\" + framegen::kLibraryFile, "stand-in"), "the folder made");
    const char* layer = framegenlib::Offer(ini.c_str());
    std::printf("%s", g_log.c_str());
    EXPECT(layer && std::string(layer) == framegen::kLayerName, "installed: the loader lists the layer and it is asked for");
    const std::wstring v = Var();
    EXPECT(v.find(L"d2r_vr_libs\\ofxr") != std::wstring::npos && v.find(L"C:\\elsewhere") != std::wstring::npos && v.find(L"d2r_vr_libs\\ofxr") < v.find(L"C:\\elsewhere"),
           "XR_API_LAYER_PATH: ours first, the earlier one kept (%ls)", v.c_str());
    EXPECT(v.find(L"..") == std::wstring::npos, "a full path, no ..");
    framegenlib::Attach(XR_NULL_HANDLE, false);   // (asked, the instance failed with it)
    EXPECT(g_log.find("made without it") != std::string::npos, "a failed instance is said");
    std::printf(g_fails ? "frame_gen_loader_test: %d check(s) failed\n" : "frame_gen_loader_test: all checks passed\n", g_fails);
    return g_fails ? 1 : 0;
}
