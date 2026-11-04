#pragma once
// flat_weapon.h - [body] flat_weapon_adjust: the monitor's first person (flat F3), each arm with its hand and what it holds moved along the
// camera's own axes - out to that hand's side, up, ahead - by [flat_weapon_<kind>] right_x/_y/_z and left_x/_y/_z (cm), so a big weapon (a
// Warlock's axe held upright before the chest, 2026-11-03) no longer stands in the middle of the view. Pure: no game, no Windows
// (cleanroom/tests/flat_weapon_test.cpp).
//
// Its own sections and its own picked kind, nothing of VR's Weapon Adjust ([weapon_<kind>]: other axes, other numbers - Serg 2026-11-04, "the VR
// settings can't be used here, the 2D view has other coefficients"). All 0 by default: ticked, nothing moves until the player sets a slider
// ("people set it for themselves"). Per kind of weapon: the right hand takes the kind it holds, the left hand its own kind when it holds a
// weapon of its own (a weapon in each hand), else the right hand's (a shield, an orb's effect, a free hand go with it).
// What moves: the upper arm and every bone under it (forearm, hand, fingers, the weapon's attach bone) - moved whole, so the hand keeps its
// grip on the weapon; the skin bends at the shoulder, which in first person is under and behind the camera.
//
// Visual only: the bones the skinning draws (skeletons.cpp PoseBody, after all else; the pose handed to the skinning, headshadow::
// PublishOurPose). The game's attack, its hit, its range, the aim and what the others in a game see are the game's.

#include <algorithm>
#include <cmath>

namespace flatweapon {

struct V { float x = 0.0f, y = 0.0f, z = 0.0f; };

constexpr float kLimitCm = 40.0f;
// The defaults (cm): out to the hand's side, up, ahead - none (Serg 2026-11-04: "by default it must be 0").
constexpr float kDefaultCm[3] = {0.0f, 0.0f, 0.0f};
// The ini: [flat_weapon_<kind>] (the kind's name lower case, as [weapon_<kind>]'s) with these keys, cm: the right arm's, then the left arm's.
constexpr const wchar_t* kSectionPrefix = L"flat_weapon_";
constexpr const wchar_t* kKeys[6] = {L"right_x", L"right_y", L"right_z", L"left_x", L"left_y", L"left_z"};

// A slider's value as used: finite and within the slider's range.
inline float ClampCm(float cm) { return std::isfinite(cm) ? std::clamp(cm, -kLimitCm, kLimitCm) : 0.0f; }

inline bool Any(const float cm[3]) { return cm && (cm[0] != 0.0f || cm[1] != 0.0f || cm[2] != 0.0f); }

// Which kind's numbers a hand takes (side 0 right, 1 left): the right hand its own; the left one its own weapon's kind when it holds one
// (unarmed < kind < count), else the right hand's.
inline int KindFor(int side, int rightKind, int leftKind, int unarmed, int count) {
    if (side == 0) return rightKind;
    return leftKind > unarmed && leftKind < count ? leftKind : rightKind;
}

// The camera's frame on the ground in the hero's model space (y up): its right and ahead flattened and made unit, up +y. camR / camF: the
// camera's right and forward in model space. Not ok when either is too short, not finite, or the two are not across each other.
struct Frame { V right, up{0.0f, 1.0f, 0.0f}, ahead; bool ok = false; };
inline Frame GroundFrame(const float camR[3], const float camF[3]) {
    Frame f;
    if (!camR || !camF) return f;
    const float rl = std::sqrt(camR[0] * camR[0] + camR[2] * camR[2]), al = std::sqrt(camF[0] * camF[0] + camF[2] * camF[2]);
    if (!std::isfinite(rl) || !std::isfinite(al) || rl < 0.1f || al < 0.1f) return f;
    f.right = {camR[0] / rl, 0.0f, camR[2] / rl};
    f.ahead = {camF[0] / al, 0.0f, camF[2] / al};
    const float dot = f.right.x * f.ahead.x + f.right.z * f.ahead.z;
    f.ok = std::fabs(dot) < 0.5f;   // (60 deg apart at least: a right and a forward of one camera are 90)
    return f;
}

// Model units per metre: the hero's eyes in model units over the player's in metres, clamped as PoseBody's own (its [weapon_<kind>] upm).
inline float UnitsPerMetre(float eyeModelY, float userHeadM) {
    const float u = eyeModelY / std::max(0.8f, std::isfinite(userHeadM) ? userHeadM : 0.0f);
    return std::isfinite(u) ? std::clamp(u, 2.0f, 8.0f) : 2.0f;
}

// The shift of one hand (side 0 right, 1 left) in model units: cm[0] out to that hand's side (+ right for the right hand, + left for the
// left one: the same numbers move both hands apart), cm[1] up, cm[2] ahead.
inline V Shift(const Frame& f, int side, const float cm[3], float upm) {
    if (!f.ok || !cm) return {};
    const float k = 0.01f * upm;
    const float out = (side == 1 ? -ClampCm(cm[0]) : ClampCm(cm[0])) * k, up = ClampCm(cm[1]) * k, ahead = ClampCm(cm[2]) * k;
    return {f.right.x * out + f.up.x * up + f.ahead.x * ahead,
            f.right.y * out + f.up.y * up + f.ahead.y * ahead,
            f.right.z * out + f.up.z * up + f.ahead.z * ahead};
}

// A two-handed hold (a paladin's two-handed sword, a staff, a pole-arm): the game's own pose has the other hand's fist on the held weapon's shaft.
// Moved by its own (mirrored) numbers, that arm left the hilt - 20 cm apart at 0.201.2's defaults of 10 out each (Serg: "the paladin does not hold
// the sword with both hands in F3"). So that arm takes the holding arm's shift instead, the grip kept as the game made it. The shaft: the weapon's attach
// bone's long axis (its Y, as vr/weapon_extents.h measures the drawn models) through its origin; the fist: the hand's middle first knuckle.
constexpr float kFistOnShaftCm = 10.0f;    // the fist's knuckle from the shaft's line: a fist round a grip is ~4 cm, a hand hanging free 20 +
constexpr float kShaftReachM = 1.2f;       // ... and along it, either way from the attach bone (a pole-arm's far hand)
constexpr float kHoldEasePerS = 8.0f;      // how fast the other arm goes over to the holder's shift and back (a swing's hand off the hilt: no jump)
// fist, origin: points; axis: the attach bone's long axis (any length, a hidden slot's bone shrunk to a point has none); upm: model units a metre.
inline bool FistOnShaft(const V& fist, const V& origin, const V& axis, float upm) {
    const float al = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (!std::isfinite(al) || al < 1e-4f || !(upm > 0.0f)) return false;
    const V u{axis.x / al, axis.y / al, axis.z / al}, d{fist.x - origin.x, fist.y - origin.y, fist.z - origin.z};
    const float along = d.x * u.x + d.y * u.y + d.z * u.z;
    const V off{d.x - u.x * along, d.y - u.y * along, d.z - u.z * along};
    const float across = std::sqrt(off.x * off.x + off.y * off.y + off.z * off.z);
    return std::isfinite(across) && std::fabs(along) <= kShaftReachM * upm && across <= kFistOnShaftCm * 0.01f * upm;
}
// ... or, whatever the weapon's axis (a sword's hilt may not run along its bone's Y: [hands] sword_hilt_axis), the two fists side by side on one
// grip: their knuckles this close while that hand shows a weapon (an empty slot's bone is shrunk to a point).
constexpr float kFistsTogetherCm = 15.0f;
inline bool FistsTogether(const V& a, const V& b, float upm) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z, d = std::sqrt(dx * dx + dy * dy + dz * dz);
    return upm > 0.0f && std::isfinite(d) && d <= kFistsTogetherCm * 0.01f * upm;
}
inline bool ShowsWeapon(const V& axis) {
    const float l = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    return std::isfinite(l) && l >= 1e-4f;
}
// The holding hand: 0 / 1 when the other hand's fist is on that hand's weapon (the right first), -1 none. onShaft[s]: the other fist on side s's weapon.
inline int Holder(const bool onShaft[2]) { return onShaft[0] ? 0 : onShaft[1] ? 1 : -1; }
// The share of the holder's shift the other arm takes, eased toward 1 (held) or 0 over dt seconds.
inline float EaseHold(float w, bool held, float dt) {
    if (!std::isfinite(w)) w = held ? 1.0f : 0.0f;
    const float step = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.25f) * kHoldEasePerS : 0.0f;
    return held ? std::min(1.0f, w + step) : std::max(0.0f, w - step);
}
inline V Blend(const V& own, const V& holder, float w) {
    w = std::isfinite(w) ? std::clamp(w, 0.0f, 1.0f) : 0.0f;
    return {own.x + (holder.x - own.x) * w, own.y + (holder.y - own.y) * w, own.z + (holder.z - own.z) * w};
}

