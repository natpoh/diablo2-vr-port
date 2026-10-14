// stray_walk.h - the hero must never run off on a click or a command nobody meant, in VR first person (F4). Pure: no game, no
// Windows, no clock; the unit test is cleanroom/tests/stray_walk_test.cpp, the wiring vr/stray_walk.cpp.
//
// Two guards, two keys ([gameplay], default 1, F4 only, live):
//
// block_stray_clicks - the game's window gets mouse buttons from outside the plugin (a log of 2026-10-14 16:26 has four left clicks
// "from a device" in 1.7 s, the rhythm of a bow's draw and release, with no panel open: Windows says they are hardware, ours are all
// SendInput). In the F4 world with no panel or menu open a mouse button is dropped before the game sees it; panels, menus, the title
// and character screens (where the laser clicks), mouse look and the plugin's own [bow] mode 2 clicks go through.
//
// block_stray_walk - the game's client has ONE funnel for a player's commands: 0xFABE0(cmd, unit, a1, a2). cmd is the D2 packet
// id: 1 walk to a point, 2 walk to a unit, 3 run to a point, 4 run to a unit, 5.. skills on a point or a unit, 0x42 the direction of
// the stick (new in D2R, the stick's walk). A walk or run to a POINT (1, 3) that comes out of a click on bare ground - the game's map
// click 0xFE3B0 of type 0 / 1 / 3 / 4 with nothing under the pointer and no Stand Still - is the stray walk: dropped there, before
// the client predicts it and before the packet goes. Everything else is sanctioned: the stick (0x42), walks to a unit (pick-up, NPC,
// door, waypoint, stairs), every skill (Teleport, Leap, Charge are skills), the stick's stop (click type 6), Force Move (11 / 12 /
// 13). A walk to a point from anywhere else, or from the pad's casts (7 .. 10), is only noted in the log until a log says what it is.
// The server-side walk of a melee attack towards its target is no command: it is what Stand Still ([gameplay] attack_in_place) stops.
#pragma once

#include <cstdint>

