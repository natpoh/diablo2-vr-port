// walk_flat.h - walking with the controller's stick in VR F1, F2, F3 without BodyWalk ([direct] walk_flat_views). Pure: no game, no
// XInput, no clock of its own; the unit test is cleanroom/tests/walk_flat_test.cpp, the wiring is vr/direct_input.cpp (the stick, per
// headset frame) and vr/vrcam.cpp (PadMirrorRead: the pad the game's walking code is given).
//
// The rule of the plugin is "everything new acts in F4 only" (vr/view_gate.h). This is the one deliberate exception, and it is only
// MOVEMENT: in F1 (from above), F2 (third person) and F3 (the table) the picture is the game's camera seen as a screen, a window or a
// diorama, and BodyWalk was the only thing that could walk the hero there. With direct input on and no BodyWalk, the controller's left
// stick walks the hero - through the game's own walking code, exactly as BodyWalk's stick does (the pad's left stick, or the walking
// hooks' vector: the game's dead zone, the game's own stick command, the game's own stop), turned into the game's frame the same way
// BodyWalk's stick is in that view. Nothing else of direct input acts there: no attack, no button, no gesture, panel, pulse, command
// or action - the pad the game gets has the left stick and nothing more.
//
// BodyWalk running (its Game Link is alive, or its pad report moves) means nothing changes: it has the controllers in these views, its
// pad is the pad, whole, as it always was.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace walkflat {

// The left stick as the pad's left stick: finite, on the unit disc, XInput's range - what the F4 layout does with it (direct_layout.h).
inline void StickToPad(float x, float y, int16_t* lx, int16_t* ly) {
    if (!std::isfinite(x) || !std::isfinite(y)) x = y = 0.0f;
    const float len = std::sqrt(x * x + y * y);
    if (len > 1.0f) { x /= len; y /= len; }
    *lx = (int16_t)std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f);
    *ly = (int16_t)std::lround(std::clamp(y, -1.0f, 1.0f) * 32767.0f);
}

// What the pad poll knows.
struct Inputs {
    bool keyOn = false;            // [direct] walk_flat_views
    bool directOn = false;         // [openxr] direct_input (the controllers are the plugin's)
    bool flatView = false;         // VR F1, F2 or F3
    bool framesFresh = false;      // a headset frame carried the stick in the last second
    bool bodyWalkRunning = false;  // BodyWalk's Game Link is alive, or its pad report moves
    bool inWorld = true;           // a game is running (not the title or character screens)
    bool menuOpen = false;         // a panel or menu is open: the stick is the menu's
    bool chatOpen = false;         // the chat line is open
};

enum class Verdict : uint8_t {
    Walk,             // the controller's left stick walks the hero
    Off,              // [direct] walk_flat_views=0, or [openxr] direct_input=0: as before
    NotFlatView,      // F4 (the layout walks), or the flat game: not this module's
    BodyWalk,         // BodyWalk is running: its pad is the pad, as it was
    NoFrames,         // no headset frame for a second (a stalled draw thread, the session lost focus or ended)
    NoGame,           // no game running: the stick is the menu's
    Menu,             // a panel or menu is open: the stick is the menu's
    Chat,             // the chat line is open
};

inline Verdict Decide(const Inputs& in) {
    if (!in.keyOn || !in.directOn) return Verdict::Off;
    if (!in.flatView) return Verdict::NotFlatView;
    if (in.bodyWalkRunning) return Verdict::BodyWalk;
    if (!in.framesFresh) return Verdict::NoFrames;
    if (!in.inWorld) return Verdict::NoGame;
    if (in.menuOpen) return Verdict::Menu;
    if (in.chatOpen) return Verdict::Chat;
    return Verdict::Walk;
}

inline const char* Name(Verdict v) {
    switch (v) {
    case Verdict::Walk: return "the controller's left stick walks the hero (movement only; BodyWalk is not running)";
    case Verdict::Off: return "off ([direct] walk_flat_views=0 or [openxr] direct_input=0): BodyWalk's input as before";
    case Verdict::NotFlatView: return "not VR F1, F2, F3";
    case Verdict::BodyWalk: return "BodyWalk is running: its pad and its walking as they were (the controller's stick is not added)";
    case Verdict::NoFrames: return "no headset frame for a second or more (a stalled draw thread, or the session lost focus or ended)";
    case Verdict::NoGame: return "no game running (the stick is the menu's)";
    case Verdict::Menu: return "a panel or menu is open (the stick is the menu's)";
    case Verdict::Chat: return "the chat line is open";
    }
    return "?";
}

// A line when the verdict changes - the first kHead of them at once, then at most one in kEveryMs, a change swallowed in between said
// when its time comes. NotFlatView and Off are no news (a view that is not ours; the key's own line says the rest). The pad poll calls
// Step only while it is in a flat view with the key on, so a pause of more than kGapMs between two calls was a visit elsewhere (F4, the
// key off, direct input off): the next verdict is news again. Caller holds a lock.
class VerdictLog {
public:
    static constexpr uint32_t kHead = 30;
    static constexpr uint64_t kEveryMs = 5000;
    static constexpr uint64_t kGapMs = 2000;
    bool Step(Verdict now, uint64_t nowMs, Verdict* say, uint32_t* changes) {
        if (seen_ && nowMs - seenAt_ > kGapMs) last_ = Verdict::NotFlatView;
        seen_ = true;
        seenAt_ = nowMs;
        if (now == Verdict::NotFlatView || now == Verdict::Off) { last_ = now; pending_ = 0; return false; }
        if (now != last_) { last_ = now; ++pending_; ++total_; }
        if (pending_ == 0) return false;
        if (lines_ < kHead || nowMs - lastAt_ >= kEveryMs) {
            ++lines_;
            lastAt_ = nowMs;
            *say = last_;
            *changes = pending_;
            pending_ = 0;
            return true;
        }
        return false;
    }
    uint32_t Lines() const { return lines_; }
    uint32_t Changes() const { return total_; }

private:
    Verdict last_ = Verdict::NotFlatView;
    uint32_t pending_ = 0, total_ = 0, lines_ = 0;
    uint64_t lastAt_ = 0, seenAt_ = 0;
    bool seen_ = false;
};

// The stick, packed with the headset frame's number into one atomic word (direct_input.cpp writes, the pad poll reads: one load, no tear).
inline uint64_t Pack(int16_t lx, int16_t ly, uint32_t counter) {
    return (uint64_t)counter | ((uint64_t)(uint16_t)lx << 32) | ((uint64_t)(uint16_t)ly << 48);
}
inline void Unpack(uint64_t w, int16_t* lx, int16_t* ly, uint32_t* counter) {
    *counter = (uint32_t)(w & 0xFFFFFFFFull);
    *lx = (int16_t)(uint16_t)((w >> 32) & 0xFFFF);
    *ly = (int16_t)(uint16_t)((w >> 48) & 0xFFFF);
}

}  // namespace walkflat
