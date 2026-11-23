// Only a real head reaches the camera (vr/head_hold.h): a NaN, a blank (identity) or a gone head is replaced by the last good head, its flags
// too, so the camera stands still where it was (Serg 2026-11-22, FlatVR stereo: "without data it must stand still"). No game, no headset:
//
//   head_hold_test      every case; exit code 0 = all passed
//
// good       a tracked head goes through as it is and becomes the last good one
// nan        a NaN / inf anywhere: the last good head back, valid, its flags
// blank      the identity pose (pitch 0, roll -0.0, the room's yaw -0.0 - as BodyWalk makes it from a runtime with no pose): held
// noroom     a BodyWalk without the room: pitch 0, roll 0 and height 0 is blank; a height is a head
// notblank   exact zeros in some but not all (a yaw-only test head, a head at the room's zero looking down): a head
// unknown    no pitch or no roll sent: never blank (nothing to tell it by)
// missing    valid 0 after a good head: the last good head, valid, with the room's flags (its zero kept: no jump to BodyWalk's own)
// none       before any good head: blank, NaN and gone are "no head" (valid 0, finite) - never a made-up one, and a gone head untold
// flicker    good, blank, NaN, gone, blank ...: the yaw the camera reads never leaves the good one (the spin); a new good head is taken at once
// stale      the same good sample again and again: taken each time, nothing told
// log        Due: the first held head at once, then at most every period; the counts by kind, reset by the line

#include "head_hold.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

int g_failed = 0, g_checks = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}

const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

// A tracked head as BodyWalk's Game Link sends it (room on).
headhold::Head Tracked(float yawRoom, float pitch = -12.5f, float roll = 2.25f) {
    headhold::Head h;
    h.valid = 1; h.pitchValid = 1; h.rollValid = 1; h.room = 1;
    h.yaw = yawRoom - 40.0f; h.height = 1.72f; h.pitch = pitch; h.roll = roll;
    h.roomPos[0] = 0.1f; h.roomPos[1] = 1.72f; h.roomPos[2] = -0.3f;
    h.yawRoom = yawRoom;
    return h;
}
// The identity pose through BodyWalk: straight ahead of its room's zero (yaw = -its base), pitch asin(0), roll -asin(0), the room's yaw atan2(-0, 1).
headhold::Head Identity() {
    headhold::Head h;
    h.valid = 1; h.pitchValid = 1; h.rollValid = 1; h.room = 1;
    h.yaw = 40.0f; h.height = 0.0f; h.pitch = 0.0f; h.roll = -0.0f;
    h.yawRoom = -0.0f;
    return h;
}
bool Same(const headhold::Head& a, const headhold::Head& b) {
    return a.valid == b.valid && a.pitchValid == b.pitchValid && a.rollValid == b.rollValid && a.room == b.room && a.yaw == b.yaw &&
           a.height == b.height && a.pitch == b.pitch && a.roll == b.roll && a.roomPos[0] == b.roomPos[0] && a.roomPos[1] == b.roomPos[1] &&
           a.roomPos[2] == b.roomPos[2] && a.yawRoom == b.yawRoom;
}
bool AllFinite(const headhold::Head& h) { return headhold::Finite(h); }

void Good() {
    headhold::Hold hold;
    const headhold::Head want = Tracked(25.0f);
    headhold::Head h = want;
    Check(hold.Take(&h) == headhold::Kind::Good, "good: a tracked head is good");
    Check(Same(h, want), "good: it goes through as it is");
    Check(hold.HadGood(), "good: it is the last good one");
    Check(!hold.Due(0), "good: nothing to tell");
}

void Nan() {
    const float bad[] = {kNan, kInf, -kInf};
    for (float b : bad) {
        for (int field = 0; field < 8; ++field) {
            headhold::Hold hold;
            const headhold::Head want = Tracked(25.0f);
            headhold::Head h = want;
            hold.Take(&h);
            h = Tracked(30.0f);
            float* f[8] = {&h.yaw, &h.height, &h.pitch, &h.roll, &h.roomPos[0], &h.roomPos[1], &h.roomPos[2], &h.yawRoom};
            *f[field] = b;
            Check(hold.Take(&h) == headhold::Kind::NotFinite, "nan: a NaN / inf anywhere is not finite");
            Check(Same(h, want), "nan: the last good head back, whole");
            Check(h.valid == 1 && AllFinite(h), "nan: valid and finite");
        }
    }
}