// Marks `root` and every bone under it (a rig lists a parent before its children); how many. mark: n entries, set to 0 or 1.
inline int Subtree(const int* parent, int n, int root, char* mark) {
    if (!parent || !mark || n <= 0) return 0;
    std::fill(mark, mark + n, (char)0);
    if (root < 0 || root >= n) return 0;
    mark[root] = 1;
    int k = 1;
    for (int i = root + 1; i < n; ++i) {
        const int p = parent[i];
        if (p >= 0 && p < n && mark[p]) { mark[i] = 1; ++k; }
    }
    return k;
}

// Where it acts, and why not elsewhere (the log names the reason at every change). platform 0 the monitor; firstPerson: our camera's own
// first person (flat F3, not F1 / F2); armsMode: ArmsMode() (0 the game's arms; 1 the debug test pose keeps its own).
enum class Why { On, KeyOff, NotFlat, NotFirstPerson, TestPose, AllZero };
inline Why Gate(bool key, int platform, bool firstPerson, int armsMode, bool anyNumbers) {
    if (!key) return Why::KeyOff;
    if (platform != 0) return Why::NotFlat;
    if (!firstPerson) return Why::NotFirstPerson;
    if (armsMode != 0) return Why::TestPose;
    if (!anyNumbers) return Why::AllZero;
    return Why::On;
}
inline const char* WhyText(Why w) {
    switch (w) {
        case Why::On: return "on";
        case Why::KeyOff: return "off ([body] flat_weapon_adjust=0): the arms where the game's animation has them";
        case Why::NotFlat: return "not in VR (VR F4 has Weapon Adjust)";
        case Why::NotFirstPerson: return "not in this view - flat first person (F3) only";
        case Why::TestPose: return "not with [arms] mode=1 (the test pose)";
        case Why::AllZero: return "nothing to move - this weapon's numbers are all 0";
    }
    return "?";
}

}  // namespace flatweapon
