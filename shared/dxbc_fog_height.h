// dxbc_fog_height.h - [fog] game_fog_top / game_fog_soft: the game's own distance fog thinned with height, in its own fog pass.
//
// Serg (2026-10-27): "туман закрывает небо и вверху, надо подумать, как ограничить его высоту" - the game's fog lies over [sky] game_sky's
// dome overhead too; its height on a slider. The game's fog pass (ps_atmosphere_apply, Shader Model 5 DXBC, 30 permutations; 15 - 29 hold
// the distance fog) computes
//     d = length(viewPos) / cb13[9].x - 1;   t = min(1, exp(-cb13[9].y * d))   (t: how much of the scene is kept)
// and its height fog is a band over the terrain - nothing that thins with height. Here the shader's own bytecode gets, right after that
// `min`, the point's height above the eye (its view-space position into the world by the shader's own camera constants cb3[9..11], as
// its height fog does) and
//     f = saturate((top - h) / soft);   t' = 1 - (1 - t) * f
// - the fog as the game's below top - soft, none above top. `top` and `soft` are immediates: a variant per setting (the caller builds a
// pipeline with it and binds it in place of the game's). The shader is found by its own code (the division by cb13[9].x of a dp4's square
// root, then exp and min with 1), never by a hash of Blizzard's: after a game update that changes it, it is simply not found and the game's
// fog stays as it is. The container's checksum is the DXBC hash (MD5 with its own padding), checked against the game's own in the tests.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "unleashed_fog.h"   // [fog] ease_in: D2R VR Unleashed's ease over the fog's first 40 units (MPL 2.0, bearstonem) - Curve.ease

namespace dxbcfog {

// ---- the DXBC checksum: MD5 over everything after the 20-byte magic + checksum, with DXBC's own last block --------------------------
namespace md5 {
inline uint32_t Rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
inline void Block(uint32_t st[4], const uint32_t m[16]) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1,
        0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453,
        0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942,
        0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
        0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
        0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
        else { f = c ^ (b | ~d); g = (7 * i) % 16; }
        const uint32_t t = d;
        d = c;
        c = b;
        b = b + Rol(a + f + K[i] + m[g], R[i]);
        a = t;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}
}  // namespace md5

