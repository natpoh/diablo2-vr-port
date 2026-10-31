#pragma once
// [headset_shot] (vr/headset_shot.h): the pieces of a headset screenshot apart from D3D12 and the files - the file names, a picture
// read back turned into 8-bit BGRA rows for a PNG, the two eyes side by side, which old shots go - so that they can be tested
// (tools/headset_shot_test.cpp).

#include <dxgiformat.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace headshot {

// The time of a shot as it goes into its files' names: 20261030_191502 (local time).
inline std::string Stamp(int year, int month, int day, int hour, int minute, int second) {
    char b[32];
    snprintf(b, sizeof b, "%04d%02d%02d_%02d%02d%02d", year, month, day, hour, minute, second);
    return b;
}

// A picture's part of the name: lower-case letters, digits and '_' only (a layer's slot name may have spaces and brackets).
inline std::string PartName(const char* part) {
    std::string s;
    for (const char* c = part ? part : ""; *c; ++c) {
        const char ch = (*c >= 'A' && *c <= 'Z') ? (char)(*c - 'A' + 'a') : *c;
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) s += ch;
        else if (!s.empty() && s.back() != '_') s += '_';
    }
    while (!s.empty() && s.back() == '_') s.pop_back();
    return s.empty() ? "picture" : s;
}

// headset_shot_<stamp>_<part>.png; the shot's text without a part: headset_shot_<stamp>.txt. A clip's: headset_clip_<stamp>.mp4 / .csv.
inline std::wstring FileName(const std::string& stamp, const char* part, const wchar_t* ext, const char* prefix = "headset_shot_") {
    std::string n = prefix + stamp;
    if (part && *part) n += "_" + PartName(part);
    std::wstring w(n.begin(), n.end());
    return w + ext;
}

// The shot a file in the logs folder belongs to: its stamp ("" for a file that is not a shot's).
inline std::string StampOf(const std::wstring& name, const wchar_t* prefix = L"headset_shot_") {
    const size_t pre = wcslen(prefix);
    if (name.size() < pre + 15 || name.compare(0, pre, prefix) != 0) return {};
    std::string s;
    for (size_t i = pre; i < pre + 15; ++i) {
        const wchar_t c = name[i];
        if (i == pre + 8 ? c != L'_' : (c < L'0' || c > L'9')) return {};
        s += (char)c;
    }
    return s;
}

// Of the shots found (any order, repeats allowed - one stamp per file), the stamps to delete so that the newest `keep` stay.
inline std::vector<std::string> OldShots(std::vector<std::string> stamps, size_t keep) {
    stamps.erase(std::remove(stamps.begin(), stamps.end(), std::string()), stamps.end());
    std::sort(stamps.begin(), stamps.end());
    stamps.erase(std::unique(stamps.begin(), stamps.end()), stamps.end());
    if (stamps.size() <= keep) return {};
    stamps.resize(stamps.size() - keep);
    return stamps;
}

// How a format's pixels are read back: 4 bytes RGBA / BGRA (8 bits each), 4 bytes R10G10B10A2, 8 bytes RGBA half floats. 0 = not read.
enum class Layout { None, Rgba8, Bgra8, Rgb10A2, Rgba16f };
inline Layout LayoutOf(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_UINT:
        return Layout::Rgba8;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return Layout::Bgra8;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_UINT:
        return Layout::Rgb10A2;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return Layout::Rgba16f;
    default:
        return Layout::None;
    }
}
inline uint32_t BytesPerPixel(Layout l) { return l == Layout::Rgba16f ? 8u : l == Layout::None ? 0u : 4u; }
// A read-back row's pitch: D3D12 wants 256-byte rows in a buffer.
inline uint32_t RowPitch(uint32_t width, Layout l) { return (width * BytesPerPixel(l) + 255u) & ~255u; }

// A format whose values the runtime takes as linear light although they are the game's sRGB-encoded picture (an UNORM swapchain fed a
// gamma-encoded back buffer): the headset shows it brighter than the PNG, which keeps the values as they are. Said in the shot's text.
inline bool TakenAsLinear(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_B8G8R8X8_UNORM || f == DXGI_FORMAT_R10G10B10A2_UNORM;
}

inline float HalfToFloat(uint16_t h) {
    const uint32_t s = (uint32_t)(h >> 15) << 31, e = (h >> 10) & 0x1Fu, m = h & 0x3FFu;
    uint32_t bits;
    if (e == 0) {
        if (m == 0) bits = s;
        else {   // subnormal
            float f = std::ldexp((float)m, -24);
            return s ? -f : f;
        }
    } else if (e == 31) bits = s | 0x7F800000u | (m << 13);
    else bits = s | ((e + 112u) << 23) | (m << 13);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}
