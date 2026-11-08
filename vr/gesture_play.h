#pragma once
// gesture_play.h - the Gestures tab's Play (Serg, 2026-11-08: "a Play button as in BodyWalk", "that moves the real hands as the pose wants",
// "played once, not looped as in BodyWalk"): a recorded gesture played back ONCE on the hero's own drawn arms in VR first person (F4) - the arms
// leave the controllers, follow the take from the body as it stands now at its real size, and go back to the controllers when it ends.
// Visual only: no game input, no cast, no hit (vr/gesture_play_wire.cpp pauses the touch's attacks and the grab while it plays).
//
// The take: the engine's own samples (shared/gesture_engine_abi.h BWGE_GetTake: the hands in the body frame the engine measures in - metres from
// the pelvis, x right, y up, z back, turned in it) or, from an engine without that call / BodyWalk over Game Link, made up from what the list
// carries: a pose's hands (BW_LinkGesture.joints) reached from the arms hanging, a glyph's or a motion's path (pts, a 2D shape) drawn by the hand
// on a sheet before the chest.
//
// Out: the hands where the body's drawing takes them (vr/body/arms.cpp skel::Input.hand / rot - metres from the head in the frame of its turn,
// x right, y up, z back, [0] RIGHT [1] LEFT), blended from the controllers in at the start and back to them at the end.
// Pure: no Windows (cleanroom/tests/gesture_overlay_test.cpp).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "gesture_engine_abi.h"
#include "skill_link_abi.h"

namespace gplay {

constexpr float kLeadS = 0.25f;   // the arms leave the controllers for the take's first place in this long
constexpr float kHoldS = 0.35f;   // and keep its last place (a pose: the pose) this long
constexpr float kTailS = 0.25f;   // then go back to the controllers in this long
constexpr float kAwayM = 0.6f;    // the player walked this far from where Play started: released at once
constexpr float kDefaultHeightM = 1.7f;

using Sample = BWGE_TakeSample;

struct Take {
    std::vector<Sample> s;   // in time, the first at 0
    float DurationS() const { return s.empty() ? 0.0f : std::max(0.0f, s.back().timeS); }
    // The whole playing: in, the take, its last place held, out.
    float TotalS() const { return kLeadS + DurationS() + kHoldS + kTailS; }
};

inline void QNorm(float q[4]) {
    const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!(l > 1e-6f)) { q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f; return; }
    for (int k = 0; k < 4; ++k) q[k] /= l;
}
// a to b by t, the short way (normalised lerp: close turns, every frame)
inline void QBlend(const float a[4], const float b[4], float t, float out[4]) {
    const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], s = d < 0.0f ? -1.0f : 1.0f;
    for (int k = 0; k < 4; ++k) out[k] = a[k] + (s * b[k] - a[k]) * t;
    QNorm(out);
}

// The take at `t` seconds from its start (held at its ends), interpolated.
inline Sample At(const Take& tk, float t) {
    Sample o{};
    if (tk.s.empty()) return o;
    if (!(t > tk.s.front().timeS)) return tk.s.front();
    if (!(t < tk.s.back().timeS)) return tk.s.back();
    size_t i = 1;
    while (i < tk.s.size() && tk.s[i].timeS < t) ++i;
    const Sample& a = tk.s[i - 1];
    const Sample& b = tk.s[i];
    const float span = b.timeS - a.timeS, u = span > 1e-6f ? (t - a.timeS) / span : 1.0f;
    o.timeS = t;
    o.handValid = a.handValid & b.handValid;
    for (int h = 0; h < 2; ++h) {
        for (int k = 0; k < 3; ++k) o.hand[h][k] = a.hand[h][k] + (b.hand[h][k] - a.hand[h][k]) * u;
        QBlend(a.rot[h], b.rot[h], u, o.rot[h]);
        o.grip[h] = a.grip[h] + (b.grip[h] - a.grip[h]) * u;
    }
    return o;
}

// How much of the played arm is the take's at `t` seconds since Play (0 the controller's, 1 the take's), and the take's own time then.
inline float Weight(const Take& tk, float t, float* takeT) {
    const float d = tk.DurationS();
    *takeT = std::clamp(t - kLeadS, 0.0f, d);
    if (t < 0.0f) return 0.0f;
    if (t < kLeadS) { const float u = t / kLeadS; return u * u * (3.0f - 2.0f * u); }
    const float out = kLeadS + d + kHoldS;
    if (t < out) return 1.0f;
    if (t < out + kTailS) { const float u = 1.0f - (t - out) / kTailS; return u * u * (3.0f - 2.0f * u); }
    return 0.0f;
}

// A controller held the usual way, pointing ahead (-z): no turn.
inline void Ahead(float q[4]) { q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f; }

