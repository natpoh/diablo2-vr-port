// [openxr] block_listed_layers (shared/api_layers.h, vr/api_layers_lib.cpp): the manifest reading, the deny-list, what is turned off (only a
// listed layer, only while the eyes are drawn our own way) and the Status line, on made-up manifests; then against the real (static) Khronos
// OpenXR loader and this PC's registry, read only - every implicit layer the loader lists is one Scan found, the default list touches none of
// this PC's, and a layer put on the list leaves the loader's list (back with runtime_fov on or blocking off). The live part says so and passes
// when the PC has no implicit layer. Run: api_layers_test.exe
#define XR_USE_PLATFORM_WIN32
#include <windows.h>
#include <openxr/openxr.h>

#include <cstdio>
#include <string>
#include <vector>

#include "api_layers.h"
#include "api_layers_lib.h"
#include "frame_gen.h"

namespace {
int g_fails = 0, g_checks = 0;
#define EXPECT(c)                                                               \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(c)) { ++g_fails; std::printf("  FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

using namespace apilayers;

// mbucchia's Quad-Views-Foveated, as its installer writes it (openxr-api-layer.json with the name filled in)
const char* kQvf = R"({
  "file_format_version" : "1.0.0",
  "api_layer": {
    "name": "XR_APILAYER_MBUCCHIA_quad_views_foveated",
    "library_path": ".\\XR_APILAYER_MBUCCHIA_quad_views_foveated.dll",
    "api_version": "1.0",
    "implementation_version": "1",
    "description": "Emulate quad views foveation support",
    "instance_extensions": [
      { "name": "XR_VARJO_quad_views", "extension_version": 1, "entrypoints": [] },
      { "name": "XR_VARJO_foveated_rendering", "extension_version": 3, "entrypoints": [] }
    ],
    "functions": { "xrNegotiateLoaderApiLayerInterface": "xrNegotiateLoaderApiLayerInterface" },
    "disable_environment": "DISABLE_XR_APILAYER_MBUCCHIA_quad_views_foveated"
  }
})";

void TestParse() {
    const Manifest q = Parse(kQvf);
    EXPECT(q.ok);
    EXPECT(q.name == "XR_APILAYER_MBUCCHIA_quad_views_foveated");   // api_layer's own name, not an extension's "name" further in
    EXPECT(q.disableEnv == "DISABLE_XR_APILAYER_MBUCCHIA_quad_views_foveated");
    EXPECT(q.enableEnv.empty());
    // a BOM, an enable variable, tabs, escapes, numbers and literals
    const Manifest r = Parse("\xEF\xBB\xBF{\t\"file_format_version\": \"1.0.0\", \"x\": [1, -2.5e3, true, null, {}],\n\"api_layer\": {\"name\": \"A\\\"B\","
                             " \"enable_environment\": \"ON_A\", \"disable_environment\": \"OFF_A\", \"n\": 3}}");
    EXPECT(r.ok && r.name == "A\"B" && r.enableEnv == "ON_A" && r.disableEnv == "OFF_A");
    // a "name" outside api_layer is not the layer's
    EXPECT(!Parse(R"({"name": "X", "api_layer": {"disable_environment": "D"}})").ok);
    // broken files fail as a whole
    EXPECT(!Parse("").ok);
    EXPECT(!Parse("{").ok);
    EXPECT(!Parse(R"({"api_layer": {"name": "A",}})").ok);
    EXPECT(!Parse(R"({"api_layer": {"name": "A"}} trailing)").ok);
    EXPECT(!Parse(R"({"api_layer": {"name": "A")").ok);
    EXPECT(!Parse("[1, 2]").ok);
}

