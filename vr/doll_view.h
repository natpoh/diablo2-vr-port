// doll_view.h - [weight] monster_ragdoll: the mod's side of the monsters' weight (docs/notes/monster_ragdoll.md). How the bodies move is the doll library's
// (d2r_vr_doll.dll, vr/doll_lib.h, shared/d2r_vr_doll.h); here only what the mod hands it and draws of it. Pure (cleanroom/tests/doll_view_test.cpp).
//
// Humanoids only (the author, 2026-10-22: «механику скелета ставим только на гуманойдов»): a body may move only when its skeleton reads as a humanoid's
// (monsterbodies::Body::doll, vr/monster_humanoid.h) - an animal, an insect, a blob, a monster on the game's capsule stay as they are.
// Nothing of the game's moves: only the capsules the weapon meets and [debug] melee_boxes draws - not the drawn monster, not the game's.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "d2r_vr_doll.h"
#include "melee_boxes.h"
#include "monster_bodies.h"

namespace dollview {

// The head's frame (metres from the eyes, x right, y up, z back) -> the render world: the inverse of monsterbodies::RenderToHead (and bladeshape's own).
inline void HeadDirToRender(const monsterbodies::View& v, const float d[3], float out[3]) {
    for (int k = 0; k < 3; ++k) out[k] = d[0] * v.camR[k] - d[2] * v.camF[k];
    out[1] += d[1];
}
inline void HeadToRender(const monsterbodies::View& v, const float p[3], float out[3]) {
    HeadDirToRender(v, p, out);
    for (int k = 0; k < 3; ++k) out[k] += v.eye[k] / v.upm;
}

// A body of the snapshot as the weapon meets it: a limb as thick as its mesh, the game's capsule less the touch's margin.
inline float MeetRadius(const monsterbodies::Body& b) { return b.rig ? b.radius : monsterbodies::CoreRadius(b.radius); }

// The snapshot's bodies for the library (render metres; the radius as the weapon meets them), and back: each body's ends from the library's (the
// radius and the rest of the snapshot untouched).
inline int ToDoll(const monsterbodies::Snapshot& s, D2rdBody* out) {
    const int n = std::min(s.n, monsterbodies::kMax);
    for (int i = 0; i < n; ++i) {
        const monsterbodies::Body& b = s.b[i];
        D2rdBody& d = out[i];
        d = D2rdBody{};
        d.id = b.id;
        d.bone = b.rig ? b.bone : -1;
        for (int k = 0; k < 3; ++k) { d.a[k] = b.bottom[k]; d.b[k] = b.top[k]; }
        d.radius = MeetRadius(b);
        d.rig = b.rig ? 1 : 0;
        d.doll = b.doll ? 1 : 0;
    }
    return n;
}
inline void FromDoll(const D2rdBody* d, int n, monsterbodies::Snapshot* s) {
    for (int i = 0; i < n && i < s->n; ++i)
        for (int k = 0; k < 3; ++k) { s->b[i].bottom[k] = d[i].a[k]; s->b[i].top[k] = d[i].b[k]; }
}

// [debug] monster_ragdoll_view: what the doll does, drawn with [debug] melee_boxes (vr/melee_boxes.h's colours, no new ones in the effect).
//   the push: an amber arrow from where the weapon met the body the way the body was pushed, 5 cm per N s (the push slider in it), at most 60 cm,
//     an amber ball where it met; for kArrowMs after the push;
//   each humanoid: a grey bar standing over its head, 1 cm for every 2 kg it weighs (the weight slider in it), at most 1 m; at its foot a ball
//     its balance's colour - green standing, amber swaying, red at the edge of its balance (under a third, under two thirds, more) - moved by its
//     body's sway three times over, so it leans the way the monster was pushed.
// massOf(id) -> kg and balanceOf(id, off[3]) -> 0..1 are the library's (dolllib::Mass / Balance). Into the effect's capsules a / b (world units from
// eyeWorld, as monsterbodies::Pack); the snapshot as Bodies hands it over (swayed). How many.
struct Arrow {
    float at[3] = {}, imp[3] = {};   // render metres; N s (the push slider in it)
    uint64_t ms = 0;
};
constexpr uint64_t kArrowMs = 500;
constexpr float kArrowMPerNs = 0.05f, kArrowMaxM = 0.6f, kBarMPerKg = 0.005f, kBarMaxM = 1.0f;
template <class MassOf, class BalanceOf>
int PackDebug(const monsterbodies::Snapshot& s, const Arrow* arrows, int nArrows, uint64_t nowMs, const float eyeWorld[3], MassOf&& massOf, BalanceOf&& balanceOf,
              float (*a)[4], float (*b)[4], int max) {
    if (!(s.upm > 1e-3f) || max <= 0) return 0;
    int k = 0;
    const auto put = [&](const float p[3], const float q[3], float radiusM, int kind) {
        if (k >= max) return;
        for (int j = 0; j < 3; ++j) { a[k][j] = p[j] * s.upm - eyeWorld[j]; b[k][j] = q[j] * s.upm - eyeWorld[j]; }
        a[k][3] = radiusM * s.upm;
        b[k][3] = static_cast<float>(kind);
        ++k;
    };
    using meleeboxes::TouchKind;
    for (int i = 0; i < nArrows; ++i) {
        const Arrow& r = arrows[i];
        if (!r.ms || nowMs < r.ms || nowMs - r.ms > kArrowMs) continue;
        const float j = std::sqrt(r.imp[0] * r.imp[0] + r.imp[1] * r.imp[1] + r.imp[2] * r.imp[2]);
        if (!(j > 1e-6f)) continue;
        const float len = std::min(kArrowMaxM, j * kArrowMPerNs);
        const float tip[3] = {r.at[0] + r.imp[0] / j * len, r.at[1] + r.imp[1] / j * len, r.at[2] + r.imp[2] / j * len};
        put(r.at, tip, meleeboxes::kTouchArrowRadiusM, TouchKind(meleeboxes::kCAmber, meleeboxes::kTube));
        const float up[3] = {r.at[0], r.at[1] + 0.002f, r.at[2]};   // (a ball is a capsule of some length: the effect's ray test needs one)
        put(r.at, up, meleeboxes::kTouchRadiusM, TouchKind(meleeboxes::kCAmber, meleeboxes::kBall));
    }
    uint32_t done[monsterbodies::kMax];
    int nDone = 0;
    for (int i = 0; i < s.n && k < max; ++i) {
        const monsterbodies::Body& body = s.b[i];
        if (!body.doll) continue;
        bool seen = false;
        for (int d = 0; d < nDone; ++d) seen = seen || done[d] == body.id;
        if (seen) continue;
        done[nDone++] = body.id;
        float top[3] = {0.0f, -1e30f, 0.0f}, sx = 0.0f, sz = 0.0f, n = 0.0f;   // over the middle of its limbs, above the highest of them
        for (int j = i; j < s.n; ++j) {
            if (s.b[j].id != body.id || s.b[j].held) continue;   // (its body's limbs, not what it holds)
            top[1] = std::max(top[1], std::max(s.b[j].bottom[1], s.b[j].top[1]) + MeetRadius(s.b[j]));
            sx += s.b[j].bottom[0] + s.b[j].top[0];
            sz += s.b[j].bottom[2] + s.b[j].top[2];
            n += 2.0f;
        }
        top[0] = sx / n;
        top[2] = sz / n;
        top[1] += 0.15f;
        const float kg = massOf(body.id);
        const float bar[3] = {top[0], top[1] + std::min(kBarMaxM, std::max(0.0f, kg) * kBarMPerKg), top[2]};
        put(top, bar, 0.015f, TouchKind(meleeboxes::kCGrey, meleeboxes::kTube));
        float off[3] = {};
        const float bal = balanceOf(body.id, off);
        const float ball[3] = {top[0] + 3.0f * off[0], top[1] + 3.0f * off[1], top[2] + 3.0f * off[2]};
        const int colour = bal < 1.0f / 3.0f ? meleeboxes::kCGreen : bal < 2.0f / 3.0f ? meleeboxes::kCAmber : meleeboxes::kCRed;
        const float ballUp[3] = {ball[0], ball[1] + 0.002f, ball[2]};
        put(ball, ballUp, 0.04f, TouchKind(colour, meleeboxes::kBall));
    }
    return k;
}

}  // namespace dollview