namespace straywalk {

// -- the window's mouse messages ----------------------------------------------------------------------------------------------
constexpr unsigned kWmLButtonDown = 0x201, kWmLButtonUp = 0x202, kWmLButtonDblClk = 0x203, kWmRButtonDown = 0x204, kWmRButtonUp = 0x205,
                   kWmRButtonDblClk = 0x206, kWmMButtonDown = 0x207, kWmMButtonUp = 0x208, kWmMButtonDblClk = 0x209,
                   kWmXButtonDown = 0x20B, kWmXButtonUp = 0x20C, kWmXButtonDblClk = 0x20D;

// 0 left, 1 right, 2 middle, 3 an X button; -1 not a button message
inline int ButtonOf(unsigned msg) {
    switch (msg) {
    case kWmLButtonDown: case kWmLButtonUp: case kWmLButtonDblClk: return 0;
    case kWmRButtonDown: case kWmRButtonUp: case kWmRButtonDblClk: return 1;
    case kWmMButtonDown: case kWmMButtonUp: case kWmMButtonDblClk: return 2;
    case kWmXButtonDown: case kWmXButtonUp: case kWmXButtonDblClk: return 3;
    default: return -1;
    }
}
inline bool IsUp(unsigned msg) { return msg == kWmLButtonUp || msg == kWmRButtonUp || msg == kWmMButtonUp || msg == kWmXButtonUp; }

// Where a click may not reach the game.
struct World {
    bool on = false;          // [gameplay] block_stray_clicks
    bool f4 = false;          // VR first person
    bool inWorld = false;     // a game is running (not the title or character screens)
    bool menuOpen = false;    // a panel or menu is open
    bool mouseLook = false;   // [input] mouse look: the mouse is the game's pointer by design
    bool ownClicks = false;   // [bow] mode 2: the plugin sends Shift + click itself (injected)
};
inline bool Forbidden(const World& w) { return w.on && w.f4 && w.inWorld && !w.menuOpen && !w.mouseLook; }

// Drops a button's press and, with it, its release (a release alone would be a click the game never saw begin).
class ClickFilter {
public:
    // True: the game does not get this message. `injected`: a program sent it (SendInput), not a device.
    bool Drop(unsigned msg, const World& w, bool injected) {
        const int b = ButtonOf(msg);
        if (b < 0) return false;
        if (IsUp(msg)) {
            const bool was = dropped_[b];
            dropped_[b] = false;
            return was;
        }
        const bool drop = Forbidden(w) && !(injected && w.ownClicks);
        dropped_[b] = drop;
        return drop;
    }
    void Reset() { for (bool& d : dropped_) d = false; }

private:
    bool dropped_[4] = {};
};

// -- the player's commands --------------------------------------------------------------------------------------------------
// The click the command comes out of: what the plugin's hook of the map click 0xFE3B0 says while it runs.
enum class Ctx : uint8_t {
    None,           // no click on the stack: the command comes from elsewhere
    GroundClick,    // type 0 / 1 (left) or 3 / 4 (right) with nothing under the pointer: a click on bare ground
    UnitClick,      // the same with a unit under the pointer (a monster, an item, a door, an NPC)
    Stop,           // type 6: the stick let go, "go to your own point"
    ForceMove,      // type 11 / 12 / 13: the game's Force Move key
    Pad,            // type 7 .. 10: the pad's casts
    Other,          // any other type
};
inline Ctx ContextOf(int type, bool bare) {
    switch (type) {
    case 0: case 1: case 3: case 4: return bare ? Ctx::GroundClick : Ctx::UnitClick;
    case 6: return Ctx::Stop;
    case 7: case 8: case 9: case 10: return Ctx::Pad;
    case 11: case 12: case 13: return Ctx::ForceMove;
    default: return Ctx::Other;
    }
}
inline const char* CtxName(Ctx c) {
    switch (c) {
    case Ctx::None: return "no click (another caller)";
    case Ctx::GroundClick: return "a click on bare ground";
    case Ctx::UnitClick: return "a click on a unit";
    case Ctx::Stop: return "the stick's stop";
    case Ctx::ForceMove: return "Force Move";
    case Ctx::Pad: return "a pad cast";
    case Ctx::Other: return "another click type";
    }
    return "?";
}

constexpr int kWalkToPoint = 1, kRunToPoint = 3, kWalkToUnit = 2, kRunToUnit = 4, kStickDirection = 0x42;
inline bool IsWalkToPoint(int cmd) { return cmd == kWalkToPoint || cmd == kRunToPoint; }

enum class Verdict : uint8_t {
    Pass,   // sanctioned (or not ours to judge)
    Drop,   // the stray walk: the command is not made
    Note,   // goes through, but a walk to a point from a place not known to be sanctioned: said in the log
};
// ourAttack ([gameplay] block_attack_run): an attack of the plugin's (a bow shot, a hit by touch, a throw) is pressed now with the game's Stand Still, so the
// game's own run to a point out of the pad's cast of it (the click types 7 .. 10 and the ones that follow its release) is not meant: a log of 2026-10-14 18:05
// has the bow's shot aimed at a point far out along the hand and the game running the hero there (command 3, caller 0x101F78, 3 ms after the pad's A went
// down, with Stand Still held) in six shots of twenty. Dropped then; every other source of a walk to a point stays only noted.
inline Verdict WalkVerdict(bool on, bool f4, int cmd, Ctx c, bool ourAttack = false) {
    if (!on || !f4 || !IsWalkToPoint(cmd)) return Verdict::Pass;
    switch (c) {
    case Ctx::GroundClick: return Verdict::Drop;
    case Ctx::Stop: case Ctx::ForceMove: case Ctx::UnitClick: return Verdict::Pass;
    case Ctx::Pad: case Ctx::Other: return ourAttack ? Verdict::Drop : Verdict::Note;
    case Ctx::None: return Verdict::Note;
    }
    return Verdict::Pass;
}

// A log that may flap: the first `kHead` lines, then at most one in `kEveryMs`, said with how many it stands for.
class Limiter {
public:
    static constexpr uint32_t kHead = 30;
    static constexpr uint64_t kEveryMs = 2000;
    // True when a line is to be said now; *count = how many events it stands for (1 for the first lines).
    bool Say(uint64_t nowMs, uint32_t* count) {
        ++pending_;
        ++total_;
        if (total_ <= kHead || nowMs - last_ >= kEveryMs) {
            last_ = nowMs;
            *count = pending_;
            pending_ = 0;
            return true;
        }
        return false;
    }
    uint32_t Total() const { return total_; }

private:
    uint32_t pending_ = 0, total_ = 0;
    uint64_t last_ = 0;
};

}  // namespace straywalk
