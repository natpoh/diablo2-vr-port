// flat_body.h - [input] flat_body_pick: in flat F2 / F3 a monster, an NPC, a door, a shrine, a chest, a portal can be clicked by its body,
// not only by its feet. Pure; the test is cleanroom/tests/flat_body_test.cpp, the wiring vr/vrcam.cpp (HookMouseGround, FlatBodyScan).
//
// From sajmonsoft's flat fork (github.com/sotodeshi/d2r3dflat, CHANGES.md sections 1 and 9, 2026-10): the game finds what is under the
// pointer by the GROUND point its mouse ray stands for (its hover tests each unit's box around that point, built for its own view from
// above), so from our camera only the feet were a target. Short invisible upright capsules round the units near the hero; when the
// crosshair's ray meets one before the ground, the game's mouse point is moved to the cell that unit's body stands for in the game's own
// view - the game's hover, click and skills do the rest, as for a click at the feet.
//   - a body (monster, NPC): the point of its axis at the height the ray meets it, kept within 25 - 60 % of its height, with its height's
//     push taken off (cell = place - up * k on both axes). His measure (Tyrael, who floats): the game's hover of a body lies along that
//     diagonal behind its place, the feet are outside it for a floating one; standing ones are hovered there too.
//   - an object: its feet, its own place.
// Adapted: the fork bent the camera's ray into the game's old isometric line; here the cell goes into the game's mouse point directly
// (the [input] flat_aim_ground hook, vr/flat_ground.h), and a capsule counts only before the ground the ray meets as drawn.
// Sizes as his: a body's height from its MonStats row (pixel height * 0.1 when 20 - 200, else 7), its radius from the footprint
// (* 0.7, 1.5 - 3.5); an object's by its kind (vr/flat_pick_obj.inc). All in game world units; every unit stands on the hero's floor.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "flat_ground.h"

namespace flatbody {

constexpr float kReach = 55.0f;          // world units from the hero's place
constexpr int kBodyCap = 48, kObjCap = 32;

struct Cap {
    float x = 0.0f, z = 0.0f;   // its place on the grid, world units (unit_scan.h Place: twice the cell)
    float floorY = 0.0f;        // the ground it stands on (the hero's)
    float height = 0.0f, radius = 0.0f;
    bool body = true;           // a monster or NPC; false: an object
    uint32_t cls = 0;
};

// A body's size from its MonStats row (footprint bytes 8 / 9, pixel height byte 13); known false: no row.
inline void BodySize(uint8_t fa, uint8_t fb, uint8_t h, bool known, float* height, float* radius) {
    *height = (known && h >= 20 && h <= 200) ? h * 0.10f : 7.0f;
    const int fp = known ? std::max(fa, fb) : 0;
    *radius = std::clamp((fp >= 1 && fp <= 8) ? fp * 0.7f : 1.8f, 1.5f, 3.5f);
}

// An object's kind (0 none) and size from its packed byte (flat_pick_obj.inc).
inline int ObjKind(uint8_t packed) { return packed & 7; }
inline float ObjRadius(uint8_t packed) {
    const int size = (packed >> 3) < 1 ? 2 : (packed >> 3);
    return std::clamp(size * 0.72f, 1.6f, 4.8f);
}
inline float ObjHeight(int kind) {
    switch (kind) {
        case 1: return 11.0f;   // a door, by its frame
        case 2: return 8.0f;    // a shrine
        case 3: return 5.5f;    // a chest, urn, rack: low, so a body behind it still wins
        case 4: return 12.0f;   // a portal
        case 5: return 9.0f;    // a waypoint
        case 6: return 8.5f;    // stairs, a trap door, a level entrance
        default: return 7.0f;   // a well, the stash, a quest object
    }
}
// A door, shrine, chest or fixture already used (mode above 1) is no target (an open door would block its doorway); a portal, a
// waypoint, stairs stay one in every mode.
inline bool ObjTarget(int kind, uint32_t mode) {
    if (!kind) return false;
    const bool closedOnly = kind == 1 || kind == 2 || kind == 3 || kind == 7;
    return !closedOnly || mode <= 1;
}

// Where the capsule stands as drawn: its place pushed by its floor's height (flat_ground.h), upright from there.
inline void Base(const Cap& c, float out[3]) {
    out[0] = c.x + c.floorY * flatground::kShear;
    out[1] = c.floorY;
    out[2] = c.z + c.floorY * flatground::kShear;
}

// The ray o + d*t (d a unit vector) against the drawn capsule: the first t > 0 it enters (a small t when o is inside).
inline bool RayCapsule(const float o[3], const float d[3], const Cap& c, float* tHit) {
    float b[3];
    Base(c, b);
    const float bot = b[1], top = b[1] + c.height, r = c.radius;
    const float ax = o[0] - b[0], az = o[2] - b[2];
    if (ax * ax + az * az <= r * r && o[1] >= bot && o[1] <= top) { *tHit = 0.02f; return true; }
    float best = 1.0e30f;
    bool hit = false;
    auto take = [&](float t) { if (t > 1.0e-3f && t < best) { best = t; hit = true; } };
    const float A = d[0] * d[0] + d[2] * d[2], B = 2.0f * (ax * d[0] + az * d[2]), C = ax * ax + az * az - r * r;
    if (A > 1.0e-8f) {
        const float disc = B * B - 4.0f * A * C;
        if (disc >= 0.0f) {
            const float s = std::sqrt(disc);
            for (const float t : {(-B - s) / (2.0f * A), (-B + s) / (2.0f * A)}) {
                const float y = o[1] + t * d[1];
                if (y >= bot && y <= top) take(t);
            }
        }
    }
    for (const float sy : {bot, top}) {   // the round ends
        const float px = o[0] - b[0], py = o[1] - sy, pz = o[2] - b[2];
        const float bb = px * d[0] + py * d[1] + pz * d[2], cc = px * px + py * py + pz * pz - r * r, disc = bb * bb - cc;
        if (disc < 0.0f) continue;
        const float s = std::sqrt(disc);
        take(-bb - s);
        take(-bb + s);
    }
    if (hit) *tHit = best;
    return hit;
}

// The first capsule the ray meets before tFloor (the ground it meets as drawn; INFINITY for none), and the cell for the game's mouse
// point (grid cells, the game's x / y: world units / 2).
struct Hit { int index = -1; float t = 0.0f, cellX = 0.0f, cellZ = 0.0f; };
inline bool Pick(const Cap* caps, int n, const float o[3], const float d[3], float tFloor, Hit* out) {
    Hit h;
    float bestT = tFloor;
    for (int i = 0; i < n; ++i) {
        float t;
        if (RayCapsule(o, d, caps[i], &t) && t < bestT) { bestT = t; h.index = i; h.t = t; }
    }
    if (h.index < 0) return false;
    const Cap& c = caps[h.index];
    const float up = c.body ? std::clamp(o[1] + h.t * d[1] - c.floorY, 0.25f * c.height, 0.6f * c.height) : 0.0f;
    h.cellX = (c.x - up * flatground::kShear) / flatground::kRenderPerCell;
    h.cellZ = (c.z - up * flatground::kShear) / flatground::kRenderPerCell;
    *out = h;
    return std::isfinite(h.cellX) && std::isfinite(h.cellZ);
}

}  // namespace flatbody