void TestListed() {
    const std::string list = kDefaultList;
    EXPECT(Listed("XR_APILAYER_MBUCCHIA_quad_views_foveated", list));
    EXPECT(Listed("xr_apilayer_mbucchia_QUAD_VIEWS_FOVEATED", list));   // any case
    // never touched by the default list: overlays, kneeboards, toolkits, hand tracking, the runtime's own, ReShade, our OFXR Bridge
    for (const char* n : {"XR_APILAYER_NOVENDOR_toolkit", "XR_APILAYER_FREDEMMOTT_OpenKneeboard", "XR_APILAYER_XSOverlay", "XR_APILAYER_fpsVR",
                          "XR_APILAYER_ULTRALEAP_hand_tracking", "XR_APILAYER_VIRTUALDESKTOP_oculus_compatibility", "XR_APILAYER_reshade",
                          framegen::kLayerName})
        EXPECT(!Listed(n, list));
    EXPECT(Listed("XR_APILAYER_NOVENDOR_toolkit", "obs, toolkit"));
    EXPECT(Listed("XR_APILAYER_NOVENDOR_toolkit", " XR_APILAYER_NOVENDOR_TOOLKIT ;"));
    EXPECT(!Listed("XR_APILAYER_MBUCCHIA_quad_views_foveated", ", ,"));   // empty parts match nothing
    EXPECT(!Listed("XR_APILAYER_MBUCCHIA_quad_views_foveated", ""));      // an emptied list: nothing
}

Found Make(const char* name, const char* disable, bool asleep = false) {
    Found f;
    f.path = std::string("C:\\layers\\") + name + ".json";
    f.manifest.ok = true;
    f.manifest.name = name;
    f.manifest.disableEnv = disable;
    f.asleep = asleep;
    return f;
}

void TestDecide() {
    const std::string list = std::string(kDefaultList) + ", XR_APILAYER_NOVAR, XR_APILAYER_SLEEPY";
    std::vector<Found> v{Make("XR_APILAYER_MBUCCHIA_quad_views_foveated", "DISABLE_QVF"), Make("XR_APILAYER_XSOverlay", "DISABLE_XS"),
                         Make("XR_APILAYER_NOVENDOR_toolkit", "DISABLE_TK"), Make("XR_APILAYER_SLEEPY", "DISABLE_S", true), Make("XR_APILAYER_NOVAR", "")};
    Found broken;
    broken.path = "C:\\layers\\broken.json";
    v.push_back(broken);
    // on, drawn our own way: only the listed, loadable, switchable one
    Decide(v, true, true, list);
    EXPECT(v[0].turnedOff && v[0].listed);
    EXPECT(!v[1].turnedOff && !v[1].listed);   // an overlay: never
    EXPECT(!v[2].turnedOff);
    EXPECT(!v[3].turnedOff);   // asleep: the loader leaves it out anyway
    EXPECT(!v[4].turnedOff && v[4].listed);   // names no variable (the loader refuses it anyway)
    EXPECT(!v[5].turnedOff);
    EXPECT(Names(v, Kind::Off) == "XR_APILAYER_MBUCCHIA_quad_views_foveated");
    EXPECT(Names(v, Kind::ListedIn) == "XR_APILAYER_NOVAR");
    EXPECT(Names(v, Kind::In) == "XR_APILAYER_XSOverlay, XR_APILAYER_NOVENDOR_toolkit");   // (the sleeping one, the broken one: not)
    // runtime_fov on: nothing turned off, the listed one only named
    Decide(v, true, false, list);
    EXPECT(!v[0].turnedOff && v[0].listed);
    EXPECT(Names(v, Kind::Off).empty());
    EXPECT(Names(v, Kind::ListedIn) == "XR_APILAYER_MBUCCHIA_quad_views_foveated, XR_APILAYER_NOVAR");
    // blocking off: the same
    Decide(v, false, true, list);
    EXPECT(!v[0].turnedOff && Names(v, Kind::Off).empty());
    // an emptied list: nothing listed
    Decide(v, true, true, "");
    EXPECT(!v[0].turnedOff && !v[0].listed && Names(v, Kind::ListedIn).empty());
}

void TestStatus() {
    int lv = -1;
    EXPECT(StatusLine(L"", L"", L"", false, &lv).empty() && lv == 0);
    const std::wstring off = StatusLine(L"XR_APILAYER_MBUCCHIA_quad_views_foveated", L"", L"", false, &lv);
    EXPECT(off.find(L"OpenXR layer XR_APILAYER_MBUCCHIA_quad_views_foveated turned off for D2R VR") == 0 && lv == 2);
    const std::wstring in = StatusLine(L"", L"X", L"", false, &lv);
    EXPECT(in.find(L"OpenXR layer X loads with the game: it is known to distort") == 0 && lv == 2);   // named, never silent
    const std::wstring rt = StatusLine(L"", L"X", L"", true, &lv);
    EXPECT(rt.find(L"should do no harm") != std::wstring::npos && lv == 1);   // runtime_fov: told, not a warning
    const std::wstring others = StatusLine(L"", L"", L"XR_APILAYER_XSOverlay", false, &lv);
    EXPECT(others.find(L"left as they are): XR_APILAYER_XSOverlay") != std::wstring::npos && lv == 1);
    StatusLine(L"A", L"", L"B", false, &lv);
    EXPECT(lv == 2);
}

