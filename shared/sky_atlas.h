#pragma once
// sky_atlas.h - [sky] game_sky: every live sky dome's picture in ONE texture, each dome's own band of it (Serg on 0.188 / 0.189: loaded in
// act 1 and on to act 2, act 2's dome - placed, shown, its own model - drew act 1's sky; loaded in act 2, every act drew act 2's). By
// then no two domes shared a name, a mesh's bytes, a unit, a model file or a texture file: the game keeps the picture the first dome
// brought (the same UnlitMaterial and its HDRTexture in every one - whatever it keys it by). So every dome names the same picture - this
// atlas - and its own mesh's uv reach only its own band: whichever dome's material the game keeps, each dome draws its own sky.
// D2R_VR_Settings stacks the acts' painted skies (the mod's data: sky_act<1-5>_day_HDR.dds, BC6H, 2048 x 512, all the same) and a
// black band for the cave dome into data/hd/env/texture/d2r_vr/sky/sky_domes_HDR.dds (dome_assets.cpp Prepare); the debug domes keep their
// own. Pure; tested in cleanroom/tests/granny_remesh_test.cpp and dome_assets_test.cpp.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace skyatlas {

constexpr const char* kAtlas = "data/hd/env/texture/d2r_vr/sky/sky_domes_HDR.dds";
// The bands, top to bottom: the domes by their names (dome_assets.cpp kDomes); the acts' from their own pictures, the cave's black
constexpr const char* kBands[] = {"act1", "act2", "act3", "act4", "act5", "cave"};
constexpr int kBandCount = (int)(sizeof kBands / sizeof kBands[0]);
constexpr int kActBands = 5;   // (the first five: the acts' pictures; the rest black)
inline int BandOf(const std::string& dome) {
    for (int i = 0; i < kBandCount; ++i) if (dome == kBands[i]) return i;
    return -1;
}
// A dome's panorama v (0 its top, 1 its bottom) in the atlas: its band, kept kInset of a band off its edges (the next band's never
// filtered in, down to the 3rd mip)
constexpr float kInset = 4.0f / 512.0f;
inline float BandV(int band, float v) { return ((float)band + kInset + v * (1.0f - 2.0f * kInset)) / (float)kBandCount; }
inline void BandRange(int band, float* from, float* to) { *from = BandV(band, 0.0f); *to = BandV(band, 1.0f); }

// ---- the DDS files (a DX10 header: BC6H) ----
constexpr size_t kHeader = 4 + 124 + 20;   // "DDS ", DDS_HEADER, DDS_HEADER_DXT10
constexpr uint32_t kBC6HUF16 = 95, kBC6HSF16 = 96;
struct Info { uint32_t width = 0, height = 0, mips = 0, format = 0; };
inline uint32_t U32(const std::string& s, size_t at) { uint32_t v = 0; if (at + 4 <= s.size()) std::memcpy(&v, s.data() + at, 4); return v; }
inline void Put32(std::string* s, size_t at, uint32_t v) { std::memcpy(&(*s)[at], &v, 4); }
inline bool Read(const std::string& dds, Info* out) {
    if (dds.size() < kHeader || dds.compare(0, 4, "DDS ") != 0 || dds.compare(84, 4, "DX10") != 0) return false;
    out->height = U32(dds, 12);
    out->width = U32(dds, 16);
    out->mips = U32(dds, 28) ? U32(dds, 28) : 1;
    out->format = U32(dds, 128);
    return out->width && out->height && (out->format == kBC6HUF16 || out->format == kBC6HSF16) && U32(dds, 140) <= 1;   // (one array slice)
}
// One mip's bytes: 16 a 4 x 4 block
inline size_t MipBytes(uint32_t width, uint32_t height, uint32_t mip) {
    const uint32_t w = width >> mip ? width >> mip : 1, h = height >> mip ? height >> mip : 1;
    return (size_t)((w + 3) / 4) * ((h + 3) / 4) * 16;
}
// The atlas' mips: every band at least a block (4 rows) high at each
inline uint32_t AtlasMips(const Info& band) {
    uint32_t m = 0;
    while (m < band.mips && (band.height >> m) >= 4 && (band.width >> m) >= 4) ++m;
    return m;
}

// The acts' pictures (kActBands of them, the same size and format) stacked into the atlas, then the black bands (all-zero BC6H blocks:
// mode 0, every endpoint 0 - black). False and `why` when one is missing or not like the others.
inline bool Make(const std::vector<std::string>& acts, std::string* out, std::string* why) {
    out->clear();
    if ((int)acts.size() != kActBands) { *why = "not five acts' pictures"; return false; }
    Info first;
    for (int i = 0; i < kActBands; ++i) {
        Info in;
        if (!Read(acts[i], &in)) { *why = "act " + std::to_string(i + 1) + "'s picture is not a BC6H DDS"; return false; }
        if (i == 0) first = in;
        if (in.width != first.width || in.height != first.height || in.format != first.format || in.mips != first.mips) {
            *why = "act " + std::to_string(i + 1) + "'s picture is not the size of act 1's";
            return false;
        }
        size_t need = kHeader;
        for (uint32_t m = 0; m < in.mips; ++m) need += MipBytes(in.width, in.height, m);
        if (acts[i].size() < need) { *why = "act " + std::to_string(i + 1) + "'s picture is cut short"; return false; }
    }
    if (first.height % 4 || first.width % 4) { *why = "the pictures are not whole blocks"; return false; }
    const uint32_t mips = AtlasMips(first), height = first.height * kBandCount;
    *out = acts[0].substr(0, kHeader);
    Put32(out, 12, height);
    Put32(out, 20, (uint32_t)MipBytes(first.width, height, 0));   // (the top mip's size: DDSD_LINEARSIZE)
    Put32(out, 28, mips);
    for (uint32_t m = 0; m < mips; ++m) {
        size_t at = kHeader;   // this mip in a band's own file
        for (uint32_t k = 0; k < m; ++k) at += MipBytes(first.width, first.height, k);
        const size_t n = MipBytes(first.width, first.height, m);
        for (int b = 0; b < kBandCount; ++b) {
            if (b < kActBands) out->append(acts[b], at, n);
            else out->append(n, '\0');
        }
    }
    return true;
}

}  // namespace skyatlas
