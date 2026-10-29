#pragma once
// barrel_touch.h - [gameplay] melee_touch_barrels: a swing of the held weapon through a barrel, a pot, an urn or a basket breaks it the
// game's own way - the press of the game's interact (pad A, as the A key does now) with the controller's interact target answered with
// that object, so the hero kicks / breaks it with his own animation, its loot, traps and explosions the game's. Pure: no game memory, no
// Windows (cleanroom/tests/barrel_touch_test.cpp); the scan and the contact are melee_touch.cpp's, the press vrcam.cpp's (ApplyActions,
// HookInteractTarget), as [weight] grab_items' pick-up.
//
// After bearstonem/d2r-pcvr 9b75aa2 "Break pots and barrels with physical weapon contacts": which objects (the Objects row's
// Selectable0, IsAttackable0 and OperateFn 3 urn / basket, 5 barrel, 7 exploding barrel; the footprint SizeX / SizeY) and the volume
// (the footprint across, Bear's heights). His version calls the game's server object operation (0x592C40) and skips the hero's kick;
// ours never does (never break the game's mechanics, the author 2026-10-29: "путём игры") - only the target of the game's own
// interact is steered, for the press our touch makes.

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cmath>

#include "stroke_contact.h"

namespace barreltouch {

// -- which objects: the Objects table's compiled row (D2R 3.3.93787, Bear's offsets) --------------------------------------
constexpr size_t kRowSelectable0 = 0x85, kRowSizeX = 0x90, kRowSizeY = 0x94, kRowAttackable0 = 0xE8, kRowOperateFn = 0x15E;
constexpr size_t kRowBytes = 0x168;
constexpr uint8_t kOpUrn = 3, kOpBarrel = 5, kOpExploding = 7;   // OperateFn: an urn / pot / basket, a barrel, an exploding barrel

struct Row { uint8_t selectable0 = 0, attackable0 = 0, operateFn = 0; int32_t sizeX = 0, sizeY = 0; };

inline Row RowOf(const uint8_t* bytes) {
    Row r;
    r.selectable0 = bytes[kRowSelectable0];
    r.attackable0 = bytes[kRowAttackable0];
    r.operateFn = bytes[kRowOperateFn];
    r.sizeX = (int32_t)((uint32_t)bytes[kRowSizeX] | (uint32_t)bytes[kRowSizeX + 1] << 8 | (uint32_t)bytes[kRowSizeX + 2] << 16 | (uint32_t)bytes[kRowSizeX + 3] << 24);
    r.sizeY = (int32_t)((uint32_t)bytes[kRowSizeY] | (uint32_t)bytes[kRowSizeY + 1] << 8 | (uint32_t)bytes[kRowSizeY + 2] << 16 | (uint32_t)bytes[kRowSizeY + 3] << 24);
    return r;
}

// A whole one (mode 0: not broken, not opening) the hero may attack and select, of the three kinds - no chest, door, shrine, rack,
// portal, corpse or quest object (their own OperateFn).
inline bool Breakable(const Row& r, uint32_t mode) {
    if (mode != 0 || r.selectable0 != 1 || r.attackable0 != 1) return false;
    if (r.operateFn != kOpUrn && r.operateFn != kOpBarrel && r.operateFn != kOpExploding) return false;
    return r.sizeX >= 1 && r.sizeY >= 1 && r.sizeX <= 8 && r.sizeY <= 8;
}

constexpr int kMax = 32;   // objects kept from a scan
constexpr float kScanM = 4.0f;   // ... within this of the hero (a swing reaches 2.5 m at most)

struct Barrel {
    uint32_t id = 0, cls = 0;
    uintptr_t unit = 0;   // the client's unit (the interact target answered with it)
    uint8_t op = 0;
    int32_t sizeX = 0, sizeY = 0;
    float gridX = 0.0f, gridZ = 0.0f;   // its cell on the grid, game world units
};

// Its volume in render metres, standing on the hero's floor (floorM), as melee_bodies.h's BodyFor puts a monster: across the game's
// footprint, Bear's heights (the big urns 4.8, a barrel 4.0, a pot / basket 3.2 game units). An approximation, not the model's mesh.
inline bool Volume(const Barrel& b, float floorM, float upm, stroke::Body& out, float slackM = 0.05f) {
    if (!std::isfinite(b.gridX + b.gridZ + floorM) || !std::isfinite(upm) || upm < 1.0f || upm > 12.0f) return false;
    const bool large = b.cls == 4 || b.cls == 94 || b.cls == 95;
    const float radius = std::clamp((float)std::max(b.sizeX, b.sizeY) / upm, 0.16f, 0.6f);
    const float height = std::max(2.0f * radius, (large ? 4.8f : b.op == kOpUrn ? 3.2f : 4.0f) / upm);
    constexpr float kShear = 1.22474487139f;   // melee_bodies.h kGroundShear: the renderer's shear of raised ground
    const float gx = b.gridX / upm + floorM * kShear, gz = b.gridZ / upm + floorM * kShear;
    out.radius = radius + slackM;
    out.bottom = {gx, floorM + out.radius, gz};
    out.top = {gx, std::max(floorM + out.radius, floorM + height - out.radius), gz};
    return true;
}

// -- the press ------------------------------------------------------------------------------------------------------------
// One object at a time: asked by a counting stroke's touch, held (the game's interact pressed on it) until the object is no longer whole
// (broken: its mode left 0, or gone) or kHoldS passes; then let go. An object is asked once per hand and stroke.
constexpr double kHoldS = 1.2;     // the hero walks the last step and kicks within this; longer is a press the game did not take
constexpr int kAskedKept = 8;

enum Event : unsigned { kAsked = 1, kBroken = 2, kNotBroken = 4 };

class Presser {
public:
    // A counting stroke of hand `side` touched `b`: the press starts, unless one is in flight or this object was asked in this stroke already.
    bool Ask(int side, uint64_t stroke, const Barrel& b, double now) {
        if (pressing_ || !b.unit || !b.id || !stroke) return false;
        for (const Asked& a : asked_) if (a.side == side && a.stroke == stroke && a.id == b.id) return false;
        asked_[next_] = {side, stroke, b.id};
        next_ = (next_ + 1) % kAskedKept;
        pressing_ = true;
        side_ = side;
        target_ = b;
        since_ = now;
        fresh_ = true;
        return true;
    }
    // Each step while a press is in flight: `mode` the object's mode now (-1: the object is gone). Events of this step.
    unsigned Step(double now, int mode) {
        unsigned ev = 0;
        if (fresh_) { ev |= kAsked; fresh_ = false; }
        if (!pressing_) return ev;
        if (mode != 0) { pressing_ = false; ev |= kBroken; }
        else if (now - since_ > kHoldS) { pressing_ = false; ev |= kNotBroken; }
        return ev;
    }
    void Reset() { pressing_ = false; fresh_ = false; }
    bool Pressing() const { return pressing_; }
    uintptr_t Target() const { return pressing_ ? target_.unit : 0; }
    const Barrel& Object() const { return target_; }
    int Side() const { return side_; }
    double Since() const { return since_; }

private:
    struct Asked { int side = -1; uint64_t stroke = 0; uint32_t id = 0; };
    Asked asked_[kAskedKept];
    int next_ = 0;
    bool pressing_ = false, fresh_ = false;
    int side_ = 0;
    Barrel target_;
    double since_ = 0.0;
};

inline const char* KindName(uint8_t op) { return op == kOpBarrel ? "barrel" : op == kOpExploding ? "exploding barrel" : op == kOpUrn ? "pot / urn / basket" : "object"; }

}  // namespace barreltouch
