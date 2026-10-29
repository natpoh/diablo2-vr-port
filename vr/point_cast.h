// point_cast.h - [gameplay] teleport_circle: the skills cast onto a point on the ground (Teleport, Fire Wall, Meteor, Blizzard, the curses, the traps ...)
// aimed with an arc and a circle (VR F4). Pure: no game calls, no globals; cleanroom/tests/point_cast_test.cpp. The game's side is vr/point_cast_wire.cpp
// (the pad, the slots, the arc and circle drawn) and vrcam's spell resolver (the cast's point, with leap_by_gaze's answers: vr/leap_gaze.h).
//
// What a press of the pad button (or the right trigger) whose slot holds such a skill does:
//   - the press is taken out of the pad the game polls while the button is held (Hold::Poll's strip): the game casts nothing yet;
//   - let go within kHoldMs: a tap - the cast goes straight to where the head looks (leap_by_gaze's gaze point);
//   - held longer: an arc leaves the staff's tip (or the casting hand) along the staff, as a thrown thing would fly - the hand raised throws it farther,
//     lowered nearer (MakeArc) - and a circle stands where it meets the ground, never farther from the hero than the cap; let go: the slot's own button is
//     pressed once and the cast goes to the circle.
// The cast itself is the game's: its slot, its key, its mana, cooldown and checks; only the cast's point is ours. The left stick stays the walk's.
//
// Which skills: the list in the ini ([gameplay] point_skills, skill ids); the default is the game's own Skills.txt (3.3) read for the skills that are cast at a
// ground location (kDefaultSkills below says how).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "wheel_types.h"   // the game's controller skill slots, as the skill panel binds them (kSlotDefs)

