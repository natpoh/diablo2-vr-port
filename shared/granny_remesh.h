// granny_remesh.h - [sky] game_sky's dome made from the game's own sky model: the Arreat Summit's skybox card (a Granny 2 file the game
// takes - PC1 2026-10-27, [debug] game_sky_test 2: loaded, drawn), its mesh's vertices and triangles replaced by our dome's, its texture
// by our sky's. Every other byte - its type tree, its material, its layout in eight sections - stays the game's: our own .model files of
// the same type tree were never taken (no ModelComponent made, 5 placings, PC1 11:43 and 11:51). Pure C++ (cleanroom/tests/granny_remesh_test).
//
// The file (as gr2dump reads it, tools/game_sky): a 32-byte magic, a 72-byte header, a 44-byte header for each section, the sections' data,
// then their fixups (each pointer in a section: where it is, the section and offset it points at; stored as 0 in the data). The game's
// sections are BitKnit2-compressed (compression 4): read with pybg3's decoder (third_party/pybg3/rans.h, MIT); written back as BitKnit2
// streams of raw quanta (each 64 KiB quantum a NUL word and the bytes as they are - the format's own way to store a quantum that does not
// compress), so the section headers keep the game's kind of compression. The marshalling tables (byte swapping for another endianness) are
// left out: the file is little-endian, as the game is.
//
// The edits, found by the file's own type tree (by member name, never by a fixed offset):
//   Meshes[0].PrimaryVertexData.Vertices     our vertices (the same vertex type: int16 x4 position, int8 x4 normal and tangent, half x2 uv)
//   the mesh's topology (and TriTopologies') one group of all our triangles, Indices16 ours
//   the mesh's BoneBindings[*].OBBMin / OBBMax, ExtendedData bbMin / bbMax / VertexScale: our dome's
//   every skeleton's bones: an identity transform (the card's bone scales it by 1.70)
//   Models[0].ExtendedData.lodLevel          the LOD the file is written for
//   every pointer to the card's texture name  to ours
//
// [ceiling] game_ceiling's slab is made the same way from a lit model of the game's (act 1's cave rock, StandardMaterial: its albedo,
// normal and ORM maps - one mesh, the card's vertex type, one group): CeilingMesh in it, its three maps renamed to the floor's own
// tiling rock.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#if defined(_MSC_VER) && !defined(__clang__)
#pragma push_macro("__attribute__")
#pragma push_macro("__builtin_unreachable")
#pragma push_macro("min")
#pragma push_macro("max")
#undef min   // (windows.h's macros: rans.h calls std::min)
#undef max
#define __attribute__(x)
#define __builtin_unreachable() __assume(0)
#include "rans.h"
#pragma pop_macro("max")
#pragma pop_macro("min")
#pragma pop_macro("__builtin_unreachable")
#pragma pop_macro("__attribute__")
#else
#include "rans.h"
#endif

