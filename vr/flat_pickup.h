// flat_pickup.h - two flat first-person helpers for items on the ground (a player's ask, 2026-11-09: "a key to pick up items, preferably E" and
// "select an item by its hitbox, not just its name, when Alt is held"). Pure; the test is cleanroom/tests/flat_pickup_test.cpp, the wiring
// vr/input/flat_click.cpp (HookMouseGround, FlatItemClickAnswer, FlatPickupFrame) and vr/hud/window.cpp (the key).
//
// [input] flat_item_hitbox (flat F2 / F3, mouse look): with Alt held (the game's item names shown), the crosshair's ray against a low upright
// capsule at every item on the floor round the hero (its static path cell's middle, the hero's floor as drawn). The first one met, before any
// body or object the ray meets (flat_body_pick's capsules), answers the game's own click lookup - unless the game's own hover already is an item
// (its name under the crosshair): that one stays. The click is then the game's own click on an item (walk up, pick up).
//
// [input] flat_pickup_key_on / flat_pickup_key (flat F2 / F3, mouse look): one key (E by default) picks up an item. Which one: the game's own hover
// when it is an item (a name under the crosshair), else the item the crosshair's ray meets (the hitbox above, Alt or not), else the nearest one
// within flat_pickup_reach_cells in a cone ahead of the look. Never a command of ours: as portal_auto (vr/portal_auto.h) the game's mouse point is
// put on that item (Aim), its click lookup answered with it, and the left button goes down through the window and 60 ms later up - the game's own
// click on an item, with its walk, its checks and its pick-up command. A button that went down always comes up.
#pragma once

#include <cmath>
#include <cstdint>

#include "flat_body.h"
#include "key_capture.h"

