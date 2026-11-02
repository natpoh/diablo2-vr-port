// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit 07016801d25e5b0b5fa718511c3aa90a99dff800 (in v0.3.31, on by default since f096e76), its steamvr/loot_aim.h,
// taken into D2R VR Authentic for [input] point_pickup: the choice of the pointed item (FloorDistance, Best, Select), the arm held out
// (Pointing), the pick-up reach (InReach) and the floor item (OnFloor) kept with their headset-tuned values; the namespace, Vec and Item's
// id changed; Answer added (Unleashed's rule inside its InteractTarget hook, written out pure). The pointed item's name look (RarityColor,
// NameLook, the rim: Ring / RimRgba / BoxRgba, Pulse), the label owners (LabelOwners) and the Show items hold's tail (OnlyPointedUntil)
// taken as they are, for vr/hud/labels.cpp. Unleashed's room walk and press log are not taken: the items come from our own scan of the
// client's units (vr/input/flat_click.cpp). The rest of D2R VR Authentic is MIT; this file stays MPL 2.0.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Loot pick: which dropped item Interact (A) takes. The game's own answer
// (InteractTarget 0x18D960) is the item nearest the hero, so with several on
// the floor A kept taking the closest one, never the one the player wanted.
// Here the player chooses by pointing: the item nearest where the aiming
// hand's ray meets the floor while that arm is held out. Values tuned in the
// headset (2026-10-18 to 10-29). World units: render = grid * 2 (+ the floor's shear).
namespace unleashed::lootaim {
constexpr int MaxItems = 60;                // the most dropped items one scan keeps
constexpr float CellUnits = 2.f;            // world units per grid cell (render = grid*2)
constexpr float PickRadiusCells = 1.5f;     // an item this far from the pointed floor spot can be picked
constexpr float KeepFactor = 1.3f;          // the current pick stays while inside the radius times this
constexpr float SwitchCells = .25f;         // another item takes over when nearer by this much (cells)
constexpr float ReachCells = 40.f;          // never an item farther than this from the hero
constexpr float DefaultPickupCells = 4.f;   // Unleashed's loot_pick_reach_cells: A picks up without walking (ours, [input] point_pickup_reach_cells, is 0: any distance)
// The hand points while held out from the head (horizontal reach, m); it lets
// go 6 cm nearer so the answer does not flicker.
constexpr float ArmOutM = .12f, ArmReleaseM = .06f;

struct Vec { float x = 0, y = 0, z = 0; };
struct Item { uint32_t id = 0; Vec at; float heroCells = 0; };   // id: the game's unit id; at: the item's floor point in the world
struct Ray { Vec origin, direction; bool valid = false; };
struct Choice { uint32_t id = 0; float distance = 0; };          // distance from the pointed floor spot (world units)

// How far the item lies on the floor from where the ray meets the floor at the
// item's height; a ray level or upward never meets it.
inline float FloorDistance(const Ray& ray, Vec at) {
    const Vec& d = ray.direction;
    if (!(d.y < -1e-4f) || !(ray.origin.y > at.y)) return 1e9f;
    const float t = (at.y - ray.origin.y) / d.y;
    return std::hypot(ray.origin.x + d.x * t - at.x, ray.origin.z + d.z * t - at.z);
}

// The item nearest the ray's floor point inside radius, keeping current while it
// is inside radius*KeepFactor and nothing beats it by margin, so two items side
// by side do not flicker. Radius and margin in world units.
inline Choice Best(const Item* items, int count, const Ray& ray, float radius, float margin, uint32_t current) {
    Choice best{}, kept{};
    if (!ray.valid || !items) return {};
    float bestValue = radius, keptValue = -1;
    for (int i = 0; i < count; ++i) {
        const Item& item = items[i];
        if (!item.id || !(item.heroCells <= ReachCells)) continue;
        const float a = FloorDistance(ray, item.at);
        if (item.id == current && a <= radius * KeepFactor) { keptValue = a; kept = {item.id, a}; }
        if (a < bestValue) { bestValue = a; best = {item.id, a}; }
    }
    if (keptValue >= 0 && (!best.id || best.distance > keptValue - margin)) return kept;
    return best;
}

// The item the pointing hand is on, or none (an invalid ray: the arm is not out).
inline Choice Select(const Item* items, int count, const Ray& hand, uint32_t current) {
    return Best(items, count, hand, PickRadiusCells * CellUnits, SwitchCells * CellUnits, current);
}

// Is the hand held out to point? reachM: its horizontal distance from the head;
// on is the previous answer.
inline bool Pointing(bool on, float reachM) {
    if (!std::isfinite(reachM)) return false;
    return reachM >= (on ? ArmOutM - ArmReleaseM : ArmOutM);
}

// Close enough for A to pick it up without walking? limit 0 = any distance.
inline bool InReach(float cells, float limit) { return limit <= 0 || (cells >= 0 && cells <= limit); }

// An item (unit type 4) on the ground (mode 3) or dropping (mode 5): picked up
// (stored, on the cursor, in the belt) it is no longer one.
inline bool OnFloor(uint32_t type, uint32_t mode) { return type == 4 && (mode == 3 || mode == 5); }

// What the game's Interact lookup is answered (Unleashed's InteractTarget hook): game, its own answer (gameIsItem: a dropped item);
// pick, the pointed item's live unit (0 none). Nothing pointed: the game's own. The pick within reach: it, whatever the game found.
// Past the reach: no item at all - neither a walk to the pointed one nor the game's nearest instead; a door, a waypoint or an NPC
// the game chose still works.
inline uintptr_t Answer(uintptr_t game, bool gameIsItem, uintptr_t pick, bool inReach) {
    if (!pick) return game;
    if (!inReach) return gameIsItem ? 0 : game;
    return pick;
}

// The item's name color, as the game draws it: white normal (gray when
// socketed or ethereal), blue magic, green set, yellow rare, gold unique,
// orange crafted/tempered. Item data (unit +0x10): quality +0x00 (1 low ..
// 8 crafted), flags +0x18 (0x800 socketed, 0x400000 ethereal).
inline void RarityColor(uint32_t quality, uint32_t flags, float rgb[3]) {
    float c[3] = {1, 1, 1};
    switch (quality) {
        case 4: c[0] = .41f; c[1] = .41f; c[2] = 1; break;         // magic
        case 5: c[0] = .1f; c[1] = 1; c[2] = .1f; break;           // set
        case 6: c[0] = 1; c[1] = 1; c[2] = .39f; break;            // rare
        case 7: c[0] = .9f; c[1] = .75f; c[2] = .45f; break;       // unique
        case 8: case 9: c[0] = 1; c[1] = .6f; c[2] = .1f; break;   // crafted, tempered
        default: if (flags & (0x800u | 0x400000u)) c[0] = c[1] = c[2] = .6f; break;   // socketed / ethereal
    }
    std::copy_n(c, 3, rgb);
}

// The pointed item's name, as it is drawn: in reach (A takes it from here) a rim
// in its rarity color, a dark shade of that color behind the name and the name
// at 180%; out of reach (A takes nothing) a red rim on the game's own box and the
// name at 130%. Each rim pulses. Values tuned in the headset (2026-10-29).
struct NameLook {
    bool rarityRim;        // the rim in the item's rarity color, else red
    float rimAlpha;        // the rim's opacity at the pulse's peak
    float pulseHz, pulseDepth;
    bool rarityBox;        // the box a dark shade of the rarity color, else the game's own fill
    float boxAlpha;
    float nameScale;       // the name's size, times the game's (capped at MaxNameScale)
};
inline constexpr NameLook InReachLook{true, 1.f, 1.f, .4f, true, 1.f, 1.8f};
inline constexpr NameLook OutOfReachLook{false, .9f, 1.5f, .5f, false, .9f, 1.3f};
constexpr float MaxNameScale = 2.f;
constexpr int RimPx = 1;
constexpr float RedRgb[3] = {1.f, .22f, .18f};
// A box in the name's own rarity color would hide the name (drawn in that color):
// this dark a shade of it keeps the hue and the contrast of the game's dark box.
constexpr float RarityShade = .3f;
inline const NameLook& LookFor(bool inReach) { return inReach ? InReachLook : OutOfReachLook; }

// The pulse as an alpha: steady (0 Hz) is full; else between 1-2*depth and 1.
inline float Pulse(uint64_t ms, float hz, float depth) {
    if (!(hz > 0)) return 1;
    return 1 - depth + depth * std::sin(float(ms % 100000) * .0062831853f * hz);
}
// The rim's color and alpha now.
inline void RimRgba(const NameLook& look, const float rarity[3], uint64_t ms, float out[4]) {
    for (int i = 0; i < 3; ++i) out[i] = std::clamp(look.rarityRim ? rarity[i] : RedRgb[i], 0.f, 1.f);
    out[3] = look.rimAlpha * Pulse(ms, look.pulseHz, look.pulseDepth);
}
// The box's fill: the game's (null when it passed none: black) or the rarity shade.
inline void BoxRgba(const NameLook& look, const float* game, const float rarity[3], float out[4]) {
    for (int i = 0; i < 3; ++i) out[i] = look.rarityBox ? std::clamp(rarity[i], 0.f, 1.f) * RarityShade : game ? game[i] : 0.f;
    out[3] = look.boxAlpha;
}
// The rim round box {x, y, w, h} (the game's label box), pad pixels thick: top,
// bottom (each across the full outer width), then left and right. Returns the
// strips written: 0 for no pad or an empty box. The strips never cover the box.
inline int Ring(const int32_t box[4], int32_t pad, int32_t out[4][4]) {
    if (!box || pad <= 0 || box[2] <= 0 || box[3] <= 0) return 0;
    const int32_t x = box[0], y = box[1], w = box[2], h = box[3];
    const int32_t strips[4][4] = {{x - pad, y - pad, w + 2 * pad, pad}, {x - pad, y + h, w + 2 * pad, pad}, {x - pad, y, pad, h}, {x + w, y, pad, h}};
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) out[i][j] = strips[i][j];
    return 4;
}

