#pragma once
// [openxr] gesture_overlay: the game's commands to BodyWalk's gestures (Game Link's GestureCmds block, shared/skill_link_abi.h) - the ring
// writer and the check that BodyWalk acted on one (its Gestures block's cmdDone). Pure: a local copy of the block in, the same copy out; the
// wiring (vr/link.cpp PushGestureCmd) writes the copy whole into the shared block, so BodyWalk sees the entry and lastSequence together.
//
// The ring: entry ring[sequence % 8] is written first, then lastSequence moved to it - BodyWalk's reader takes every sequence after the last
// one it saw (at most the ring's eight), each once. A new game (another writer) continues from what the block and BodyWalk's cmdDone already
// hold (Seed), so "acted on" can never be read off a number from before.

#include <cstdint>
#include <cstring>

#include "skill_link_abi.h"

namespace gesturecmd {

struct Cmd {
    uint32_t op = 0;          // BW_LINK_GC_*
    uint32_t gesture = 0;     // the gesture's id (0: RECORD a new one)
    int32_t skill = -1;       // RECORD of a new one / BIND: the Skills id (-1 none)
    uint8_t type = 0;         // RECORD of a new one: BW_LINK_GT_*
    uint8_t hand = 0;         // ... and BW_LINK_GH_*
    uint32_t rgba = 0;        // COLOR
    float value = 0.0f;       // THRESHOLD, percent
};

// The next sequence after `last` (0 is never one).
inline uint32_t Next(uint32_t last) { return last + 1u == 0u ? 1u : last + 1u; }

// Wrap-safe "a is at or after b".
inline bool AtOrAfter(uint32_t a, uint32_t b) { return int32_t(a - b) >= 0; }

// The first push of a game: our copy continues after whatever the shared block already says (`blockLast`, 0 for a block never written) and
// after the last command BodyWalk acted on (`cmdDone`), so neither BodyWalk's reader nor our "done" check is fooled by an old number.
inline void Seed(BW_LinkGestureCmds& local, uint32_t blockLast, uint32_t cmdDone) {
    uint32_t last = local.lastSequence;
    if (AtOrAfter(blockLast, last)) last = blockLast;
    if (AtOrAfter(cmdDone, last)) last = cmdDone;
    local.lastSequence = last;
}

// A command into the ring: its entry, then lastSequence. Returns its sequence.
inline uint32_t Push(BW_LinkGestureCmds& local, const Cmd& c) {
    const uint32_t seq = Next(local.lastSequence);
    BW_LinkGestureCmd& e = local.ring[seq % BW_LINK_GESTURECMDS_RING];
    std::memset(&e, 0, sizeof e);
    e.sequence = seq;
    e.op = c.op;
    e.gesture = c.gesture;
    e.skill = c.skill;
    e.type = c.type;
    e.hand = c.hand;
    e.rgba = c.rgba;
    e.value = c.value;
    local.lastSequence = seq;
    return seq;
}

// BodyWalk has acted on command `seq` (its Gestures block says so).
inline bool Done(const BW_LinkGestures& g, uint32_t seq) { return seq != 0 && g.cmdDone != 0 && AtOrAfter(g.cmdDone, seq); }

inline const char* OpName(uint32_t op) {
    switch (op) {
    case BW_LINK_GC_RECORD: return "RECORD";
    case BW_LINK_GC_STOP: return "STOP";
    case BW_LINK_GC_KEEP: return "KEEP";
    case BW_LINK_GC_DISCARD: return "DISCARD";
    case BW_LINK_GC_CLEAR: return "CLEAR";
    case BW_LINK_GC_BIND: return "BIND";
    case BW_LINK_GC_COLOR: return "COLOR";
    case BW_LINK_GC_THRESHOLD: return "THRESHOLD";
    default: return "?";
    }
}

}  // namespace gesturecmd
