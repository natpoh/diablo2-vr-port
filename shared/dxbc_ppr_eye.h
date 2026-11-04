// dxbc_ppr_eye.h - [render] reflections_per_eye: the game's puddle / blood / water reflections in the right place for each eye.
//
// zbiromir (Quest 3S, F4, 2026-11-03): reflections in puddles and blood pools look as if they float above the puddle - eye strain.
// The game's reflections are pixel projected reflections (data/hd/shaders): cs_ppr_projection_c takes every pixel's world point from
// the linear depth, mirrors it over the water planes (cb13) and writes it into the pixel it is seen at in the water (by cb3[21..24],
// view * projection); ps_ppr_reflection_p reads that and lays the scene's colour of the mirrored pixel over the water. Both take the
// world point as the game's camera record (cb3, words 20 on) allows:
//     view = (ndc.x * depth * invProj[0][0], ndc.y * depth * invProj[1][1], -depth);   world = view * invView   (rows 9..12, row 12 the eye)
// - the inverse projection's diagonal only. The replay ("one pass, two pictures", the default) gives the right eye its camera FOLDED
// ([stereo] replay_proj 3, vr/render/depth/depth_fix.cpp): the view and its inverse stay the left eye's and the eyes' distance goes into
// the projection's translation, which this never reads. The right eye's point came out one eye distance to the side, was mirrored and
// put in the picture by the right eye's (right) view * projection: each reflection drawn as seen from twice the eyes' distance - twice
// the parallax, half the depth: it stood above the water. Here the one `mad world.xyz, cb3[12].xyz, valid, ...` (the inverse view's
// eye) reads cb10[1] instead: the frame's eye position, which both shaders take as their eye anyway (the ray to the water, the Fresnel
// term) and which the replay moves for the right eye. Wherever the camera is not folded (the left eye, flat, the eyes by turns) cb10[1]
// is the inverse view's own eye: the same picture. One operand changed: no instruction more on the GPU.
//
// The shaders are found by their own code, never a hash of Blizzard's (an update that changes them: simply not found, the game's
// reflections as they are): a structured UAV, cb3, cb10 and cb13 declared, the inverse view's rows cb3[9] and cb3[11] and the eye
// cb10[1] and the linear depth's range cb10[53] read, and cb3[12] read exactly once - as that mad's first source.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "dxbc_fog_height.h"   // dxbcfog:: the DXBC checksum, the SM5 token reader

namespace dxbcppr {

enum : uint32_t { kDclUavStructured = 158 /* 0x9E */ };

struct Found {
    size_t shexAt = 0;      // the SHEX / SHDR chunk in the container
    size_t slotAt = 0;      // a token index in the chunk's tokens: cb3[12]'s first index (its slot; the row follows)
    uint32_t dst = 0, valid = 0;   // the mad's destination register and the register its "inside the depth range" factor is in (the log)
    bool compute = false;   // a compute shader (cs_5_0), else a pixel shader
};

namespace detail {
// a constant buffer operand cb<slot>[<row>] with plain immediate indices at t (any operand token: the raw count below)
inline bool CbAt(const uint32_t* t, size_t avail, uint32_t* slot, uint32_t* row) {
    if (avail < 3) return false;
    const uint32_t tok = t[0];
    if (((tok >> 12) & 0xFF) != dxbcfog::kCb || ((tok >> 20) & 3) != 2 || ((tok >> 22) & 0x3F) != 0) return false;
    const size_t at = (tok & 0x80000000u) ? 2 : 1;
    if (at + 2 > avail) return false;
    *slot = t[at];
    *row = t[at + 1];
    return true;
}
inline bool Chunk(const uint8_t* p, size_t n, size_t* shex, std::vector<uint32_t>* c, std::string* why) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (n < 32 || std::memcmp(p, "DXBC", 4) != 0) return no("not DXBC");
    uint32_t chunks = 0;
    std::memcpy(&chunks, p + 28, 4);
    if (32 + (size_t)chunks * 4 > n) return no("chunk table cut short");
    *shex = 0;
    for (uint32_t i = 0; i < chunks; ++i) {
        uint32_t off = 0;
        std::memcpy(&off, p + 32 + i * 4, 4);
        if ((size_t)off + 8 <= n && (std::memcmp(p + off, "SHEX", 4) == 0 || std::memcmp(p + off, "SHDR", 4) == 0)) *shex = off;
    }
    if (!*shex) return no("no shader code chunk");
    uint32_t bytes = 0;
    std::memcpy(&bytes, p + *shex + 4, 4);
    if (*shex + 8 + (size_t)bytes > n || bytes < 8) return no("shader chunk cut short");
    c->assign(bytes / 4, 0);
    std::memcpy(c->data(), p + *shex + 8, c->size() * 4);
    return true;
}
}  // namespace detail