namespace pointcast {

constexpr int kTeleport = 54;              // skills.txt row (Sorceress)
constexpr uint64_t kHoldMs = 200;          // a press let go sooner is a tap: straight to the gaze
constexpr float kMinUnits = 2.0f;          // the circle never nearer the hero than this (world units: one cell) - it keeps a heading
constexpr float kUnitsPerCell = 2.0f;      // game world units in one grid cell
constexpr float kDefaultCapCells = 50.0f;  // [gameplay] point_max_cells: the circle's farthest from the hero, cells (the user: "no farther than 50 cells")
constexpr float kHardCapCells = 48.0f;     // ... never past this whatever the ini says: the game refuses a point 50 cells off the hero on either axis (aim_point.h),
                                           // and the hero's step between our point and the game's check of it takes up to two
constexpr uint8_t kLtDown = 30;            // XINPUT_GAMEPAD_TRIGGER_THRESHOLD: the left trigger counts as held (the second bank of slots)
constexpr int kArcPoints = 17;             // the arc drawn: this many points, kArcPoints - 1 segments
// [gameplay] point_arc_height: how high the arc is DRAWN above the straight line from the staff's tip to the circle, % of the throw's own rise (0 a straight
// line, 100 the throw's own - the first build's, which the user found "too high": at the cap a 45-degree throw rises about 8 m). Only the drawing: where it
// lands is the throw's whatever this is.
constexpr float kDefaultHeightPct = 25.0f, kMaxHeightPct = 100.0f;
inline float HeightScale(float pct) {
    if (!std::isfinite(pct)) return kDefaultHeightPct / 100.0f;
    return std::clamp(pct, 0.0f, kMaxHeightPct) / 100.0f;
}

// The default list: the game's Skills.txt (D2R 3.3, data\global\excel\skills.txt) read for the class skills that are cast onto a ground location - not passive,
// not an aura, not TargetableOnly (a unit only: Bone Spirit, Fist of the Heavens), not TargetCorpse (Corpse Explosion, Raise Skeleton), no srvmissile of
// their own (the bolts and arrows fly along the hand already) and none of the self-centred ones (the novas, the armours, the warcries, Thunder Storm):
// SearchOpenXY (the game finds an open cell at the point: Fire Wall, Meteor, Blizzard, Fissure, Volcano, the sigils, Abyss), warp (Teleport, Blade Warp),
// lob (Fire Blast lands where it is aimed), the area curses (srvdofunc 30), the summons and traps placed at the point (Hydra, Bone Wall, the golems, the
// sentries, Blade Sentinel, the Warlock's demons), Mind Blast (SearchEnemyXY: an area at the point), Apocalypse (Volcano's own server function).
// Left out, by the same data: Bone Prison, Iron Golem, Confuse, Attract (SearchEnemyNear: a monster or an item is the target), Dragon Flight (a warp to
// a monster), Firestorm and Flame Wave (sent along a heading), Ring of Fire, Death Mark, Blood Boil, Miasma Chains (not plainly a ground point), and the
// summons the game places by the hero (Valkyrie, Decoy, the Druid's spirits and beasts, Raven, the Shadow Warrior and Master). Leap and Leap Attack join
// with [gameplay] leap_arc only. None of these rows has a cast distance of its own in Skills.txt: the cap is the circle's.
inline constexpr int kDefaultSkills[] = {
    54, 51, 56, 59, 62,                       // Sorceress: Teleport, Fire Wall, Meteor, Blizzard, Hydra
    66, 71, 72, 76, 77, 82, 87, 91,           // Necromancer curses: Amplify Damage, Dim Vision, Weaken, Iron Maiden, Terror, Life Tap, Decrepify, Lower Resist
    78, 75, 85, 94,                           // ... Bone Wall, Clay Golem, Blood Golem, Fire Golem
    234, 244,                                 // Druid: Fissure, Volcano
    251, 256, 257, 261, 262, 271, 272, 276, 273,   // Assassin: Fire Blast, Shock Web, Blade Sentinel, the five sentries, Mind Blast
    390, 393, 396, 400, 401, 402, 373, 376, 377,   // Warlock: Blade Warp, Sigil Lethargy, Sigil Rancor, Sigil Death, Apocalypse, Abyss, Summon Goatman, Tainted, Defiler
};
constexpr int kLeap = 132, kLeapAttack = 143;   // [gameplay] leap_arc: on the list too

// The skills as a set of ids (0 .. 1023).
struct SkillSet {
    uint64_t bits[16] = {};
    void Add(int id) { if (id >= 0 && id < 1024) bits[id >> 6] |= 1ull << (id & 63); }
    bool Has(int id) const { return id >= 0 && id < 1024 && ((bits[id >> 6] >> (id & 63)) & 1) != 0; }
    void Clear() { *this = SkillSet{}; }
};
// The ini's list: skill ids separated by commas, spaces or semicolons; anything else in it is skipped. An empty string is an empty list.
inline SkillSet ParseSkills(const char* s) {
    SkillSet set;
    if (!s) return set;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            int v = 0;
            while (*s >= '0' && *s <= '9') {
                v = v * 10 + (*s - '0');
                if (v > 100000) v = 100000;
                ++s;
            }
            set.Add(v);
        } else {
            ++s;
        }
    }
    return set;
}
inline SkillSet DefaultSkills() {
    SkillSet set;
    for (int id : kDefaultSkills) set.Add(id);
    return set;
}

// The circle's cap in cells: the ini's, held to [3, kHardCapCells].
inline float CapCells(float iniCells) {
    if (!std::isfinite(iniCells)) return kHardCapCells;
    return std::clamp(iniCells, 3.0f, kHardCapCells);
}