namespace flatpickup {

constexpr float kItemRadius = 1.2f;          // world units (a cell is 2): the item's hitbox round its cell's middle
constexpr float kItemHeight = 1.0f;          // world units above its floor: items lie on it
constexpr float kConeDeg = 50.0f;            // the key's "ahead": this far either side of the look's heading
constexpr float kDefaultReachCells = 5.0f;   // [input] flat_pickup_reach_cells: the key's nearest-ahead item at most this far (cells)
constexpr float kMaxReachCells = 20.0f;

// [input] flat_pickup_key: the key's virtual-key code (69 = E; D2R VR Settings offers ten letters, the in-game panel takes any key pressed:
// vr/key_capture.h) - one the pick-up key cannot be (W A S D, Alt, F1 - F4 ...) is E
constexpr unsigned kDefaultKey = 'E';
inline unsigned KeyOf(int iniValue) {
    return iniValue > 0 && keycap::Bindable((unsigned)iniValue, keycap::Use::PickupKey) ? (unsigned)iniValue : kDefaultKey;
}

struct Item {
    uint32_t id = 0;
    float x = 0.0f, z = 0.0f;   // its place on the grid, world units (twice the cell, + 1: the middle)
    float floorY = 0.0f;        // the hero's floor as drawn
    float heroCells = 0.0f;     // from the hero, cells
};

// The item's hitbox as drawn (flat_body.h's capsule: pushed by its floor's height).
inline flatbody::Cap Box(const Item& it) {
    flatbody::Cap c;
    c.x = it.x;
    c.z = it.z;
    c.floorY = it.floorY;
    c.height = kItemHeight;
    c.radius = kItemRadius;
    c.body = false;
    c.id = it.id;
    return c;
}

// The item whose hitbox the ray o + d*t (d a unit vector) meets first before tLimit (a body met first: its t; INFINITY none): its index, -1 none.
inline int RayItem(const Item* items, int n, const float o[3], const float d[3], float tLimit, float* tOut = nullptr) {
    int met = -1;
    float best = tLimit;
    for (int i = 0; i < n; ++i) {
        float t;
        if (!items[i].id || !flatbody::RayCapsule(o, d, Box(items[i]), &t) || !(t < best)) continue;
        best = t;
        met = i;
    }
    if (met >= 0 && tOut) *tOut = best;
    return met;
}

// The nearest item within reachCells in a cone of kConeDeg either side of the heading (dx, dz on the grid, any length) from the hero's place
// (heroX, heroZ, world units on the grid): its index, -1 none. An item under the hero's feet (under half a cell) counts whatever its bearing.
inline int AheadItem(const Item* items, int n, float heroX, float heroZ, float dx, float dz, float reachCells) {
    const float hl = std::sqrt(dx * dx + dz * dz);
    if (!(hl > 1e-6f) || !(reachCells > 0.0f)) return -1;
    const float hx = dx / hl, hz = dz / hl, cosCone = std::cos(kConeDeg * 3.14159265f / 180.0f);
    int best = -1;
    float bestCells = reachCells;
    for (int i = 0; i < n; ++i) {
        if (!items[i].id) continue;
        const float ix = items[i].x - heroX, iz = items[i].z - heroZ, l = std::sqrt(ix * ix + iz * iz), cells = l / 2.0f;
        if (!(cells <= bestCells)) continue;
        if (cells >= 0.5f && (ix * hx + iz * hz) / l < cosCone) continue;
        bestCells = cells;
        best = i;
    }
    return best;
}

// The key's item: the game's hover (an item), the crosshair's, the nearest ahead - the first there is.
enum class From { None, Hover, Crosshair, Ahead };
inline From ChooseKey(bool hoverIsItem, bool crossItem, bool aheadItem) {
    return hoverIsItem ? From::Hover : crossItem ? From::Crosshair : aheadItem ? From::Ahead : From::None;
}
inline const char* FromText(From f) {
    return f == From::Hover ? "its name under the crosshair" : f == From::Crosshair ? "the crosshair on it" : f == From::Ahead ? "the nearest ahead" : "none";
}

// [input] flat_item_hitbox: the crosshair's item answers the click lookup - the box on, Alt held, an item met before any body, the game's own
// hover not an item already (its name under the crosshair stays the game's).
inline bool HitboxAnswers(bool wanted, bool altHeld, bool itemMet, bool gameIsItem) { return wanted && altHeld && itemMet && !gameIsItem; }

// The key's press (the frame's thread, once a frame): Aim puts the game's mouse point on the item and waits at least kAimMinMs (the point is asked
// again before the button) and until the click lookup can be answered (hooked) or the game's own hover is the item; then the left button down,
// up kDownMs later; After keeps the lookup answered a moment for the click's own late asks.
constexpr uint64_t kAimMinMs = 50, kHoverWaitMs = 700, kDownMs = 60, kAfterMs = 120;
struct In {
    uint64_t now = 0;
    bool wanted = false;        // flat, F2 / F3 with mouse look, in the world, no menu, the box on
    bool focused = false;       // the game's window in front (a press goes down only then)
    bool pressed = false;       // the key went down since the last step
    uint32_t target = 0;        // the item the key would take now (0 none) - ChooseKey's
    bool lookupHooked = false;  // the map click's own lookup can be answered (spell_aim's hooks in)
    bool hoverIsTarget = false; // the game's own hover is the aimed item (the forced point's)
    bool gone = false;          // the aimed item is no longer on the floor
};
struct Out {
    bool down = false, up = false;   // the left button through the window
    bool aim = false;                // the game's mouse point on the item, its click lookup answered with it
    uint32_t id = 0;                 // the item aimed at (aim)
    const char* said = nullptr;      // what happened, for the log (nullptr: nothing new)
};
class Machine {
public:
    enum class Stage { Idle, Aim, Down, After };
    Out Step(const In& in) {
        Out o;
        switch (stage_) {
            case Stage::Idle:
                if (!in.pressed || !in.wanted) break;
                if (!in.focused) { o.said = "the key pressed with the game's window not in front - nothing"; break; }
                if (!in.target) { o.said = "the key pressed with no item under the crosshair, none ahead within reach - nothing"; break; }
                id_ = in.target;
                at_ = in.now;
                stage_ = Stage::Aim;
                o.said = "aimed";
                break;
            case Stage::Aim:
                if (!in.wanted || in.gone) { o.said = in.gone ? "the item left the floor before the click" : "no longer wanted before the click"; Reset(); break; }
                if (in.now - at_ >= kAimMinMs && (in.lookupHooked || in.hoverIsTarget)) {
                    if (!in.focused) { o.said = "the game's window left the front before the click"; Reset(); break; }
                    o.down = true;
                    stage_ = Stage::Down;
                    at_ = in.now;
                    o.said = in.lookupHooked ? "clicked (the click's lookup answered with the item)" : "clicked (the game's own hover on the item)";
                } else if (in.now - at_ >= kHoverWaitMs) {
                    o.said = "the game's hover never came to the item (the click lookup not hooked) - no click";
                    Reset();
                }
                break;
            case Stage::Down:
                if (!in.wanted || in.now - at_ >= kDownMs) {
                    o.up = true;
                    stage_ = Stage::After;
                    at_ = in.now;
                }
                break;
            case Stage::After:
                if (!in.wanted || in.now - at_ >= kAfterMs) Reset();
                break;
        }
        o.aim = stage_ != Stage::Idle;
        o.id = o.aim ? id_ : 0;
        return o;
    }
    Stage stage() const { return stage_; }
    // the button is down (for a forced release: it must always come up)
    bool ButtonDown() const { return stage_ == Stage::Down; }

private:
    void Reset() { stage_ = Stage::Idle; id_ = 0; at_ = 0; }
    Stage stage_ = Stage::Idle;
    uint32_t id_ = 0;
    uint64_t at_ = 0;
};

}  // namespace flatpickup