inline bool Find(const uint8_t* p, size_t n, Found* f, std::string* why = nullptr) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    size_t shex = 0;
    std::vector<uint32_t> c;
    if (!detail::Chunk(p, n, &shex, &c, why)) return false;
    // c[0]: the version token - its program type in bits 16..31 (0 pixel, 5 compute)
    const uint32_t type = c[0] >> 16;
    if (type != 0 && type != 5) return no("neither a pixel nor a compute shader");
    Found r;
    r.shexAt = shex;
    r.compute = type == 5;
    bool uav = false, cb3 = false, cb10 = false, cb13 = false;
    int eye = 0, range = 0, row9 = 0, row11 = 0, row12 = 0;
    for (size_t at = 2; at < c.size();) {
        dxbcfog::Instr in;
        if (!dxbcfog::Decode(c, at, &in)) return no("an instruction did not decode");
        if (in.op == kDclUavStructured) uav = true;
        if (in.op == dxbcfog::kDclCb && in.len >= 3) {
            dxbcfog::Operand o;
            if (dxbcfog::ReadOperand(&c[at + 1], in.len - 1, &o) && o.dims == 2) {
                if (o.idx[0] == 3 && o.idx[1] >= 13) cb3 = true;
                if (o.idx[0] == 10 && o.idx[1] >= 54) cb10 = true;
                if (o.idx[0] == 13) cb13 = true;
            }
        } else if (in.op != dxbcfog::kCustomData) {
            // every constant buffer operand this instruction reads (raw: whatever its opcode)
            for (size_t k = at + 1; k < at + in.len; ++k) {
                uint32_t slot = 0, row = 0;
                if (!detail::CbAt(&c[k], at + in.len - k, &slot, &row)) continue;
                eye += slot == 10 && row == 1;
                range += slot == 10 && row == 53;
                row9 += slot == 3 && row == 9;
                row11 += slot == 3 && row == 11;
                if (slot == 3 && row == 12) {
                    ++row12;
                    // the one place: mad dst.xyz, cb3[12].xyz?, valid, sum (the game's into .xyz; fxc's into any three components)
                    const uint32_t mask = in.o[0].sel & 0xF;
                    const bool three = mask == 7 || mask == 0xB || mask == 0xD || mask == 0xE;
                    if (in.op == dxbcfog::kMad && in.n == 4 && in.o[0].type == dxbcfog::kTemp && in.o[0].mode == 0 && three &&
                        in.o[1].type == dxbcfog::kCb && in.o[1].idx[0] == 3 && in.o[1].idx[1] == 12 && in.o[2].type == dxbcfog::kTemp &&
                        in.o[3].type == dxbcfog::kTemp) {
                        const size_t src1 = at + 1 + ((c[at] & 0x80000000u) ? 1 : 0) + in.o[0].len;   // the mad's first source
                        if (k == src1) {
                            r.slotAt = k + ((c[k] & 0x80000000u) ? 2 : 1);
                            r.dst = in.o[0].idx[0];
                            r.valid = in.o[2].idx[0];
                        }
                    }
                }
            }
        }
        at += in.len;
    }
    if (!uav) return no("no structured UAV (not a reflection pass)");
    if (!cb3 || !cb10 || !cb13) return no("cb3, cb10 and cb13 are not all declared");
    if (!eye || !range || !row9 || !row11) return no("the eye (cb10[1]), the depth's range (cb10[53]) or the inverse view's rows are not read");
    if (row12 != 1) return no(row12 ? "cb3[12] read more than once" : "cb3[12] not read");
    if (!r.slotAt) return no("cb3[12] is not the first source of a mad into three components");
    *f = r;
    return true;
}

// The patched container (a new one: the game's left alone), or empty with `why`.
inline std::vector<uint8_t> Patch(const uint8_t* p, size_t n, std::string* why = nullptr, Found* found = nullptr) {
    Found f;
    if (!Find(p, n, &f, why)) return {};
    std::vector<uint8_t> out(p, p + n);
    const size_t at = f.shexAt + 8 + f.slotAt * 4;   // the operand's two indices in the container's bytes
    const uint32_t slot = 10, row = 1;
    std::memcpy(&out[at], &slot, 4);
    std::memcpy(&out[at + 4], &row, 4);
    uint32_t sum[4];
    dxbcfog::Checksum(out.data() + 20, out.size() - 20, sum);
    std::memcpy(&out[4], sum, 16);
    if (found) *found = f;
    return out;
}

}  // namespace dxbcppr
