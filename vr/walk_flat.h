// walk_flat.h - walking with the controller's stick in VR F1, F2, F3 without BodyWalk ([direct] walk_flat_views). Pure: no game, no
// XInput, no clock of its own; the unit test is cleanroom/tests/walk_flat_test.cpp, the wiring is vr/direct_input.cpp (the stick, per
// headset frame) and vr/vrcam.cpp (PadMirrorRead: the pad the game's walking code is given).
//
// The rule of the plugin is "everything new acts in F4 only" (vr/view_gate.h). The exception, decided with the user (2026-10-15): the CONTROLS,
// where BodyWalk is not running. In F1 (from above), F2 (third person) and F3 (the table) the picture is the game's camera seen as a screen, a
// window or a diorama, and BodyWalk was the only thing that could play it there. With direct input on and no BodyWalk the plugin's own
// controller is the game's pad: the layout of F4 (the right trigger attacks, A picks up, B / X / Y the skills ...), the left stick walks the hero
// through the game's own walking code exactly as BodyWalk's stick does, the left trigger + a flick of a stick drinks a belt potion
// (vr/belt_flick.h). What stays F4's only: the gestures, hit by touch, the bow's draw, throws, the plugin's panels (the skill wheel), aiming by
// the hand (the attack goes where the hero faces in F1 / F3, vr/attack_view.h).
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

// -- the pad the game gets in VR F1-F3 -------------------------------------------------------------------------------------------
// The rule (2026-10-15): BodyWalk running = nothing of ours touches the pad - its report whole, or the real / virtual pad as it answers, with every
// button, trigger and stick (AsBefore). BodyWalk NOT running: the plugin's own controller is the pad in these views - the layout of F4 (the right
// trigger attacks, A picks up, B / X / Y the skills, the triggers, the sticks ...) without what only F4 has - and the walk and the belt on the left
// stick are part of it. The pad is `ours` (OurPad); where a real pad (or BodyWalk's virtual one, its Game Link not alive) answers the poll as well,
// ours is added to it, never over it: buttons ORed, triggers the larger, a stick of ours taken only where that pad's own stick rests (RealPlusOurs).
// With the controller switched off ([direct] flat_controller=0) `ours` is the walk alone: the left stick and nothing else.
struct PadBits {
    uint16_t buttons = 0;
    uint8_t lt = 0, rt = 0;
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};
enum class PadSource : uint8_t { AsBefore, OurPad, RealPlusOurs };

inline PadSource ChooseSource(Verdict v, bool padAnswers) {
    if (v != Verdict::Walk) return PadSource::AsBefore;
    return padAnswers ? PadSource::RealPlusOurs : PadSource::OurPad;
}

// The game's own dead zone (0x1210DA0: 10349 of 32767): a stick past it is pushed.
inline bool StickPushed(int x, int y) { return (long long)x * x + (long long)y * y > 10349ll * 10349ll; }

// Ours added to an answering pad. True = the pad was changed.
inline bool MergePads(PadBits* pad, const PadBits& ours) {
    bool changed = false;
    if ((pad->buttons | ours.buttons) != pad->buttons) { pad->buttons |= ours.buttons; changed = true; }
    if (ours.lt > pad->lt) { pad->lt = ours.lt; changed = true; }
    if (ours.rt > pad->rt) { pad->rt = ours.rt; changed = true; }
    if (StickPushed(ours.lx, ours.ly) && !StickPushed(pad->lx, pad->ly)) { pad->lx = ours.lx; pad->ly = ours.ly; changed = true; }
    if (StickPushed(ours.rx, ours.ry) && !StickPushed(pad->rx, pad->ry)) { pad->rx = ours.rx; pad->ry = ours.ry; changed = true; }
    return changed;
}

// A gamepad in the player's hands, next to the VR controllers (the Discord report of 2026-10-18: "every button works except the thumbsticks - I cannot walk or
// turn", a PS VR2 player who also held a gamepad): the plugin's pad - F4's whole layout, the F1-F3 controller, FlatVR's raw controls - used to be THE pad the game
// polled, so the physical pad's report was thrown away; and the game's walking is not the pad's stick but a hook that takes the plugin's stick (vr/vrcam.cpp
// HookStickGet / BodyWalkWalk) while the pad's own left stick is kept from the game. So a real pad joins in two places:
//  - the pad the game polls: MergePads - buttons ORed, the larger trigger, a stick of ours only where the real pad's own stick rests (its pushed stick wins);
//  - the stick the walking hook is given: FoldRealStick - the real pad's left stick where it is pushed past the game's dead zone, else ours.
// It joins only when the XInput poll answered AND BodyWalk's own pad is not in charge: BodyWalk's virtual Xbox pad also answers that poll (ViGEm), and what
// it carries - its Mapping, its gestures - is exactly what direct input has taken out of the game ("nothing is pressed twice").
inline bool RealPadJoins(bool xinputAnswered, bool bodyWalkInCharge) { return xinputAnswered && !bodyWalkInCharge; }
constexpr uint64_t kRealPadFreshMs = 150;   // the real pad's last report counts for the walking hook this long (the game polls every frame)
inline bool RealPadFresh(uint64_t nowMs, uint64_t seenAtMs) { return seenAtMs != 0 && nowMs >= seenAtMs && nowMs - seenAtMs <= kRealPadFreshMs; }
// The stick (x y) the walking hook is given: the real pad's, if it is fresh and pushed; ours otherwise. True = it was replaced.
inline bool FoldRealStick(int16_t* x, int16_t* y, bool realFresh, int16_t realX, int16_t realY) {
    if (!realFresh || !StickPushed(realX, realY)) return false;
    *x = realX;
    *y = realY;
    return true;
}

// The walk alone as a pad: the left stick and nothing else.
inline PadBits WalkPad(int16_t lx, int16_t ly) { PadBits p; p.lx = lx; p.ly = ly; return p; }

// The pad the game is given: `base` is what answers (BodyWalk's report, a real or virtual pad; ignored for OurPad), `ours` the plugin's.
inline PadBits Compose(PadSource src, const PadBits& base, const PadBits& ours) {
    switch (src) {
    case PadSource::AsBefore: return base;
    case PadSource::RealPlusOurs: { PadBits p = base; MergePads(&p, ours); return p; }
    case PadSource::OurPad: return ours;
    }
    return base;
}

// A control pressed in VR F1-F3 that nothing of ours takes there (the right trigger, a grip, a button of either hand): said in the log, at most once in
// kHintEveryMs, so "the right controller does nothing here" is answered by the log - the walk and the belt are all the plugin does in these views without
// BodyWalk; the rest of the controls is F4's (the plugin's own layout) or BodyWalk's Mapping.
constexpr uint64_t kHintEveryMs = 30000;

inline const char* Name(Verdict v) {
    switch (v) {
    case Verdict::Walk: return "the plugin's own controller plays here (the left stick walks the hero, the buttons and triggers as in F4; BodyWalk is not running)";
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