// The slots: every skill slot the game's controller has, as the skill panel binds them - wheel_types.h kSlotDefs, its numbering (B X Y RB 0..3, the same with the
// left trigger held 4..7, A 8, RT 9, LT + A 10, LT + RT 11): a slot is its control - the XInput button of kSlotDefs, or the right trigger where it names none
// (RT, found by elimination there too) - with the left trigger held or not. Nothing else casts a slot's skill (the left trigger is the second row itself; LB
// and the d-pad hold no skill slot in the game's controller layout). The right trigger is an analog value: here it is the bit kRt of a control word, set
// while the trigger is past kRtDown.
constexpr uint16_t kPadB = wheel::kPadB, kPadX = wheel::kPadX, kPadY = wheel::kPadY, kPadRB = wheel::kPadRb, kPadA = wheel::kPadA;
constexpr uint32_t kRt = 1u << 16;
constexpr uint8_t kRtDown = 30;            // XINPUT_GAMEPAD_TRIGGER_THRESHOLD
constexpr int kSlotCount = wheel::kSlotCount;
// The control a slot's press is (a button bit, or kRt), and whether it is the left trigger's row.
constexpr uint32_t ControlOf(int slot) {
    return slot < 0 || slot >= kSlotCount ? 0u : wheel::kSlotDefs[slot].button ? uint32_t(wheel::kSlotDefs[slot].button) : kRt;
}
constexpr bool AltOf(int slot) { return slot >= 0 && slot < kSlotCount && wheel::kSlotDefs[slot].alt; }
// The slot a control presses with the left trigger held or not; -1 none.
constexpr int SlotOf(uint32_t control, bool lt) {
    for (int s = 0; s < kSlotCount; ++s)
        if (ControlOf(s) == control && AltOf(s) == lt) return s;
    return -1;
}
// The pad press of a slot (its own button, the left trigger for the second row, the right trigger for RT): what the release pulses.
struct SlotPad {
    uint16_t buttons = 0;
    uint8_t lt = 0, rt = 0;
};
constexpr SlotPad PadOf(int slot) {
    if (slot < 0 || slot >= kSlotCount) return {};
    const uint32_t c = ControlOf(slot);
    return {uint16_t(c == kRt ? 0u : c), uint8_t(AltOf(slot) ? 255 : 0), uint8_t(c == kRt ? 255 : 0)};
}

// ---- the press: tap or hold ----------------------------------------------------------------------------------------------------------
struct Step {
    uint32_t strip = 0;      // the controls to take out of the game's pad this poll (button bits; kRt: the right trigger to 0)
    bool started = false;    // a point skill's button went down this poll (taken)
    bool circle = false;     // ... has been held kHoldMs: the arc and circle are up from this poll on
    bool released = false;   // ... was let go this poll: cast now (tap or circle) - unless cancelled
    bool tap = false;        // released before kHoldMs
    bool cancelled = false;  // released, but the press was not allowed any more (a menu, another view): no cast
    int slot = -1;           // the press's slot (0..11)
    int skill = -1;          // ... and the skill on it when it went down
};
class Hold {
public:
    // Every poll of the game's pad: its buttons and its two triggers (as they go to the game), whether a press may be ours now (F4, the box, in the world, no
    // menu or panel), skillOf(slot) -> the skill the game has on that slot (-1 none / not read), isPoint(skill) -> it is on the list. Only a press that goes
    // down while allowed, on a slot that holds a listed skill, is taken; a control already down then, and every other control, is the game's - so a held RT
    // or button of any other skill repeats its casts in the game as ever, and only a point skill's is held for the arc. ignore: controls that start nothing this
    // poll (the release's own press of the slot, on its way to the game).
    template <class F, class P>
    Step Poll(uint16_t buttons, uint8_t lt, uint8_t rt, bool allowed, F skillOf, P isPoint, uint64_t nowMs, uint32_t ignore = 0) {
        Step s;
        const uint32_t down = buttons | (rt > kRtDown ? kRt : 0u);
        if (active_) {
            s.slot = slot_;
            s.skill = skill_;
            if (!allowed) cancelled_ = true;
            if (down & bit_) {
                s.strip = bit_;   // held: still ours (a cancelled press too, until it is let go - the game is not to see it half way)
                if (!circle_ && !cancelled_ && nowMs - start_ >= kHoldMs) s.circle = circle_ = true;
            } else {
                s.released = true;
                s.tap = !circle_;
                s.cancelled = cancelled_;
                active_ = false;
            }
        } else if (allowed) {
            for (int k = 0; k < kSlotCount; ++k) {   // each control once: the slots without the left trigger name them all
                if (AltOf(k)) continue;
                const uint32_t b = ControlOf(k);
                if (!(down & b) || (prev_ & b) || (ignore & b)) continue;
                const int slot = SlotOf(b, lt > kLtDown);
                if (slot < 0) continue;
                const int skill = skillOf(slot);
                if (!isPoint(skill)) continue;
                active_ = true;
                circle_ = cancelled_ = false;
                bit_ = b;
                slot_ = slot;
                skill_ = skill;
                start_ = nowMs;
                s.started = true;
                s.strip = b;
                s.slot = slot;
                s.skill = skill;
                break;
            }
        }
        prev_ = down;
        return s;
    }
    // (the buttons alone: no right trigger)
    template <class F, class P>
    Step Poll(uint16_t buttons, uint8_t lt, bool allowed, F skillOf, P isPoint, uint64_t nowMs) { return Poll(buttons, lt, 0, allowed, skillOf, isPoint, nowMs); }
    bool Active() const { return active_; }
    bool CircleUp() const { return active_ && circle_ && !cancelled_; }
    int Skill() const { return active_ ? skill_ : -1; }
    void Reset() { *this = Hold{}; }

private:
    bool active_ = false, circle_ = false, cancelled_ = false;
    uint32_t bit_ = 0, prev_ = 0;
    int slot_ = -1, skill_ = -1;
    uint64_t start_ = 0;
};