// Linear light (scRGB: 1 = SDR white) to an sRGB byte, clamped.
inline uint8_t LinearToSrgb8(float v) {
    if (!(v > 0.0f)) return 0;
    if (v >= 1.0f) return 255;
    const float s = v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return (uint8_t)std::lround(std::clamp(s, 0.0f, 1.0f) * 255.0f);
}

// A picture read back (w x h pixels, rows `pitch` bytes apart) as BGRA 8-bit rows, top to bottom, for the PNG. opaque: alpha 255 (an
// eye's picture - the game leaves anything in its alpha; the runtime ignores it for a projection layer). False for an unread format.
inline bool ToBgra8(const uint8_t* src, uint32_t pitch, uint32_t w, uint32_t h, Layout l, bool opaque, std::vector<uint8_t>& out) {
    if (!src || l == Layout::None || !w || !h || pitch < w * BytesPerPixel(l)) return false;
    out.resize((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* s = src + (size_t)y * pitch;
        uint8_t* d = out.data() + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; ++x, d += 4) {
            uint8_t r, g, b, a;
            switch (l) {
            case Layout::Rgba8: r = s[x * 4]; g = s[x * 4 + 1]; b = s[x * 4 + 2]; a = s[x * 4 + 3]; break;
            case Layout::Bgra8: b = s[x * 4]; g = s[x * 4 + 1]; r = s[x * 4 + 2]; a = s[x * 4 + 3]; break;
            case Layout::Rgb10A2: {
                uint32_t v;
                memcpy(&v, s + x * 4, 4);
                r = (uint8_t)(((v & 0x3FFu) * 255u + 511u) / 1023u);
                g = (uint8_t)((((v >> 10) & 0x3FFu) * 255u + 511u) / 1023u);
                b = (uint8_t)((((v >> 20) & 0x3FFu) * 255u + 511u) / 1023u);
                a = (uint8_t)(((v >> 30) * 255u) / 3u);
                break;
            }
            default: {   // Rgba16f: linear light, as the runtime takes a float swapchain
                uint16_t c[4];
                memcpy(c, s + x * 8, 8);
                r = LinearToSrgb8(HalfToFloat(c[0]));
                g = LinearToSrgb8(HalfToFloat(c[1]));
                b = LinearToSrgb8(HalfToFloat(c[2]));
                const float fa = HalfToFloat(c[3]);
                a = (uint8_t)std::lround(std::clamp(std::isfinite(fa) ? fa : 1.0f, 0.0f, 1.0f) * 255.0f);
                break;
            }
            }
            d[0] = b; d[1] = g; d[2] = r; d[3] = opaque ? 255 : a;
        }
    }
    return true;
}

// Two BGRA pictures side by side, the left one first, as high as the higher (the rest black). The width of the whole in *w.
inline std::vector<uint8_t> SideBySide(const std::vector<uint8_t>& left, uint32_t lw, uint32_t lh, const std::vector<uint8_t>& right, uint32_t rw,
                                       uint32_t rh, uint32_t* w, uint32_t* h) {
    const uint32_t W = lw + rw, H = std::max(lh, rh);
    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
    for (uint32_t y = 0; y < lh && left.size() >= (size_t)lw * lh * 4; ++y) memcpy(&out[(size_t)y * W * 4], &left[(size_t)y * lw * 4], (size_t)lw * 4);
    for (uint32_t y = 0; y < rh && right.size() >= (size_t)rw * rh * 4; ++y)
        memcpy(&out[((size_t)y * W + lw) * 4], &right[(size_t)y * rw * 4], (size_t)rw * 4);
    *w = W;
    *h = H;
    return out;
}

// How far apart two eyes' pictures are, to say in the text whether "left" and "right" are really two pictures: the share of pixels that
// differ (0 = the same picture twice - flat in the headset; ~1 = two different pictures). Both BGRA, the same size; every 4th pixel.
inline double DifferentShare(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size() || a.empty()) return -1.0;
    size_t n = 0, differ = 0;
    for (size_t i = 0; i + 3 < a.size(); i += 16, ++n)
        if (std::abs(a[i] - b[i]) > 2 || std::abs(a[i + 1] - b[i + 1]) > 2 || std::abs(a[i + 2] - b[i + 2]) > 2) ++differ;
    return n ? (double)differ / n : -1.0;
}

// -- the 10 s clip ([headset_shot] video, vr/headset_clip.h) ----------------------------------------------------------------------------