namespace grannyremesh {

#pragma pack(push, 1)
struct SecPtr { uint32_t section, offset; };
struct Fixup { uint32_t at; SecPtr ptr; };
struct Header { uint32_t version, fileSize, crc, sectionTable, numSections; SecPtr rootType, rootObj; uint32_t typeTag, extra[4], stringsCrc, reserved[3]; };
struct SecHeader { uint32_t compression, offset, compressedLen, uncompressedLen, alignment, first16, first8, fixupsOffset, numFixups, marshalOffset, numMarshal; };
#pragma pack(pop)
static_assert(sizeof(Header) == 72 && sizeof(SecHeader) == 44 && sizeof(Fixup) == 12, "the Granny file's layout");

constexpr uint32_t kMagicBytes = 32, kHeaderAt = 32, kSectionsAt = 32 + 72;
constexpr uint32_t kBitKnit2 = 4;

struct File {
    uint8_t magic[kMagicBytes] = {};
    Header h{};
    std::vector<SecHeader> sh;
    std::vector<std::vector<uint8_t>> sec;    // decompressed
    std::vector<std::vector<Fixup>> fix;
};

// ---- reading -------------------------------------------------------------------------------------------------------------------

inline bool Unknit(uint8_t* out, size_t outLen, const uint8_t* in, size_t inLen) {
    if (!outLen) return true;
    std::vector<uint16_t> words(inLen / 2 + 2, 0);
    std::memcpy(words.data(), in, inLen);
    try {
        rans::bitknit2_state st(out, outLen);
        return st.decode(words.data(), inLen);
    } catch (...) {
        return false;
    }
}

inline bool Read(const std::string& b, File* f, std::string* why) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    *f = File{};
    if (b.size() < kSectionsAt) return no("too short for a Granny file");
    std::memcpy(f->magic, b.data(), kMagicBytes);
    std::memcpy(&f->h, b.data() + kHeaderAt, sizeof f->h);
    if (f->h.version != 7 || f->h.numSections == 0 || f->h.numSections > 32) return no("not a Granny 2 file of version 7");
    const size_t table = kHeaderAt + f->h.sectionTable;
    if (table + (size_t)f->h.numSections * sizeof(SecHeader) > b.size()) return no("its section table is cut short");
    f->sh.resize(f->h.numSections);
    std::memcpy(f->sh.data(), b.data() + table, f->h.numSections * sizeof(SecHeader));
    f->sec.resize(f->h.numSections);
    f->fix.resize(f->h.numSections);
    for (uint32_t i = 0; i < f->h.numSections; ++i) {
        const SecHeader& s = f->sh[i];
        if (s.uncompressedLen > (64u << 20) || (size_t)s.offset + s.compressedLen > b.size()) return no("a section lies outside the file");
        f->sec[i].assign(s.uncompressedLen, 0);
        if (s.compression == 0) {
            if ((size_t)s.offset + s.uncompressedLen > b.size()) return no("a section lies outside the file");
            std::memcpy(f->sec[i].data(), b.data() + s.offset, s.uncompressedLen);
        } else if (s.compression == kBitKnit2) {
            if (!Unknit(f->sec[i].data(), s.uncompressedLen, (const uint8_t*)b.data() + s.offset, s.compressedLen)) return no("a section did not decompress (BitKnit2)");
        } else {
            return no("a section's compression is not BitKnit2 or none");
        }
        f->fix[i].resize(s.numFixups);
        if (!s.numFixups) continue;
        const size_t bytes = (size_t)s.numFixups * sizeof(Fixup);
        if (s.compression == kBitKnit2) {
            if ((size_t)s.fixupsOffset + 4 > b.size()) return no("a section's fixups lie outside the file");
            uint32_t clen = 0;
            std::memcpy(&clen, b.data() + s.fixupsOffset, 4);
            if ((size_t)s.fixupsOffset + 4 + clen > b.size() ||
                !Unknit((uint8_t*)f->fix[i].data(), bytes, (const uint8_t*)b.data() + s.fixupsOffset + 4, clen))
                return no("a section's fixups did not decompress");
        } else {
            if ((size_t)s.fixupsOffset + bytes > b.size()) return no("a section's fixups lie outside the file");
            std::memcpy(f->fix[i].data(), b.data() + s.fixupsOffset, bytes);
        }
        for (const Fixup& x : f->fix[i])
            if (x.at + 8 > f->sec[i].size() || x.ptr.section >= f->h.numSections) return no("a fixup points outside the file");
    }
    if (f->h.rootType.section >= f->h.numSections || f->h.rootObj.section >= f->h.numSections) return no("its root is outside the file");
    return true;
}

// ---- walking the file by its own types --------------------------------------------------------------------------------------------

struct Addr {
    uint32_t s = ~0u, o = 0;
    bool ok() const { return s != ~0u; }
    Addr at(uint32_t d) const { return Addr{s, o + d}; }
    bool operator==(const Addr& b) const { return s == b.s && o == b.o; }
};

class Walk {
  public:
    explicit Walk(File& f) : f_(f) { Index(); }
    void Index() {
        map_.assign(f_.fix.size(), {});
        for (size_t i = 0; i < f_.fix.size(); ++i)
            for (size_t k = 0; k < f_.fix[i].size(); ++k) map_[i][f_.fix[i][k].at] = k;
    }
    bool In(Addr a, uint32_t n) const { return a.ok() && a.s < f_.sec.size() && (uint64_t)a.o + n <= f_.sec[a.s].size(); }
    template <class T> T Get(Addr a) const { T v{}; if (In(a, sizeof v)) std::memcpy(&v, f_.sec[a.s].data() + a.o, sizeof v); return v; }
    template <class T> bool Put(Addr a, const T& v) { if (!In(a, sizeof v)) return false; std::memcpy(f_.sec[a.s].data() + a.o, &v, sizeof v); return true; }
    Addr Ptr(Addr a) const {
        if (!a.ok() || a.s >= map_.size()) return {};
        const auto it = map_[a.s].find(a.o);
        if (it == map_[a.s].end()) return {};
        const Fixup& x = f_.fix[a.s][it->second];
        return Addr{x.ptr.section, x.ptr.offset};
    }
    // a pointer at `a` set to point at `to` (its fixup changed, or one added)
    void SetPtr(Addr a, Addr to) {
        const auto it = map_[a.s].find(a.o);
        if (it != map_[a.s].end()) { f_.fix[a.s][it->second].ptr = SecPtr{to.s, to.o}; return; }
        f_.fix[a.s].push_back(Fixup{a.o, SecPtr{to.s, to.o}});
        map_[a.s][a.o] = f_.fix[a.s].size() - 1;
        const uint64_t zero = 0;
        Put(a, zero);
    }
    std::string Str(Addr a) const {
        std::string s;
        if (!a.ok() || a.s >= f_.sec.size()) return s;
        for (uint32_t o = a.o; o < f_.sec[a.s].size() && f_.sec[a.s][o]; ++o) s.push_back((char)f_.sec[a.s][o]);
        return s;
    }
    // bytes appended to a section (aligned), their place
    Addr Append(uint32_t s, const void* p, size_t n, uint32_t align = 8) {
        std::vector<uint8_t>& v = f_.sec[s];
        while (v.size() % align) v.push_back(0);
        const Addr at{s, (uint32_t)v.size()};
        v.insert(v.end(), (const uint8_t*)p, (const uint8_t*)p + n);
        return at;
    }