inline void Checksum(const uint8_t* p, size_t n, uint32_t out[4]) {
    uint32_t st[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    const uint32_t bits = (uint32_t)(n * 8);
    const size_t full = n & ~(size_t)63;
    uint32_t m[16];
    for (size_t at = 0; at < full; at += 64) { std::memcpy(m, p + at, 64); md5::Block(st, m); }
    const size_t last = n - full;
    std::memset(m, 0, 64);
    if (last >= 56) {
        std::memcpy(m, p + full, last);
        ((uint8_t*)m)[last] = 0x80;
        md5::Block(st, m);
        std::memset(m, 0, 64);
        m[0] = bits;
        m[15] = (bits >> 2) | 1;
        md5::Block(st, m);
    } else {
        m[0] = bits;
        std::memcpy(&m[1], p + full, last);
        ((uint8_t*)&m[1])[last] = 0x80;
        m[15] = (bits >> 2) | 1;
        md5::Block(st, m);
    }
    std::memcpy(out, st, 16);
}
// the container's own checksum holds (bytes 4 - 19 over everything from 20)
inline bool ChecksumHolds(const uint8_t* p, size_t n) {
    if (n < 32 || std::memcmp(p, "DXBC", 4) != 0) return false;
    uint32_t c[4];
    Checksum(p + 20, n - 20, c);
    return std::memcmp(c, p + 4, 16) == 0;
}

// ---- SM5 tokens -----------------------------------------------------------------------------------------------------------------
enum : uint32_t { kAdd = 0, kAnd = 1, kDiv = 14, kDp4 = 17, kExp = 25, kLog = 47, kLt = 49, kMad = 50, kMin = 51, kMov = 54, kMovc = 55, kMul = 56,
                  kSqrt = 75,
                  kCustomData = 53 /* 0x35: its length in the next dword */, kDclCb = 89, kDclTemps = 104 };
enum : uint32_t { kTemp = 0, kImm32 = 4, kCb = 8 };

struct Operand {
    uint32_t type = ~0u, comps = 0, mode = 0, sel = 0, dims = 0, idx[2] = {}, len = 0;
    bool neg = false;
    float imm = 0.0f;
};
inline bool ReadOperand(const uint32_t* t, size_t avail, Operand* o) {
    if (avail < 1) return false;
    const uint32_t tok = t[0];
    size_t n = 1;
    o->comps = tok & 3;
    o->mode = (tok >> 2) & 3;
    o->sel = (tok >> 4) & 0xFF;
    o->type = (tok >> 12) & 0xFF;
    o->dims = (tok >> 20) & 3;
    if (tok & 0x80000000u) {
        if (avail < 2) return false;
        const uint32_t ext = t[1];
        if ((ext & 0x3F) == 1) o->neg = ((ext >> 6) & 0xFF) == 1 || ((ext >> 6) & 0xFF) == 3;
        n = 2;
    }
    for (uint32_t d = 0; d < o->dims && d < 2; ++d) {
        const uint32_t rep = (tok >> (22 + 3 * d)) & 7;
        if (rep != 0 || n >= avail) return false;   // (only plain immediate indices are read: what the fog's operands use)
        o->idx[d] = t[n++];
    }
    if (o->type == kImm32) {
        const size_t k = o->comps == 1 ? 1 : 4;
        if (n + k > avail) return false;
        std::memcpy(&o->imm, &t[n], 4);
        n += k;
    }
    o->len = (uint32_t)n;
    return true;
}
struct Instr { size_t at = 0, len = 0; uint32_t op = 0; Operand o[4]; int n = 0; };
inline bool Decode(const std::vector<uint32_t>& c, size_t at, Instr* in) {
    const uint32_t tok = c[at];
    in->at = at;
    in->op = tok & 0x7FF;
    in->len = in->op == kCustomData ? (at + 1 < c.size() ? c[at + 1] : 0) : (tok >> 24) & 0x7F;
    if (!in->len || at + in->len > c.size()) return false;
    in->n = 0;
    if (in->op == kAdd || in->op == kAnd || in->op == kDiv || in->op == kDp4 || in->op == kExp || in->op == kLt || in->op == kMad || in->op == kMin ||
        in->op == kMov || in->op == kMovc || in->op == kMul) {
        size_t p = at + 1 + ((tok & 0x80000000u) ? 1 : 0);
        while (p < at + in->len && in->n < 4) {
            // an operand this reader does not take (a relative index - the game's sampling loop's icb[r0.z]): the instruction is
            // stepped over by its length, never matched
            if (!ReadOperand(&c[p], at + in->len - p, &in->o[in->n])) { in->n = 0; break; }
            p += in->o[in->n].len;
            ++in->n;
        }
    }
    return true;
}

// ---- the operands we write ------------------------------------------------------------------------------------------------------
inline void DstTemp(std::vector<uint32_t>& v, uint32_t r, uint32_t mask) { v.push_back(2u | (0u << 2) | (mask << 4) | (kTemp << 12) | (1u << 20)); v.push_back(r); }
inline void SrcTemp(std::vector<uint32_t>& v, uint32_t r, uint32_t comp, bool neg = false) {
    v.push_back(2u | (2u << 2) | (comp << 4) | (kTemp << 12) | (1u << 20) | (neg ? 0x80000000u : 0u));
    if (neg) v.push_back(1u | (1u << 6));
    v.push_back(r);
}
inline void SrcCb(std::vector<uint32_t>& v, uint32_t slot, uint32_t i, uint32_t comp) {
    v.push_back(2u | (2u << 2) | (comp << 4) | (kCb << 12) | (2u << 20));
    v.push_back(slot);
    v.push_back(i);
}
inline void SrcTempSwz(std::vector<uint32_t>& v, uint32_t r, uint32_t swz) {
    v.push_back(2u | (1u << 2) | (swz << 4) | (kTemp << 12) | (1u << 20));
    v.push_back(r);
}
inline void SrcImm4(std::vector<uint32_t>& v, float x, float y, float z, float w) {
    v.push_back(2u | (kImm32 << 12));
    for (float f : {x, y, z, w}) {
        uint32_t b;
        std::memcpy(&b, &f, 4);
        v.push_back(b);
    }
}
inline void SrcImm(std::vector<uint32_t>& v, float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    v.push_back(1u | (kImm32 << 12));
    v.push_back(b);
}
// an instruction: its opcode token (with its length) and its operands
template <class F>
inline void Emit(std::vector<uint32_t>& out, uint32_t op, bool sat, F&& operands) {
    std::vector<uint32_t> body;
    operands(body);
    out.push_back(op | (sat ? (1u << 13) : 0u) | ((uint32_t)(body.size() + 1) << 24));
    out.insert(out.end(), body.begin(), body.end());
}

// ---- finding the distance fog, and patching it ----------------------------------------------------------------------------------
struct Found {
    size_t shexAt = 0;         // the SHEX (or SHDR) chunk in the container
    size_t insertAt = 0;       // a token index in the chunk's tokens: after the `min t, t, 1`
    uint32_t t = 0, tComp = 0, view = 0;   // the register t is in and its component, and the view-space position's register (x y z)
    size_t tempsAt = 0;        // dcl_temps's count token
    uint32_t temps = 0;
    size_t cb3SizeAt = 0;      // the camera constants' declared size (widened to 12 when shorter: rows 9 - 11 are read)
    uint32_t cb3Size = 0;
    // the mist: `mad D.xyz, cb13[10].xyz, (1 - t), M.xyz` - the fog's colour and the volumetric mist added after it, the mist never fogged
    // by distance (in a cave with our fog its torch-lit mist floated as a light band over the far walls fogged black, PC1 2026-10-27)
    size_t mistEnd = 0;        // a token index after that mad (0: none in this permutation)
    uint32_t mistD = 0, mistM = 0;
    // the void: past the linear depth's far end (`lt V, depth, cb10[53].z + cb10[53].x` false) the pass gives the mist as it is - `movc
    // D.xyzw, V, fogged, mist`: over the void of a cave (past the ceiling's rim) the torch-lit mist alone, a light band in the black (PC1
    // 2026-10-27, the ceiling session's finding). Where its fourth operand is a temp: that temp is set to (0, 0, 0, 1) - the void left as it
    // is, as the pass leaves what lies past 10000 planes in its permutations without a mist.
    size_t voidAt = 0;         // that movc's token index (0: none - a permutation without a mist gives (0, 0, 0, 1) there already)
    uint32_t voidB = 0;
};

inline bool Find(const uint8_t* p, size_t n, Found* f, std::string* why = nullptr) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (n < 32 || std::memcmp(p, "DXBC", 4) != 0) return no("not DXBC");
    uint32_t chunks = 0;
    std::memcpy(&chunks, p + 28, 4);
    if (32 + (size_t)chunks * 4 > n) return no("chunk table cut short");
    size_t shex = 0;
    for (uint32_t i = 0; i < chunks; ++i) {
        uint32_t off = 0;
        std::memcpy(&off, p + 32 + i * 4, 4);
        if ((size_t)off + 8 <= n && (std::memcmp(p + off, "SHEX", 4) == 0 || std::memcmp(p + off, "SHDR", 4) == 0)) shex = off;
    }
    if (!shex) return no("no shader code chunk");
    uint32_t bytes = 0;
    std::memcpy(&bytes, p + shex + 4, 4);
    if (shex + 8 + (size_t)bytes > n || bytes < 8) return no("shader chunk cut short");
    std::vector<uint32_t> c(bytes / 4);
    std::memcpy(c.data(), p + shex + 8, c.size() * 4);
    // c[0] version, c[1] length in dwords
    Found r;
    r.shexAt = shex;
    bool haveDp4 = false, haveDiv = false, haveExp = false, cb3 = false;
    uint32_t dp4Dst = ~0u, dp4Src = ~0u, divReg = ~0u, expReg = ~0u;
    bool viewKept = false;
    for (size_t at = 2; at < c.size();) {
        Instr in;
        if (!Decode(c, at, &in)) return no("an instruction did not decode");
        if (in.op == kDclTemps && in.len == 2) { r.tempsAt = at + 1; r.temps = c[at + 1]; }
        if (in.op == kDclCb && in.len >= 3) {
            Operand o;
            if (ReadOperand(&c[at + 1], in.len - 1, &o) && o.dims == 2 && o.idx[0] == 3) {
                cb3 = true;
                r.cb3Size = o.idx[1];
                r.cb3SizeAt = at + 1 + ((c[at + 1] & 0x80000000u) ? 2 : 1) + 1;   // (the operand token, then index 0, then index 1)
            }
        }
        if (in.op == kDp4 && in.n == 3 && in.o[0].type == kTemp && in.o[1].type == kTemp && in.o[2].type == kTemp && in.o[1].idx[0] == in.o[2].idx[0]) {
            haveDp4 = true; dp4Dst = in.o[0].idx[0]; dp4Src = in.o[1].idx[0];
            viewKept = true;
        }
        // the view-space position (x y z) must still be there at the min: written over in between, the height could not be read from it
        if (haveDp4 && viewKept && in.n >= 1 && in.o[0].type == kTemp && in.o[0].idx[0] == dp4Src && in.o[0].mode == 0 && (in.o[0].sel & 7) &&
            !(in.op == kMin && haveExp && in.o[1].type == kTemp && in.o[1].idx[0] == expReg && (in.o[0].sel & 7) == 0))
            viewKept = false;
        if (haveDp4 && in.op == kDiv && in.n == 3 && in.o[0].type == kTemp && in.o[1].type == kTemp && in.o[1].idx[0] == dp4Dst &&
            in.o[2].type == kCb && in.o[2].dims == 2 && in.o[2].idx[0] == 13 && in.o[2].idx[1] == 9) {
            haveDiv = true; divReg = in.o[0].idx[0];
        }
        if (haveDiv && in.op == kExp && in.n == 2 && in.o[0].type == kTemp && in.o[1].type == kTemp && in.o[1].idx[0] == divReg) {
            haveExp = true;
            expReg = in.o[0].idx[0];
        }
        // min t, exp, 1 - t anywhere (the game's permutations put it in r0.x, r1.w, r2.w ...): one component written
        if (haveExp && in.op == kMin && in.n == 3 && in.o[0].type == kTemp && in.o[1].type == kTemp && in.o[1].idx[0] == expReg &&
            in.o[2].type == kImm32 && in.o[2].imm == 1.0f && in.o[0].mode == 0) {
            const uint32_t mask = in.o[0].sel & 0xF;
            if (mask != 1 && mask != 2 && mask != 4 && mask != 8) return no("the fog's min writes more than one component");
            if (!viewKept || (in.o[0].idx[0] == dp4Src && mask != 8)) return no("the view-space position is written over before the fog's min");
            r.insertAt = at + in.len;
            r.t = in.o[0].idx[0];
            r.tComp = mask == 1 ? 0 : mask == 2 ? 1 : mask == 4 ? 2 : 3;
            r.view = dp4Src;
            // the mist's mad after it, t still where it is there
            for (size_t a2 = r.insertAt; a2 < c.size();) {
                Instr m;
                if (!Decode(c, a2, &m)) break;
                if (m.op == kMad && m.n == 4 && m.o[0].type == kTemp && m.o[1].type == kCb && m.o[1].dims == 2 && m.o[1].idx[0] == 13 &&
                    m.o[1].idx[1] == 10 && m.o[3].type == kTemp && (m.o[0].sel & 7) == 7) {
                    r.mistEnd = a2 + m.len;
                    r.mistD = m.o[0].idx[0];
                    r.mistM = m.o[3].idx[0];
                    break;
                }
                if (m.n >= 1 && m.o[0].type == kTemp && m.o[0].idx[0] == r.t && m.o[0].mode == 0 && (m.o[0].sel & (1u << r.tComp))) break;   // t gone
                a2 += m.len;
            }
            break;
        }
        at += in.len;
    }
    if (!r.insertAt) return no("no distance fog (dp4, sqrt, div by cb13[9].x, exp, min with 1) in it");
    // the void's test and the movc that gives the mist there (after the fog's min)
    {
        uint32_t sumReg = ~0u, sumMask = 0, vReg = ~0u, vComp = 0;
        for (size_t at = 2; at < c.size();) {
            Instr in;
            if (!Decode(c, at, &in)) break;
            const bool one = in.n >= 1 && in.o[0].type == kTemp && in.o[0].mode == 0;
            const uint32_t mask = one ? in.o[0].sel & 0xF : 0;
            if (in.op == kAdd && in.n == 3 && one && in.o[1].type == kCb && in.o[1].dims == 2 && in.o[1].idx[0] == 10 && in.o[1].idx[1] == 53 &&
                in.o[2].type == kCb && in.o[2].dims == 2 && in.o[2].idx[0] == 10 && in.o[2].idx[1] == 53 && (mask == 1 || mask == 2 || mask == 4 || mask == 8)) {
                sumReg = in.o[0].idx[0];
                sumMask = mask;
            } else if (in.op == kLt && in.n == 3 && one && (mask == 1 || mask == 2 || mask == 4 || mask == 8) && in.o[2].type == kTemp &&
                       in.o[2].idx[0] == sumReg) {   // (the game's into the sum's own register; fxc's anywhere)
                vReg = in.o[0].idx[0];
                vComp = mask == 1 ? 0 : mask == 2 ? 1 : mask == 4 ? 2 : 3;
                sumReg = ~0u;
            } else if (vReg != ~0u && at >= r.insertAt && in.op == kMovc && in.n == 4 && in.o[0].mode == 0 && (in.o[0].sel & 0xF) == 0xF &&
                       in.o[1].type == kTemp && in.o[1].idx[0] == vReg &&
                       ((in.o[1].mode == 1 && in.o[1].sel == vComp * 0x55) || (in.o[1].mode == 2 && (in.o[1].sel & 3) == vComp)) &&
                       in.o[2].type == kTemp && in.o[3].type == kTemp && in.o[2].idx[0] != in.o[3].idx[0] && in.o[3].mode == 1 && in.o[3].sel == 0xE4) {
                r.voidAt = at;
                r.voidB = in.o[3].idx[0];
                break;
            } else if (vReg != ~0u && one && in.o[0].idx[0] == vReg && (mask & (1u << vComp))) {
                vReg = ~0u;   // the test written over: not ours to follow
            }
            at += in.len;
        }
    }
    if (!r.tempsAt) return no("no dcl_temps");
    if (!cb3) return no("the camera constants (cb3) are not declared");
    *f = r;
    return true;
}

