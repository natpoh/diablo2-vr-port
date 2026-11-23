// head_hold.h - the head from BodyWalk (Game Link) as every view is built from it: only a real head reaches the camera, and without one the
// camera stands where the last real head put it. Serg (2026-11-22, FlatVR stereo): "the camera spins chaotically as if it got no data from
// the headset - without data it must stand still, but it seems to get null, so it does not". Three kinds of sample are no head:
//
//  - not finite: a NaN or an infinity in an angle or the place (finite_hold.h's case of 2026-11-06, now part of this);
//  - blank: the identity pose a runtime hands over while it has no pose - FlatVR starting, the headset asleep or off the face. BodyWalk
//    makes it the head straight ahead of its room's zero with pitch and roll exactly 0 (its roll -0.0: -asin(0)); the log of 2026-11-22
//    19:51-19:56 has it twice - the camera jumped to the room's zero (145.6 deg) for a second as FlatVR started, and again at 19:54:54
//    when the headset went to sleep, and stayed there. A tracked head never reads exact zeros in all of pitch, roll and the room's yaw
//    (or, from a BodyWalk without the room, pitch, roll and the height);
//  - missing: valid 0 after a good head - BodyWalk holds a gone head for a second, then says it is gone, and the views fell back to the
//    body's straight ahead (no head turn, no head pitch), then back to the head when it came again: a jump each way.
//
// Each of them gets the last good head in its place, its flags too (pitch / roll / room valid), so the yaw keeps its zero (the room's,
// [flatvr] room_centre - a sample without the room fell back to BodyWalk's own zero, another jump). Before any good head: no head (valid
// 0), never a made-up one. Pure (no Windows): cleanroom/tests/head_hold_test.cpp runs this very code.
#pragma once

#include <cstdint>
#include <cstring>

#include "finite_hold.h"

namespace headhold {

// One sample's head: what D2RVR_Shared carries of it.
struct Head {
    uint32_t valid = 0, pitchValid = 0, rollValid = 0, room = 0;   // room: headRoom / yawRoom valid (D2RVR_ROOM_MAGIC)
    float yaw = 0.0f, height = 0.0f, pitch = 0.0f, roll = 0.0f;   // degrees (yaw + left), metres
    float roomPos[3] = {0.0f, 0.0f, 0.0f};
    float yawRoom = 0.0f;
};

enum class Kind { Good, NotFinite, Blank, Missing };

inline bool Finite(const Head& h) {
    const float v[8] = {h.yaw, h.height, h.pitch, h.roll, h.roomPos[0], h.roomPos[1], h.roomPos[2], h.yawRoom};
    return finitehold::Finite(v, 8);
}

// The identity pose: pitch and roll sent and both exactly 0, and the room's yaw exactly 0 too (or, without the room, the height).
// Unknown (a BodyWalk that sends no pitch or no roll): never blank.
inline bool Blank(const Head& h) {
    if (!h.pitchValid || !h.rollValid || h.pitch != 0.0f || h.roll != 0.0f) return false;
    return h.room ? h.yawRoom == 0.0f : h.height == 0.0f;
}

inline Kind Classify(const Head& h) {
    if (!h.valid) return Kind::Missing;
    if (!Finite(h)) return Kind::NotFinite;
    if (Blank(h)) return Kind::Blank;
    return Kind::Good;
}

struct Hold {
    Head good;
    bool ok = false;   // a good head was seen
    uint64_t count[4] = {};          // per Kind, all told (Good's too)
    uint64_t sinceTold[4] = {};      // per Kind, since the last line (Good's unused)
    unsigned long long toldAt = 0;
    bool told = false;

    // *h is the sample's head; afterwards the head to use. Returns what it was. A good one becomes the last good; any other is replaced by
    // the last good (valid, its flags) - or, with none yet, made "no head": valid 0 and finite (the last good is never invented).
    Kind Take(Head* h) {
        const Kind k = Classify(*h);
        ++count[(int)k];
        if (k == Kind::Good) {
            good = *h;
            ok = true;
            return k;
        }
        // (missing with none ever good: as it came, valid 0 - nothing to hold, nothing to tell)
        if (k != Kind::Missing || ok) ++sinceTold[(int)k];
        if (ok) *h = good;
        else if (!(k == Kind::Missing && Finite(*h))) *h = Head{};   // (a gone head's finite angles stay as BodyWalk kept them, valid 0)
        return k;
    }
    bool HadGood() const { return ok; }
    // Held heads since the last line, and none told yet or `periodMs` gone by: a line is due. Marks it told.
    bool Due(unsigned long long nowMs, unsigned long long periodMs = 10000) {
        if (!sinceTold[1] && !sinceTold[2] && !sinceTold[3]) return false;
        if (told && nowMs - toldAt < periodMs) return false;
        told = true;
        toldAt = nowMs;
        return true;
    }
    // The counts the line reports (not finite, blank, missing - since the last line), reset by it.
    void TakeCounts(uint64_t* notFinite, uint64_t* blank, uint64_t* missing) {
        *notFinite = sinceTold[1];
        *blank = sinceTold[2];
        *missing = sinceTold[3];
        sinceTold[1] = sinceTold[2] = sinceTold[3] = 0;
    }
};

}  // namespace headhold