// Which item a label paint belongs to, from the layouts that showed it. The game
// lays every name out through one scratch box and paints from each label's own
// record (2026-10-29 log: 2707 of 2707 paints matched their layout by name, none
// by box), so a table keyed by box found only the last name laid out. A slot is
// the pair (box, name); a lookup takes the exact pair, else a name, else a box
// that every slot holding it agrees on; anything ambiguous is 0 (an ordinary
// name). The least recently written slot is reused.
template <int N> struct LabelOwners {
    static_assert(N > 0);
    struct Slot { const void* box = nullptr; const void* name = nullptr; uintptr_t unit = 0; uint64_t seq = 0; };
    Slot slot[N]{};
    uint64_t seq = 0;
    // The slot written; the caller may keep more (the name's text) at the same index.
    int Remember(const void* box, const void* name, uintptr_t unit) {
        int at = -1;
        for (int i = 0; i < N && at < 0; ++i) if (slot[i].seq && slot[i].box == box && slot[i].name == name) at = i;
        if (at < 0) { at = 0; for (int i = 1; i < N; ++i) if (slot[i].seq < slot[at].seq) at = i; }   // an empty slot (seq 0) first, else the oldest
        slot[at] = {box, name, unit, ++seq};
        return at;
    }
    uintptr_t Unit(const void* box, const void* name) const {
        if (box && name) for (const auto& s : slot) if (s.seq && s.box == box && s.name == name) return s.unit;
        if (const uintptr_t u = Agreed(&Slot::name, name)) return u;
        return Agreed(&Slot::box, box);
    }
    // The newest slot holding unit, or -1.
    int Find(uintptr_t unit) const {
        int at = -1;
        for (int i = 0; i < N; ++i) if (unit && slot[i].seq && slot[i].unit == unit && (at < 0 || slot[i].seq > slot[at].seq)) at = i;
        return at;
    }
private:
    uintptr_t Agreed(const void* Slot::*key, const void* v) const {
        if (!v) return 0;
        uintptr_t u = 0;
        for (const auto& s : slot) {
            if (!s.seq || !s.unit || s.*key != v) continue;
            if (u && s.unit != u) return 0;   // two items on this address: not knowable from it
            u = s.unit;
        }
        return u;
    }
};

// The pointed item's name when the game lays out none (2026-10-29 headset: an
// item a few cells off showed its name only while Show items was held). While an
// item is pointed at, Show items is held for it, so the game lays its name out,
// and every other name is cut in the label layout: the pointed one shows by itself.
// Until when the other names are cut: while held, the hold's lease ahead; once it
// ends, a short tail while the game lets Show items go, so the other names do not
// flash up; the player's own Show items: no cut at all.
constexpr uint64_t PointedTailMs = 150;
inline uint64_t OnlyPointedUntil(bool holding, bool ownHolds, uint64_t was, uint64_t now, uint64_t leaseMs) {
    if (ownHolds) return 0;
    if (holding) return now + leaseMs;
    return was ? std::min(was, now + PointedTailMs) : 0;
}
inline bool OnlyPointed(uint64_t until, uint64_t now) { return until && now < until; }
}  // namespace unleashed::lootaim
