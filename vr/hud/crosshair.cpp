// vr/hud/crosshair.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M9); what it gives: vr/hud/hud_vr.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// The crosshair in place of the game's gauntlet (flat mouse look). D2R's
// pointer is a plain Windows cursor (its own HCURSORs through SetCursor,
// GetCursorInfo showing, 2026-10-06), so the swap is the cursor itself:
// user32!SetCursor hooked, the game's handle replaced by ours while the
// crosshair is wanted, and WM_SETCURSOR answered with it. Hot spot in the
// middle - the window's middle, where MouseTick keeps the pointer and every
// click lands. The picture: tools/gen_crosshair.py (OpenAI), 32/48/64 px by
// the window's height. Off ([input] flat_crosshair=0, F9, a panel open, F1):
// the game's own pointer at its next SetCursor.
// The player's own picture: [input] crosshair, like the skies - a bare name
// under reshade-shaders\Textures, or a full path; empty or unreadable = the
// built-in one. Fitted into the square, its middle the hot spot; a picture
// with no transparency at all has its black made see-through (as ours was
// drawn). Built on the update thread (WIC) whenever the key changes.
SetCursorFn OrigSetCursor;
std::atomic<HCURSOR> g_cross[std::size(crosshair::kSizes)] = {};
std::atomic<bool> g_crossHooked{false};

bool CrosshairNow() {
    return g_set.flatCrosshair.load() && KeyMoveMode() && ShooterActive() && (g_set.platform.load() == 0 || g_set.crosshairOn.load());
}
// VR F2 with mouse look and [hud_third] crosshair=0: no pointer in the headset at all.
bool CrosshairHidden() { return g_set.platform.load() == 1 && ViewNow() == 2 && KeyMoveMode() && ShooterActive() && !g_set.crosshairOn.load(); }

HCURSOR MakeCrosshair(const crosshair::Size& s) {
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof bi; bi.bV5Width = s.side; bi.bV5Height = -s.side; bi.bV5Planes = 1; bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) return nullptr;
    memcpy(bits, s.bgra, (size_t)s.side * s.side * 4);
    HBITMAP mask = CreateBitmap(s.side, s.side, 1, 1, nullptr);   // all 0: the alpha decides
    ICONINFO ii{FALSE, (DWORD)(s.side / 2), (DWORD)(s.side / 2), mask, color};
    HCURSOR c = (HCURSOR)CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return c;
}

// The player's picture as side x side BGRA (straight alpha), fitted and centred; false = cannot read it.
bool ReadCrosshairFile(const std::wstring& path, int side, std::vector<uint8_t>* out) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool ok = false;
    IWICImagingFactory* wic = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    UINT w = 0, h = 0;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w && h &&
        SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(wic->CreateBitmapScaler(&scaler))) {
        const UINT fw = std::max(1u, (UINT)((uint64_t)side * w / std::max(w, h))), fh = std::max(1u, (UINT)((uint64_t)side * h / std::max(w, h)));
        std::vector<uint8_t> fit((size_t)fw * fh * 4);
        if (SUCCEEDED(scaler->Initialize(conv, fw, fh, WICBitmapInterpolationModeFant)) &&
            SUCCEEDED(scaler->CopyPixels(nullptr, fw * 4, (UINT)fit.size(), fit.data()))) {
            bool opaque = true;
            for (size_t i = 3; i < fit.size(); i += 4) if (fit[i] < 250) { opaque = false; break; }
            out->assign((size_t)side * side * 4, 0);
            const UINT ox = (side - fw) / 2, oy = (side - fh) / 2;
            for (UINT y = 0; y < fh; ++y)
                for (UINT x = 0; x < fw; ++x) {
                    const uint8_t* p = &fit[((size_t)y * fw + x) * 4];
                    uint8_t* q = &(*out)[((size_t)(oy + y) * side + ox + x) * 4];
                    float a = p[3] / 255.0f;
                    if (opaque) a = std::clamp((std::max({p[0], p[1], p[2]}) / 255.0f - 0.06f) / 0.5f, 0.0f, 1.0f);
                    const float k = opaque ? 1.0f / std::max(a, 1e-3f) : 1.0f / std::max(p[3] / 255.0f, 1e-3f);   // straight colour
                    for (int c = 0; c < 3; ++c) q[c] = (uint8_t)std::min(255.0f, p[c] * k + 0.5f);
                    q[3] = (uint8_t)(a * 255.0f + 0.5f);
                }
            ok = true;
        }
    }
    for (IUnknown* u : {(IUnknown*)scaler, (IUnknown*)conv, (IUnknown*)frame, (IUnknown*)dec, (IUnknown*)wic}) if (u) u->Release();
    if (SUCCEEDED(co)) CoUninitialize();
    return ok;
}