// The clip's picture: both eyes side by side, each half the first picture's shape at half its size, at most 960 x 1080 a half (a support
// zip, a light encode). The half's width a multiple of 32 (the whole's rows then 256-byte aligned for the read-back), the height of 16.
inline void ClipSize(uint32_t srcW, uint32_t srcH, uint32_t* halfW, uint32_t* h) {
    const double a = srcW && srcH ? (double)srcW / srcH : 1.0;
    double hw = std::min(960.0, std::max(64.0, srcW / 2.0)), hh = hw / a;
    if (hh > 1080.0) { hh = 1080.0; hw = hh * a; }
    *halfW = std::max(32u, ((uint32_t)std::lround(hw) + 16u) / 32u * 32u);
    *h = std::max(16u, ((uint32_t)std::lround(hh) + 8u) / 16u * 16u);
}

// A picture of srcW x srcH fitted into a cell (cw x ch) with its own shape, in the middle: black bars on two sides. Offsets in the cell.
struct Fit { uint32_t x, y, w, h; };
inline Fit FitInto(uint32_t cw, uint32_t ch, uint32_t srcW, uint32_t srcH) {
    if (!srcW || !srcH || !cw || !ch) return {0, 0, cw, ch};
    const double s = std::min((double)cw / srcW, (double)ch / srcH);
    const uint32_t w = std::min(cw, std::max(1u, (uint32_t)std::lround(srcW * s))), h = std::min(ch, std::max(1u, (uint32_t)std::lround(srcH * s)));
    return {(cw - w) / 2, (ch - h) / 2, w, h};
}

// How a picture of format f is read by the clip's shader: the view's format (raw values: a typeless one as UNORM, a typed one as itself) and
// whether the shader must write the values back as sRGB (a typed sRGB view reads linear light; a float one is linear). UNKNOWN: not read.
struct ViewOf { DXGI_FORMAT view; bool encodeSrgb; };
inline ViewOf ClipView(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return {DXGI_FORMAT_R8G8B8A8_UNORM, false};
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return {DXGI_FORMAT_B8G8R8A8_UNORM, false};
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return {DXGI_FORMAT_B8G8R8X8_UNORM, false};
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return {DXGI_FORMAT_R10G10B10A2_UNORM, false};
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return {DXGI_FORMAT_R16G16B16A16_FLOAT, true};
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_R10G10B10A2_UNORM:
        return {f, false};
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return {f, true};
    default: return {DXGI_FORMAT_UNKNOWN, false};
    }
}
// The typeless format of f's family (the clip's own copy of a picture that may not be read by a shader), UNKNOWN for none.
inline DXGI_FORMAT TypelessOf(DXGI_FORMAT f) {
    switch (LayoutOf(f)) {
    case Layout::Rgba8: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case Layout::Bgra8:
        return (f == DXGI_FORMAT_B8G8R8X8_TYPELESS || f == DXGI_FORMAT_B8G8R8X8_UNORM || f == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB) ? DXGI_FORMAT_B8G8R8X8_TYPELESS
                                                                                                                             : DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case Layout::Rgb10A2: return DXGI_FORMAT_R10G10B10A2_TYPELESS;
    case Layout::Rgba16f: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

// BGRA rows (w x h, both even, rows `pitch` bytes apart) to NV12 (a Y plane w x h, then interleaved U V at half size), BT.709 video range -
// what the H.264 encoder takes, and the clip's type says so (decoders turn it back with the same matrix).
inline void BgraToNv12(const uint8_t* src, uint32_t pitch, uint32_t w, uint32_t h, uint8_t* out) {
    uint8_t* yp = out;
    uint8_t* uv = out + (size_t)w * h;
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* s = src + (size_t)y * pitch;
        for (uint32_t x = 0; x < w; ++x, s += 4)   // Y = 16 + 219 * (0.2126 R + 0.7152 G + 0.0722 B) / 255, in 1/65536ths
            yp[(size_t)y * w + x] = (uint8_t)((65536 * 16 + 11966 * s[2] + 40254 * s[1] + 4064 * s[0] + 32768) >> 16);
    }
    for (uint32_t y = 0; y < h; y += 2)
        for (uint32_t x = 0; x < w; x += 2) {
            int r = 0, g = 0, b = 0;
            for (uint32_t dy = 0; dy < 2; ++dy)
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint8_t* s = src + (size_t)(y + dy) * pitch + (size_t)(x + dx) * 4;
                    b += s[0]; g += s[1]; r += s[2];
                }
            // U = 128 + 224 * (B - Y) / 1.8556 / 255, V = 128 + 224 * (R - Y) / 1.5748 / 255 (sums of four: / 4)
            const int u = (65536 * 128 * 4 - 6596 * r - 22189 * g + 28784 * b + 131072) >> 18;
            const int v = (65536 * 128 * 4 + 28784 * r - 26145 * g - 2639 * b + 131072) >> 18;
            uint8_t* d = uv + (size_t)(y / 2) * w + x;
            d[0] = (uint8_t)std::clamp(u, 0, 255);
            d[1] = (uint8_t)std::clamp(v, 0, 255);
        }
}

}  // namespace headshot
