// dxbc_ao_radius.h - [render] ao_vr_radius_m: the game's own ambient occlusion (ASSAO) with a radius for eyes a few metres from the ground.
//
// Serg 2026-11-13 on 0.215.4 - 0.215.6: without DLSS the tall grass cards were black silhouettes. Their see-through shader multiplies its
// colour by the AO's copy (t42). With depth_fix_mode 0 (the AO's depth flattened) they turned green, so the AO itself made them black. The
// game's generate pass (ps_assao_generate_p, all six permutations) is Intel's ASSAO:
//     tooCloseLimitMod = saturate(dist * cb4[6].y) * 0.8 + 0.2          (cb4[6].y = 1 / near limit, Intel's 1 / (1.2 * radius))
//     effectRadius     = tooCloseLimitMod * cb4[4].z                    (cb4[4].z = the radius in view-space world units)
//     disk in pixels   = 0.85 * effectRadius / (pixel size at that depth);  falloff = -1 / effectRadius^2
// Its radius and near limit are the game's numbers for its camera hundreds of units above the ground. From our eyes, dense grass a metre
// away fills that disk and the AO goes to full. Here the two constants become immediates: the radius `r` (world units, the caller's metres
// times the world's units a metre) and 1 / (1.2 r) - Intel's own rule. Everything else stays the game's: strength, power, clamp, fade-out,
// the blur and the apply.
//
// The three instructions are found by their own code (mul_sat by cb4[6].y, mad by 0.8 and 0.2, mul by cb4[4].z - one register, one
// component), never by a hash: after a game update that changes them, nothing is found and the AO stays as the game made it. The
// container's checksum is made again (dxbc_fog_height.h's DXBC MD5).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "dxbc_fog_height.h"   // the SM5 token reader, the emitters, the DXBC checksum

