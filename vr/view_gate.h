// view_gate.h - where the behaviours added after the base mod may act: VR first person (F4) only.
//
// The user's rule for everything that was ported in: "direct movements work only on F4, and everything we do; on the other
// views leave everything as it was". F1 (from above), F2 (third person), F3 (the table), the flat modes and the title and
// character screens' flat picture keep BodyWalk's own input and layout, the game's own animation and poses; no new gesture, panel,
// press, pulse or hook acts there.
//
// ONE predicate, computed in one place: vr/vrcam.cpp registers FullBody() (VR platform and view 4) at the plugin's start.
// Every module that would act asks F4() at the moment it decides - a pull, not a pushed flag, so there is no stale copy
// to lag a view change by a frame. A module that finds it false stands down and ends whatever it had under way (closes
// its panel, lets go of its pad button or command, drops its stroke); coming back to F4 starts it fresh.
//
// Nothing registered (the unit tests, the settings program): true, so a module is tested as it acts in F4 and a test
// that wants the other views registers its own function.
//
// THE ONE EXCEPTION, movement (2026-10-15, "now movement in F1-F3 without BodyWalk"): Flat() is true in VR F1, F2, F3 - the views BodyWalk's
// input still belongs to - and only vr/direct_input.cpp's walk-only feed asks it ([direct] walk_flat_views): the controller's left stick
// walks the hero there, nothing else of direct input acts (no attack, gesture, panel, pulse, command or action).
#pragma once

#include <atomic>

namespace viewgate {

using Fn = bool (*)();

inline std::atomic<Fn>& Slot() {
    static std::atomic<Fn> slot{nullptr};
    return slot;
}

// vrcam, once, before any module that asks runs: the plugin's own view test.
inline void Set(Fn fn) { Slot().store(fn, std::memory_order_release); }

// True where the new behaviours act (VR first person, F4). Any thread; two atomic loads behind a call.
inline bool F4() {
    const Fn fn = Slot().load(std::memory_order_acquire);
    return fn ? fn() : true;
}

// VR F1, F2, F3 (not F4, not the flat game). Registered by vrcam next to Set; nothing registered: false (a test that wants the
// movement in these views registers its own function).
inline std::atomic<Fn>& FlatSlot() {
    static std::atomic<Fn> slot{nullptr};
    return slot;
}
inline void SetFlat(Fn fn) { FlatSlot().store(fn, std::memory_order_release); }
inline bool Flat() {
    const Fn fn = FlatSlot().load(std::memory_order_acquire);
    return fn ? fn() : false;
}

// A module that has to END something when the view changes keeps one of these (its own thread only): Step(F4()) is
// true exactly once at each change.
class Edge {
 public:
    // `now`: this frame's answer. Returns true if it differs from the last one seen (the first call never does:
    // a module starting up has nothing under way).
    bool Step(bool now) {
        const bool changed = seen_ && now != last_;
        last_ = now;
        seen_ = true;
        return changed;
    }
    bool Last() const { return last_; }

 private:
    bool last_ = true;
    bool seen_ = false;
};

}  // namespace viewgate