// [fog] game_fog_our_curve: the fog's share as our effect's (D2R_DepthFog.fx FogAt) in place of the game's exponential -
//     fog = strength x saturate((distance - start) / (end - start)) ^ curve;   t = 1 - fog
// (the game's: 1 - exp(-density (distance / plane - 1)), nearly all of it right after the start, the far range one grey - Serg, 2026-10-27:
// "градиентов не хватает на тумане"). The distance as the game's own: the length of its view position (x, y, z, w = 1).
// ease ([fog] ease_in): that share times unleashed::fog::Ease - a smoothstep over the first kEaseUnits past the start, no hard ring.
struct Curve {
    bool on = false;
    float start = 0.0f, end = 1.0f, curve = 1.0f, strength = 1.0f;
    bool ease = false;
    bool operator==(const Curve& o) const {
        return on == o.on && (!on || (start == o.start && end == o.end && curve == o.curve && strength == o.strength && ease == o.ease));
    }
    bool operator!=(const Curve& o) const { return !(*this == o); }
};
inline float CurveFog(const Curve& c, float dist) {   // (for the tests and the log: what the patched pass lays)
    const float span = c.end - c.start > 1.0f ? c.end - c.start : 1.0f;
    float x = (dist - c.start) / span;
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    const float cv = c.curve > 0.05f ? c.curve : 0.05f;
    return (x > 0.0f ? std::pow(x, cv) : 0.0f) * c.strength * (c.ease ? unleashed::fog::Ease(dist, c.start) : 1.0f);
}

