#pragma once
// [gestures] engine_in_game: the pure part of feeding and asking BodyWalk's gesture engine in the game (shared/gesture_engine_abi.h; the wiring is
// vr/gesture_engine_wire.cpp, the loader vr/gesture_engine_lib.cpp). No Windows, no game: tested with a stub engine
// (cleanroom/tests/gesture_overlay_test.cpp).
//
//   - The Gestures tab's commands (gesturecmd::Cmd, the same the Game Link ring carried) become the engine's calls; each is numbered here and its
//     result kept, and the list handed to the tab carries them as cmdDone / cmdResult - so the tab's model (vr/gesture_overlay.h) is the same for
//     either backend, with its guards (an answer is matched by its number; a take's end is read only after its READY).
//   - Which trigger is a pen this frame (Pens): while the tab records, only the take's hand(s); with the panel up otherwise none (a click is not a
//     stroke); in play (F4, a game area, no menu) the ones [gestures] engine_pen names; else none.
//   - The engine's frame from the headset's (FrameOf).

#include <cstdint>
#include <cstring>

#include "direct_input.h"
#include "gesture_cmds.h"
#include "gesture_engine_abi.h"

namespace gengine {

// [gestures] engine_pen: the triggers that are pens in play (bit 0 the left, bit 1 the right).
enum : int { kPenNone = 0, kPenLeft = 1, kPenRight = 2, kPenBoth = 3 };

struct PenIn {
    bool play = false;        // F4, a game area, no menu, the session focused
    bool panelOpen = false;   // the skill panel's window is up
    bool recording = false;   // the Gestures tab's take runs (or is armed)
    uint8_t recHand = BW_LINK_GH_RIGHT;   // ... with this hand (BW_LINK_GH_*)
    int playPens = kPenBoth;  // [gestures] engine_pen
};
inline void Pens(const PenIn& in, uint8_t pen[2]) {
    pen[0] = pen[1] = 0;
    if (in.recording) {
        pen[0] = in.recHand == BW_LINK_GH_LEFT || in.recHand == BW_LINK_GH_BOTH;
        pen[1] = in.recHand == BW_LINK_GH_RIGHT || in.recHand == BW_LINK_GH_BOTH;
        return;
    }
    if (in.panelOpen || !in.play) return;
    pen[0] = (in.playPens & kPenLeft) != 0;
    pen[1] = (in.playPens & kPenRight) != 0;
}

inline BWGE_Pose PoseOf(const directinput::Pose& p) {
    BWGE_Pose o{};
    for (int i = 0; i < 3; ++i) o.pos[i] = p.pos[i];
    for (int i = 0; i < 4; ++i) o.rot[i] = p.rot[i];
    o.valid = p.valid ? 1u : 0u;
    return o;
}
// The engine's frame: the head, both controllers as held, triggers, grips, the pens; `play` sets BWGE_FRAME_PLAY (a recognised stroke may cast).
inline BWGE_Frame FrameOf(const directinput::Frame& f, bool play, const uint8_t pen[2]) {
    BWGE_Frame o{};
    o.size = sizeof o;
    o.flags = play ? BWGE_FRAME_PLAY : 0u;
    o.timeS = f.time;
    o.head = PoseOf(f.head);
    for (int h = 0; h < 2; ++h) {
        o.hand[h] = PoseOf(f.hand[h].grip);
        if (!f.hand[h].active) o.hand[h].valid = 0;   // (no controller in it)
        o.trigger[h] = f.hand[h].trigger;
        o.grip[h] = f.hand[h].squeeze;
        o.pen[h] = pen[h];
    }
    return o;   // (chest / pelvis: none - valid 0)
}

// The tab's command as the engine's call: its result (BW_LINK_GR_*). `E` has the gelib calls (Record, Stop ... SetThreshold).
template <class E> uint32_t Do(E& e, const gesturecmd::Cmd& c) {
    switch (c.op) {
    case BW_LINK_GC_RECORD: return (uint32_t)e.Record(c.gesture, c.type, c.hand, c.skill);
    case BW_LINK_GC_STOP: return (uint32_t)e.Stop();
    case BW_LINK_GC_KEEP: return (uint32_t)e.Keep();
    case BW_LINK_GC_DISCARD: return (uint32_t)e.Discard();
    case BW_LINK_GC_CLEAR: return (uint32_t)e.Clear(c.gesture);
    case BW_LINK_GC_BIND: return (uint32_t)e.Bind(c.gesture, c.skill);
    case BW_LINK_GC_COLOR: return (uint32_t)e.SetColor(c.gesture, c.rgba);
    case BW_LINK_GC_THRESHOLD: return (uint32_t)e.SetThreshold(c.gesture, c.value);
    default: return BW_LINK_GR_BAD;
    }
}

// The numbers the tab matches answers by: each command gets the next, done at once (the engine answers in the call).
class Answers {
    uint32_t seq_ = 0, done_ = 0, result_ = 0;

public:
    template <class E> uint32_t Push(E& e, const gesturecmd::Cmd& c) {
        seq_ = gesturecmd::Next(seq_);
        result_ = Do(e, c);
        done_ = seq_;
        return seq_;
    }
    // The engine's list made the tab's: the answers on it, and the header a reader of the Game Link block would see.
    void Stamp(BW_LinkGestures& g) const {
        g.cmdDone = done_;
        g.cmdResult = result_;
        g.header.magic = BW_LINK_MAGIC;
        g.header.version = BW_LINK_GESTURES_VERSION;
        g.header.size = (uint32_t)sizeof(BW_LinkGestures);
    }
    uint32_t Done() const { return done_; }
    uint32_t Result() const { return result_; }
};

}  // namespace gengine
