// xbow_grip.h - the crossbow's left (support) hand, the pure part: no game memory, no state of the plugin. vr/skeletons.cpp
// runs it (PoseBody's staff block, NoteCrossbowBarrel), cleanroom/tests/xbow_grip_test.cpp checks it. The ideas are
// bearstonem/d2r-pcvr's (MIT; steamvr/crossbow_pose.h, v0.3.16, 2026-10-15), each behind its own [hands] key, VR F4 only:
//
//   xbow_grip_latch      Latch: the left grip holds the crossbow while held, and lets go of it - and has to be let go of before it
//                        takes it again - when the weapon or the hero's model changes, or the left controller loses tracking. Lost
//                        tracking frees the hand (before: the hand jumped onto the stock), and no grip reading from the bridge
//                        means no hold (before: always held).
//   xbow_grip_wide       WideCaptureAllowed: the game's grip may be taken from a pose with the hands far apart (after a swap from
//                        a shield set) - the crossbow is placed by the controller, not by that pose.
//   xbow_support_model   BarrelInCog / FromBolt / Support: the left palm under the barrel of the crossbow AS DRAWN (its bolt_helper
//                        bone), a few cm below it and never past the bow's limbs - not on the line set by eye.
//
// Matrices are row-vector 4x4 like the skeletons' (a point p goes p * M; rows 0..2 the axes, row 3 the place). The shot is not
// here: the bolt still flies along the old line (vrcam HandRay), whatever the hand does.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace xbowgrip {