void Blank() {
    headhold::Hold hold;
    const headhold::Head want = Tracked(-118.5f, -21.7f, 3.5f);
    headhold::Head h = want;
    hold.Take(&h);
    h = Identity();
    Check(headhold::Blank(h), "blank: the identity pose is blank");
    Check(hold.Take(&h) == headhold::Kind::Blank, "blank: taken as blank");
    Check(Same(h, want), "blank: the last good head held - the camera does not jump to the room's zero");
    // +0.0 the same as -0.0
    h = Identity(); h.roll = 0.0f; h.yawRoom = 0.0f;
    Check(hold.Take(&h) == headhold::Kind::Blank && Same(h, want), "blank: +0 as -0");
}

void NoRoom() {
    headhold::Hold hold;
    headhold::Head want = Tracked(10.0f);
    want.room = 0; want.roomPos[0] = want.roomPos[1] = want.roomPos[2] = 0.0f; want.yawRoom = 0.0f;
    headhold::Head h = want;
    Check(hold.Take(&h) == headhold::Kind::Good, "noroom: a head without the room is good");
    h = headhold::Head{}; h.valid = 1; h.pitchValid = 1; h.rollValid = 1; h.yaw = 40.0f;   // pitch 0, roll 0, height 0
    Check(hold.Take(&h) == headhold::Kind::Blank, "noroom: pitch, roll and height all 0 is blank");
    Check(Same(h, want), "noroom: held");
    h = headhold::Head{}; h.valid = 1; h.pitchValid = 1; h.rollValid = 1; h.yaw = 40.0f; h.height = 1.6f;
    Check(hold.Take(&h) == headhold::Kind::Good, "noroom: with a height it is a head");
}

void NotBlank() {
    headhold::Hold hold;
    headhold::Head h = Tracked(30.0f, 0.0f, 0.0f);   // level to the float, but turned in the room
    Check(hold.Take(&h) == headhold::Kind::Good, "notblank: pitch and roll 0 with a room yaw is a head");
    h = Tracked(0.0f, -45.0f, 0.0f);   // at the room's zero, looking down
    Check(hold.Take(&h) == headhold::Kind::Good, "notblank: the room's zero with a pitch is a head");
    h = Tracked(0.0f, 0.0f, 1.0f);
    Check(hold.Take(&h) == headhold::Kind::Good, "notblank: the room's zero with a roll is a head");
}

void Unknown() {
    headhold::Hold hold;
    headhold::Head h = Identity();
    h.rollValid = 0;
    Check(hold.Take(&h) == headhold::Kind::Good, "unknown: no roll sent - never blank");
    h = Identity();
    h.pitchValid = 0;
    Check(hold.Take(&h) == headhold::Kind::Good, "unknown: no pitch sent - never blank");
}

void Missing() {
    headhold::Hold hold;
    const headhold::Head want = Tracked(77.0f);
    headhold::Head h = want;
    hold.Take(&h);
    // BodyWalk's gone head: valid 0, no room flag (game_link sets the room only with a head), its last angles kept
    h = Tracked(80.0f); h.valid = 0; h.room = 0;
    Check(hold.Take(&h) == headhold::Kind::Missing, "missing: valid 0 is gone");
    Check(Same(h, want), "missing: the last good head, valid, the room's flag and yaw kept");
    for (int i = 0; i < 1000; ++i) { h = headhold::Head{}; hold.Take(&h); }
    Check(Same(h, want), "missing: held for as long as it is gone");
    h = Tracked(-5.0f);
    Check(hold.Take(&h) == headhold::Kind::Good && h.yawRoom == -5.0f, "missing: the head back - taken at once");
}

