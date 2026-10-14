#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "hset_paint.h"

namespace hset {
namespace {

constexpr COLORREF kBack = RGB(14, 22, 30), kRowA = RGB(22, 34, 44), kRowB = RGB(18, 28, 37), kButton = RGB(32, 50, 62),
                   kHot = RGB(66, 108, 120), kSelected = RGB(40, 84, 94), kOff = RGB(30, 38, 44), kText = RGB(228, 234, 236),
                   kQuiet = RGB(150, 170, 180), kAccent = RGB(116, 220, 219), kWarn = RGB(241, 207, 139);

}  // namespace

bool Paint(const Model& m, const Hit& hover, std::vector<uint32_t>& rgba, const char* status) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = kW;
    bi.bmiHeader.biHeight = -kH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP bmp = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!dc || !bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return false;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    SetBkMode(dc, TRANSPARENT);
    HFONT normal = CreateFontW(26, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT bold = CreateFontW(30, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT small = CreateFontW(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, normal);

    auto colour = [](Role r) -> COLORREF {
        switch (r) {
            case Role::Back: return kBack;
            case Role::RowA: return kRowA;
            case Role::RowB: return kRowB;
            case Role::Button: return kButton;
            case Role::Hot: return kHot;
            case Role::Selected: return kSelected;
            case Role::Off: return kOff;
            case Role::Quiet: return kQuiet;
            case Role::Accent: return kAccent;
            case Role::Warn: return kWarn;
            case Role::Dim: return RGB(100, 116, 126);
            default: return kText;
        }
    };
    const Scene scene = Compose(m, hover, ComposeOptions{true, false, status});
    for (const Item& it : scene) {
        const COLORREF c = colour(it.colour);
        RECT rc{it.r.x, it.r.y, it.r.x + it.r.w, it.r.y + it.r.h};
        if (it.type == Item::Type::Fill) {
            HBRUSH br = CreateSolidBrush(c);
            FillRect(dc, &rc, br);
            DeleteObject(br);
            continue;
        }
        HBRUSH br = nullptr;
        if (it.type == Item::Type::Button) {
            br = CreateSolidBrush(!it.enabled ? kOff : it.hot ? kHot : it.selected ? kSelected : kButton);
            FillRect(dc, &rc, br);
            DeleteObject(br);
        }
        wchar_t w[256]{};
        MultiByteToWideChar(CP_UTF8, 0, it.text.c_str(), -1, w, 256);
        SelectObject(dc, it.face == Face::Bold ? bold : it.face == Face::Small ? small : normal);
        SetTextColor(dc, it.type == Item::Type::Button && !it.enabled ? RGB(100, 116, 126) : c);
        DrawTextW(dc, w, -1, &rc, DT_NOPREFIX | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | (it.align == Align::Center ? DT_CENTER : it.align == Align::Right ? DT_RIGHT : DT_LEFT));
    }

    GdiFlush();
    rgba.resize((size_t)kW * kH);
    const uint8_t* p = (const uint8_t*)bits;   // BGRA
    for (size_t i = 0; i < rgba.size(); ++i)
        rgba[i] = (uint32_t)p[4 * i + 2] | (uint32_t)p[4 * i + 1] << 8 | (uint32_t)p[4 * i] << 16 | 0xFF000000u;

    SelectObject(dc, oldFont);
    DeleteObject(normal);
    DeleteObject(bold);
    DeleteObject(small);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    return true;
}

}  // namespace hset
