#pragma once
// hand_orbs.h - [hud] hand_vitals, style "orbs": the hero's life and mana as two 3D glass balls with liquid in them, one on the back of
// each wrist like a medallion. Pure: no game, no Windows, no OpenXR, no clock (the caller passes the time), so the test
// (cleanroom/tests/hand_orbs_test.cpp) drives it with synthetic arms and lives. The drawing is the ReShade effect's
// (reshade/D2R_DepthFog.fx, the ORBS block: the balls are ray-cast per eye in the world with the game's depth, so a hand or a monster in front
// hides them); the wiring - the arm from the skeleton, the numbers from vr/hand_vitals_wire.h, the uniforms - is a block in vr/vrcam.cpp.
//
// Where a ball sits. The hero's own forearm as the skeleton hook poses it (skel::HeroForearm) gives the elbow, the wrist and the way the palm
// faces ("across", out of the palm), in BodyWalk's hand frame: metres from the head, x right, y up, z back, head yaw only. The ball's middle is
// on the back of the hand: from the wrist, X `alongCm` on toward the knuckles (the hand's axis, else the forearm's), Y `sideCm` across the hand (+ toward the
// thumb) and Z `upCm` out of the back of the hand (against `across`, made square to the axis) - three numbers for each wrist (handvitals::OrbSpot), the right wrist
// taking the left's when `orbMirror` is set. Life is on the LEFT wrist and mana on the RIGHT one, as the game's own globes are (a setting swaps them, or puts both
// on one hand, side by side across the hand). Without a posed arm (the arms off) the controller's grip point stands in, the ball `upCm` above it. Nothing here
// moves with the render size or the window: the place is in metres on the hero's own arm.
//
// What it shows. The liquid's level follows life / mana, eased (the game's numbers come five times a second; the level rises and falls
// smoothly instead of stepping). Under the low-life threshold, with the pulse ticked, the life ball beats softly like a heart (the low-life
// vignette's rhythm, its gentle shape: 50 - 72 beats a minute): the glow and the liquid's brightness swell and the ball throbs a few percent.
// The balls fade with the distance from the eyes (a hand held far out) and are not drawn when the numbers are unknown.
//
// Idea: bearstonem/d2r-pcvr (MIT) shows life and mana on the hand (steamvr/hand_vitals.h); this is a different look and written here. The orb's
// look in the effect follows FlatVR's map orb (PSOrb), which the user allowed taking for the orb alone.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "hand_vitals.h"
#include "low_hp.h"

