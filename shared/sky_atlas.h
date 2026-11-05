#pragma once
// sky_atlas.h - [sky] game_sky: every live sky dome's picture in ONE texture, each dome's own band of it (Serg on 0.188 / 0.189: loaded in
// act 1 and on to act 2, act 2's dome - placed, shown, its own model - drew act 1's sky; loaded in act 2, every act drew act 2's). By
// then no two domes shared a name, a mesh's bytes, a unit, a model file or a texture file: the game keeps the picture the first dome
// brought (the same UnlitMaterial and its HDRTexture in every one - whatever it keys it by). So every dome names the same picture - this
// atlas - and its own mesh's uv reach only its own band: whichever dome's material the game keeps, each dome draws its own sky.
// D2R_VR_Settings stacks the acts' painted skies (the mod's data: sky_act<1-5>_day_HDR.dds, BC6H, 2048 x 512, all the same) and a
// black band for the cave dome into data/hd/env/texture/d2r_vr/sky/sky_domes_HDR.dds (dome_assets.cpp Prepare); the debug domes keep their
// own. Pure; tested in cleanroom/tests/granny_remesh_test.cpp and dome_assets_test.cpp.
#include <cmath>
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

// ---- a band's picture with its zenith cap (2026-11-05) ----
// Serg: "почему на старом типе отрисовки небо нормальное а на куполе оно с треугольниками" - at the zenith the dome showed a fan of streaks.
// The panorama lays the sky by (azimuth, elevation), and its top row is ONE point of the sky: the dome's last ring closed on it, one
// triangle a column, each a thin wedge of the zenith picture squeezed round the pole (and the graphics card's mip there the coarsest).
// Our old sky (D2R_DepthFog.fx PaintedSkyAt) takes its zenith picture by the view ray - dir.xz / dir.y * 0.4, one tile of it per 2.5 of
// that - and round the pole those coordinates are as smooth as anywhere. So a picture made by tools/game_sky/make_sky_dome.py with
// HasCap's mark holds, in the top quarter of its rows and as many columns (a square: the picture is four times as wide as high), the sky
// straight overhead laid exactly so: q = dir.xz / dir.y from -kCapHalf to +kCapHalf - at the shipped 4096 x 1024 that square is 256
// texels for 0.25 of the 1024-texel zenith picture, texel for texel what the old sky sampled. The dome above CapEl() is a disc with those
// coordinates as its uv (shared/granny_remesh.h DomeMesh), the panorama below it (PanoV: its rows from kCapRows down, CapEl() + 1 degree to
// -12 - its first rows a little above the disc's edge, so the cap's square is never filtered into the band's top ring). Pure.
constexpr double kPanoLowEl = -12.0;                   // the panorama's bottom (make_sky_dome.py LOW_EL)
constexpr double kCapRows = 0.25;                      // of a band's picture: the cap's square's rows ...
constexpr double kCapCols = kCapRows / 4.0;            // ... and columns (the picture four times as wide as high)
constexpr double kCapHalf = 256.0 / (2.0 * 0.4 * 1024.0);   // its half side in dir.xz / dir.y: 256 texels for 0.25 of the 1024-texel zenith picture
constexpr double kCapDisc = kCapHalf * (1.0 - 2.0 * 8.0 / 256.0);   // the disc's radius: 8 texels inside the square all round (its mips' margin)
constexpr size_t kCapMarkAt = 32;                      // the DDS header's dwReserved1 (the format leaves it to tools)
// "cap2" (2026-11-05, Serg on 0.202.2: a circle at the disc's edge, the clouds inside it "a different tone" and not carrying on the ones
// outside): the dome's uv (BandV) reach a band's picture kInset off its top and bottom, but "cap1" pictures were laid by the band's v
// straight over all their rows - the square drawn up to 8 texels off along z and the panorama 4 rows (0.45 degrees) low, so the two
// sides of the rim showed the sky from different places. A "cap2" picture is laid in the atlas' own frame: its row at PictureV(the band's
// v). The mesh is the same; a "cap1" picture is not taken as a capped one (the old dome) rather than drawn with the seam.
constexpr char kCapMark[8] = {'D', '2', 'R', 'V', 'c', 'a', 'p', '2'};
inline bool HasCap(const std::string& dds) { return dds.size() >= kCapMarkAt + 8 && std::memcmp(dds.data() + kCapMarkAt, kCapMark, 8) == 0; }
inline double CapEl() { return 90.0 - std::atan(kCapDisc) * 57.29577951308232; }   // the disc's edge, degrees (73.68)
inline double PanoTopEl() { return CapEl() + 1.0; }
// A band's v (the dome's uv: PanoV, CapUv) as the row of its picture the atlas puts there (BandV: kInset off the band's edges), and back
inline double PictureV(double bandV) { return kInset + bandV * (1.0 - 2.0 * kInset); }
inline double BandVOfPicture(double pictureV) { return (pictureV - kInset) / (1.0 - 2.0 * kInset); }
// The panorama's v (0 the picture's top, 1 its bottom) at an elevation, and back
inline double PanoV(double el) { return kCapRows + (PanoTopEl() - el) / (PanoTopEl() - kPanoLowEl) * (1.0 - kCapRows); }
inline double PanoEl(double v) { return PanoTopEl() - (v - kCapRows) / (1.0 - kCapRows) * (PanoTopEl() - kPanoLowEl); }
// The cap's uv in the picture for q = dir.xz / dir.y, and back
inline void CapUv(double qx, double qz, double* u, double* v) {
    *u = (qx / kCapHalf + 1.0) * 0.5 * kCapCols;
    *v = (qz / kCapHalf + 1.0) * 0.5 * kCapRows;
}
inline void CapQ(double u, double v, double* qx, double* qz) {
    *qx = (u / kCapCols * 2.0 - 1.0) * kCapHalf;
    *qz = (v / kCapRows * 2.0 - 1.0) * kCapHalf;
}
inline bool InCap(double u, double v) { return u < kCapCols && v < kCapRows; }

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
// The atlas as the game's graphics card has it (vrcam's probe of the game's view of it, vr/native_d3d12.cpp): a BC6H 2D texture (DXGI 94
// typeless, 95 UF16, 96 SF16) as high as kBandCount bands of a quarter of its width (the acts' 2048 x 512 pictures: 2048 x 3072)
inline bool IsAtlasTexture(uint64_t width, uint32_t height, uint32_t dxgiFormat) {
    return (dxgiFormat == 94 || dxgiFormat == kBC6HUF16 || dxgiFormat == kBC6HSF16) && width >= 4 && width % 4 == 0 &&
           (uint64_t)height == width / 4 * kBandCount;
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