SRWLOCK g_crossLock = SRWLOCK_INIT;
std::wstring g_crossFile;              // [input] crosshair as written
std::atomic<uint32_t> g_crossGen{1};   // bumped when it changes; CrosshairTick rebuilds
std::atomic<HCURSOR> g_crossVr{nullptr};   // VR: one more, at [hud_third] crosshair_size (CrosshairTick)

// The size the window's height picks: 32 px below 900, 48 below 1500, 64 above.
int CrosshairSizeIndex() {
    RECT rc{};
    vrres::Size game;
    const int h = vrres::GameSize(g_gameWnd, &game) ? (int)game.h   // [render] vr_resolution: the game's picture, not the window
                  : g_gameWnd && GetClientRect(g_gameWnd, &rc) ? rc.bottom : 1080;
    return h < 900 ? 0 : h < 1500 ? 1 : 2;
}

// The built-in picture at any size: each output pixel the average of the source
// pixels it covers (straight alpha, colour weighted by it).
std::vector<uint8_t> ScaledBuiltin(int side) {
    const crosshair::Size& s = crosshair::kSizes[std::size(crosshair::kSizes) - 1];
    std::vector<uint8_t> out((size_t)side * side * 4, 0);
    const float k = (float)s.side / (float)side;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const int x0 = (int)(x * k), x1 = std::max(x0 + 1, (int)((x + 1) * k)), y0 = (int)(y * k), y1 = std::max(y0 + 1, (int)((y + 1) * k));
            float c[3] = {}, a = 0.0f;
            int n = 0;
            for (int sy = y0; sy < std::min(y1, s.side); ++sy)
                for (int sx = x0; sx < std::min(x1, s.side); ++sx, ++n) {
                    const uint8_t* p = &s.bgra[((size_t)sy * s.side + sx) * 4];
                    const float pa = p[3] / 255.0f;
                    for (int i = 0; i < 3; ++i) c[i] += p[i] * pa;
                    a += pa;
                }
            uint8_t* q = &out[((size_t)y * side + x) * 4];
            if (n && a > 1e-4f) for (int i = 0; i < 3; ++i) q[i] = (uint8_t)std::min(255.0f, c[i] / a + 0.5f);
            q[3] = n ? (uint8_t)(a / n * 255.0f + 0.5f) : 0;
        }
    return out;
}

void LoadCrosshairFile() {
    wchar_t buf[MAX_PATH];
    GetPrivateProfileStringW(L"input", L"crosshair", L"D2R_Sky_ours\\D2R_Crosshair.png", buf, MAX_PATH, g_iniPath);
    std::wstring f = buf;
    while (!f.empty() && (f.back() == L' ' || f.back() == L'"')) f.pop_back();
    while (!f.empty() && (f.front() == L' ' || f.front() == L'"')) f.erase(0, 1);
    AcquireSRWLockExclusive(&g_crossLock);
    const bool changed = f != g_crossFile;
    g_crossFile = f;
    ReleaseSRWLockExclusive(&g_crossLock);
    if (changed) g_crossGen.fetch_add(1);
}