namespace dxbcao {

using dxbcfog::Decode;
using dxbcfog::Instr;
using dxbcfog::kCb;
using dxbcfog::kImm32;
using dxbcfog::kMad;
using dxbcfog::kMul;
using dxbcfog::kTemp;

struct Found {
    size_t shexAt = 0;
    size_t nearAt = 0, nearLen = 0;       // the mul_sat by cb4[6].y: its token index and length in the chunk's tokens
    size_t radiusAt = 0, radiusLen = 0;   // the mul by cb4[4].z
    uint32_t reg = 0, comp = 0;           // the register and component they work on
};

// `in` is `op dst rR.c, src rR.c, cb4[slot].comp` (the dst a one-component mask)
inline bool IsTempByCb(const Instr& in, uint32_t op, uint32_t slot, uint32_t comp, uint32_t* reg, uint32_t* c) {
    if (in.op != op || in.n != 3) return false;
    const auto& d = in.o[0];
    const auto& a = in.o[1];
    const auto& b = in.o[2];
    if (d.type != kTemp || d.mode != 0 || a.type != kTemp || a.mode != 2 || a.neg || b.type != kCb || b.mode != 2 || b.neg) return false;
    if (b.dims != 2 || b.idx[0] != 4 || b.idx[1] != slot || (b.sel & 3) != comp) return false;
    const uint32_t mask = d.sel & 0xF;
    uint32_t dc = 0;
    if (mask == 1) dc = 0; else if (mask == 2) dc = 1; else if (mask == 4) dc = 2; else if (mask == 8) dc = 3; else return false;
    if (d.idx[0] != a.idx[0] || (a.sel & 3) != dc) return false;
    *reg = d.idx[0];
    *c = dc;
    return true;
}

inline bool Find(const uint8_t* p, size_t n, Found* f, std::string* why = nullptr) {
    auto no = [&](const char* w) { if (why) *why = w; *f = Found{}; return false; };
    *f = Found{};
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
    if ((c[0] >> 16) != 0) return no("not a pixel shader");   // (program type 0: pixel)
    // the last three instructions seen, in order: the mul_sat, the mad, the mul
    Instr prev2, prev1;
    bool have2 = false, have1 = false;
    for (size_t at = 2; at < c.size();) {
        Instr in;
        if (!Decode(c, at, &in)) return no("an instruction did not decode");
        uint32_t r = 0, k = 0, r2 = 0, k2 = 0;
        if (have2 && have1 && IsTempByCb(in, kMul, 4, 2, &r, &k) && !((c[in.at] >> 13) & 1) &&
            IsTempByCb(prev2, kMul, 6, 1, &r2, &k2) && ((c[prev2.at] >> 13) & 1) && r2 == r && k2 == k &&
            prev1.op == kMad && prev1.n == 4 && prev1.o[0].type == kTemp && prev1.o[0].idx[0] == r && prev1.o[2].type == kImm32 &&
            prev1.o[3].type == kImm32 && std::fabs(prev1.o[2].imm - 0.8f) < 1e-6f && std::fabs(prev1.o[3].imm - 0.2f) < 1e-6f) {
            if (f->radiusAt) return no("Intel's radius found twice");
            f->shexAt = shex;
            f->nearAt = prev2.at;
            f->nearLen = prev2.len;
            f->radiusAt = in.at;
            f->radiusLen = in.len;
            f->reg = r;
            f->comp = k;
        }
        prev2 = prev1; have2 = have1;
        prev1 = in; have1 = true;
        at += in.len;
    }
    if (!f->radiusAt) return no("no ASSAO radius (mul_sat by cb4[6].y, mad by 0.8 / 0.2, mul by cb4[4].z) in it");
    return true;
}

// The shader with the radius `radius` (world units, > 0) and its near limit 1 / (1.2 radius); empty: not found (`why` says)
inline std::vector<uint8_t> Patch(const uint8_t* p, size_t n, float radius, std::string* why = nullptr) {
    Found f;
    if (!Find(p, n, &f, why)) return {};
    if (!(radius > 1e-4f) || !std::isfinite(radius)) { if (why) *why = "no radius"; return {}; }
    uint32_t bytes = 0;
    std::memcpy(&bytes, p + f.shexAt + 4, 4);
    std::vector<uint32_t> c(bytes / 4);
    std::memcpy(c.data(), p + f.shexAt + 8, c.size() * 4);
    std::vector<uint32_t> nearI, radI;
    dxbcfog::Emit(nearI, kMul, true, [&](std::vector<uint32_t>& v) {
        dxbcfog::DstTemp(v, f.reg, 1u << f.comp); dxbcfog::SrcTemp(v, f.reg, f.comp); dxbcfog::SrcImm(v, 1.0f / (1.2f * radius)); });
    dxbcfog::Emit(radI, kMul, false, [&](std::vector<uint32_t>& v) {
        dxbcfog::DstTemp(v, f.reg, 1u << f.comp); dxbcfog::SrcTemp(v, f.reg, f.comp); dxbcfog::SrcImm(v, radius); });
    // (the later one first: replacing it does not move the earlier)
    c.erase(c.begin() + (std::ptrdiff_t)f.radiusAt, c.begin() + (std::ptrdiff_t)(f.radiusAt + f.radiusLen));
    c.insert(c.begin() + (std::ptrdiff_t)f.radiusAt, radI.begin(), radI.end());
    c.erase(c.begin() + (std::ptrdiff_t)f.nearAt, c.begin() + (std::ptrdiff_t)(f.nearAt + f.nearLen));
    c.insert(c.begin() + (std::ptrdiff_t)f.nearAt, nearI.begin(), nearI.end());
    c[1] = (uint32_t)c.size();
    const long grow = (long)(c.size() * 4) - (long)bytes;
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
        if (off > f.shexAt) { off = (uint32_t)((long)off + grow); std::memcpy(&out[32 + i * 4], &off, 4); }
    }
    const uint32_t total = (uint32_t)out.size();
    std::memcpy(&out[24], &total, 4);
    uint32_t sum[4];
    dxbcfog::Checksum(out.data() + 20, out.size() - 20, sum);
    std::memcpy(&out[4], sum, 16);
    return out;
}

}  // namespace dxbcao