    // A type entry (44 bytes): its kind, name, the struct it refers to, its width (array count)
    int Kind(Addr t) const { return Get<int32_t>(t); }
    std::string Name(Addr t) const { return Str(Ptr(t.at(4))); }
    Addr Ref(Addr t) const { return Ptr(t.at(12)); }
    int Width(Addr t) const { return Get<int32_t>(t.at(20)); }
    int Bytes(Addr t, int depth = 0) const {
        const int w = Width(t) ? Width(t) : 1;
        switch (Kind(t)) {
        case 1: return StructBytes(Ref(t), depth + 1) * w;
        case 2: case 8: case 22: return 8 * w;
        case 3: case 4: return 12 * w;
        case 5: return 16 * w;
        case 7: return 20 * w;
        case 9: return 68 * w;
        case 10: case 19: case 20: return 4 * w;
        case 11: case 12: case 13: case 14: return 1 * w;
        case 15: case 16: case 17: case 18: case 21: return 2 * w;
        default: return 0;
        }
    }
    int StructBytes(Addr t, int depth = 0) const {
        int n = 0;
        for (int k = 0; depth < 32 && In(t, 44) && Kind(t) != 0 && k < 256; ++k, t = t.at(44)) n += Bytes(t, depth);
        return n;
    }
    // A member of a struct of type `t` at `obj`: its place, and its type entry (`type`); not found: !ok()
    Addr Member(Addr t, Addr obj, const char* name, Addr* type = nullptr) const {
        uint32_t off = 0;
        for (int k = 0; In(t, 44) && Kind(t) != 0 && k < 256; ++k, t = t.at(44)) {
            if (Name(t) == name) { if (type) *type = t; return obj.at(off); }
            off += (uint32_t)Bytes(t);
        }
        return {};
    }

  private:
    File& f_;
    std::vector<std::map<uint32_t, size_t>> map_;   // a section's fixups by where they are
};

// ---- writing --------------------------------------------------------------------------------------------------------------------

inline uint32_t Crc32(const uint8_t* p, size_t n) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// A BitKnit2 stream of raw quanta: the magic, then each 64 KiB of the data as a NUL word and the bytes themselves (an odd last byte padded)
inline std::vector<uint8_t> KnitRaw(const uint8_t* p, size_t n) {
    std::vector<uint8_t> out;
    out.reserve(n + n / 32768 + 8);
    auto word = [&](uint16_t w) { out.push_back((uint8_t)(w & 0xFF)); out.push_back((uint8_t)(w >> 8)); };
    word(LIBBG3_BITKNIT2_MAGIC);
    for (size_t at = 0; at < n; at += 0x10000) {
        const size_t len = n - at < 0x10000 ? n - at : 0x10000;
        word(0);
        out.insert(out.end(), p + at, p + at + len);
        if (len & 1) out.push_back(0);
    }
    return out;
}