// The update thread: the three sizes again when the picture changed. The old
// handles are left alone (one may be the cursor on screen right now).
void CrosshairTick() {
    static uint32_t built = 0;
    static int builtVrSide = 0;
    const uint32_t gen = g_crossGen.load();
    if (g_set.platform.load() != 0 && !g_set.vrKeyWalk.load()) return;   // flat, or VR F2's mouse look
    // VR: the window's size times [hud_third] crosshair_size
    const int vrSide = g_set.platform.load() == 1
        ? std::clamp((int)lroundf(crosshair::kSizes[CrosshairSizeIndex()].side * g_set.crosshairSize.load() * 0.01f), 8, 256) : 0;
    if (gen == built && vrSide == builtVrSide) return;
    const bool all = gen != built;
    built = gen;
    builtVrSide = vrSide;
    AcquireSRWLockShared(&g_crossLock); std::wstring f = g_crossFile; ReleaseSRWLockShared(&g_crossLock);
    std::wstring full = f;
    if (!f.empty() && f.find(L':') == std::wstring::npos && f.rfind(L"\\\\", 0) != 0) {
        full = OurFilesDir() + L"reshade-shaders\\Textures\\" + f;
    }
    if (vrSide) {
        std::vector<uint8_t> px;
        HCURSOR c = nullptr;
        if (!full.empty() && ReadCrosshairFile(full, vrSide, &px)) c = MakeCrosshair({vrSide, px.data()});
        if (!c) { px = ScaledBuiltin(vrSide); c = MakeCrosshair({vrSide, px.data()}); }
        g_crossVr.store(c);
        LogF("vrcam: VR: crosshair %d px (%.0f%%)", vrSide, g_set.crosshairSize.load());
    }
    if (!all) return;
    bool own = false;
    for (size_t k = 0; k < std::size(crosshair::kSizes); ++k) {
        const int side = crosshair::kSizes[k].side;
        std::vector<uint8_t> px;
        HCURSOR c = nullptr;
        if (!full.empty() && ReadCrosshairFile(full, side, &px)) { c = MakeCrosshair({side, px.data()}); own = c != nullptr; }
        if (!c) c = MakeCrosshair(crosshair::kSizes[k]);
        g_cross[k].store(c);
    }
    if (own) LogF("vrcam: flat: crosshair from '%s'", Utf8(full.c_str()).c_str());
    else if (f.empty()) Log("vrcam: flat: the built-in crosshair");
    else LogF("vrcam: flat: crosshair '%s' NOT readable - the built-in one", Utf8(full.c_str()).c_str());
}

// The size for the game window's height (CrosshairSizeIndex); in VR the one at [hud_third] crosshair_size.
HCURSOR Crosshair() {
    if (g_set.platform.load() == 1)
        if (HCURSOR c = g_crossVr.load()) return c;
    return g_cross[CrosshairSizeIndex()].load();
}

HCURSOR WINAPI HookSetCursor(HCURSOR c) {
    if (c && CrosshairNow())
        if (HCURSOR x = Crosshair()) c = x;
    return OrigSetCursor(c);
}

// Only in flat mode; once (user32 is there from the start).
void HookCursor() {
    if (g_crossHooked.load() || (g_set.platform.load() != 0 && !g_set.vrKeyWalk.load())) return;   // flat, or VR F2's mouse look
    g_crossHooked.store(true);
    void* fn = (void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetCursor");
    const bool ok = fn && MH_CreateHook(fn, (void*)&HookSetCursor, (void**)&OrigSetCursor) == MH_OK && MH_EnableHook(fn) == MH_OK;
    Log(ok ? "vrcam: flat: user32!SetCursor hooked - the crosshair in place of the gauntlet while the mouse looks"
           : "vrcam: flat: user32!SetCursor hook FAILED - the game's own pointer stays");
}

}  // namespace vrc
