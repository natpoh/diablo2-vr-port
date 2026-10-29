// bone_segments.h - [weight] monster_ragdoll_all: a body for a skeleton whose model has no part meshes to read boxes from - each bone's box from the segments
// to its children as the game posed them (docs/notes/monster_ragdoll.md). Pure (cleanroom/tests/bone_segments_test.cpp).
//
// The idea is bearstonem/d2r-pcvr's (feat/monster-limb-push b18e11a, steamvr/limb_rig.h SegmentBoxes, MIT): a bone's box reaches from its origin to each of its
// children, in its own frame, as thick as a quarter of its longest reach - held between 1.5 % and 9 % of the skeleton's size. A skeleton's root and the bones
// that are no limbs (vr/monster_humanoid.h NotLimbName: fingers, toes, helpers, attach points, the "anim" bone the game moves its monsters by) get none - their
// segments still count for their parent: the "anim" bone on the floor under the hips would otherwise make a box from the floor to the hips (a zombie's
// torso that is not there). Bones come parents first (the game's order); a skeleton that does not is refused.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "monster_rig.h"

namespace bonesegments {

constexpr float kThick = 0.25f, kMinThick = 0.015f, kMaxThick = 0.09f;

// mats: the bones as posed, 16 floats each (row vectors, origin in [12..14]); notLimb[i] (may be null): bone i gets no box. Up to max boxes, by bone; how many.
inline int Boxes(const int16_t* parent, int n, const float (*mats)[16], const uint8_t* notLimb, monsterrig::BoneBox* out, int max) {
    if (n <= 0 || n > monsterrig::kMaxBones || max <= 0) return 0;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < n; ++i) {
        if (parent[i] >= i) return 0;   // (parents first, or not a skeleton we know)
        for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], mats[i][12 + k]); hi[k] = std::max(hi[k], mats[i][12 + k]); }
    }
    const float size = std::max(hi[0] - lo[0], std::max(hi[1] - lo[1], hi[2] - lo[2]));
    if (!std::isfinite(size) || !(size > 0.0f)) return 0;
    // each child's place in its parent's frame: (child - parent) x the inverse of the parent's 3x3
    static thread_local float reach[monsterrig::kMaxBones][3];
    static thread_local float longest[monsterrig::kMaxBones];
    static thread_local bool ok[monsterrig::kMaxBones];
    for (int i = 0; i < n; ++i) { longest[i] = 0.0f; ok[i] = false; }
    for (int b = 0; b < n; ++b) {
        const int p = parent[b];
        if (p < 0) continue;
        const float* m = mats[p];
        const float c00 = m[5] * m[10] - m[6] * m[9], c01 = m[6] * m[8] - m[4] * m[10], c02 = m[4] * m[9] - m[5] * m[8];
        const float det = m[0] * c00 + m[1] * c01 + m[2] * c02;
        if (!(std::fabs(det) > 1e-12f) || !std::isfinite(det)) continue;
        const float inv[9] = {c00 / det, (m[2] * m[9] - m[1] * m[10]) / det, (m[1] * m[6] - m[2] * m[5]) / det,
                              c01 / det, (m[0] * m[10] - m[2] * m[8]) / det, (m[2] * m[4] - m[0] * m[6]) / det,
                              c02 / det, (m[1] * m[8] - m[0] * m[9]) / det, (m[0] * m[5] - m[1] * m[4]) / det};
        const float d[3] = {mats[b][12] - m[12], mats[b][13] - m[13], mats[b][14] - m[14]};
        float r2 = 0.0f;
        for (int j = 0; j < 3; ++j) {
            reach[b][j] = d[0] * inv[j] + d[1] * inv[3 + j] + d[2] * inv[6 + j];
            r2 += reach[b][j] * reach[b][j];
        }
        if (!std::isfinite(r2)) continue;
        ok[b] = true;
        longest[p] = std::max(longest[p], std::sqrt(r2));
    }
    static thread_local monsterrig::BoneBox box[monsterrig::kMaxBones];
    static thread_local bool any[monsterrig::kMaxBones];
    for (int i = 0; i < n; ++i) any[i] = false;
    for (int b = 0; b < n; ++b) {
        const int p = parent[b];
        if (p < 0 || !ok[b] || parent[p] < 0 || (notLimb && notLimb[p]) || !(longest[p] > 0.0f)) continue;   // (the root, a bone that is no limb: no box of its own)
        const float* c = reach[b];
        if (std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]) > size) continue;
        const float r = std::clamp(longest[p] * kThick, size * kMinThick, size * kMaxThick);
        monsterrig::BoneBox& x = box[p];
        for (int k = 0; k < 3; ++k) {
            const float l = std::min(0.0f, c[k]) - r, h = std::max(0.0f, c[k]) + r;
            if (!any[p]) { x.min[k] = l; x.max[k] = h; }
            else { x.min[k] = std::min(x.min[k], l); x.max[k] = std::max(x.max[k], h); }
        }
        x.bone = p;
        x.mirror = -1;
        any[p] = true;
    }
    int k = 0;
    for (int i = 0; i < n && k < max; ++i)
        if (any[i]) out[k++] = box[i];
    return k;
}

}  // namespace bonesegments