// A take made up from the list's own data when the engine gives no samples. `heightM` scales nothing here (the joints are metres already); the
// sheet a glyph / motion is drawn on stands before the chest: 0.45 m ahead, its middle at the shoulders' height, 0.5 m across.
inline bool FromGesture(const BW_LinkGesture& g, Take* out) {
    out->s.clear();
    const bool useL = g.hand == BW_LINK_GH_LEFT || g.hand == BW_LINK_GH_BOTH, useR = g.hand != BW_LINK_GH_LEFT;
    // the hands hanging at the hips (where a pose is reached from; a hand the gesture does not use stays there)
    const float down[2][3] = {{-0.21f, -0.02f, -0.04f}, {0.21f, -0.02f, -0.04f}};
    if (g.type == BW_LINK_GT_POSE) {
        Sample a{}, b{};
        b.timeS = 0.7f;
        for (int h = 0; h < 2; ++h) {
            const int j = h ? BW_LINK_J_RHAND : BW_LINK_J_LHAND;
            const bool use = (h ? useR : useL) && (g.jointValid & (1u << j));
            for (int k = 0; k < 3; ++k) {
                a.hand[h][k] = down[h][k];
                b.hand[h][k] = use ? g.joints[j][k] : down[h][k];
            }
            Ahead(a.rot[h]);
            Ahead(b.rot[h]);
            a.grip[h] = 0.0f;
            b.grip[h] = use ? std::clamp(g.grip[h], 0.0f, 1.0f) : 0.0f;
        }
        a.handValid = b.handValid = (useL ? 1u : 0u) | (useR ? 2u : 0u);
        out->s.push_back(a);
        out->s.push_back(b);
        return true;
    }
    if (g.type != BW_LINK_GT_GLYPH && g.type != BW_LINK_GT_MOTION) return false;
    const int n = std::min<int>(g.pointCount ? g.pointCount : BW_LINK_GESTURE_POINTS, BW_LINK_GESTURE_POINTS);
    if (n < 2) return false;
    const float dur = g.type == BW_LINK_GT_GLYPH ? 1.6f : 1.4f;
    const int h = useR ? 1 : 0;   // (a glyph is drawn by one hand: the right, or the left)
    const float shoulderY = 0.42f + 0.08f, side = h ? 0.12f : -0.12f;   // (the chest 0.42 m over the pelvis, the shoulders a little higher; the sheet before the drawing hand)
    for (int i = 0; i < n; ++i) {
        Sample s{};
        s.timeS = dur * (float)i / (float)(n - 1);
        for (int k = 0; k < 2; ++k) {
            for (int c = 0; c < 3; ++c) s.hand[k][c] = down[k][c];
            Ahead(s.rot[k]);
        }
        const float x = std::isfinite(g.pts[i][0]) ? std::clamp(g.pts[i][0], -0.5f, 0.5f) : 0.0f, y = std::isfinite(g.pts[i][1]) ? std::clamp(g.pts[i][1], -0.5f, 0.5f) : 0.0f;
        s.hand[h][0] = side + 0.5f * x;
        s.hand[h][1] = shoulderY + 0.5f * y;
        s.hand[h][2] = -0.45f;
        s.grip[h] = 0.0f;
        s.handValid = h ? 2u : 1u;
        out->s.push_back(s);
    }
    return true;
}

// The body frame's point (metres from the pelvis, x right, y up, z back, the heading) in the drawing's frame (metres from the head, the frame of
// its turn: the same axes, the head's pitch in) - where the engine puts the pelvis from the head (shared/gesture_engine_abi.h BWGE_PoseZone).
inline void ToHeadFrame(const float p[3], float heightM, float pitchRad, float out[3]) {
    const float H = heightM > 0.5f && heightM < 2.6f ? heightM : kDefaultHeightM;
    const float sp = std::sin(pitchRad), cp = std::cos(pitchRad);
    const float fwd[3] = {0.0f, sp, -cp}, up[3] = {0.0f, cp, sp};   // (the head's own forward and up in its turn's frame, + pitch up)
    for (int k = 0; k < 3; ++k) out[k] = p[k] - 0.07f * H * fwd[k] - 0.04f * H * up[k];
    out[1] -= 0.11f * H + 0.42f;
}

// One frame of the arms while Play runs: `ctl` the controllers' hands now (the drawing's frame, [0] RIGHT [1] LEFT, valid bits 1 right 2 left),
// out the drawn ones. False when nothing is played now (before, after, nothing to play).
struct Arms {
    float hand[2][3];   // [0] right, [1] left (the drawing's order)
    float rot[2][4];
    float grip[2];
    uint32_t valid = 0;   // bit 0 right, bit 1 left: the hands Play moves
    float weight = 0;
};
inline bool Frame(const Take& tk, float sinceS, float heightM, float pitchRad, const float ctl[2][3], const float ctlRot[2][4], const float ctlGrip[2], Arms* out) {
    if (tk.s.empty() || sinceS < 0.0f || sinceS >= tk.TotalS()) return false;
    float tt = 0.0f;
    const float w = Weight(tk, sinceS, &tt);
    const Sample s = At(tk, tt);
    *out = Arms{};
    out->weight = w;
    for (int d = 0; d < 2; ++d) {   // d: the drawing's side (0 right); the take's hand is 1 - d
        const int h = 1 - d;
        if (!(s.handValid & (1u << h))) {   // a hand the take does not move: the controller's, as it is
            for (int k = 0; k < 3; ++k) out->hand[d][k] = ctl[d][k];
            for (int k = 0; k < 4; ++k) out->rot[d][k] = ctlRot[d][k];
            out->grip[d] = ctlGrip[d];
            continue;
        }
        float p[3];
        ToHeadFrame(s.hand[h], heightM, pitchRad, p);
        for (int k = 0; k < 3; ++k) out->hand[d][k] = ctl[d][k] + (p[k] - ctl[d][k]) * w;
        QBlend(ctlRot[d], s.rot[h], w, out->rot[d]);
        out->grip[d] = std::max(0.0f, ctlGrip[d]) + (s.grip[h] - std::max(0.0f, ctlGrip[d])) * w;
        out->valid |= 1u << d;
    }
    return out->valid != 0;
}

}  // namespace gplay
