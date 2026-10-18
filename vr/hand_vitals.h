#pragma once
// hand_vitals.h - [hud] hand_vitals: life and mana on the hands. Two looks ([hud] hand_vitals_style): 0 (the default) two 3D glass balls on the
// back of the wrists, drawn by the effect (vr/hand_orbs.h; this header holds their settings and the numbers' maths); 1 the palm panel below -
// the logic of "life and mana on the hand when you look at the palm". Pure: no game, no
// Windows, no OpenXR, no clock (the caller passes the seconds since the last step), so the test
// (cleanroom/tests/hand_vitals_test.cpp) drives it with synthetic hands, heads and lives. The plumbing - the hero's life and
// mana read from the game, the per-frame hands, the painter thread, the quad layer - is vr/hand_vitals_wire.cpp; the picture is
// vr/hand_vitals_paint.cpp.
//
// What the player sees: turn a palm toward your face (the hand raised in front of you, the palm's normal pointing at the head
// within a cone) and a small panel comes up over that hand - two round globes in the Diablo style, a red one for LIFE and a blue
// one for MANA, each filling from the bottom, with "now / max" under it. It fades in when the palm turns to you and out when it
// turns away (a wider cone to keep than to start, so it does not flicker at the edge). It hangs at the hand and follows it, turned
// to the head and kept upright so the numbers read; it is not locked to the head. Nothing when the hand is far from the head,
// down at the side, closed in a fist, when a menu or one of the plugin's own panels is up, when the hero is dead or the numbers
// are not known, or anywhere but first person VR (F4).
//
// The hand's axes: OpenXR's grip pose has +X out of the LEFT palm and into the RIGHT one (the back of the right hand is +X),
// the same as the skeleton code in this repo assumes (vr/skeletons.cpp, "the grip's +X goes into the right palm / out of the left
// one") and WebXR's grip space. So the palm's normal, pointing away from the palm, is +X of the grip pose for the left hand and
// -X for the right. [debug] hand_vitals_flip=1 turns it round (the back of the hand) in case a runtime disagrees.
//
// Idea: bearstonem/d2r-pcvr (MIT), steamvr/hand_vitals.h and the palm panel of steamvr/interactions.h, shows the hero's life and
// mana on the hand when the palm turns up. This is written here: its own thresholds, its own maths, its own drawing.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "hset_ray.h"

