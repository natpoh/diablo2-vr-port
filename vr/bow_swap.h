// bow_swap.h - [bow] swap_by_grab: the bow passed from hand to hand the way a thing is - the free hand squeezes its grip at the bow's grip and takes it, and the string is the
// other hand's from then on; the same back. Pure: no game calls, no globals (cleanroom/tests/bow_swap_test.cpp feeds it).
//
// The bow is the game's equipped weapon; it never leaves its slot. Only which controller holds the drawn bow and which one draws its string change - the holster
// (vr/ranged_hand.h, Holster::Step with In::grab) keeps that side, as it does for the cross draw from behind a shoulder, and everything that reads the bow's hand
// (the draw, the string, the rig, the aim, the quiver and its orbs) follows it.
//
// Where a squeeze takes the bow: the free hand's grip point within kReachM of the bow hand's (the bow's grip is in that palm) and NEARER the grip than the string - the
// draw's touch point, kNockBehindM behind the grip along the bow (bowdraw::BowForward, the convention the draw has). Two real controllers do not bring their grip points
// much nearer than 10-15 cm (vr/xbow_grip.h: a 12 cm reach never fired in 0.170), so the reach is wider than that and the string decides: a hand at the string takes the
// string (the draw), a hand on the riser or a limb takes the bow.
#pragma once

#include <cwchar>
#include <cwctype>

#include "bow_draw.h"

namespace bowswap {

constexpr float kReachM = 0.25f;        // the free hand's grip point to the bow hand's
constexpr float kNockBehindM = 0.16f;   // the string's place behind the grip (bow_draw_wire.cpp's constant touch point)

// The string's place for the rule: kNockBehindM behind the bow hand's grip, along the bow.
inline bowdraw::Vec StringPoint(bowdraw::Vec bowHand, bowdraw::Quat bowRot, float behind = kNockBehindM) {
    return bowHand - bowdraw::BowForward(bowRot) * behind;
}

// The free hand is at the bow's grip: within reach of it, and nearer it than the string. A turn that is no turn (not unit: no pose yet) tells no string: nothing is taken.
inline bool AtGrip(bowdraw::Vec freeHand, bowdraw::Vec bowHand, bowdraw::Quat bowRot, float reach = kReachM) {
    if (!bowdraw::Finite(freeHand) || !bowdraw::Finite(bowHand)) return false;
    const float n = bowRot.x * bowRot.x + bowRot.y * bowRot.y + bowRot.z * bowRot.z + bowRot.w * bowRot.w;
    if (!(n > 0.5f && n < 1.5f)) return false;
    const float toGrip = bowdraw::Length(freeHand - bowHand);
    const float toString = bowdraw::Length(freeHand - StringPoint(bowHand, bowRot));
    return std::isfinite(toGrip) && std::isfinite(toString) && toGrip <= reach && toGrip < toString;
}

// [bow] bow_hand, the side remembered: 1 the left hand (the game's own), 0 the right - the mod's sides (0 right, 1 left). Written as a number (Settings' choice reads
// it so); a hand-edited "left" / "right" is read too. Anything else: `def`.
inline int ParseSide(const wchar_t* s, int def) {
    if (!s) return def & 1;
    while (*s == L' ' || *s == L'\t') ++s;
    wchar_t w[8] = {};
    int n = 0;
    while (s[n] && n < 7 && s[n] != L' ' && s[n] != L'\t' && s[n] != L';') { w[n] = (wchar_t)std::towlower(s[n]); ++n; }
    if (!std::wcscmp(w, L"left") || !std::wcscmp(w, L"1")) return 1;
    if (!std::wcscmp(w, L"right") || !std::wcscmp(w, L"0")) return 0;
    return def & 1;
}
inline const wchar_t* SideValue(int side) { return (side & 1) ? L"1" : L"0"; }
inline const char* SideName(int side) { return (side & 1) ? "left" : "right"; }

}  // namespace bowswap