// The file as the game's are laid out: its magic, the header, the section table, every section's data, then every section's fixups;
// no marshalling. Uncompressed (the default): every section stored as it is, compression 0 - what Granny reads with no decoder at all.
// The first build wrote BitKnit2 sections of raw quanta where the game had them; with those ten files in the mod the game would not
// enter a game ("Unable to enter game, generic bad file", PC1 2026-10-27 12:40) - a raw quantum may be pybg3's reading, not what the
// game's own decoder takes. `knitRaw` writes them so again (tests only).
inline std::string Write(const File& f, bool knitRaw = false) {
    const uint32_t n = (uint32_t)f.sec.size();
    std::vector<SecHeader> sh(f.sh.begin(), f.sh.end());
    std::vector<std::vector<uint8_t>> data(n), fixups(n);
    for (uint32_t i = 0; i < n; ++i) {
        if (!knitRaw) sh[i].compression = 0;
        const bool knit = sh[i].compression == kBitKnit2;
        data[i] = knit && !f.sec[i].empty() ? KnitRaw(f.sec[i].data(), f.sec[i].size()) : f.sec[i];   // (an empty one stays empty, as the game's)
        const uint8_t* fp = (const uint8_t*)f.fix[i].data();
        const size_t fb = f.fix[i].size() * sizeof(Fixup);
        if (!f.fix[i].empty()) {
            if (knit) {
                const std::vector<uint8_t> k = KnitRaw(fp, fb);
                const uint32_t clen = (uint32_t)k.size();
                fixups[i].resize(4);
                std::memcpy(fixups[i].data(), &clen, 4);
                fixups[i].insert(fixups[i].end(), k.begin(), k.end());
            } else {
                fixups[i].assign(fp, fp + fb);
            }
        }
    }
    uint32_t at = kSectionsAt + n * (uint32_t)sizeof(SecHeader);
    std::string out(at, '\0');
    for (uint32_t i = 0; i < n; ++i) {
        while (at % 4) { out.push_back('\0'); ++at; }
        sh[i].offset = at;
        sh[i].compressedLen = (uint32_t)data[i].size();
        sh[i].uncompressedLen = (uint32_t)f.sec[i].size();
        if (sh[i].first16 > sh[i].uncompressedLen) sh[i].first16 = sh[i].uncompressedLen;
        if (sh[i].first8 > sh[i].uncompressedLen) sh[i].first8 = sh[i].uncompressedLen;
        out.append((const char*)data[i].data(), data[i].size());
        at += (uint32_t)data[i].size();
    }
    for (uint32_t i = 0; i < n; ++i) {
        while (at % 4) { out.push_back('\0'); ++at; }
        sh[i].fixupsOffset = at;
        sh[i].numFixups = (uint32_t)f.fix[i].size();
        out.append((const char*)fixups[i].data(), fixups[i].size());
        at += (uint32_t)fixups[i].size();
        sh[i].marshalOffset = at;
        sh[i].numMarshal = 0;
    }
    Header h = f.h;
    h.numSections = n;
    h.sectionTable = kSectionsAt - kHeaderAt;
    h.fileSize = (uint32_t)out.size();
    std::memcpy(&out[0], f.magic, kMagicBytes);
    const uint32_t headerSize = kSectionsAt + n * (uint32_t)sizeof(SecHeader);
    std::memcpy(&out[16], &headerSize, 4);   // (the magic's header size: up to the end of the section table)
    std::memcpy(&out[kSectionsAt], sh.data(), n * sizeof(SecHeader));
    h.crc = 0;
    std::memcpy(&out[kHeaderAt], &h, sizeof h);
    h.crc = Crc32((const uint8_t*)out.data() + kSectionsAt, out.size() - kSectionsAt);   // (the game's rule: from +104 to the end)
    std::memcpy(&out[kHeaderAt], &h, sizeof h);
    return out;
}

// ---- our dome -------------------------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
struct Vertex { int16_t pos[4]; int8_t normal[4]; int8_t tangent[4]; uint16_t uv[2]; };   // the card's vertex type (20 bytes)
#pragma pack(pop)
static_assert(sizeof(Vertex) == 20, "the vertex");

struct Mesh {
    std::vector<Vertex> v;
    std::vector<uint16_t> idx;   // triangles, both windings (seen from inside whatever the cull mode)
    float scale = 0.0f;          // VertexScale: a position is int16 / 32767 times it
    float mn[3] = {}, mx[3] = {};
};

inline uint16_t Half(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint32_t s = (x >> 16) & 0x8000u;
    int e = (int)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t m = x & 0x7FFFFFu;
    if (e <= 0) {
        if (e < -10) return (uint16_t)s;
        m = (m | 0x800000u) >> (1 - e);
        return (uint16_t)(s | ((m + 0x1000u) >> 13));
    }
    if (e >= 31) return (uint16_t)(s | 0x7C00u);
    const uint32_t r = (uint32_t)(e << 10) | (m >> 13);
    return (uint16_t)(s + r + ((m >> 12) & 1));   // (round half up; a carry runs into the exponent as it should)
}
inline int8_t Snorm8(float v) { v = v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v; return (int8_t)std::lround(v * 127.0f); }

constexpr float kLowEl = -12.0f, kHighEl = 90.0f;   // the panorama's and the dome's bottom and top, degrees (make_sky_dome.py)
constexpr int kSegments = 64;
constexpr float kRings[] = {kLowEl, -6, 0, 4, 8, 13, 18, 24, 30, 37, 44, 52, 60, 68, 76, 83, kHighEl};