// ---- the circle on the ground ----------------------------------------------------------------------------------------------------------
// Its place against the hero's, across the ground in the world's x and z (world units).
struct Offset {
    float x = 0.0f, z = 0.0f;
};
inline float Length(Offset o) { return std::hypot(o.x, o.z); }

// Held to [minUnits, capUnits] from the hero along its own heading; `fallback` when it has none (it sits on the hero) or is not finite.
inline Offset Clamp(Offset o, float minUnits, float capUnits, Offset fallback) {
    const float l = Length(o);
    if (!std::isfinite(l) || !(l > 1e-4f)) return fallback;
    const float hi = std::max(minUnits, std::isfinite(capUnits) ? capUnits : minUnits);
    const float to = std::clamp(l, minUnits, hi);
    return {o.x / l * to, o.z / l * to};
}

// ---- the arc ------------------------------------------------------------------------------------------------------------------------
struct V3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
struct Arc {
    bool ok = false;
    int skill = -1;       // the skill whose press it is (its colour)
    int source = 0;       // where it leaves from (the log): 1 the weapon's tip as drawn, 2 the drawn caster or hand, 3 the controller, 4 the belly aimed by the head
    Offset land;          // the circle: where the arc comes down, against the hero (world units), held to the cap
    bool capped = false;  // the throw would land farther: the circle stopped at the cap along its heading (the arc is drawn to it)
    int n = 0;            // the arc's points (world)
    V3 pts[kArcPoints];
};
// A thing thrown from `o` along `dir` (world units; dir need not be unit) with the reach `reach` (v^2 / g: on flat ground, thrown at 45 degrees from the
// ground itself, it lands that far) comes down on the hero's ground (y = groundY) - the hero stands at (hx, hz). The hand raised to 45 degrees throws it
// farthest, lower nearer; a line pointing down meets the ground nearer still. The landing is held to [minUnits, capUnits] from the hero along its heading;
// held, the arc is bent to come down on the circle. No heading on the ground (a line within ~3 degrees of straight up or down), or not finite: !ok.
// heightScale (HeightScale): the drawn points rise that share of the throw's own height above the straight line from `o` to the circle (1: the throw's).
inline Arc MakeArc(V3 o, V3 dir, float groundY, float hx, float hz, float reach, float minUnits, float capUnits, float heightScale = 1.0f) {
    Arc a;
    const float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (!(dl > 1e-6f) || !std::isfinite(dl) || !std::isfinite(o.x + o.y + o.z) || !std::isfinite(groundY) || !std::isfinite(hx + hz) || !(reach > 0.0f)) return a;
    const float ux = dir.x / dl, uy = dir.y / dl, uz = dir.z / dl;
    const float dh = std::hypot(ux, uz);
    if (dh < 0.05f) return a;
    const float tanT = uy / dh;
    const float h = std::max(0.0f, o.y - groundY);
    // where it comes down: oy + s tan - s^2 / (2 K dh^2) = groundY
    const float qa = 1.0f / (2.0f * reach * dh * dh);
    const float s = (tanT + std::sqrt(tanT * tanT + 4.0f * qa * h)) / (2.0f * qa);
    const float hxs = ux / dh, hzs = uz / dh;   // the heading on the ground
    const Offset raw{o.x + hxs * s - hx, o.z + hzs * s - hz};
    const Offset fallback{hxs * minUnits, hzs * minUnits};
    a.land = Clamp(raw, minUnits, capUnits, fallback);
    a.capped = Length(raw) > Length(a.land) + 1e-3f;
    // the arc as drawn, in its own upright plane from o to the circle: the throw's own when it lands there, else bent (K refit) to come down on the circle
    const float px = hx + a.land.x, pz = hz + a.land.z;
    const float sx = px - o.x, sz = pz - o.z, sd = std::hypot(sx, sz);
    float k = reach;
    bool straight = false;
    if (std::fabs(sd - s) > 1e-3f) {
        const float den = 2.0f * dh * dh * (h + sd * tanT);
        if (den > 1e-6f) k = sd * sd / den;
        else straight = true;   // pointed so far down the line meets the ground before the circle: a straight line to it
    }
    const float qa2 = 1.0f / (2.0f * k * dh * dh);
    const float hs = std::isfinite(heightScale) ? std::clamp(heightScale, 0.0f, 1.0f) : 1.0f;
    a.n = kArcPoints;
    for (int i = 0; i < kArcPoints; ++i) {
        const float t = float(i) / float(kArcPoints - 1);
        const float d = sd * t;
        V3& p = a.pts[i];
        p.x = o.x + sx * t;
        p.z = o.z + sz * t;
        const float line = o.y + (groundY - o.y) * t;   // the straight line from the tip to the circle
        p.y = straight ? line : line + hs * (o.y + d * tanT - d * d * qa2 - line);
        if (!std::isfinite(p.y)) p.y = groundY;
    }
    a.pts[kArcPoints - 1].y = groundY;
    a.ok = true;
    return a;
}