std::vector<std::string> LoaderList() {
    uint32_t n = 0;
    std::vector<std::string> out;
    if (XR_FAILED(xrEnumerateApiLayerProperties(0, &n, nullptr)) || !n) return out;
    std::vector<XrApiLayerProperties> p(n, {XR_TYPE_API_LAYER_PROPERTIES});
    if (XR_FAILED(xrEnumerateApiLayerProperties(n, &n, p.data()))) return out;
    for (uint32_t i = 0; i < n; ++i) out.push_back(p[i].layerName);
    return out;
}
bool Has(const std::vector<std::string>& v, const std::string& s) {
    for (const std::string& x : v)
        if (x == s) return true;
    return false;
}
std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // (the variables' names are ASCII)

void TestLive() {
    SetEnvironmentVariableW(L"XR_API_LAYER_PATH", nullptr);   // explicit layers out of the way: only the registry's implicit ones list
    const std::vector<Found> found = apilayerslib::Scan();
    const std::vector<std::string> listed = LoaderList();
    std::printf("  this PC: %zu implicit layer manifest(s) enabled in the registry, the loader lists %zu layer(s)\n", found.size(), listed.size());
    for (const Found& f : found) std::printf("    %s (%s, %s)%s\n", f.manifest.ok ? f.manifest.name.c_str() : "?", f.path.c_str(),
                                             f.manifest.disableEnv.c_str(), f.asleep ? " asleep" : "");
    for (const std::string& l : listed) {
        bool known = false;
        for (const Found& f : found) known |= f.manifest.ok && f.manifest.name == l;
        EXPECT(known);   // nothing the loader lists that Scan missed
    }
    // the default list touches nothing on this PC (no Quad-Views-Foveated here): every listed layer still lists after Apply
    apilayerslib::Apply(true, true, kDefaultList, nullptr);
    std::vector<std::string> after = LoaderList();
    for (const std::string& l : listed) EXPECT(Has(after, l) || l.find("quad_views") != std::string::npos);
    const Found* any = nullptr;
    for (const Found& f : found)
        if (f.manifest.ok && !f.asleep && !f.manifest.disableEnv.empty() && Has(listed, f.manifest.name) && !any) any = &f;
    if (!any) { std::printf("  (no listed implicit layer to switch off here: the live part has nothing more to check)\n"); return; }
    // that layer put on the list: Apply turns it off - gone from the loader's list - and the others stay
    const std::vector<Found> applied = apilayerslib::Apply(true, true, any->manifest.name, nullptr);
    after = LoaderList();
    for (const Found& f : applied) {
        if (!f.manifest.ok || f.asleep || !Has(listed, f.manifest.name)) continue;
        EXPECT(Has(after, f.manifest.name) == !f.turnedOff);
        EXPECT(f.turnedOff == (f.manifest.name == any->manifest.name));
    }
    EXPECT(!Has(after, any->manifest.name));
    std::printf("    Apply with %s on the list: out of the loader's list\n", any->manifest.name.c_str());
    // runtime_fov on: Apply takes back what it set - the layer is back, nothing turned off
    for (const Found& f : apilayerslib::Apply(true, false, any->manifest.name, nullptr)) EXPECT(!f.turnedOff);
    EXPECT(Has(LoaderList(), any->manifest.name));
    // and on again, then blocking off: back again
    apilayerslib::Apply(true, true, any->manifest.name, nullptr);
    EXPECT(!Has(LoaderList(), any->manifest.name));
    apilayerslib::Apply(false, true, any->manifest.name, nullptr);
    EXPECT(Has(LoaderList(), any->manifest.name));
    std::printf("    ... back with runtime_fov on, and with blocking off\n");
}
}  // namespace

int main() {
    TestParse();
    TestListed();
    TestDecide();
    TestStatus();
    TestLive();
    std::printf("api_layers_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