// The dome of make_sky_dome.py: a sphere's upper part round the origin of `radius`, its uv the panorama's (u round, v from the top)
inline Mesh DomeMesh(float radius) {
    Mesh m;
    m.scale = radius;
    const int rings = (int)(sizeof kRings / sizeof kRings[0]), row = kSegments + 1;
    const int16_t w = (int16_t)std::lround(std::log2(radius) / 13.0 * 32767.0);   // (the game's own: 326.123 -> 21045)
    for (int k = 0; k < 3; ++k) { m.mn[k] = 1e30f; m.mx[k] = -1e30f; }
    for (int i = 0; i < rings; ++i) {
        const double e = kRings[i] * 3.14159265358979 / 180.0;
        for (int j = 0; j <= kSegments; ++j) {
            const double u = (double)j / kSegments, a = u * 2.0 * 3.14159265358979;
            const double d[3] = {std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)};
            Vertex v{};
            for (int k = 0; k < 3; ++k) {
                v.pos[k] = (int16_t)std::lround(d[k] * 32767.0);
                v.normal[k] = Snorm8((float)-d[k]);   // inward
                const float p = (float)(d[k] * radius);
                if (p < m.mn[k]) m.mn[k] = p;
                if (p > m.mx[k]) m.mx[k] = p;
            }
            v.pos[3] = w;
            v.tangent[0] = Snorm8((float)std::cos(a)); v.tangent[1] = 0; v.tangent[2] = Snorm8((float)-std::sin(a)); v.tangent[3] = -127;
            v.uv[0] = Half((float)u);
            v.uv[1] = Half((kHighEl - kRings[i]) / (kHighEl - kLowEl));
            m.v.push_back(v);
        }
    }
    for (int i = 0; i + 1 < rings; ++i)
        for (int j = 0; j < kSegments; ++j) {
            const uint16_t a = (uint16_t)(i * row + j), b = (uint16_t)(i * row + j + 1), c = (uint16_t)((i + 1) * row + j), d = (uint16_t)((i + 1) * row + j + 1);
            const bool zenith = i + 1 == rings - 1;   // (one point: one triangle a column)
            const uint16_t tris[2][3] = {{a, b, c}, {b, d, c}};
            for (int t = 0; t < (zenith ? 1 : 2); ++t) {
                m.idx.insert(m.idx.end(), {tris[t][0], tris[t][1], tris[t][2]});
                m.idx.insert(m.idx.end(), {tris[t][0], tris[t][2], tris[t][1]});   // and the other way round
            }
        }
    return m;
}

// ---- our cave ceiling ([ceiling] game_ceiling, vr/game_ceiling.h lays it) ------------------------------------------------------------

// The rock's underside in a tile, u and v in tiles: 0 (flat at the top) .. 1 (hanging down the most). Whole frequencies only, so every
// tile is the same and a slab moved by whole tiles (the hero walking) shows no step.
inline double CeilingRelief(double u, double v) {
    constexpr double k2pi = 6.28318530717958647692;
    const double h = 0.35 * std::sin(k2pi * (u + 0.13)) * std::cos(k2pi * (v - 0.21)) + 0.25 * std::sin(k2pi * (2.0 * u + v) + 1.3) +
                     0.15 * std::cos(k2pi * (u - 3.0 * v) + 0.4) + 0.10 * std::sin(k2pi * (3.0 * u + 2.0 * v) + 2.1);
    return 0.5 + 0.5 * h / 0.85;   // (|h| <= 0.85)
}

// A square slab round the origin, `tilesHalf` tiles of `tile` units each way, `cells` cells a tile: its underside from y 0 up top down
// to -relief (CeilingRelief), seen from below - its normals down, both windings (whatever the cull mode); the uv one picture a tile,
// u along +x and v along -z (with the tangent along +x and its sign the card's -1, the normal map's bitangent runs with v).
inline Mesh CeilingMesh(float tile, int tilesHalf, int cells, float relief) {
    Mesh m;
    const int n = 2 * tilesHalf * cells, row = n + 1;   // cells across; (n + 1)^2 vertices, at most 65536 for 16-bit indices
    const double half = (double)tile * tilesHalf, step = (double)tile / cells;
    m.scale = (float)(half > relief ? half : relief);   // (no std::max: windows.h's macro may be about)
    const int16_t w = (int16_t)std::lround(std::log2(m.scale) / 13.0 * 32767.0);
    for (int k = 0; k < 3; ++k) { m.mn[k] = 1e30f; m.mx[k] = -1e30f; }
    auto y = [&](double x, double z) { return -relief * CeilingRelief(x / tile, -z / tile); };
    const double e = step * 0.25;
    for (int i = 0; i <= n; ++i)
        for (int j = 0; j <= n; ++j) {
            const double x = -half + j * step, z = -half + i * step, py = y(x, z);
            const double dx = (y(x + e, z) - y(x - e, z)) / (2.0 * e), dz = (y(x, z + e) - y(x, z - e)) / (2.0 * e);
            const double nl = std::sqrt(dx * dx + 1.0 + dz * dz), tl = std::sqrt(1.0 + dx * dx);
            const double p[3] = {x, py, z};
            Vertex v{};
            for (int k = 0; k < 3; ++k) {
                v.pos[k] = (int16_t)std::lround(p[k] / m.scale * 32767.0);
                if (p[k] < m.mn[k]) m.mn[k] = (float)p[k];
                if (p[k] > m.mx[k]) m.mx[k] = (float)p[k];
            }
            v.pos[3] = w;
            v.normal[0] = Snorm8((float)(dx / nl)); v.normal[1] = Snorm8((float)(-1.0 / nl)); v.normal[2] = Snorm8((float)(dz / nl));
            v.tangent[0] = Snorm8((float)(1.0 / tl)); v.tangent[1] = Snorm8((float)(dx / tl)); v.tangent[2] = 0; v.tangent[3] = -127;
            v.uv[0] = Half((float)(x / tile));
            v.uv[1] = Half((float)(-z / tile));
            m.v.push_back(v);
        }
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            const uint16_t a = (uint16_t)(i * row + j), b = (uint16_t)(i * row + j + 1), c = (uint16_t)((i + 1) * row + j), d = (uint16_t)((i + 1) * row + j + 1);
            const uint16_t tris[2][3] = {{a, b, c}, {b, d, c}};
            for (const auto& t : tris) {
                m.idx.insert(m.idx.end(), {t[0], t[1], t[2]});
                m.idx.insert(m.idx.end(), {t[0], t[2], t[1]});   // and the other way round
            }
        }
    return m;
}