// ---- the look: an energy beam and a glowing ring (the effect draws them: reshade/D2R_DepthFog.fx PointArc) ------------------------------------
// [gameplay] point_arc_color (#RRGGBB) and point_arc_opacity (0 .. 100 %): the beam's and the ring's colour and strength; red at 60 % by default (the first
// build's red tubes were 60 % opaque). The beam's core is white-hot, its glow the colour, a pulse flows along it from the staff to the circle and it
// crackles: its middle points are shaken across the line a little, afresh every kJitterMs (Jitter), never its two ends.
constexpr float kDefaultRgb[3] = {1.0f, 0.15f, 0.1f};
constexpr float kDefaultOpacityPct = 60.0f;
constexpr uint64_t kJitterMs = 45;
constexpr float kJitterM = 0.05f;         // the shake at the arc's middle, metres (none at the ends: sin of the way along)
constexpr float kBeamCoreM = 0.012f;      // the beam's white core, metres (its glow runs out several times that)
constexpr float kRingLineM = 0.03f;       // the ring's line, metres
constexpr float kRingRadiusM = 0.5f;      // the circle's radius on the ground, metres
constexpr float kMinLineRad = 0.0012f;    // a line never thinner than this (radians, from the eyes): a far circle is not lost under a pixel
// ---- a colour for each skill ([gameplay] point_arc_by_skill, point_arc_color_<id>) ------------------------------------------------------------
// By default each skill's beam and ring are the colour of its element, as the game's own Skills.txt (3.3) gives it in its EType column (Abyss's
// missile says magic too): fire orange, cold a deep icy blue, lightning light blue (the user: "lightning light blue, fire orange"), poison green, magic
// violet, stun (Mind Blast) lavender, none (Teleport, the curses, Bone Wall, the golems but the Fire Golem, Blade Sentinel, two sigils, Goatman, Defiler,
// Leap) a neutral white. The shipped d2r_vr.ini sets every listed skill's own colour (point_arc_color_<id>; the curses plum, the golems their own ...), so
// the elements are the fallback for a skill without one. A skill's own colour in the ini wins over its element's; with point_arc_by_skill off every
// skill is point_arc_color (empty: kDefaultRgb, the first build's red).
enum class Element : uint8_t { None, Fire, Cold, Lightning, Poison, Magic, Stun };
struct SkillElement { int id; Element e; };
inline constexpr SkillElement kElements[] = {
    {54, Element::None}, {51, Element::Fire}, {56, Element::Fire}, {59, Element::Cold}, {62, Element::Fire},
    {66, Element::None}, {71, Element::None}, {72, Element::None}, {76, Element::None}, {77, Element::None}, {82, Element::None}, {87, Element::None},
    {91, Element::None}, {78, Element::None}, {75, Element::None}, {85, Element::None}, {94, Element::Fire},
    {234, Element::Fire}, {244, Element::Fire},
    {251, Element::Fire}, {256, Element::Lightning}, {257, Element::None}, {261, Element::Lightning}, {262, Element::Fire}, {271, Element::Lightning},
    {272, Element::Fire}, {276, Element::Lightning}, {273, Element::Stun},
    {390, Element::Magic}, {393, Element::None}, {396, Element::None}, {400, Element::Fire}, {401, Element::Fire}, {402, Element::Magic},
    {373, Element::None}, {376, Element::Fire}, {377, Element::None},
    {132, Element::None}, {143, Element::None},
};
// A skill's element; a skill not in the table (one added to point_skills by hand) is None.
inline Element ElementOf(int skill) {
    for (const SkillElement& s : kElements)
        if (s.id == skill) return s.e;
    return Element::None;
}
inline void ElementRgb(Element e, float rgb[3]) {
    static constexpr float k[7][3] = {
        {0.95f, 0.95f, 1.00f},   // none: neutral white
        {1.00f, 0.48f, 0.10f},   // fire: orange
        {0.20f, 0.40f, 1.00f},   // cold: deep icy blue
        {0.45f, 0.75f, 1.00f},   // lightning: light blue
        {0.40f, 1.00f, 0.30f},   // poison: green
        {0.70f, 0.40f, 1.00f},   // magic: violet
        {0.85f, 0.70f, 1.00f},   // stun: lavender
    };
    const int i = static_cast<int>(e) < 7 ? static_cast<int>(e) : 0;
    for (int c = 0; c < 3; ++c) rgb[c] = k[i][c];
}
// The colour a skill's arc is drawn in: bySkill on - its own (hasOwn: the ini gave it one) or its element's; off - the one for all (hasAll: point_arc_color
// was given) or kDefaultRgb.
inline void SkillRgb(bool bySkill, int skill, bool hasOwn, const float own[3], bool hasAll, const float all[3], float out[3]) {
    if (bySkill && hasOwn && own) { for (int c = 0; c < 3; ++c) out[c] = own[c]; return; }
    if (bySkill) { ElementRgb(ElementOf(skill), out); return; }
    for (int c = 0; c < 3; ++c) out[c] = hasAll && all ? all[c] : kDefaultRgb[c];
}