namespace handorbs {

using handvitals::Config;
using handvitals::Vitals;
using hset::V3;

constexpr int kLife = 0, kMana = 1;

// The hero's arm as drawn: elbow, wrist, and the way the palm faces (`across`, out of the palm), in ONE frame - eye-relative metres in the world's own axes
// (ArmFromModel, from the drawn skeleton's model space: the way a belt bottle rides the palm), or the hand frame of skel::HeroForearm as the fallback; side 0 right,
// 1 left. With `hasPoint` the hand's own frame is known too (`point`: wrist -> middle knuckle, unit; `across` then is the palm's normal from the same knuckles): the ball is
// laid out in THAT frame and turns with the wrist as it bends; without it, in the forearm's.
struct Arm {
    bool ok = false;
    V3 elbow, wrist, across;
    bool hasPoint = false;
    V3 point;
};
// The controller's grip pose in the same frame: where the hand is held (`pos`), and - when its orientation is known - the grip's own axes as unit vectors in the world (`x`, `y`, `z`:
// the columns of its rotation). The fallback when no arm is posed, and what the last drawn wrist is carried on by (Carry).
struct Grip {
    bool ok = false;
    V3 pos;
    bool hasAxes = false;
    V3 x{1.0f, 0.0f, 0.0f}, y{0.0f, 1.0f, 0.0f}, z{0.0f, 0.0f, 1.0f};
};

struct Orb {
    bool on = false;
    V3 c;                  // the middle, hand frame (metres)
    float radius = 0.0f;   // metres
    float fill = 0.0f;     // 0 - 1, eased
    float opacity = 0.0f;  // 0 - 1: the setting, faded with the distance
    float glow = 0.0f;     // 0 - 1: the low-life heartbeat (life only)
    int kind = kLife;
    int side = 1;          // the skeleton's: 0 right, 1 left
    V3 axis{0.0f, 1.0f, 0.0f};   // the way the liquid fills (unit): the fixed direction of the wrist's frame the setting chose (FillAxis), the level rising along it
    V3 back{0.0f, 1.0f, 0.0f};   // out of the back of the hand (unit): the medallion's base lies on it, its dome rises along it, the game's globe is a disc facing along it
    V3 along{0.0f, 0.0f, -1.0f}; // along the hand toward the knuckles (unit): the picture's up, the base's studs' turn
};
struct Set {
    Orb orb[2];            // [kLife], [kMana]
    bool any = false;
    bool globes = false;   // the look is the game's own globes (a picture cut from its interface), not the glass balls
    bool medallion = true; // the shape: a dome on a metal base (handvitals::kShapeMedallion), else the full ball
    float patina = 0.0f;   // 0 - 1
    int metal = 0;         // 0 gold, 1 bronze, 2 silver
    bool discFixed = true; // the game's globe as a ball: its disc is fixed on the wrist (else it turns to the eyes)
};

// ---- the medallion's shape, in units of the whole medallion's radius (the base's: Orb::radius) - the effect's own numbers (reshade/D2R_DepthFog.fx, OrbMedal) ----------------
constexpr float kDomeR = 0.80f;       // the glass dome's radius, its flat face on the base's top plane
constexpr float kPlateT = 0.20f;      // the base plate's thickness under that plane
constexpr float kBezelOuter = 0.91f;  // a raised bezel ring round the dome's foot, from the dome's edge out to this radius ...
constexpr float kBezelH = 0.09f;      // ... standing this high over the plane; the plate runs out to radius 1 (a beaded lip with a groove)
inline float DomeRadius(float baseRadius) { return baseRadius * kDomeR; }
inline float DomeHeight(float baseRadius) { return baseRadius * kDomeR; }        // a hemisphere: as high as it is wide at its foot
inline float PlateThickness(float baseRadius) { return baseRadius * kPlateT; }
inline float BaseRadius(float sizeCm) { return sizeCm * 0.005f; }                // the Size slider is the medallion's diameter, the base's


// The wrist each ball sits on (skeleton side: 0 right, 1 left).
inline int SideOf(const Config& c, int kind) {
    switch (c.orbHands) {
        case 1: return kind == kLife ? 0 : 1;   // swapped: life right, mana left
        case 2: return 1;                       // both on the left
        case 3: return 0;                       // both on the right
        default: return kind == kLife ? 1 : 0;  // life left, mana right
    }
}
inline bool BothOnOne(const Config& c) { return c.orbHands >= 2; }

// ---- the fade with the distance from the eyes -------------------------------------------------------------------------------------------

constexpr float kFullBelowM = 0.85f, kGoneAtM = 1.25f;   // a hand at arm's length is full; held further out the ball fades, gone at 1.25 m
inline float DistanceFade(float dist) {
    if (!std::isfinite(dist) || dist < 0.0f) return 0.0f;
    const float t = std::clamp((dist - kFullBelowM) / (kGoneAtM - kFullBelowM), 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

// ---- where it sits ---------------------------------------------------------------------------------------------------------------------

inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Len(V3 a) { return std::sqrt(hset::Dot(a, a)); }
inline bool Finite(V3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

struct Place {
    bool ok = false;
    V3 c;
    bool hasArm = false;
    V3 armAxis;            // the forearm's axis, elbow -> wrist, unit
    V3 axis, back, thumb;  // the wrist's frame (with hasArm), unit: along the hand toward the knuckles, out of the back of the hand, toward the thumb
};

// A vector turned about a unit axis by `deg` degrees (right-hand rule).
inline V3 RotateAbout(V3 v, V3 u, float deg) {
    const float t = deg * 0.017453292519943295f, c = std::cos(t), s = std::sin(t);
    return hset::Add(hset::Add(hset::Mul(v, c), hset::Mul(Cross(u, v), s)), hset::Mul(u, hset::Dot(u, v) * (1.0f - c)));
}
// The medallion's own turn (handvitals::OrbSpot rotX / rotY / rotZ): the wrist's frame vectors - the hand's axis, the back, the thumb and the forearm's axis, which the fill direction and
// the disc are made from - turned about the medallion's middle, which stays where it is. Each angle is about the WRIST's own axis (X the hand's, Y the across one toward the thumb,
// Z the one out of the back of the hand), the original axes, applied in the order X, Y, Z. The frame's handedness flips with the hand (the left's hand axis x thumb is minus its back,
// the right's is plus), so on the right wrist (`side` 0) the angles are negated: the same numbers then give the mirror image of the left wrist's medallion. Nothing without a frame.
inline void TurnFrame(Place& p, const handvitals::OrbSpot& spot, int side) {
    if (!p.hasArm) return;
    const auto deg = [](float v) { return std::clamp(std::isfinite(v) ? v : 0.0f, handvitals::kOrbRotMin, handvitals::kOrbRotMax); };
    const float h = side == 1 ? 1.0f : -1.0f, rx = deg(spot.rotXDeg) * h, ry = deg(spot.rotYDeg) * h, rz = deg(spot.rotZDeg) * h;
    if (rx == 0.0f && ry == 0.0f && rz == 0.0f) return;
    const V3 A = p.axis, T = p.thumb, B = p.back;
    const auto turn = [&](V3 v) { return hset::Unit(RotateAbout(RotateAbout(RotateAbout(v, A, rx), T, ry), B, rz)); };
    p.armAxis = turn(p.armAxis);
    p.axis = turn(A);
    p.thumb = turn(T);
    p.back = turn(B);
}

// The way the liquid fills for a fill setting (handvitals::kFill*): a fixed direction of the wrist's frame, or the world's up. The liquid sits on the LOW side of the level, which rises
// along this direction. Made from the wrist's frame and from nothing the eyes or the head do: the same wrist seen from anywhere fills the same way.
inline V3 FillAxis(int fill, const Place& p) {
    const V3 up{0.0f, 1.0f, 0.0f};
    if (!p.hasArm) return up;
    switch (fill) {
        case handvitals::kFillElbow: return p.armAxis;                    // rises toward the hand: the elbow side is full
        case handvitals::kFillHand: return hset::Mul(p.armAxis, -1.0f);   // rises toward the elbow: the hand side is full
        case handvitals::kFillThumb: return hset::Mul(p.thumb, -1.0f);    // rises toward the little finger: the thumb side is full
        case handvitals::kFillPinky: return p.thumb;                      // rises toward the thumb: the little finger side is full
        case handvitals::kFillOutOfHand: return p.back;                   // rises out of the skin: the level is parallel to the back of the hand
        default: return up;
    }
}
// The numbers of the ball on a wrist (skeleton side: 0 right, 1 left): the right wrist takes the left's when mirrored.
inline handvitals::OrbSpot SpotFor(const Config& cfg, int side) { return side == 1 || cfg.orbMirror ? cfg.orbLeft : cfg.orbRight; }

// The ball's middle on the back of the hand of `side` (0 right, 1 left). `lateralM` shifts it across the hand on top of the spot's own Y (both balls on one wrist side by side).
inline Place Centre(const Arm& arm, const Grip& grip, const Config& cfg, int side, float lateralM = 0.0f, float zClassCm = 0.0f) {
    Place p;
    const handvitals::OrbSpot spot = SpotFor(cfg, side);
    const auto cm = [](float v) { return std::clamp(std::isfinite(v) ? v : 0.0f, handvitals::kOrbSpotMin, handvitals::kOrbSpotMax) * 0.01f; };
    // (the hero class's depth offset goes on the wrist's own Z, either wrist: the arm's model is the class's)
    const float up = cm(spot.upCm + (std::isfinite(zClassCm) ? zClassCm : 0.0f)), along = cm(spot.alongCm), across = cm(spot.sideCm);
    if (arm.ok && Finite(arm.elbow) && Finite(arm.wrist) && Finite(arm.across)) {
        V3 forearm = hset::Sub(arm.wrist, arm.elbow);
        const float fl = Len(forearm);
        if (!(fl > 1e-4f)) return p;
        forearm = hset::Mul(forearm, 1.0f / fl);
        // the frame the ball is laid out in: the hand's own (along = wrist -> middle knuckle) when known, so it turns with the wrist; else the forearm's
        V3 axis = forearm;
        if (arm.hasPoint && Finite(arm.point) && Len(arm.point) > 1e-3f) axis = hset::Unit(arm.point);
        V3 a = hset::Sub(arm.across, hset::Mul(axis, hset::Dot(arm.across, axis)));   // out of the palm, square to the axis
        const float l = Len(a);
        if (!(l > 1e-3f)) return p;
        const V3 back = hset::Mul(a, -1.0f / l);                                      // out of the back of the hand
        const V3 lateral = Cross(axis, back);   // (toward the thumb on the left hand, toward the little finger on the right one - a hand's own handedness)
        const V3 thumb = side == 1 ? lateral : hset::Mul(lateral, -1.0f);
        p.c = hset::Add(hset::Add(arm.wrist, hset::Mul(axis, along)),
                        hset::Add(hset::Add(hset::Mul(back, up), hset::Mul(thumb, across)), hset::Mul(lateral, lateralM)));
        p.ok = Finite(p.c);
        p.hasArm = true;
        p.armAxis = forearm;
        p.axis = axis;
        p.back = back;
        p.thumb = Len(thumb) > 1e-6f ? hset::Unit(thumb) : V3{1.0f, 0.0f, 0.0f};
        return p;
    }
    if (grip.ok && Finite(grip.pos)) {
        p.c = hset::Add(grip.pos, V3{lateralM, up, 0.0f});   // (no hand frame here: only the height of the spot applies)
        p.ok = Finite(p.c);
    }
    return p;
}

// ---- the drawn arm, from the skeleton's world ---------------------------------------------------------------------------------------------

// What skel::HeroArmWorld gives: the arm exactly as the skeleton hook drew it, in the game's render world (world units; directions unit) - made at the very pose from the
// hero's own model-to-world matrix, the way a belt bottle rides the palm (vr/vrcam.cpp BeltFrame; NotePosedWeapon reads the matrix then for the same reason: a later copy
// displaced fast swings). `eye`: where the effect's frame starts (the hands' origin, vrcam's g_eyeWorld); `perM` the world units a metre. The result is eye-relative METRES in
// the world's own axes, which is the frame Step and Pack work in (Pack with the identity axes). Nothing here knows a camera or a head turn, so nothing can lag them.
struct WorldArm {
    V3 elbow, wrist, across, point, face;
    bool hand = false;   // the hand's own frame (point, face) was found
};
inline Arm ArmFromWorld(const float eye[3], float perM, const WorldArm& m) {
    Arm a;
    if (!(std::isfinite(perM) && perM > 1e-6f)) return a;
    const V3 e{eye[0], eye[1], eye[2]};
    const float k = 1.0f / perM;
    a.elbow = hset::Mul(hset::Sub(m.elbow, e), k);
    a.wrist = hset::Mul(hset::Sub(m.wrist, e), k);
    a.across = hset::Unit(m.hand ? m.face : m.across);   // the palm's normal: from the hand's own knuckles when they were found
    if (m.hand) {
        a.hasPoint = true;
        a.point = hset::Unit(m.point);
    }
    a.ok = Finite(a.elbow) && Finite(a.wrist) && Finite(a.across) && Finite(a.point);
    return a;
}

// ---- the wrist's frame, always: the drawn arm, carried on by the controller when it is not read ----------------------------------------------

// The grip's axes (unit vectors in the hand frame, the columns of its rotation) from its orientation quaternion x y z w (D2RVR_Shared::rightRot / leftRot). Not ok for a quaternion
// that is not one.
inline bool AxesFromQuat(const float q[4], V3 out[3]) {
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!std::isfinite(n) || n < 1e-6f) return false;
    const float x = q[0] / n, y = q[1] / n, z = q[2] / n, w = q[3] / n;
    out[0] = {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + z * w), 2.0f * (x * z - y * w)};
    out[1] = {2.0f * (x * y - z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + x * w)};
    out[2] = {2.0f * (x * z + y * w), 2.0f * (y * z - x * w), 1.0f - 2.0f * (x * x + y * y)};
    return true;
}

// What the wrist's frame is, in the controller's own axes - learned while the drawn arm is read (every frame it is), and what stands in for it while it is not. The hero's hand is
// held to the controller (the skeleton follows it), so the wrist, the hand's axis and the palm's normal keep the same place and direction in the grip's axes: a frame of the
// grip's orientation and position gives the drawn wrist back, to a few millimetres, from the controller alone - and it turns with the hand, which a point does not.
struct Carry {
    bool have = false, hasPoint = false;
    V3 wrist, elbow;       // from the grip's position, in the grip's axes (metres)
    V3 across, point;      // directions in the grip's axes (unit once Make has them)
    double learnedAt = -1.0;

    static V3 ToLocal(const Grip& g, V3 v) { return {hset::Dot(v, g.x), hset::Dot(v, g.y), hset::Dot(v, g.z)}; }
    static V3 FromLocal(const Grip& g, V3 v) {
        return hset::Add(hset::Add(hset::Mul(g.x, v.x), hset::Mul(g.y, v.y)), hset::Mul(g.z, v.z));
    }
    // The drawn arm `a` and the grip `g` of the same moment: learn them. `dt` seconds since the last learn sets how fast the new one is taken (the first is taken as it is).
    void Learn(const Arm& a, const Grip& g, double now, float dt) {
        if (!a.ok || !g.ok || !g.hasAxes || !Finite(g.pos) || !Finite(g.x) || !Finite(g.y) || !Finite(g.z) || !Finite(a.wrist) || !Finite(a.elbow) || !Finite(a.across)) return;
        const V3 w = ToLocal(g, hset::Sub(a.wrist, g.pos)), e = ToLocal(g, hset::Sub(a.elbow, g.pos)), x = ToLocal(g, hset::Unit(a.across));
        const bool pt = a.hasPoint && Finite(a.point) && Len(a.point) > 1e-3f;
        const V3 p = pt ? ToLocal(g, hset::Unit(a.point)) : V3{};
        const float k = !have || !std::isfinite(dt) ? 1.0f : std::clamp(1.0f - std::exp(-dt / 0.12f), 0.02f, 1.0f);
        const auto mix = [k](V3 was, V3 now) { return hset::Add(hset::Mul(was, 1.0f - k), hset::Mul(now, k)); };
        wrist = have ? mix(wrist, w) : w;
        elbow = have ? mix(elbow, e) : e;
        across = have ? mix(across, x) : x;
        if (pt) point = have && hasPoint ? mix(point, p) : p;
        hasPoint = pt;
        have = true;
        learnedAt = now;
    }
    // The wrist's frame from the grip alone. Not ok without a learned one or a grip with axes.
    bool Make(const Grip& g, Arm* out) const {
        if (!have || !g.ok || !g.hasAxes || !Finite(g.pos)) return false;
        Arm a;
        a.wrist = hset::Add(g.pos, FromLocal(g, wrist));
        a.elbow = hset::Add(g.pos, FromLocal(g, elbow));
        a.across = hset::Unit(FromLocal(g, across));
        if (hasPoint && Len(point) > 1e-3f) {
            a.hasPoint = true;
            a.point = hset::Unit(FromLocal(g, point));
        }
        a.ok = Finite(a.wrist) && Finite(a.elbow) && Finite(a.across) && Finite(a.point);
        if (a.ok) *out = a;
        return a.ok;
    }
};

// Where the wrist's frame came from: the skeleton as drawn (Drawn); the controller carrying the last drawn frame on (Carried); the skeleton's hand-frame forearm through the head's turn
// (Forearm: the head's turn at another instant, no wrist bend); the controller's point alone (GripPoint: no frame, so no turn); or nothing.
enum class Src { None, Drawn, Carried, Forearm, GripPoint };
inline const char* SrcName(Src s) {
    switch (s) {
        case Src::Drawn: return "the drawn wrist";
        case Src::Carried: return "carried on by the controller";
        case Src::Forearm: return "the forearm through the head's turn (no wrist bend)";
        case Src::GripPoint: return "the controller's point (no turn)";
        default: return "nothing";
    }
}
constexpr double kDrawnFreshS = 0.09;     // a drawn arm older than this is not this frame's: the poses come every frame, one that did not is a miss
constexpr double kCarryMaxS = 30.0;       // a learned frame is carried on this long after it was last learned, then the older fallbacks
constexpr float kLearnDtMax = 0.1f;

struct Wrist {                   // what is known of one wrist this frame
    bool drawnOk = false;        // the skeleton's world arm was read at all (any age)
    Arm drawn;
    double drawnAgeS = 1e9;      // how old it is
    bool forearmOk = false;
    Arm forearm;                 // skel::HeroForearm in the same frame, as the old fallback made it
    Grip grip;
};
struct Resolved {
    Arm arm;                     // ok false: no frame (the grip's point may still stand in)
    Src src = Src::None;
    bool changed = false;        // the source is not the one of the last frame
    Src from = Src::None;
    double lastedS = 0.0;        // how long `from` lasted
};
// Which wrist frame a side uses this frame, and what the log should say when it changes. One per wrist; keeps what it learned.
class Tracker {
public:
    void Reset() { *this = Tracker{}; }
    Resolved Resolve(int side, const Wrist& w, double now) {
        const int s = side & 1;
        Resolved r;
        const float dt = lastAt_[s] >= 0.0 ? (float)std::clamp(now - lastAt_[s], 0.0, (double)kLearnDtMax) : 0.0f;
        lastAt_[s] = now;
        if (w.drawnOk && w.drawnAgeS <= kDrawnFreshS && w.drawn.ok) {
            r.arm = w.drawn;
            r.src = Src::Drawn;
            carry_[s].Learn(w.drawn, w.grip, now, dt);
        } else if (carry_[s].have && now - carry_[s].learnedAt <= kCarryMaxS && carry_[s].Make(w.grip, &r.arm)) {
            r.src = Src::Carried;
        } else if (w.forearmOk && w.forearm.ok) {
            r.arm = w.forearm;
            r.src = Src::Forearm;
        } else if (w.grip.ok) {
            r.src = Src::GripPoint;
        }
        if (r.src != src_[s] || !told_[s]) {
            r.changed = true;
            r.from = src_[s];
            r.lastedS = told_[s] ? now - since_[s] : 0.0;
            src_[s] = r.src;
            since_[s] = now;
            told_[s] = true;
        }
        return r;
    }
    const Carry& carry(int side) const { return carry_[side & 1]; }
    Src src(int side) const { return src_[side & 1]; }

private:
    Carry carry_[2];
    Src src_[2] = {Src::None, Src::None};
    double since_[2] = {0.0, 0.0}, lastAt_[2] = {-1.0, -1.0};
    bool told_[2] = {false, false};
};

// ---- the state: the eased levels and the heartbeat ---------------------------------------------------------------------------------------

constexpr float kFillTauS = 0.22f;       // the level follows the numbers with this time constant
constexpr float kBeatFadeS = 0.8f;       // the heartbeat fades in and out over this
constexpr float kThrob = 0.05f;          // the life ball swells by this at the top of a beat
constexpr float kGapM = 0.004f;          // between two balls on one wrist

struct State {
    float fill[2] = {0.0f, 0.0f};
    bool haveFill = false;
    double lastTime = -1.0;
    float phase = 0.0f, beat = 0.0f;
    void Reset() { *this = State{}; }
};

struct In {
    Config cfg;
    bool vitalsOk = false;   // fresh, known numbers
    Vitals v;
    Arm arm[2];              // [0] right, [1] left
    Grip grip[2];
    double time = 0.0;       // seconds, any origin, monotonic
    int heroClass = -1;      // the hero playing now (skel::HeroClass(): 0 amazon ... 7 warlock), -1 not known: its depth offset (Config::orbClassZCm) goes on both wrists' Z
};

inline float Ease(float now, float target, float dt) {
    const float k = 1.0f - std::exp(-dt / kFillTauS);
    const float v = now + (target - now) * k;
    return std::fabs(target - v) < 0.0005f ? target : v;
}

// One step: what to draw now. Nothing (and the state forgotten) when the setting is off or another look is chosen; nothing while the numbers
// are unknown (the eased levels are kept: a short gap in the reads does not empty the balls).
inline Set Step(State& st, const In& in) {
    Set out;
    const Config cfg = handvitals::Sanitized(in.cfg);
    if (!cfg.on || cfg.style == handvitals::kStylePanel) {
        st.Reset();
        return out;
    }
    out.globes = cfg.style == handvitals::kStyleGlobes;
    out.medallion = cfg.orbShape == handvitals::kShapeMedallion;
    out.patina = cfg.orbPatina;
    out.metal = cfg.orbMetal;
    out.discFixed = cfg.orbDiscFaces != 0;
    float dt = 0.0f;
    if (std::isfinite(in.time) && st.lastTime >= 0.0 && in.time > st.lastTime) dt = std::min((float)(in.time - st.lastTime), 0.1f);
    if (std::isfinite(in.time)) st.lastTime = in.time;
    if (!in.vitalsOk || !in.v.known) {
        st.haveFill = false;   // (the next numbers are shown as they are, not eased from the old ones)
        return out;
    }
    const float target[2] = {handvitals::Fraction(in.v.life, in.v.maxLife), handvitals::Fraction(in.v.mana, in.v.maxMana)};
    for (int k = 0; k < 2; ++k) st.fill[k] = st.haveFill ? Ease(st.fill[k], target[k], dt) : target[k];
    st.haveFill = true;

    // the heartbeat: under the low-life threshold, only with the pulse ticked, only while alive
    const float frac = target[kLife];
    const bool alert = handvitals::Alert(in.v, cfg);
    st.beat = std::clamp(st.beat + (alert ? dt : -dt) / kBeatFadeS, 0.0f, 1.0f);
    if (alert) {
        st.phase += dt * lowhp::Bpm(lowhp::Severity(frac, cfg.lowFrac), true) / 60.0f;
        st.phase -= std::floor(st.phase);
    }
    const float glow = st.beat > 0.0f ? st.beat * lowhp::Beats01(st.phase, lowhp::ShapeFor(true)) : 0.0f;

    const float baseR = cfg.orbSizeCm * 0.005f;
    const bool both = BothOnOne(cfg);
    for (int kind = 0; kind < 2; ++kind) {
        Orb& o = out.orb[kind];
        o.kind = kind;
        o.side = SideOf(cfg, kind);
        o.glow = kind == kLife ? glow : 0.0f;
        o.radius = baseR * (1.0f + (kind == kLife ? kThrob * glow : 0.0f));
        const float lateral = both ? (kind == kLife ? -1.0f : 1.0f) * (baseR + 0.5f * kGapM) : 0.0f;
        Place p = Centre(in.arm[o.side], in.grip[o.side], cfg, o.side, lateral, handvitals::ClassZCm(cfg, in.heroClass));
        if (!p.ok) continue;
        TurnFrame(p, SpotFor(cfg, o.side), o.side);   // the medallion's own turn: its back, its along and the fill direction go with it
        o.c = p.c;
        o.axis = FillAxis(cfg.orbFill, p);                                              // a fixed direction of the wrist's frame (the world's up without one)
        o.back = p.hasArm ? p.back : V3{0.0f, 1.0f, 0.0f};
        o.along = p.hasArm ? p.axis : V3{0.0f, 0.0f, -1.0f};
        o.fill = st.fill[kind];
        o.opacity = cfg.orbOpacity * DistanceFade(Len(p.c));
        o.on = o.opacity > 0.001f;
        out.any = out.any || o.on;
    }
    return out;
}

// ---- what the effect gets -----------------------------------------------------------------------------------------------------------------

struct Uniforms {
    bool on = false;
    bool picture = false;    // OrbPic.x: the game's own globes (their picture is bound) instead of the glass balls
    float fit = 0.0f;        // OrbPic.y: the disc's radius over half its square (vr/hud_globes.h)
    float pos[2][4] = {};    // OrbPos: the middle in world units relative to where the hands hang, the radius
    float look[2][4] = {};   // OrbLook: fill, opacity, glow, kind (0 life, 1 mana)
    float axis[2][4] = {};   // OrbAxis: the way the liquid fills (unit vector in the world's axes; w unused)
    float back[2][4] = {};   // OrbBack: out of the back of the hand (unit, the world's axes): the medallion's base lies on it, the dome rises along it, a globe's disc faces along it
    float along[2][4] = {};  // OrbAlong: along the hand toward the knuckles (unit): the picture's up, the base's studs' turn
    float med[4] = {};       // OrbMed: x 1 = the medallion (0 the full ball), y the patina 0 - 1, z the metal (0 gold, 1 bronze, 2 silver), w 1 = a globe's disc is fixed on the wrist
    float time = 0.0f;       // OrbTime: seconds, wrapped
};

constexpr double kTimeWrapS = 7200.0;

// A hand-frame point to the world: vrcam's `world` (the head's turn-only axes: camR right, camF forward) and the world units per metre.
inline void ToWorld(V3 v, const float camR[3], const float camF[3], float upm, float out[3]) {
    const float in[3] = {v.x, v.y, v.z};
    for (int i = 0; i < 3; ++i) out[i] = in[0] * camR[i] - in[2] * camF[i];
    out[1] += in[1];
    for (int i = 0; i < 3; ++i) out[i] *= upm;
}

inline Uniforms Pack(const Set& s, const float camR[3], const float camF[3], float upm, double seconds, float fit = 0.0f) {
    Uniforms u;
    u.on = s.any && std::isfinite(upm) && upm > 0.0f;
    u.picture = s.globes;
    u.fit = std::isfinite(fit) ? std::clamp(fit, 0.0f, 1.0f) : 0.0f;
    u.time = (float)std::fmod(std::isfinite(seconds) ? std::fabs(seconds) : 0.0, kTimeWrapS);
    for (int k = 0; k < 2; ++k) {
        const Orb& o = s.orb[k];
        ToWorld(o.c, camR, camF, upm, u.pos[k]);
        u.pos[k][3] = o.radius * upm;
        u.look[k][0] = std::clamp(o.fill, 0.0f, 1.0f);
        u.look[k][1] = o.on && u.on ? std::clamp(o.opacity, 0.0f, 1.0f) : 0.0f;
        u.look[k][2] = std::clamp(o.glow, 0.0f, 1.0f);
        u.look[k][3] = (float)o.kind;
        const V3 ax = hset::Unit(o.axis), bk = hset::Unit(o.back), al = hset::Unit(o.along);
        u.axis[k][0] = ax.x;
        u.axis[k][1] = ax.y;
        u.axis[k][2] = ax.z;
        u.back[k][0] = bk.x;
        u.back[k][1] = bk.y;
        u.back[k][2] = bk.z;
        u.along[k][0] = al.x;
        u.along[k][1] = al.y;
        u.along[k][2] = al.z;
    }
    u.med[0] = s.medallion ? 1.0f : 0.0f;
    u.med[1] = std::clamp(std::isfinite(s.patina) ? s.patina : 0.0f, 0.0f, 1.0f);
    u.med[2] = (float)std::clamp(s.metal, 0, 2);
    u.med[3] = s.discFixed ? 1.0f : 0.0f;
    return u;
}

}  // namespace handorbs