// [ceiling] game_ceiling_wall: a ring of rock round the origin, `radius` round, from y 0 down to -height, seen from inside - its normals in,
// both windings; the uv one picture a `tile`, a whole number of them round (no seam where it closes), u round, v up (with the tangent round
// and its sign the card's -1, the normal map's bitangent runs up).
inline Mesh WallMesh(float radius, float height, int segments, int rows, float tile) {
    Mesh m;
    constexpr double k2pi = 6.28318530717958647692;
    m.scale = radius > height ? radius : height;
    const int16_t w = (int16_t)std::lround(std::log2(m.scale) / 13.0 * 32767.0);
    const double around = std::floor(k2pi * radius / tile + 0.5) < 1.0 ? 1.0 : std::floor(k2pi * radius / tile + 0.5);   // pictures round it
    for (int k = 0; k < 3; ++k) { m.mn[k] = 1e30f; m.mx[k] = -1e30f; }
    for (int i = 0; i <= rows; ++i)
        for (int j = 0; j <= segments; ++j) {
            const double a = k2pi * j / segments, s = std::sin(a), c = std::cos(a);
            const double p[3] = {s * radius, -(double)height * i / rows, c * radius};
            Vertex v{};
            for (int k = 0; k < 3; ++k) {
                v.pos[k] = (int16_t)std::lround(p[k] / m.scale * 32767.0);
                if (p[k] < m.mn[k]) m.mn[k] = (float)p[k];
                if (p[k] > m.mx[k]) m.mx[k] = (float)p[k];
            }
            v.pos[3] = w;
            v.normal[0] = Snorm8((float)-s); v.normal[1] = 0; v.normal[2] = Snorm8((float)-c);   // in
            v.tangent[0] = Snorm8((float)c); v.tangent[1] = 0; v.tangent[2] = Snorm8((float)-s); v.tangent[3] = -127;
            v.uv[0] = Half((float)(around * j / segments));
            v.uv[1] = Half((float)(p[1] / tile));
            m.v.push_back(v);
        }
    const int row = segments + 1;
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < segments; ++j) {
            const uint16_t a = (uint16_t)(i * row + j), b = (uint16_t)(i * row + j + 1), c = (uint16_t)((i + 1) * row + j), d = (uint16_t)((i + 1) * row + j + 1);
            const uint16_t tris[2][3] = {{a, b, c}, {b, d, c}};
            for (const auto& t : tris) {
                m.idx.insert(m.idx.end(), {t[0], t[1], t[2]});
                m.idx.insert(m.idx.end(), {t[0], t[2], t[1]});
            }
        }
    return m;
}

// ---- the edits ------------------------------------------------------------------------------------------------------------------

struct Done {
    uint32_t vertices = 0, triangles = 0, textureNames = 0, bones = 0;
};

using Renames = std::vector<std::pair<std::string, std::string>>;   // a texture name the file has, the one it gets