inline float Opacity(float pct) {
    if (!std::isfinite(pct)) return kDefaultOpacityPct / 100.0f;
    return std::clamp(pct, 0.0f, 100.0f) / 100.0f;
}
inline uint32_t Hash(uint32_t x) {   // (lowbias32)
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
inline float Signed(uint32_t h) { return float(h & 0xFFFFFF) / float(0xFFFFFF) * 2.0f - 1.0f; }   // -1 .. 1
// The crackle: every inner point moved by up to ampUnits * sin(pi * its way along) on each axis, from `seed` (the same seed, the same shake).
inline void Jitter(Arc* a, uint32_t seed, float ampUnits) {
    if (!a || a->n < 3 || !(ampUnits > 0.0f) || !std::isfinite(ampUnits)) return;
    for (int i = 1; i + 1 < a->n; ++i) {
        const float k = ampUnits * std::sin(3.14159265f * float(i) / float(a->n - 1));
        const uint32_t h = Hash(seed * 0x9E3779B9U + uint32_t(i) * 0x85EBCA6BU);
        a->pts[i].x += k * Signed(Hash(h ^ 0x1u));
        a->pts[i].y += k * Signed(Hash(h ^ 0x2u));
        a->pts[i].z += k * Signed(Hash(h ^ 0x3u));
    }
}

// ---- [gameplay] leap_head_gain: the head's tilt that aims the leap's arc, multiplied ----------------------------------------------------------------
// The head's pitch (up or down from level) times the gain, its heading as it is: 2 throws as far with half the tilt (the user: "so I don't have to lift my
// head"). Held to kMaxThrowDeg up - a throw is farthest at 45 degrees, steeper comes back nearer, so a gained raise past it still means "farthest" - and to
// kMinThrowDeg down. gain 1 (or not finite, or not above 0): the direction as it is. A direction with no heading on the ground is returned as it is.
constexpr float kDefaultHeadGain = 1.0f, kMinHeadGain = 0.5f, kMaxHeadGain = 4.0f;
constexpr float kMaxThrowDeg = 45.0f, kMinThrowDeg = -85.0f;
inline float HeadGain(float iniGain) {
    if (!std::isfinite(iniGain)) return kDefaultHeadGain;
    return std::clamp(iniGain, kMinHeadGain, kMaxHeadGain);
}
inline V3 TiltGain(V3 dir, float gain) {
    const float dh = std::hypot(dir.x, dir.z), dl = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (!(dh > 1e-4f) || !std::isfinite(dl) || !(dl > 1e-6f) || !std::isfinite(gain) || !(gain > 0.0f) || gain == 1.0f) return dir;
    constexpr float kDeg = 3.14159265f / 180.0f;
    const float pitch = std::clamp(std::atan2(dir.y, dh) * gain, kMinThrowDeg * kDeg, kMaxThrowDeg * kDeg);
    const float c = std::cos(pitch);
    return {dir.x / dh * c, std::sin(pitch), dir.z / dh * c};
}

// The ring drawn round the circle's middle (cx, cy, cz): `n` chords of a circle of radius r on the ground (y = cy), as segments a[i] -> b[i]
// (xyz each). Returns how many were written (at most n, at most max).
inline int Ring(float cx, float cy, float cz, float r, int n, float (*a)[3], float (*b)[3], int max) {
    if (!a || !b || n < 3 || max <= 0 || !(r > 0.0f)) return 0;
    const int count = std::min(n, max);
    constexpr float kTwoPi = 6.28318531f;
    for (int i = 0; i < count; ++i) {
        const float t0 = kTwoPi * i / n, t1 = kTwoPi * (i + 1) / n;
        a[i][0] = cx + r * std::cos(t0); a[i][1] = cy; a[i][2] = cz + r * std::sin(t0);
        b[i][0] = cx + r * std::cos(t1); b[i][1] = cy; b[i][2] = cz + r * std::sin(t1);
    }
    return count;
}

}  // namespace pointcast