namespace handvitals {

using hset::Q;
using hset::V3;

// ---- the settings --------------------------------------------------------------------------------------------------

// Where one wrist's ball sits, in that wrist's own frame, centimetres from the wrist joint (vr/hand_orbs.h Centre): the hand's axis (wrist -> middle knuckle) when the
// skeleton gives it, else the forearm's. The same numbers mean the same place on either hand: `sideCm` is anatomical (toward the thumb), not left or right of the room.
struct OrbSpot {
    float alongCm = 3.0f;   // X: from the wrist toward the knuckles (negative: toward the elbow)
    float sideCm = 0.0f;    // Y: across the hand, + toward the thumb, - toward the little finger
    float upCm = 4.0f;      // Z: out of the back of the hand (negative: into it)
    // The whole medallion turned about its own middle, degrees, each about the wrist's own axis (applied in this order, X then Y then Z): X round the hand's axis (+ turns its face
    // toward the thumb), Y round the across axis (+ turns its face toward the elbow), Z round the axis out of the back of the hand (+ turns its along-axis from the knuckles toward the
    // little finger). Anatomical like sideCm: the same numbers on the two hands are the mirror image (the right hand's angles are negated in the room's axes - vr/hand_orbs.h TurnFrame).
    float rotXDeg = 0.0f, rotYDeg = 0.0f, rotZDeg = 0.0f;
};

// The medallion on the wrist that wears the shield (vr/hand_orbs.h Centre, Step): the shield covers the back of the wrist, so the ball rolls round the hand's axis to the other
// side of it - the inner side - and rolls back when the shield leaves that arm. These are the coefficients of that place, ON TOP of the wrist's own OrbSpot (so a change of the wrist's
// place moves this one too): `turnDeg` is the roll, the offsets are in the wrist's frame AS ROLLED (so `upCm` is always out of the skin where the ball now sits), the three angles are
// added to the wrist's own turn. Anatomical like OrbSpot: the same numbers on either wrist are the mirror image (the shield is on the left arm, or on the right one after a hand swap).
struct OrbTurn {   // three angles, degrees: about the hand's axis, the across axis, the axis out of the back of the hand
    float xDeg = 0.0f, yDeg = 0.0f, zDeg = 0.0f;
};
struct OrbUnderShield {
    float turnDeg = 180.0f;   // the roll about the hand's axis: + sends the back of the hand toward the thumb, - toward the little finger; 180 is the opposite side of the wrist
    float alongCm = 0.0f, sideCm = 0.0f, upCm = 0.0f;
    float rotXDeg = 0.0f, rotYDeg = 0.0f, rotZDeg = 0.0f;
};

// The hero classes as the game's model folders name them (skel::kHeroClasses, vr/skeletons.h: the index skel::HeroClass() gives, -1 when it is not known). `key` is the ini's suffix.
constexpr int kHeroCount = 8;
inline const char* HeroKey(int c) {
    static const char* const k[kHeroCount] = {"amazon", "sorceress", "necromancer", "paladin", "barbarian", "druid", "assassin", "warlock"};
    return c >= 0 && c < kHeroCount ? k[c] : "";
}
inline const char* HeroName(int c) {
    static const char* const k[kHeroCount] = {"Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin", "Warlock"};
    return c >= 0 && c < kHeroCount ? k[c] : "unknown";
}

struct Config {
    bool on = false;              // [hud] hand_vitals
    int hand = 0;                 // [hud] hand_vitals_hand: 0 left, 1 right, 2 either
    float sizeCm = 8.0f;          // [hud] hand_vitals_size_cm: the panel's width
    float heightCm = 6.0f;        // [hud] hand_vitals_height_cm: above the palm's middle (up), + up
    float opacity = 1.0f;         // [hud] hand_vitals_opacity
    float coneDeg = 35.0f;        // [hud] hand_vitals_cone: how far off the face the palm may turn to appear (it goes at +15)
    bool pulse = false;           // [hud] hand_vitals_pulse: the life globe glows red and the panel breathes under the low-life threshold
    float lowFrac = 0.30f;        // [hud] low_hp_threshold (a fraction): under it the pulse starts
    bool flip = false;            // [debug] hand_vitals_flip: the back of the hand instead of the palm
    // The 3D orbs on the wrists (vr/hand_orbs.h, drawn by the effect), the default look; the palm panel above is the other.
    int style = 0;                // [hud] hand_vitals_style: 0 the glass orbs, 1 the palm panel, 2 the game's own globes cut from its interface (kStyle*)
    int orbHands = 0;             // [hud] hand_vitals_orb_hands: 0 life on the left wrist and mana on the right, 1 swapped, 2 both on the left, 3 both on the right
    float orbSizeCm = 5.0f;       // [hud] hand_vitals_orb_size_cm: the ball's diameter
    OrbSpot orbLeft;              // [hud] hand_vitals_orb_left_along_cm / _side_cm / _up_cm: the ball on the left wrist (the older _up_cm / _along_cm keys are its start)
    OrbSpot orbRight;             // [hud] hand_vitals_orb_right_along_cm / _side_cm / _up_cm: the ball on the right wrist
    bool orbMirror = false;       // [hud] hand_vitals_orb_mirror: the right wrist takes the left's numbers (its own are kept, not used)
    // The wrist that wears a shield turns its ball to the other side of the wrist (VR F4 only): [hud] hand_vitals_orb_under_shield, and its coefficients hand_vitals_orb_shield_turn_deg /
    // _along_cm / _side_cm / _up_cm / _rot_x_deg / _rot_y_deg / _rot_z_deg (OrbUnderShield). Unticked: the ball stays on the back of the hand under a shield, as before.
    bool orbUnderShield = true;
    OrbUnderShield orbShield;
    // The wrist's bone and the arm's model differ with the class, so one depth (Z) fits one hero: an offset per hero class, cm, added to the Z of BOTH wrists (whatever their own
    // Z, the mirror or not) while that hero plays - [hud] hand_vitals_orb_z_cm_<class> (amazon, sorceress, ...). 0 = the class has no value of its own: the wrists' Z as it is.
    float orbClassZCm[kHeroCount] = {};
    // The wrist under a shield, per hero class ([hud] hand_vitals_orb_shield_up_cm_<class> / _along_cm_<class>, cm): added to the shield's own depth and along offsets (orbShield.upCm / alongCm)
    // of the ball rolled to the inner side while that hero plays - the forearm's model, and so the depth of the inner side of the wrist under a shield, differs with the class. 0 = no value of its own.
    float orbShieldClassUpCm[kHeroCount] = {};
    float orbShieldClassAlongCm[kHeroCount] = {};
    // ... and the three angles of the medallion's turn there ([hud] hand_vitals_orb_shield_rot_x_deg_<class> / _y_ / _z_, degrees), added to the shield's own (orbShield.rotXDeg ...) and to the wrist's own turn,
    // anatomical like those (the same numbers on either wrist are the mirror image), easing in and out with the roll. 0 = no value of its own.
    float orbShieldClassRotXDeg[kHeroCount] = {}, orbShieldClassRotYDeg[kHeroCount] = {}, orbShieldClassRotZDeg[kHeroCount] = {};
    float orbOpacity = 0.95f;     // [hud] hand_vitals_orb_opacity
    int orbFill = 4;              // (kFillOutOfHand) [hud] hand_vitals_orb_fill: the way the liquid fills, a fixed direction of the wrist's frame (kFill*); the older hand_vitals_orb_liquid 0 / 1 is 0 / 5
    int orbShape = 0;             // [hud] hand_vitals_orb_shape: 0 a medallion (a glass dome on a metal base), 1 the full glass ball (kShape*)
    float orbPatina = 0.55f;      // [hud] hand_vitals_orb_patina (percent): how aged the base's metal is, 0 clean - 1 heavy verdigris
    int orbMetal = 0;             // [hud] hand_vitals_orb_metal: the base's metal, 0 gold, 1 bronze, 2 silver
    int orbDiscFaces = 1;         // [hud] hand_vitals_orb_disc: the game's own globes' disc (full ball shape): 0 turns to the eyes, 1 fixed on the wrist
};
constexpr int kStyleOrbs = 0, kStylePanel = 1, kStyleGlobes = 2;
// The fill directions: the liquid fills along a direction of the wrist's frame (the hand's axis toward the knuckles, across it toward the thumb, out of the back of the hand) - the
// level rises along it, so the liquid sits on the opposite side, whatever the head does.
constexpr int kFillElbow = 0,      // along the arm: the elbow side full
              kFillHand = 1,       // along the arm, the other way: the hand side full
              kFillThumb = 2,      // across the hand: the thumb side full
              kFillPinky = 3,      // across the hand: the little finger side full
              kFillOutOfHand = 4,  // out of the hand: from the skin up, the level parallel to the back of the hand
              kFillWorldUp = 5;    // level in the world, whatever the hand does
constexpr int kShapeMedallion = 0, kShapeBall = 1;

constexpr float kOrbClassZMax = 5.0f;   // a class's depth offset, -5 .. 5 cm
constexpr float kOrbRotMin = -180.0f, kOrbRotMax = 180.0f;   // the medallion's turn about each axis, degrees
constexpr float kOrbSizeMin = 3.0f, kOrbSizeMax = 9.0f, kOrbSpotMin = -10.0f, kOrbSpotMax = 10.0f,   // the ball's place: each of the three, either wrist
                kOrbOpacityMin = 0.3f, kOrbOpacityMax = 1.0f;
constexpr float kSizeMin = 5.0f, kSizeMax = 14.0f, kHeightMin = -5.0f, kHeightMax = 15.0f, kOpacityMin = 0.3f, kOpacityMax = 1.0f,
                kConeMin = 20.0f, kConeMax = 60.0f, kLowMin = 0.10f, kLowMax = 0.60f;

inline float Clamp(float v, float lo, float hi, float otherwise) {
    if (!std::isfinite(v)) return otherwise;
    return v < lo ? lo : v > hi ? hi : v;
}

// What is read from the ini, put in range (a number that is not one takes the default).
inline Config Sanitized(Config c) {
    c.hand = c.hand < 0 || c.hand > 2 ? 0 : c.hand;
    c.sizeCm = Clamp(c.sizeCm, kSizeMin, kSizeMax, 8.0f);
    c.heightCm = Clamp(c.heightCm, kHeightMin, kHeightMax, 6.0f);
    c.opacity = Clamp(c.opacity, kOpacityMin, kOpacityMax, 1.0f);
    c.coneDeg = Clamp(c.coneDeg, kConeMin, kConeMax, 35.0f);
    c.lowFrac = Clamp(c.lowFrac, kLowMin, kLowMax, 0.30f);
    c.style = c.style == kStylePanel ? kStylePanel : c.style == kStyleGlobes ? kStyleGlobes : kStyleOrbs;
    c.orbHands = c.orbHands < 0 || c.orbHands > 3 ? 0 : c.orbHands;
    c.orbSizeCm = Clamp(c.orbSizeCm, kOrbSizeMin, kOrbSizeMax, 5.0f);
    for (OrbSpot* s : {&c.orbLeft, &c.orbRight}) {
        s->alongCm = Clamp(s->alongCm, kOrbSpotMin, kOrbSpotMax, 3.0f);
        s->sideCm = Clamp(s->sideCm, kOrbSpotMin, kOrbSpotMax, 0.0f);
        s->upCm = Clamp(s->upCm, kOrbSpotMin, kOrbSpotMax, 4.0f);
        s->rotXDeg = Clamp(s->rotXDeg, kOrbRotMin, kOrbRotMax, 0.0f);
        s->rotYDeg = Clamp(s->rotYDeg, kOrbRotMin, kOrbRotMax, 0.0f);
        s->rotZDeg = Clamp(s->rotZDeg, kOrbRotMin, kOrbRotMax, 0.0f);
    }
    c.orbShield.turnDeg = Clamp(c.orbShield.turnDeg, kOrbRotMin, kOrbRotMax, 180.0f);
    c.orbShield.alongCm = Clamp(c.orbShield.alongCm, kOrbSpotMin, kOrbSpotMax, 0.0f);
    c.orbShield.sideCm = Clamp(c.orbShield.sideCm, kOrbSpotMin, kOrbSpotMax, 0.0f);
    c.orbShield.upCm = Clamp(c.orbShield.upCm, kOrbSpotMin, kOrbSpotMax, 0.0f);
    c.orbShield.rotXDeg = Clamp(c.orbShield.rotXDeg, kOrbRotMin, kOrbRotMax, 0.0f);
    c.orbShield.rotYDeg = Clamp(c.orbShield.rotYDeg, kOrbRotMin, kOrbRotMax, 0.0f);
    c.orbShield.rotZDeg = Clamp(c.orbShield.rotZDeg, kOrbRotMin, kOrbRotMax, 0.0f);
    for (float& z : c.orbClassZCm) z = Clamp(z, -kOrbClassZMax, kOrbClassZMax, 0.0f);
    for (float& z : c.orbShieldClassUpCm) z = Clamp(z, -kOrbClassZMax, kOrbClassZMax, 0.0f);
    for (float& z : c.orbShieldClassAlongCm) z = Clamp(z, -kOrbClassZMax, kOrbClassZMax, 0.0f);
    for (float& a : c.orbShieldClassRotXDeg) a = Clamp(a, kOrbRotMin, kOrbRotMax, 0.0f);
    for (float& a : c.orbShieldClassRotYDeg) a = Clamp(a, kOrbRotMin, kOrbRotMax, 0.0f);
    for (float& a : c.orbShieldClassRotZDeg) a = Clamp(a, kOrbRotMin, kOrbRotMax, 0.0f);
    c.orbOpacity = Clamp(c.orbOpacity, kOrbOpacityMin, kOrbOpacityMax, 0.95f);
    c.orbFill = c.orbFill < kFillElbow || c.orbFill > kFillWorldUp ? kFillOutOfHand : c.orbFill;
    c.orbShape = c.orbShape == kShapeBall ? kShapeBall : kShapeMedallion;
    c.orbPatina = Clamp(c.orbPatina, 0.0f, 1.0f, 0.55f);
    c.orbMetal = c.orbMetal < 0 || c.orbMetal > 2 ? 0 : c.orbMetal;
    c.orbDiscFaces = c.orbDiscFaces == 0 ? 0 : 1;
    return c;
}

// The depth offset (cm) the hero now playing adds to both wrists' Z: the class's own value, 0 for a class not known (the plugin could not read it, or it is no class of ours) - then the
// wrists' Z is the one they have, as before the classes had values of their own.
inline float ClassZCm(const Config& c, int heroClass) {
    return heroClass >= 0 && heroClass < kHeroCount ? Clamp(c.orbClassZCm[heroClass], -kOrbClassZMax, kOrbClassZMax, 0.0f) : 0.0f;
}

// The depth and the along offsets (cm) the hero now playing adds under a shield: its own, 0 for a class not known or with none.
inline float ClassShieldUpCm(const Config& c, int heroClass) {
    return heroClass >= 0 && heroClass < kHeroCount ? Clamp(c.orbShieldClassUpCm[heroClass], -kOrbClassZMax, kOrbClassZMax, 0.0f) : 0.0f;
}
inline float ClassShieldAlongCm(const Config& c, int heroClass) {
    return heroClass >= 0 && heroClass < kHeroCount ? Clamp(c.orbShieldClassAlongCm[heroClass], -kOrbClassZMax, kOrbClassZMax, 0.0f) : 0.0f;
}
// The three angles (degrees, X / Y / Z) the hero now playing adds to the medallion's turn under a shield: its own, 0 for a class not known or with none.
inline OrbTurn ClassShieldRotDeg(const Config& c, int heroClass) {
    OrbTurn t;
    if (heroClass < 0 || heroClass >= kHeroCount) return t;
    t.xDeg = Clamp(c.orbShieldClassRotXDeg[heroClass], kOrbRotMin, kOrbRotMax, 0.0f);
    t.yDeg = Clamp(c.orbShieldClassRotYDeg[heroClass], kOrbRotMin, kOrbRotMax, 0.0f);
    t.zDeg = Clamp(c.orbShieldClassRotZDeg[heroClass], kOrbRotMin, kOrbRotMax, 0.0f);
    return t;
}

// ---- the hero's numbers ----------------------------------------------------------------------------------------------

constexpr float kStatScale = 256.0f;   // the game keeps life and mana with 8 fractional bits (stats 6 / 7 life / max, 8 / 9 mana / max)

struct Vitals {
    bool known = false;       // both pairs of numbers made sense
    bool dead = false;        // no life left
    float life = 0, maxLife = 0, mana = 0, maxMana = 0;   // in points (the raw value over 256)
};

// From the game's raw stat values (1/256 of a point). A maximum life under one point or past a million, a negative life or
// one more than twice the maximum, and the same for mana (whose maximum may be 0: a hero with no mana pool) are not a hero's
// numbers: unknown. The bounds are d2r-pcvr's vitals read (vr/native_ui.cpp), the same as the low-life vignette's.
inline Vitals FromStats(int32_t life, int32_t maxLife, int32_t mana, int32_t maxMana) {
    Vitals v;
    const int64_t one = (int64_t)kStatScale, cap = 1000000 * one;
    if (maxLife < one || (int64_t)maxLife >= cap) return v;
    if (life < 0 || (int64_t)life > 2 * (int64_t)maxLife) return v;
    if (maxMana < 0 || (int64_t)maxMana >= cap) return v;
    if (mana < 0 || (int64_t)mana > 2 * (int64_t)maxMana) return v;
    v.known = true;
    v.dead = life == 0;
    v.life = (float)((double)life / kStatScale);
    v.maxLife = (float)((double)maxLife / kStatScale);
    v.mana = (float)((double)mana / kStatScale);
    v.maxMana = (float)((double)maxMana / kStatScale);
    return v;
}

// A read older than this is not the hero's numbers any more.
constexpr double kStaleMs = 1500.0;
inline bool Fresh(double ageMs) { return ageMs >= 0.0 && ageMs <= kStaleMs; }

// How full a globe is, 0 - 1 (more than the maximum is full; no maximum is empty).
inline float Fraction(float now, float max) {
    if (!(max > 0.0f) || !std::isfinite(now) || !std::isfinite(max)) return 0.0f;
    return std::clamp(now / max, 0.0f, 1.0f);
}

constexpr int kFillSteps = 128;   // the picture is repainted when a globe's level moves a step (1 / 128 of its height: under a pixel)
inline int FillStep(float frac) { return (int)std::lround(std::clamp(std::isfinite(frac) ? frac : 0.0f, 0.0f, 1.0f) * kFillSteps); }

// The points shown for a value: whole points as the game's own bar shows them (fractions dropped); from 10 000 up as 12.3k / 123k /
// 1.2M so that "now / max" still fits under a small globe.
inline void FormatPoints(float points, char* out, size_t cap) {
    if (!cap) return;
    int64_t v = std::isfinite(points) && points > 0.0f ? (int64_t)points : 0;
    if (v > 999999999) v = 999999999;
    if (v < 10000) snprintf(out, cap, "%d", (int)v);
    else if (v < 100000) snprintf(out, cap, "%d.%dk", (int)(v / 1000), (int)((v % 1000) / 100));
    else if (v < 1000000) snprintf(out, cap, "%dk", (int)(v / 1000));
    else snprintf(out, cap, "%d.%dM", (int)(v / 1000000), (int)((v % 1000000) / 100000));
}
inline void FormatPair(float now, float max, char* out, size_t cap) {
    char a[16], b[16];
    FormatPoints(now, a, sizeof a);
    FormatPoints(max, b, sizeof b);
    snprintf(out, cap, "%s / %s", a, b);
}

// What the painter draws: everything that changes the picture, and nothing else (so a change in it is a repaint and no change is none).
struct Look {
    uint8_t lifeFill = 0, manaFill = 0;   // 0 - kFillSteps
    uint8_t opacity = 20;                 // 0 - 20: the settings' opacity in steps of 0.05
    bool alert = false;                   // the life globe glows (under the low-life threshold, pulse on)
    char lifeText[24] = {};
    char manaText[24] = {};
    bool operator==(const Look& o) const {
        return lifeFill == o.lifeFill && manaFill == o.manaFill && opacity == o.opacity && alert == o.alert &&
               !std::strcmp(lifeText, o.lifeText) && !std::strcmp(manaText, o.manaText);
    }
    bool operator!=(const Look& o) const { return !(*this == o); }
};

inline bool Alert(const Vitals& v, const Config& c) { return c.pulse && v.known && !v.dead && Fraction(v.life, v.maxLife) < c.lowFrac; }

inline Look MakeLook(const Vitals& v, const Config& c) {
    Look l;
    l.lifeFill = (uint8_t)FillStep(Fraction(v.life, v.maxLife));
    l.manaFill = (uint8_t)FillStep(Fraction(v.mana, v.maxMana));
    l.opacity = (uint8_t)std::lround(Clamp(c.opacity, kOpacityMin, kOpacityMax, 1.0f) * 20.0f);
    l.alert = Alert(v, c);
    FormatPair(v.life, v.maxLife, l.lifeText, sizeof l.lifeText);
    FormatPair(v.mana, v.maxMana, l.manaText, sizeof l.manaText);
    return l;
}

// The soft pulse: the panel breathes between 80% and 100% of its opacity about once a second (well under any flashing limit) while
// the life is under the threshold and the pulse is on; otherwise 1.
constexpr float kBreathHz = 1.0f, kBreathLow = 0.80f;
inline float Breath(bool alert, double seconds) {
    if (!alert || !std::isfinite(seconds)) return 1.0f;
    const float s = 0.5f + 0.5f * (float)std::sin(6.283185307179586 * (double)kBreathHz * seconds);
    return kBreathLow + (1.0f - kBreathLow) * s;
}

// ---- the palm ----------------------------------------------------------------------------------------------------------------

struct HandPose {
    V3 pos;                  // the grip pose: the palm's middle
    Q rot;                   // unit
    bool valid = false;      // the position and the turn are located (not a held one)
    float squeeze = 0.0f;    // the grip, 0 - 1
};
struct HeadPose {
    V3 pos;                  // between the eyes
    Q rot;
    bool valid = false;
};

// Where the palm faces, unit, away from the palm. Side 0 left, 1 right (directinput's order).
inline V3 PalmNormal(const Q& rot, int side, bool flip = false) {
    const bool leftLike = (side == 0) != flip;
    return hset::Rot(rot, {leftLike ? 1.0f : -1.0f, 0.0f, 0.0f});
}

// The thresholds. A palm is presented ("enter") inside the tighter ones; it stays ("keep") inside the wider ones.
constexpr float kNearEnterM = 0.15f, kFarEnterM = 0.70f, kNearKeepM = 0.12f, kFarKeepM = 0.80f;   // the hand's distance from the head
constexpr float kBelowEnterM = 0.50f, kBelowKeepM = 0.60f;     // the hand lower than the eyes by at most this: not hanging at the side
constexpr float kGazeEnterDeg = 75.0f, kGazeKeepDeg = 90.0f;   // the hand within this of where the head looks: in front of the face
constexpr float kFistEnter = 0.60f, kFistKeep = 0.80f;         // a grip squeezed past this is a fist, not a palm
constexpr float kConeKeepExtraDeg = 15.0f, kConeKeepMaxDeg = 80.0f;

struct Look3 {              // how a hand stands to the head
    bool valid = false;
    float angleDeg = 180.0f;   // palm normal to the direction to the head
    float distM = 0.0f;
    float belowM = 0.0f;       // eyes above the hand
    float gazeDeg = 180.0f;    // head forward to the direction to the hand
    bool enter = false, keep = false;
};

inline float Deg(float rad) { return rad * 57.29577951308232f; }

inline Look3 Measure(const HandPose& hand, int side, const HeadPose& head, const Config& cfg) {
    Look3 r;
    if (!hand.valid || !head.valid) return r;
    const V3 toHead = hset::Sub(head.pos, hand.pos);
    const float dist = std::sqrt(hset::Dot(toHead, toHead));
    if (!std::isfinite(dist) || dist < 1e-4f) return r;
    const V3 dir = hset::Mul(toHead, 1.0f / dist);
    const V3 n = PalmNormal(hand.rot, side, cfg.flip);
    const V3 fwd = hset::Rot(head.rot, {0.0f, 0.0f, -1.0f});
    r.valid = true;
    r.distM = dist;
    r.belowM = head.pos.y - hand.pos.y;
    r.angleDeg = Deg(std::acos(std::clamp(hset::Dot(n, dir), -1.0f, 1.0f)));
    r.gazeDeg = Deg(std::acos(std::clamp(-hset::Dot(fwd, dir), -1.0f, 1.0f)));   // fwd . (hand - head) = -fwd . dir
    if (!std::isfinite(r.angleDeg) || !std::isfinite(r.gazeDeg) || !std::isfinite(r.belowM)) { r.valid = false; return r; }
    const float cone = Clamp(cfg.coneDeg, kConeMin, kConeMax, 35.0f);
    const float coneKeep = std::min(cone + kConeKeepExtraDeg, kConeKeepMaxDeg);
    r.enter = r.angleDeg <= cone && dist >= kNearEnterM && dist <= kFarEnterM && r.belowM <= kBelowEnterM && r.gazeDeg <= kGazeEnterDeg &&
              hand.squeeze <= kFistEnter;
    r.keep = r.angleDeg <= coneKeep && dist >= kNearKeepM && dist <= kFarKeepM && r.belowM <= kBelowKeepM && r.gazeDeg <= kGazeKeepDeg &&
             hand.squeeze <= kFistKeep;
    return r;
}

// ---- where the panel hangs -------------------------------------------------------------------------------------------------

constexpr float kPanelAspect = 2.0f / 3.0f;   // height over width (the picture is 384 x 256)
constexpr float kHoverM = 0.03f;              // toward the head from the palm, so the panel is never inside the hand
constexpr float kShrinkWhenFaded = 0.92f;     // the panel grows from this to its size as it comes up

inline float PanelWidthM(const Config& c) { return Clamp(c.sizeCm, kSizeMin, kSizeMax, 8.0f) * 0.01f; }
inline float PanelHeightM(const Config& c) { return PanelWidthM(c) * kPanelAspect; }

inline Q FromAxes(V3 x, V3 y, V3 z) {   // the turn whose columns are the three axes (a right-handed, orthonormal set)
    const float m00 = x.x, m01 = y.x, m02 = z.x, m10 = x.y, m11 = y.y, m12 = z.y, m20 = x.z, m21 = y.z, m22 = z.z;
    Q q;
    const float tr = m00 + m11 + m22;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 1e-9f ? Q{q.x / n, q.y / n, q.z / n, q.w / n} : Q{};
}

// The panel: its middle over the palm (toward the head by kHoverM, `heightCm` up), its front toward the head, its top up. In the
// room's space, like the poses it is made from. `valid` false when the hand and the head are on top of each other.
inline hset::Pose PanelPose(const V3& hand, const V3& head, float heightCm) {
    hset::Pose p;
    const V3 up{0.0f, 1.0f, 0.0f};
    const V3 toHead = hset::Sub(head, hand);
    const float d = std::sqrt(hset::Dot(toHead, toHead));
    if (!std::isfinite(d) || d < 1e-3f) return p;
    p.p = hset::Add(hset::Add(hand, hset::Mul(toHead, kHoverM / d)), hset::Mul(up, Clamp(heightCm, kHeightMin, kHeightMax, 6.0f) * 0.01f));
    V3 z = hset::Sub(head, p.p);
    const float zl = std::sqrt(hset::Dot(z, z));
    if (!std::isfinite(zl) || zl < 1e-3f) return p;
    z = hset::Mul(z, 1.0f / zl);
    V3 x{up.y * z.z - up.z * z.y, up.z * z.x - up.x * z.z, up.x * z.y - up.y * z.x};   // up x z
    float xl = std::sqrt(hset::Dot(x, x));
    if (xl < 0.05f) {   // straight above or below the head: any level direction
        x = {1.0f, 0.0f, 0.0f};
        xl = 1.0f;
    }
    x = hset::Mul(x, 1.0f / xl);
    const V3 y{z.y * x.z - z.z * x.y, z.z * x.x - z.x * x.z, z.x * x.y - z.y * x.x};   // z x x
    p.q = FromAxes(x, y, z);
    p.valid = true;
    return p;
}

// ---- when it shows: the palm, a short dwell, a fade ------------------------------------------------------------------------

constexpr float kDwellS = 0.12f, kFadeInS = 0.18f, kFadeOutS = 0.28f, kFadeOutFastS = 0.12f, kMaxStepS = 0.1f;

// What the caller knows of where it may show.
struct Gate {
    bool hideNow = false;   // not F4, not switched on, no session: alpha 0 in this very step
    bool hide = false;      // a menu or one of the plugin's panels is up, the hero is dead, the numbers are unknown, the input has no focus: fades out fast
};

// What the plugin knows each frame; MakeGate turns it into the gate.
struct Flags {
    bool f4 = true;            // viewgate::F4(): VR first person
    bool otherView = false;    // directinput's kOtherView
    bool focused = true;       // the session has the input focus
    bool inMenu = false;       // a menu is open, or there is no game yet (directinput's kInMenu)
    bool uiUp = false;         // one of the plugin's own panels is up (the skill panel, the settings panel; directinput's kUiUp)
    bool vitalsFresh = false;  // the hero's numbers were read lately
};
inline Gate MakeGate(const Flags& f, const Vitals& v) {
    Gate g;
    g.hideNow = !f.f4 || f.otherView;
    g.hide = !f.focused || f.inMenu || f.uiUp || !f.vitalsFresh || !v.known || v.dead;
    return g;
}

struct In {
    float dt = 0.0f;                 // seconds since the last step
    HeadPose head;
    HandPose hand[2];                // 0 left, 1 right
    Gate gate;
    Config cfg;
};

struct Out {
    float alpha = 0.0f;              // 0 - 1, eased
    int side = -1;                   // the hand it hangs at, -1 none
    hset::Pose pose;                 // valid while alpha > 0
    float widthM = 0.0f, heightM = 0.0f;   // at full alpha
    float angleDeg = 180.0f;         // the shown hand's palm to the head
};

class Reveal {
public:
    enum class State { Idle, Dwell, Shown, Fading };