// The patched container (a new one: the game's left alone), or empty with `why`. mist: the volumetric mist fogged by distance too
// (D.xyz += M.xyz * (t' - 1): fog * (1 - t) + mist * t'), where the permutation adds one. voidAlone: the void (past the linear depth's
// far end) left as it is - (0, 0, 0, 1), no mist laid over it. noFog: the distance fog's colour none (t = 1 for it: the scene fogged
// before the game's transparent resolve, [fog] game_fog_before_fx) - the mist still fogged by t'.
inline std::vector<uint8_t> Patch(const uint8_t* p, size_t n, float top, float soft, std::string* why = nullptr, bool mist = false,
                                  bool voidAlone = false, bool noFog = false, const Curve* curve = nullptr) {
    Found f;
    if (!Find(p, n, &f, why)) return {};
    if (!(soft > 0.01f)) soft = 0.01f;
    uint32_t bytes = 0;
    std::memcpy(&bytes, p + f.shexAt + 4, 4);
    std::vector<uint32_t> c(bytes / 4);
    std::memcpy(c.data(), p + f.shexAt + 8, c.size() * 4);
    const uint32_t T = f.temps;   // a temp of our own
    std::vector<uint32_t> add;
    if (curve && curve->on) {   // our curve in place of the game's t (before the height thinning, which reads t)
        const float span = curve->end - curve->start > 1.0f ? curve->end - curve->start : 1.0f;
        const float cv = curve->curve > 0.05f ? curve->curve : 0.05f;
        // T.x = |view| ; T.x = saturate(T.x / span - start / span) ; T.x = exp2(log2(T.x) x curve) x strength ; t = 1 - T.x
        Emit(add, kDp4, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 1); SrcTempSwz(v, f.view, 0xE4); SrcTempSwz(v, f.view, 0xE4); });
        Emit(add, kSqrt, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 1); SrcTemp(v, T, 0); });
        const float ew = unleashed::fog::kEaseUnits;
        if (curve->ease) {   // T.y = smoothstep(start, start + ew, |view|): T.y = saturate(T.x / ew - start / ew) ; T.z = 3 - 2 T.y ; T.y = T.y T.y T.z
            Emit(add, kMad, true, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 2); SrcTemp(v, T, 0); SrcImm(v, 1.0f / ew); SrcImm(v, -curve->start / ew); });
            Emit(add, kMad, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 4); SrcTemp(v, T, 1); SrcImm(v, -2.0f); SrcImm(v, 3.0f); });
            Emit(add, kMul, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 2); SrcTemp(v, T, 1); SrcTemp(v, T, 1); });
            Emit(add, kMul, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 2); SrcTemp(v, T, 1); SrcTemp(v, T, 2); });
        }
        Emit(add, kMad, true, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 1); SrcTemp(v, T, 0); SrcImm(v, 1.0f / span); SrcImm(v, -curve->start / span); });
        Emit(add, kLog, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 1); SrcTemp(v, T, 0); });
        Emit(add, kMul, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 1); SrcTemp(v, T, 0); SrcImm(v, cv); });
        Emit(add, kExp, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 1); SrcTemp(v, T, 0); });
        if (curve->ease) Emit(add, kMul, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 1); SrcTemp(v, T, 0); SrcTemp(v, T, 1); });
        Emit(add, kMad, false, [&](std::vector<uint32_t>& v) { DstTemp(v, f.t, 1u << f.tComp); SrcTemp(v, T, 0, true); SrcImm(v, curve->strength); SrcImm(v, 1.0f); });
    }
    // h = viewPos.x * cb3[9].y + viewPos.y * cb3[10].y + viewPos.z * cb3[11].y   (the point's height above the eye, world units)
    Emit(add, kMul, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 1); SrcTemp(v, f.view, 1); SrcCb(v, 3, 10, 1); });
    Emit(add, kMad, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 1); SrcCb(v, 3, 9, 1); SrcTemp(v, f.view, 0); SrcTemp(v, T, 0); });
    Emit(add, kMad, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 1); SrcCb(v, 3, 11, 1); SrcTemp(v, f.view, 2); SrcTemp(v, T, 0); });
    // f = saturate(h * (-1 / soft) + top / soft)
    Emit(add, kMad, true, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 1); SrcTemp(v, T, 0); SrcImm(v, -1.0f / soft); SrcImm(v, top / soft); });
    // t' = 1 - (1 - t) * f
    Emit(add, kAdd, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 2); SrcTemp(v, f.t, f.tComp, true); SrcImm(v, 1.0f); });
    Emit(add, kMul, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 2); SrcTemp(v, T, 1); SrcTemp(v, T, 0); });
    Emit(add, kAdd, false, [&](std::vector<uint32_t>& v) { DstTemp(v, f.t, 1u << f.tComp); SrcTemp(v, T, 1, true); SrcImm(v, 1.0f); });
    if (noFog) {   // t' kept in T.w for the mist, t itself 1: no fog's colour, the scene kept whole
        Emit(add, kMov, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 8); SrcTemp(v, f.t, f.tComp); });
        Emit(add, kMov, false, [&](std::vector<uint32_t>& v) { DstTemp(v, f.t, 1u << f.tComp); SrcImm(v, 1.0f); });
    }
    if (voidAlone && f.voidAt && f.voidAt >= f.mistEnd && f.voidAt > f.insertAt) {   // (the latest place first: an insert moves what follows it)
        std::vector<uint32_t> m;
        Emit(m, kMov, false, [&](std::vector<uint32_t>& v) { DstTemp(v, f.voidB, 0xF); SrcImm4(v, 0.0f, 0.0f, 0.0f, 1.0f); });
        c.insert(c.begin() + (std::ptrdiff_t)f.voidAt, m.begin(), m.end());
    }
    if (mist && f.mistEnd) {   // (the later place first: the first insert moves it)
        std::vector<uint32_t> m;
        Emit(m, kAdd, false, [&](std::vector<uint32_t>& v) { DstTemp(v, T, 4); if (noFog) SrcTemp(v, T, 3); else SrcTemp(v, f.t, f.tComp); SrcImm(v, -1.0f); });
        Emit(m, kMad, false, [&](std::vector<uint32_t>& v) { DstTemp(v, f.mistD, 7); SrcTempSwz(v, f.mistM, 0x24); SrcTempSwz(v, T, 0xAA); SrcTempSwz(v, f.mistD, 0x24); });
        c.insert(c.begin() + (std::ptrdiff_t)f.mistEnd, m.begin(), m.end());
    }
    c.insert(c.begin() + (std::ptrdiff_t)f.insertAt, add.begin(), add.end());
    c[f.tempsAt] = T + 1;
    if (f.cb3Size < 12) c[f.cb3SizeAt] = 12;
    c[1] = (uint32_t)c.size();
    // the container again: the chunk grown, the chunks after it moved, the total and the checksum new
    const size_t grow = (c.size() * 4) - bytes;
    std::vector<uint8_t> out(p, p + n);
    const size_t codeAt = f.shexAt + 8;
    out.erase(out.begin() + (std::ptrdiff_t)codeAt, out.begin() + (std::ptrdiff_t)(codeAt + bytes));
    out.insert(out.begin() + (std::ptrdiff_t)codeAt, (const uint8_t*)c.data(), (const uint8_t*)c.data() + c.size() * 4);
    const uint32_t newBytes = (uint32_t)(c.size() * 4);
    std::memcpy(&out[f.shexAt + 4], &newBytes, 4);
    uint32_t chunks = 0;
    std::memcpy(&chunks, &out[28], 4);
    for (uint32_t i = 0; i < chunks; ++i) {
        uint32_t off = 0;
        std::memcpy(&off, &out[32 + i * 4], 4);
        if (off > f.shexAt) { off += (uint32_t)grow; std::memcpy(&out[32 + i * 4], &off, 4); }
    }
    const uint32_t total = (uint32_t)out.size();
    std::memcpy(&out[24], &total, 4);
    uint32_t sum[4];
    Checksum(out.data() + 20, out.size() - 20, sum);
    std::memcpy(&out[4], sum, 16);
    return out;
}

// The factor the patch lays on the fog at a point `h` units above the eye (for the tests and the log)
inline float FogFactor(float h, float top, float soft) {
    if (!(soft > 0.01f)) soft = 0.01f;
    const float v = (top - h) / soft;
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

}  // namespace dxbcfog