void None() {
    {
        headhold::Hold hold;
        headhold::Head h = Identity();
        Check(hold.Take(&h) == headhold::Kind::Blank, "none: blank first");
        Check(h.valid == 0 && AllFinite(h) && h.yawRoom == 0.0f && h.room == 0, "none: blank before any good head - no head");
        Check(!hold.HadGood(), "none: nothing good yet");
        Check(hold.Due(0), "none: a blank is told");
    }
    {
        headhold::Hold hold;
        headhold::Head h = Tracked(10.0f); h.yaw = kNan;
        hold.Take(&h);
        Check(h.valid == 0 && AllFinite(h), "none: a NaN before any good head - no head, finite");
    }
    {
        headhold::Hold hold;
        headhold::Head h = Tracked(10.0f); h.valid = 0; h.room = 0;
        const headhold::Head came = h;
        hold.Take(&h);
        Check(Same(h, came), "none: a gone head before any good one - as it came (valid 0)");
        Check(!hold.Due(0), "none: a gone head with nothing to hold is not told");
        h = Tracked(10.0f); h.valid = 0; h.pitch = kNan;
        hold.Take(&h);
        Check(h.valid == 0 && AllFinite(h), "none: a gone head not finite - finite zeros");
    }
}

void Flicker() {
    headhold::Hold hold;
    headhold::Head h = Tracked(-100.8f);
    hold.Take(&h);
    bool stood = true;
    for (int i = 0; i < 400; ++i) {
        switch (i % 4) {
        case 0: h = Identity(); break;
        case 1: h = Tracked(50.0f); h.pitch = kNan; break;
        case 2: h = headhold::Head{}; break;
        case 3: h = Identity(); h.roll = 0.0f; break;
        }
        hold.Take(&h);
        if (h.valid != 1 || h.yawRoom != -100.8f || h.room != 1 || !AllFinite(h)) stood = false;
    }
    Check(stood, "flicker: blank / NaN / gone in turns - the camera's yaw never leaves the good head");
    h = Tracked(12.0f);
    Check(hold.Take(&h) == headhold::Kind::Good && h.yawRoom == 12.0f, "flicker: a new good head is taken at once");
    h = Identity();
    hold.Take(&h);
    Check(h.yawRoom == 12.0f, "flicker: and held from then on");
}

void Stale() {
    headhold::Hold hold;
    const headhold::Head want = Tracked(33.0f);
    for (int i = 0; i < 100; ++i) {
        headhold::Head h = want;
        Check(hold.Take(&h) == headhold::Kind::Good && Same(h, want), "stale: the same good sample - taken as it is");
    }
    Check(!hold.Due(0), "stale: nothing told");
}

void LogPace() {
    headhold::Hold hold;
    headhold::Head h = Tracked(1.0f);
    hold.Take(&h);
    h = Identity(); hold.Take(&h);
    h = Identity(); hold.Take(&h);
    h = headhold::Head{}; hold.Take(&h);
    h = Tracked(1.0f); h.yaw = kNan; hold.Take(&h);
    Check(hold.Due(1000), "log: the first held head - a line at once");
    uint64_t nf = 0, bl = 0, mi = 0;
    hold.TakeCounts(&nf, &bl, &mi);
    Check(nf == 1 && bl == 2 && mi == 1, "log: the counts by kind");
    Check(!hold.Due(1001), "log: nothing since - no line");
    h = Identity(); hold.Take(&h);
    Check(!hold.Due(5000), "log: within the period - no line");
    Check(hold.Due(11000), "log: the period gone - a line");
    hold.TakeCounts(&nf, &bl, &mi);
    Check(nf == 0 && bl == 1 && mi == 0, "log: reset by the line");
}

}  // namespace

int main() {
    Good();
    Nan();
    Blank();
    NoRoom();
    NotBlank();
    Unknown();
    Missing();
    None();
    Flicker();
    Stale();
    LogPace();
    std::printf("head_hold_test: %d of %d checks passed\n", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