// `f` (the game's card, read) becomes our dome: mesh `m`, each texture's new name wherever the file names the old one (every one must be
// there; an empty list leaves the textures as they are), LOD `lod`. False with `why` when the file is not laid out as the card is
// (nothing half done is written by the caller).
inline bool Remesh(File& f, const Mesh& m, const Renames& textures, int lod, Done* done, std::string* why) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    Done d;
    Walk g(f);
    const Addr rt{f.h.rootType.section, f.h.rootType.offset}, ro{f.h.rootObj.section, f.h.rootObj.offset};
    Addr t;
    // Meshes[0]
    const Addr meshes = g.Member(rt, ro, "Meshes", &t);
    if (!meshes.ok() || g.Kind(t) != 4 || g.Get<int32_t>(meshes) < 1) return no("no mesh");
    const Addr meshType = g.Ref(t), mesh = g.Ptr(g.Ptr(meshes.at(4)));
    if (!mesh.ok()) return no("no mesh");
    // its vertices: the card's vertex type, ours in it
    const Addr pvd = g.Ptr(g.Member(meshType, mesh, "PrimaryVertexData", &t));
    if (!pvd.ok()) return no("no vertex data");
    const Addr vdType = g.Ref(t);
    const Addr verts = g.Member(vdType, pvd, "Vertices", &t);
    if (!verts.ok() || g.Kind(t) != 7) return no("no vertex array");
    const Addr vt = g.Ptr(verts);
    {
        Addr e;
        const Addr zero{vt.s, vt.o};
        if (!vt.ok() || g.StructBytes(vt) != (int)sizeof(Vertex)) return no("the vertex type is not the card's (20 bytes)");
        static const struct { const char* name; int kind, width; } kWant[] = {{"Position", 17, 4}, {"Normal", 13, 4}, {"Tangent", 13, 4}, {"TextureCoordinates0", 21, 2}};
        for (const auto& w : kWant)
            if (!g.Member(vt, zero, w.name, &e).ok() || g.Kind(e) != w.kind || g.Width(e) != w.width) return no("the vertex type's members are not the card's");
    }
    const Addr vdata = g.Append(0, m.v.data(), m.v.size() * sizeof(Vertex), 4);
    g.Put<int32_t>(verts.at(8), (int32_t)m.v.size());
    g.SetPtr(verts.at(12), vdata);
    d.vertices = (uint32_t)m.v.size();
    // the topology: one group of all our triangles, our indices
    const uint32_t tris = (uint32_t)(m.idx.size() / 3);
    const Addr idata = g.Append(0, m.idx.data(), m.idx.size() * sizeof(uint16_t), 4);
    std::vector<Addr> topos;
    auto editTopo = [&](Addr topo, Addr topoType) -> bool {
        for (const Addr& a : topos) if (a == topo) return true;
        topos.push_back(topo);
        Addr gt, it16, it32;
        const Addr groups = g.Member(topoType, topo, "Groups", &gt);
        const Addr i16 = g.Member(topoType, topo, "Indices16", &it16);
        const Addr i32 = g.Member(topoType, topo, "Indices", &it32);
        if (!groups.ok() || g.Kind(gt) != 3 || !i16.ok() || g.Kind(it16) != 3 || g.Get<int32_t>(groups) < 1) return false;
        if (i32.ok() && g.Get<int32_t>(i32) != 0) return false;   // (32-bit indices: not the card's)
        const Addr g0 = g.Ptr(groups.at(4)), groupType = g.Ref(gt);
        Addr e;
        const Addr first = g.Member(groupType, g0, "TriFirst", &e), count = g.Member(groupType, g0, "TriCount", &e), mat = g.Member(groupType, g0, "MaterialIndex", &e);
        if (!first.ok() || !count.ok()) return false;
        g.Put<int32_t>(groups, 1);
        g.Put<int32_t>(first, 0);
        g.Put<int32_t>(count, (int32_t)tris);
        if (mat.ok()) g.Put<int32_t>(mat, 0);
        g.Put<int32_t>(i16, (int32_t)m.idx.size());
        g.SetPtr(i16.at(4), idata);
        return true;
    };
    {
        const Addr pt = g.Member(meshType, mesh, "PrimaryTopology", &t);
        if (!pt.ok() || !editTopo(g.Ptr(pt), g.Ref(t))) return no("the mesh's topology is not the card's");
        const Addr tts = g.Member(rt, ro, "TriTopologies", &t);
        if (tts.ok() && g.Kind(t) == 4)
            for (int32_t k = 0; k < g.Get<int32_t>(tts); ++k) {
                const Addr tp = g.Ptr(g.Ptr(tts.at(4)).at((uint32_t)k * 8));
                if (tp.ok() && !editTopo(tp, g.Ref(t))) return no("a topology is not the card's");
            }
    }
    d.triangles = tris;
    // the mesh's boxes and scale
    {
        const Addr bb = g.Member(meshType, mesh, "BoneBindings", &t);
        if (bb.ok() && g.Kind(t) == 3) {
            const Addr bbType = g.Ref(t), arr = g.Ptr(bb.at(4));
            const int sz = g.StructBytes(bbType);
            for (int32_t k = 0; k < g.Get<int32_t>(bb); ++k) {
                Addr e;
                const Addr one = arr.at((uint32_t)(k * sz));
                const Addr lo = g.Member(bbType, one, "OBBMin", &e), hi = g.Member(bbType, one, "OBBMax", &e);
                if (lo.ok()) for (int c = 0; c < 3; ++c) g.Put<float>(lo.at(c * 4), m.mn[c]);
                if (hi.ok()) for (int c = 0; c < 3; ++c) g.Put<float>(hi.at(c * 4), m.mx[c]);
            }
        }
        const Addr ext = g.Member(meshType, mesh, "ExtendedData", &t);
        if (ext.ok() && g.Kind(t) == 5) {
            const Addr et = g.Ptr(ext), eo = g.Ptr(ext.at(8));
            Addr e;
            const Addr lo = g.Member(et, eo, "bbMin", &e), hi = g.Member(et, eo, "bbMax", &e), vs = g.Member(et, eo, "VertexScale", &e);
            if (!vs.ok()) return no("the mesh has no VertexScale");
            if (lo.ok()) for (int c = 0; c < 3; ++c) g.Put<float>(lo.at(c * 4), m.mn[c]);
            if (hi.ok()) for (int c = 0; c < 3; ++c) g.Put<float>(hi.at(c * 4), m.mx[c]);
            g.Put<float>(vs, m.scale);
        } else {
            return no("the mesh has no extended data");
        }
    }
    // every skeleton's bones: identity
    {
        std::vector<Addr> seen;
        auto bones = [&](Addr sk, Addr skType) {
            for (const Addr& a : seen) if (a == sk) return;
            seen.push_back(sk);
            Addr bt;
            const Addr arr = g.Member(skType, sk, "Bones", &bt);
            if (!arr.ok() || g.Kind(bt) != 3) return;
            const Addr boneType = g.Ref(bt), data = g.Ptr(arr.at(4));
            const int sz = g.StructBytes(boneType);
            for (int32_t k = 0; k < g.Get<int32_t>(arr); ++k) {
                Addr e;
                const Addr one = data.at((uint32_t)(k * sz));
                const Addr tr = g.Member(boneType, one, "Transform", &e), inv = g.Member(boneType, one, "InverseWorldTransform", &e);
                if (tr.ok()) {
                    const float id[17] = {0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0};   // pos, rot (x y z w), scale / shear 3 x 3
                    g.Put<uint32_t>(tr, 0);
                    for (int c = 0; c < 16; ++c) g.Put<float>(tr.at(4 + c * 4), id[c]);
                }
                if (inv.ok()) for (int c = 0; c < 16; ++c) g.Put<float>(inv.at(c * 4), c % 5 == 0 ? 1.0f : 0.0f);
                ++d.bones;
            }
        };
        const Addr sks = g.Member(rt, ro, "Skeletons", &t);
        if (sks.ok() && g.Kind(t) == 4)
            for (int32_t k = 0; k < g.Get<int32_t>(sks); ++k) bones(g.Ptr(g.Ptr(sks.at(4)).at((uint32_t)k * 8)), g.Ref(t));
        const Addr models = g.Member(rt, ro, "Models", &t);
        if (models.ok() && g.Kind(t) == 4 && g.Get<int32_t>(models) >= 1) {
            const Addr modelType = g.Ref(t), model = g.Ptr(g.Ptr(models.at(4)));
            Addr e;
            const Addr sk = g.Member(modelType, model, "Skeleton", &e);
            if (sk.ok()) bones(g.Ptr(sk), g.Ref(e));
            const Addr mext = g.Member(modelType, model, "ExtendedData", &e);
            if (mext.ok()) {
                Addr le;
                const Addr lodAt = g.Member(g.Ptr(mext), g.Ptr(mext.at(8)), "lodLevel", &le);
                if (lodAt.ok()) g.Put<int32_t>(lodAt, lod);
            }
        }
    }
    // the textures' names
    for (const auto& r : textures) {
        const Addr str = g.Append(0, r.second.c_str(), r.second.size() + 1, 4);
        uint32_t named = 0;
        for (uint32_t s = 0; s < f.fix.size(); ++s)
            for (Fixup& x : f.fix[s])
                if (g.Str(Addr{x.ptr.section, x.ptr.offset}) == r.first) { x.ptr = SecPtr{str.s, str.o}; ++named; }
        if (!named) return no("a texture name to change is not in the file");
        d.textureNames += named;
    }
    if (done) *done = d;
    return true;
}
inline bool Remesh(File& f, const Mesh& m, const std::string& oldTexture, const std::string& newTexture, int lod, Done* done, std::string* why) {
    return Remesh(f, m, Renames{{oldTexture, newTexture}}, lod, done, why);
}

}  // namespace grannyremesh