    void Reset() { *this = Reveal{}; }
    State state() const { return state_; }
    int side() const { return side_; }
    bool Active() const { return state_ != State::Idle; }   // the picture is wanted

    Out Step(const In& in) {
        Out out;
        const Config cfg = Sanitized(in.cfg);
        if (!cfg.on || in.gate.hideNow || !in.head.valid || !std::isfinite(in.dt)) {
            Reset();
            return out;
        }
        const float dt = std::clamp(in.dt, 0.0f, kMaxStepS);
        Look3 m[2];
        for (int s = 0; s < 2; ++s)
            if (cfg.hand == 2 || cfg.hand == s) m[s] = Measure(in.hand[s], s, in.head, cfg);

        const bool hide = in.gate.hide;
        if (state_ == State::Idle) {
            int best = -1;   // the palm turned to the face the most
            for (int s = 0; s < 2; ++s)
                if (m[s].valid && m[s].enter && (best < 0 || m[s].angleDeg < m[best].angleDeg)) best = s;
            if (best >= 0 && !hide) {
                state_ = State::Dwell;
                side_ = best;
                dwell_ = 0.0f;
                progress_ = 0.0f;
            }
        } else {
            const Look3& cur = m[side_];
            const bool keep = cur.valid && cur.keep && !hide;
            switch (state_) {
                case State::Dwell:   // it must hold a moment: a hand swept past the face shows nothing
                    if (!keep) { state_ = State::Idle; side_ = -1; }
                    else if ((dwell_ += dt) >= kDwellS) state_ = State::Shown;
                    break;
                case State::Shown:
                    if (!keep) state_ = State::Fading;
                    break;
                case State::Fading:
                    if (cur.valid && cur.enter && !hide) state_ = State::Shown;   // the palm came back: resume
                    break;
                default: break;
            }
        }

        if (state_ == State::Shown) progress_ = std::min(1.0f, progress_ + dt / kFadeInS);
        else if (state_ == State::Fading) progress_ = std::max(0.0f, progress_ - dt / (hide ? kFadeOutFastS : kFadeOutS));
        else progress_ = 0.0f;
        if (state_ == State::Fading && progress_ <= 0.0f) {   // gone: free to pick a hand again
            state_ = State::Idle;
            side_ = -1;
        }

        if (side_ >= 0 && state_ != State::Idle) {
            const HandPose& h = in.hand[side_];
            if (h.valid) {
                const hset::Pose p = PanelPose(h.pos, in.head.pos, cfg.heightCm);
                if (p.valid) held_ = p;
            }
            out.angleDeg = m[side_].valid ? m[side_].angleDeg : 180.0f;
        }
        const float t = progress_;
        out.alpha = t * t * (3.0f - 2.0f * t);
        out.side = out.alpha > 0.0f ? side_ : -1;
        out.pose = held_;
        out.widthM = PanelWidthM(cfg);
        out.heightM = PanelHeightM(cfg);
        if (!(out.alpha > 0.0f) || !held_.valid) { out.alpha = 0.0f; out.side = -1; }
        return out;
    }

private:
    State state_ = State::Idle;
    int side_ = -1;
    float dwell_ = 0.0f, progress_ = 0.0f;
    hset::Pose held_;
};

}  // namespace handvitals