struct V { float x = 0, y = 0, z = 0; };
inline V Add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V Sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V Mul(V a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline bool Finite(V a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

struct M { float m[16] = {}; };
inline V Row(const M& a, int r) { return {a.m[r * 4], a.m[r * 4 + 1], a.m[r * 4 + 2]}; }
inline V Place(const M& a) { return Row(a, 3); }
inline void SetPlace(M& a, V p) { a.m[12] = p.x; a.m[13] = p.y; a.m[14] = p.z; }
inline M Mul(const M& a, const M& b) {
    M r;
    for (int row = 0; row < 4; ++row)
        for (int k = 0; k < 4; ++k)
            for (int col = 0; col < 4; ++col) r.m[row * 4 + col] += a.m[row * 4 + k] * b.m[k * 4 + col];
    return r;
}
// An affine matrix's inverse; false when it is not one (not finite, flat, or a last column that is not 0 0 0 1).
inline bool Inverse(const M& a, M* out) {
    const float* m = a.m;
    for (float v : a.m) if (!std::isfinite(v)) return false;
    if (std::fabs(m[3]) > 1e-4f || std::fabs(m[7]) > 1e-4f || std::fabs(m[11]) > 1e-4f || std::fabs(m[15] - 1.0f) > 1e-4f) return false;
    const float c00 = m[5] * m[10] - m[6] * m[9], c01 = m[6] * m[8] - m[4] * m[10], c02 = m[4] * m[9] - m[5] * m[8];
    const float det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (!(std::fabs(det) > 1e-9f)) return false;
    const float id = 1.0f / det;
    M r;
    r.m[0] = c00 * id; r.m[1] = (m[2] * m[9] - m[1] * m[10]) * id; r.m[2] = (m[1] * m[6] - m[2] * m[5]) * id;
    r.m[4] = c01 * id; r.m[5] = (m[0] * m[10] - m[2] * m[8]) * id; r.m[6] = (m[2] * m[4] - m[0] * m[6]) * id;
    r.m[8] = c02 * id; r.m[9] = (m[1] * m[8] - m[0] * m[9]) * id; r.m[10] = (m[0] * m[5] - m[1] * m[4]) * id;
    const V t = Place(a);
    r.m[12] = -(t.x * r.m[0] + t.y * r.m[4] + t.z * r.m[8]);
    r.m[13] = -(t.x * r.m[1] + t.y * r.m[5] + t.z * r.m[9]);
    r.m[14] = -(t.x * r.m[2] + t.y * r.m[6] + t.z * r.m[10]);
    r.m[15] = 1.0f;
    *out = r;
    return true;
}

// ---- xbow_grip_latch ----
// grip: the left controller's squeeze 0..1 (< 0 or not finite: the bridge sends none). Squeezed above 0.6, let go below 0.4.
// The latch belongs to one weapon key and one hero model: another one starts it afresh, and a fresh latch takes nothing until
// it has seen the grip let go - a squeeze still held from the shoulder swap (or from before tracking came back) takes nothing.
// While held it takes the stock as soon as the hand is in reach (the grip may be squeezed before the hand gets there), and
// holds it, wherever the hand goes, until the grip is let go. busy: the hand holds something else (a belt potion).
struct Latch {
    uint64_t hero = 0;
    uint32_t weapon = 0;
    bool known = false, armed = false, pressed = false, on = false;
    bool Current(uint64_t h, uint32_t w) const { return known && hero == h && weapon == w; }
    void Reset() { *this = Latch{}; }
    bool Step(uint64_t h, uint32_t w, bool tracked, float grip, bool inReach, bool busy) {
        if (!tracked || !std::isfinite(grip) || grip < 0.0f) { Reset(); return false; }
        if (!Current(h, w)) { Reset(); known = true; hero = h; weapon = w; }
        if (grip < 0.4f) { armed = true; pressed = on = false; return false; }
        if (!armed || busy) { on = false; pressed = grip > 0.6f || pressed; return false; }
        const bool held = grip > 0.6f || pressed;   // between 0.4 and 0.6 it stays as it was
        on = held && (on || inReach);
        pressed = held;
        return on;
    }
};

// ---- xbow_handover ----
// The crossbow passed from hand to hand: the hand that does not hold it squeezes its grip anew (a press seen let go first) with its palm
// within reach of the crossbow's grip - where the holding hand's palm is - and the crossbow is in THAT hand; the other one lets go and is
// free (it may take the fore-grip as the support hand afterwards, with a press of its own). Visual and aim only: the game's slots and
// attack are untouched. grip: each controller's squeeze 0..1 (< 0 none); tracked: each controller; nearM: the taking hand's grip point to
// the crossbow's grip, metres. Step returns true when the holder changed.
struct Handover {
    int holder = 0;                     // 0 right, 1 left
    bool pressed[2] = {}, armed[2] = {};
    void Reset(int home) { *this = Handover{}; holder = home & 1; }
    // supportHeld: the other hand holds the crossbow now (its grip on the fore-grip, the support latch): then the holding hand LETTING GO
    // of its grip passes it - the way a person hands a thing over (0.170: Serg took it at the fore-grip with the free hand, never near the
    // grip; the near-the-grip squeeze below is kept too).
    bool Step(const float grip[2], const bool tracked[2], float nearM, float reachM, bool supportHeld = false) {
        bool fresh[2] = {}, released[2] = {};
        for (int s = 0; s < 2; ++s) {
            const float g = grip[s];
            if (!tracked[s] || !std::isfinite(g) || g < 0.0f) { pressed[s] = false; armed[s] = false; continue; }
            if (g < 0.4f) armed[s] = true;
            const bool p = g > 0.6f || (g >= 0.4f && pressed[s]);
            fresh[s] = p && !pressed[s] && armed[s];
            released[s] = !p && pressed[s];
            pressed[s] = p;
        }
        const int taker = holder ^ 1;
        const bool letGo = supportHeld && released[holder] && tracked[taker];
        if (letGo || (fresh[taker] && std::isfinite(nearM) && nearM <= reachM)) {
            holder = taker;
            armed[taker ^ 1] = false;   // the hand that let go starts afresh: a squeeze still held takes nothing back
            return true;
        }
        return false;
    }
};

// How near the taking hand counts for Handover::Step: its grip point to the crossbow's grip (the holding palm) gripM, and to the support
// hand's place on the fore-grip supportM (< 0 none yet). It takes the crossbow only nearer the grip than the fore-grip - a squeeze at the
// fore-grip is the support's - and within reachM; else "far" (infinity). Two real controllers cannot bring their grip points much closer
// than 10-15 cm (0.170: a 12 cm reach never fired), so the reach is wider than that and the fore-grip decides.
inline float HandoverNear(float gripM, float supportM, float reachM) {
    if (!std::isfinite(gripM) || gripM > reachM) return INFINITY;
    if (std::isfinite(supportM) && supportM >= 0.0f && supportM <= gripM) return INFINITY;
    return gripM;
}

// ---- xbow_grip_wide ----
// The game's grip of a crossbow used to be refused while the animation held the hands more than 1.0 model unit apart (a stride,
// the idle after a swap): placed by that pose, the model was a metre off. Placed by the controller (xbow_gun_frame with a fixed
// stock axis) the pose only gives the attach relation, so a wide one is fine - while the attach bone is shown (a hidden one,
// shrunk, is a weapon in transit).
inline bool WideCaptureAllowed(float palmGap, bool controllerPlaced, float mountScale) {
    if (!std::isfinite(palmGap) || palmGap < 0.0f) return false;
    if (!controllerPlaced) return palmGap <= 1.0f;
    return std::isfinite(mountScale) && mountScale >= 0.3f && mountScale <= 3.0f;
}

// ---- xbow_support_model ----
// The barrel: the bolt_helper bone's +Z (where the bolt points), through the point of the bolt's line level with where the bow's
// limbs join the stock (between L_bowBend and R_bowBend) - the front of the stock. Its up is square to the barrel and to the line
// between the limbs (the crossbow's own across), turned so that the trigger is below it; with no trigger bone, the side the
// bolt_helper's +Y is on (that alone, as d2r-pcvr takes it, came out sideways: test-42, 2026-10-16). Given the bind pose's INVERSE
// matrices as the skeleton stores them (+0x50 of each bone), it is returned in the cog bone's frame: the cog's pose now carries it.
struct Line { V center, dir, up; float scale = 0; };
inline bool FromBolt(const M& bolt, Line* out) {
    const V c = Place(bolt), z = Row(bolt, 2), y = Row(bolt, 1), x = Row(bolt, 0);
    const float scale = std::sqrt(Dot(z, z));
    if (!Finite(c) || !std::isfinite(scale) || scale < 0.01f || scale > 20.0f) return false;
    const V dir = Mul(z, 1.0f / scale);
    V up = Sub(y, Mul(dir, Dot(y, dir)));
    const float ul = std::sqrt(Dot(up, up));
    if (!(ul > 1e-6f) || std::fabs(Dot(x, Cross(y, z))) < 1e-8f) return false;
    up = Mul(up, 1.0f / ul);
    *out = {c, dir, up, scale};
    return true;
}
inline bool BarrelInCog(const M& inverseCog, const M& inverseBolt, const M& inverseLeft, const M& inverseRight, M* out,
                        const M* inverseTrigger = nullptr) {
    M bolt, left, right, check;
    if (!Inverse(inverseCog, &check) || !Inverse(inverseBolt, &bolt) || !Inverse(inverseLeft, &left) || !Inverse(inverseRight, &right)) return false;
    Line line;
    if (!FromBolt(bolt, &line)) return false;
    const V front = Mul(Add(Place(left), Place(right)), 0.5f);
    const V center = Add(line.center, Mul(line.dir, Dot(Sub(front, line.center), line.dir)));
    // up: square to the barrel and to the limbs' line; a limbs' line along the barrel (not a crossbow's) keeps the helper's
    const V side = Sub(Place(right), Place(left));
    const V across = Sub(side, Mul(line.dir, Dot(side, line.dir)));
    const float sl = std::sqrt(Dot(side, side)), al = std::sqrt(Dot(across, across));
    V up = line.up;
    if (std::isfinite(al) && al > 0.2f * sl && al > 1e-6f) {
        up = Cross(line.dir, Mul(across, 1.0f / al));
        M trigger;
        bool told = false;
        if (inverseTrigger && Inverse(*inverseTrigger, &trigger)) {   // the trigger hangs below the barrel
            const float below = Dot(Sub(Place(trigger), center), up);
            if (std::isfinite(below) && std::fabs(below) > 1e-4f * line.scale) { if (below > 0.0f) up = Mul(up, -1.0f); told = true; }
        }
        if (!told && Dot(up, line.up) < 0.0f) up = Mul(up, -1.0f);
    }
    const V x = Cross(up, line.dir);   // a right-handed frame of unit axes, scaled as the bolt helper
    M barrel;
    for (int r = 0; r < 3; ++r) {
        const V a = r == 0 ? x : r == 1 ? up : line.dir;
        barrel.m[r * 4] = a.x * line.scale; barrel.m[r * 4 + 1] = a.y * line.scale; barrel.m[r * 4 + 2] = a.z * line.scale;
    }
    barrel.m[15] = 1.0f;
    SetPlace(barrel, center);
    *out = Mul(barrel, inverseCog);
    return true;
}
// ---- the hand by its palm, not its wrist ----
// The palm's middle - halfway from the wrist (the hand bone's origin) to the middle finger's knuckle, as vr/grip_fit.h and every grip in
// the hook take it - in the hand bone's own frame, from the rig's BIND pose: the hand's own bones, whatever the fingers do this frame (a
// knuckle read off a pose whose fingers have not followed the hand yet put the wrist on the point, test-57).
inline bool PalmInHand(const M& handBind, const M& knuckleBind, V* out) {
    M inverse;
    if (!Inverse(handBind, &inverse)) return false;
    const V k = Place(Mul(knuckleBind, inverse));   // the knuckle in the hand's frame
    if (!Finite(k) || !(Dot(k, k) > 1e-12f)) return false;
    *out = Mul(k, 0.5f);
    return true;
}
// Where a posed hand's palm's middle is.
inline V PalmOf(const M& hand, V palmInHand) {
    M p; p.m[0] = p.m[5] = p.m[10] = p.m[15] = 1.0f; SetPlace(p, palmInHand);
    return Place(Mul(p, hand));
}
// The hand matrix moved (not turned) so that its palm's middle is on `target`.
inline M PlaceByPalm(M hand, V palmInHand, V target) {
    SetPlace(hand, Sub(target, Sub(PalmOf(hand, palmInHand), Place(hand))));
    return hand;
}

// [hands] xbow_support_close: the hand turned so that its palm's way out (outLocal, in the hand bone's own frame) points along outWorld and
// its knuckle line (acrossLocal) along acrossWorld (made square to it) - the hand's place, the lengths of its axes and its handedness (a left
// hand's bones are mirrors) kept. False when a direction is degenerate.
inline bool HandTurnTo(const M& hand, V outLocal, V acrossLocal, V outWorld, V acrossWorld, M* turned) {
    auto unit = [](V a, V* o) { const float l = std::sqrt(Dot(a, a)); if (!(l > 1e-6f) || !std::isfinite(l)) return false; *o = Mul(a, 1.0f / l); return true; };
    V a1, a2, b1, b2;
    if (!unit(outLocal, &a1) || !unit(Sub(acrossLocal, Mul(a1, Dot(acrossLocal, a1))), &a2)) return false;
    if (!unit(outWorld, &b1) || !unit(Sub(acrossWorld, Mul(b1, Dot(acrossWorld, b1))), &b2)) return false;
    const V a3 = Cross(a1, a2);
    // the hand's handedness: the sign its own rows have
    const V r0 = Row(hand, 0), r1 = Row(hand, 1), r2 = Row(hand, 2);
    const float det = Dot(r0, Cross(r1, r2));
    if (!std::isfinite(det) || std::fabs(det) < 1e-12f) return false;
    const V b3 = Mul(Cross(b1, b2), det < 0.0f ? -1.0f : 1.0f);
    // rows of the rotation: e_i -> sum_k a_k[i] * b_k (local -> world, row vectors), each row scaled back to the hand's own length
    const float len[3] = {std::sqrt(Dot(r0, r0)), std::sqrt(Dot(r1, r1)), std::sqrt(Dot(r2, r2))};
    const float A[3][3] = {{a1.x, a1.y, a1.z}, {a2.x, a2.y, a2.z}, {a3.x, a3.y, a3.z}};
    const V B[3] = {b1, b2, b3};
    M out = hand;
    for (int i = 0; i < 3; ++i) {
        V r = Add(Add(Mul(B[0], A[0][i]), Mul(B[1], A[1][i])), Mul(B[2], A[2][i]));
        r = Mul(r, len[i]);
        out.m[i * 4] = r.x; out.m[i * 4 + 1] = r.y; out.m[i * 4 + 2] = r.z;
    }
    *turned = out;
    return true;
}
// A direction or a point mirrored across the plane through `at` square to `side` (the crossbow's own across: the right hand's grip seen as
// the left hand's).
inline V MirrorDir(V d, V side) { return Sub(d, Mul(side, 2.0f * Dot(d, side))); }
inline V MirrorPoint(V p, V at, V side) { return Add(at, MirrorDir(Sub(p, at), side)); }

// [hands] xbow_hold_grip: where the crossbow's attach bone goes so that the crossbow's GRIP - where the game's own hand holds it, its palm's
// middle in the attach bone's frame as the game's animation has it (a rigid child of that hand: the same in every pose) - is in `palm`.
// `attach` gives the bone's turn and scale (its place is not used). Before, the attach bone's origin itself went into the palm.
inline V OriginForGrip(M attach, V gripInAttach, V palm) {
    SetPlace(attach, {0, 0, 0});
    return Sub(palm, PalmOf(attach, gripInAttach));
}

// The item as the renderer draws it (its world matrix) against the attach bone it hangs on (our posed bone in the world): the item's
// frame in the attach bone's - whatever the renderer puts between them. Both world translations are taken off first (they run large).
inline bool RenderedRelation(const M& itemWorld, const M& attachWorld, M* out) {
    M item = itemWorld, wrist = attachWorld, inverse;
    SetPlace(item, Sub(Place(itemWorld), Place(attachWorld)));
    SetPlace(wrist, {0, 0, 0});
    if (!Finite(Place(item)) || !Inverse(wrist, &inverse)) return false;
    *out = Mul(item, inverse);
    return true;
}
// Where the left palm goes: along the barrel as far ahead of the right palm as aheadM says (xbow_left_at_cm), but at least
// backM behind the front of the stock (the limbs), and dropM below the barrel - the palm under it, holding it up. upm: model
// units per metre.
constexpr float kBackM = 0.06f;
constexpr float kDropM = 0.07f;   // d2r-pcvr's SupportDrop: the palm's middle below the bolt's axis
inline V Support(const Line& line, V rightPalm, float aheadM, float dropM, float upm) {
    const float along = std::min(Dot(Sub(rightPalm, line.center), line.dir) + aheadM * upm, -kBackM * upm);
    return Sub(Add(line.center, Mul(line.dir, along)), Mul(line.up, dropM * upm));
}
// xbow_support_model's place, by the crossbow alone - backM behind the front of the stock (where the limbs meet it), dropM below the
// barrel; the right palm does not move it (test-42: "the support hand holds it at the front, under the prod"). The caller gives
// kBackM / kDropM (d2r-pcvr's numbers) plus [hands] xbow_support_back_adj_cm / xbow_support_drop_adj_cm, small adjustments around 0.
inline V SupportAt(const Line& line, float backM, float dropM, float upm) {
    return Sub(Sub(line.center, Mul(line.dir, backM * upm)), Mul(line.up, dropM * upm));
}

}  // namespace xbowgrip
