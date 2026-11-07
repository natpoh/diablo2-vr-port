// The banner in the headset (vr/banner.h: the capture key's "D2R VR capture saved ..." or why it failed) - a panel hung before the face, a little
// above the eyes' line, level, turning with the head's yaw only (as the T-pose prompt), for the message's few seconds.
//
// NOT a header of its own: xr.cpp includes this file once, inside namespace xr { namespace {, after xr_ally_portraits.h, because it uses what is
// there and in xr.cpp's unnamed namespace (QuadAtlas, the head's pose, the room's space). Painted with GDI when the message changes; its
// swapchain given back once unused (FreeIdle).

constexpr uint32_t kBannerW = 1024, kBannerH = 128;
constexpr float kBannerWidthM = 0.90f, kBannerDistanceM = 1.3f, kBannerUpM = 0.18f;
QuadAtlas g_bannerAtlas;
std::vector<uint32_t> g_bannerPx;
uint32_t g_bannerPainted = 0, g_bannerUp = 0;   // the message's serial painted / in the swapchain
XrCompositionLayerQuad g_bannerLayer;

// The message into g_bannerPx: premultiplied RGBA, R in the low byte, opaque (a dark panel, the text green or orange).
bool PaintBanner(const banner::Message& m) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = (LONG)kBannerW;
    bi.bmiHeader.biHeight = -(LONG)kBannerH;   // top row first
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!dc || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return false;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    RECT all{0, 0, (LONG)kBannerW, (LONG)kBannerH};
    HBRUSH back = CreateSolidBrush(RGB(14, 18, 24));
    FillRect(dc, &all, back);
    DeleteObject(back);
    HBRUSH edge = CreateSolidBrush(m.ok ? RGB(90, 200, 120) : RGB(240, 150, 60));
    FrameRect(dc, &all, edge);
    RECT in2{2, 2, (LONG)kBannerW - 2, (LONG)kBannerH - 2};
    FrameRect(dc, &in2, edge);
    DeleteObject(edge);
    HFONT font = CreateFontW(-46, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, m.ok ? RGB(150, 240, 170) : RGB(255, 190, 110));
    std::wstring w(m.text.begin(), m.text.end());   // (ASCII words: names and numbers)
    RECT tr{24, 0, (LONG)kBannerW - 24, (LONG)kBannerH};
    DrawTextW(dc, w.c_str(), (int)w.size(), &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    GdiFlush();
    g_bannerPx.resize((size_t)kBannerW * kBannerH);
    const uint32_t* src = static_cast<const uint32_t*>(bits);
    for (size_t i = 0; i < g_bannerPx.size(); ++i) {   // GDI's BGRX -> RGBA with R low, alpha 255
        const uint32_t p = src[i];
        g_bannerPx[i] = 0xFF000000u | ((p & 0xFFu) << 16) | (p & 0xFF00u) | ((p >> 16) & 0xFFu);
    }
    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    return true;
}

uint32_t HangBanner(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max) {
    banner::Message m;
    if (!banner::Now(GetTickCount64(), &m)) return nl;   // none: one relaxed load
    const D2RVR_XrPose& head = g_inNow.head;
    if (nl >= max || head.valid != 1) return nl;
    if (g_bannerPainted != m.serial) {
        if (!PaintBanner(m)) return nl;
        g_bannerPainted = m.serial;
    }
    if (g_bannerUp != m.serial || !g_bannerAtlas.shown) {
        if (!QuadAtlasUpload(g_bannerAtlas, kBannerW, kBannerH, g_bannerPx, "banner")) return nl;
        g_bannerUp = m.serial;
    }
    g_bannerAtlas.usedAt = GetTickCount64();
    const XrQuaternionf face = Yaw(YawOf(Q4(head.rot)));
    const XrVector3f at = Plus(P3(head.pos), Rot(face, {0.0f, kBannerUpM, -kBannerDistanceM}));
    XrCompositionLayerQuad& q = g_bannerLayer;
    q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    q.space = g_roomSpace ? g_roomSpace : g_space;   // where the head was located
    q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    q.subImage.swapchain = g_bannerAtlas.sc;
    q.subImage.imageRect = {{0, 0}, {(int32_t)kBannerW, (int32_t)kBannerH}};
    q.pose = {face, at};
    q.size = {kBannerWidthM, kBannerWidthM * (float)kBannerH / (float)kBannerW};
    layers[nl++] = (const XrCompositionLayerBaseHeader*)&q;
    return nl;
}

void BannerTeardown() {   // under g_lock or before the session exists
    QuadAtlasDestroy(g_bannerAtlas);
    g_bannerPx.clear();
    g_bannerPx.shrink_to_fit();
    g_bannerPainted = g_bannerUp = 0;
}
