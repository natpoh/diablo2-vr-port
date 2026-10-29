// [debug] bow_latency (vr/bow_latency.h) on a synthetic shot, without the game and without a headset: the split of the second between a release and its arrow, the
// heading's turn across the +-180 wrap, what is "not seen", and the one line.
//
//   bow_latency_test

#include "../../vr/bow_latency.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace bowlatency;

namespace {

int g_fails = 0;
#define EXPECT(c, ...)                                         \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// A shot like the one in the user's log (18:05:23.515): the wire presses at the release, the pad's A is down 42 ms on and up 136 ms, the hero's
// body turns from 146 deg to -50 over 250 .. 500 ms, his mode goes 1 -> 7 at 180 ms and back at 910, the server creates a missile at 810.
void Feed(Tracker& t, double t0, bool create = true) {
    for (int i = 0; i < 60; ++i) {   // idle before: heading 146, neutral
        t.Yaw(t0 - 1.0 + i * 0.016, 146.0f);
        t.HeroMode(t0 - 1.0 + i * 0.016, 1);
    }
    t.Begin(t0);
    t.WirePress(t0 + 0.001);
    for (int ms = 0; ms <= 1500; ms += 16) {
        const double now = t0 + ms * 0.001;
        float yaw = 146.0f;
        if (ms >= 20) {   // turns from 146 through 180 to -85 (231 deg the short way: 146 -> 180 -> -85) by 250 ms, then eases to -50 by 500
            if (ms < 250) { const float k = (ms - 20) / 230.0f; yaw = WrapDeg(146.0f + k * (360.0f - 85.0f - 146.0f + 0.0f)); }
            else if (ms < 500) { const float k = (ms - 250) / 250.0f; yaw = -85.0f + k * 35.0f; }
            else yaw = -50.0f;
        }
        t.Yaw(now, yaw);
        t.HeroMode(now, ms >= 180 && ms < 910 ? 7 : 1);
        if (ms == 48) t.PadA(now, true);
        if (ms == 144) t.PadA(now, false);
        if (create && ms == 816) t.Created(now, 11, 0x20);
        if (create && ms == 832) t.ClientMissile(now, 11);
    }
}

void TestSplit() {
    std::printf("the split of a shot\n");
    Tracker t;
    const double t0 = 100.0;
    Feed(t, t0);
    EXPECT(t.Active() && !t.Over(t0 + 2.0) && t.Over(t0 + 2.6), "watched for %.1f s", kWindowS);
    const Result r = t.Finish();
    EXPECT(!t.Active(), "finished");
    EXPECT(Near(r.wirePressMs, 1.0, 0.1), "wire press %.1f", r.wirePressMs);
    EXPECT(Near(r.padDownMs, 48.0, 0.1) && Near(r.padUpMs, 144.0, 0.1), "pad A %.0f..%.0f", r.padDownMs, r.padUpMs);
    EXPECT(r.attackMode == 7 && Near(r.attackModeMs, 192.0, 16.0) && Near(r.neutralAgainMs, 912.0, 16.0), "attack mode %d at %.0f, neutral again %.0f", r.attackMode, r.attackModeMs, r.neutralAgainMs);
    EXPECT(r.turnBeganMs >= 16.0 && r.turnBeganMs <= 64.0, "turn began %.0f", r.turnBeganMs);
    EXPECT(r.turnDoneMs >= 400.0 && r.turnDoneMs <= 512.0, "turn done %.0f (within %.0f deg of where it ends)", r.turnDoneMs, kSettleDeg);
    EXPECT(Near(r.yawFrom, 146.0f, 0.01) && Near(r.yawTo, -50.0f, 0.01), "heading %.0f -> %.0f", r.yawFrom, r.yawTo);
    EXPECT(Near(r.createdMs, 816.0, 0.1) && r.createdClass == 11 && r.creates == 1, "created %.0f class %d (%u)", r.createdMs, r.createdClass, (unsigned)r.creates);
    EXPECT(Near(r.clientMs, 832.0, 0.1) && r.clientClass == 11, "client %.0f", r.clientMs);
    char line[700];
    Line(r, true, line, sizeof line);
    EXPECT(std::strstr(line, "bow latency: release") && std::strstr(line, "wire press 1 ms") && std::strstr(line, "pad button down 48 ms (up 144)") &&
           std::strstr(line, "attack mode 7 at") && std::strstr(line, "arrow created 816 ms (missile class 11, the hero's; 1 create)") &&
           std::strstr(line, "client shows a missile 832 ms"), "the line: %s", line);
}

void TestNotSeen() {
    std::printf("nothing seen\n");
    Tracker t;
    t.Yaw(0.0, 10.0f);
    t.HeroMode(0.0, 1);
    t.Begin(1.0);
    for (int i = 0; i < 100; ++i) { t.Yaw(1.0 + i * 0.016, 10.0f); t.HeroMode(1.0 + i * 0.016, 1); }
    const Result r = t.Finish();
    EXPECT(r.wirePressMs < 0.0 && r.padDownMs < 0.0 && r.attackModeMs < 0.0 && r.turnBeganMs < 0.0 && r.createdMs < 0.0 && r.clientMs < 0.0 && r.creates == 0,
           "everything unseen is negative");
    char line[700];
    Line(r, true, line, sizeof line);
    EXPECT(std::strstr(line, "pad button down not seen") && std::strstr(line, "attack mode not seen") && std::strstr(line, "no turn of the body") &&
           std::strstr(line, "arrow created: none by the hero") && std::strstr(line, "client shows a missile: none"), "the line: %s", line);
    Line(r, false, line, sizeof line);
    EXPECT(std::strstr(line, "arrow created: create not watched"), "an unwatched create is said so, not 'none': %s", line);
}

void TestWrapAndOutside() {
    std::printf("the +-180 wrap, and events outside a shot\n");
    Tracker t;
    // events with no shot under way are not kept
    t.WirePress(5.0); t.PadA(5.0, true); t.Created(5.0, 3, 0); t.ClientMissile(5.0, 3);
    t.Yaw(4.0, 170.0f); t.HeroMode(4.0, 7);
    t.Begin(6.0);
    t.Yaw(6.1, 175.0f);
    t.Yaw(6.2, -175.0f);   // a 15 deg turn across the wrap
    t.Yaw(6.3, -170.0f);
    t.Yaw(6.4, -170.0f);
    Result r = t.Finish();
    EXPECT(r.wirePressMs < 0.0 && r.padDownMs < 0.0 && r.createdMs < 0.0 && r.clientMs < 0.0, "nothing from before the release");
    EXPECT(Near(r.yawFrom, 170.0f, 0.01) && Near(r.yawTo, -170.0f, 0.01), "baseline from before the release: %.0f -> %.0f", r.yawFrom, r.yawTo);
    EXPECT(r.turnBeganMs >= 0.0 && r.turnBeganMs < 250.0, "a 20 deg turn across the wrap is a turn, %.0f", r.turnBeganMs);
    EXPECT(Near(WrapDeg(190.0f), -170.0f, 1e-3) && Near(WrapDeg(-190.0f), 170.0f, 1e-3) && WrapDeg(NAN) == 0.0f, "WrapDeg");
    // a small wobble is not a turn
    Tracker u;
    u.Yaw(0.0, 90.0f);
    u.Begin(1.0);
    u.Yaw(1.1, 93.0f); u.Yaw(1.2, 88.0f); u.Yaw(1.3, 91.0f);
    r = u.Finish();
    EXPECT(r.turnBeganMs < 0.0, "3 deg is no turn");
}

void TestLimits() {
    std::printf("the caps, a second shot\n");
    Tracker t;
    t.HeroMode(0.0, 1);
    t.Yaw(0.0, 0.0f);
    t.Begin(1.0);
    for (int i = 0; i < 40; ++i) t.HeroMode(1.0 + i * 0.01, i % 2 ? 7 : 1);
    for (int i = 0; i < 1000; ++i) t.Yaw(1.0 + i * 0.001, (float)(i % 90));
    for (int i = 0; i < 20; ++i) t.Created(1.0 + i * 0.01, 10 + i, 0);
    Result r = t.Finish();
    EXPECT(r.modeChanges <= kMaxModes && r.creates <= kMaxCreates && r.createdClass == 10, "capped: %u modes, %u creates", (unsigned)r.modeChanges, (unsigned)r.creates);
    // the next shot starts clean
    t.Begin(10.0);
    t.Yaw(10.1, 5.0f);
    r = t.Finish();
    EXPECT(r.creates == 0 && r.modeChanges == 0 && r.createdMs < 0.0, "a new shot forgets the old");
    // Finish without a shot, Cancel
    Result none = t.Finish();
    EXPECT(none.createdMs < 0.0 && none.wirePressMs < 0.0, "nothing to finish");
    t.Begin(20.0);
    t.Cancel();
    EXPECT(!t.Active() && !t.Over(100.0), "cancelled");
    // a mode that was not neutral at the release: the attack is the first action mode
    Tracker w;
    w.HeroMode(0.0, 3);   // running
    w.Begin(1.0);
    w.HeroMode(1.1, 2);
    w.HeroMode(1.2, 7);
    w.HeroMode(1.7, 1);
    r = w.Finish();
    EXPECT(r.attackMode == 7 && Near(r.attackModeMs, 200.0, 0.1) && Near(r.neutralAgainMs, 700.0, 0.1), "walking is not the attack: %d at %.0f, neutral again %.0f", r.attackMode, r.attackModeMs, r.neutralAgainMs);
    EXPECT(IsActionMode(7) && IsActionMode(8) && IsActionMode(11) && !IsActionMode(1) && !IsActionMode(2) && !IsActionMode(3) && !IsActionMode(5), "which modes are acting");
}

// A row of the game's AnimData as it sits in memory: name[8], frames, speed, 144 flags (a synthetic one shaped like the Amazon's bow attack).
void MakeRecord(uint8_t* rec, const char* name, uint32_t frames, uint32_t speed, int flagAt, int flagVal) {
    std::memset(rec, 0, kAnimRecordSize);
    std::memcpy(rec, name, std::strlen(name) > 7 ? 7 : std::strlen(name));
    std::memcpy(rec + 8, &frames, 4);
    std::memcpy(rec + 12, &speed, 4);
    if (flagAt >= 0) rec[16 + flagAt] = (uint8_t)flagVal;
}

void TestAnimRecord() {
    std::printf("the animation record a unit points at\n");
    uint8_t rec[kAnimRecordSize];
    AnimInfo a;
    MakeRecord(rec, "AMA1BOW", 14, 256, 6, 2);
    EXPECT(ParseAnimRecord(rec, 0x1000, &a) && a.ok && !std::strcmp(a.name, "AMA1BOW") && a.frames == 14 && a.speed == 256 && a.flagIdx == 6 && a.flagVal == 2 && a.ptr == 0x1000,
           "a bow attack's row: %s %u %u flag %d at %d", a.name, a.frames, a.speed, a.flagVal, a.flagIdx);
    char text[160];
    AnimText(a, text, sizeof text);
    EXPECT(std::strstr(text, "AMA1BOW (14 frames, speed 256, the flag 2 at frame 6)"), "the text: %s", text);
    MakeRecord(rec, "AMNUBOW", 8, 128, -1, 0);
    EXPECT(ParseAnimRecord(rec, 1, &a) && a.flagIdx == -1, "a row with no flag set: -1");
    MakeRecord(rec, "AMA1BOW", 14, 256, 20, 2);   // a flag beyond the frames is not one
    EXPECT(ParseAnimRecord(rec, 1, &a) && a.flagIdx == -1, "a flag past the last frame does not count");
    MakeRecord(rec, "AMA1BOW", 0, 256, 6, 2);
    EXPECT(!ParseAnimRecord(rec, 1, &a) && !a.ok, "no frames: not a row");
    MakeRecord(rec, "AMA1BOW", 14, 0, 6, 2);
    EXPECT(!ParseAnimRecord(rec, 1, &a), "no speed: not a row");
    MakeRecord(rec, "AMA1BOW", 500, 256, 6, 2);
    EXPECT(!ParseAnimRecord(rec, 1, &a), "more frames than a row has: not a row");
    MakeRecord(rec, "AM\x01\x02OW", 14, 256, 6, 2);
    EXPECT(!ParseAnimRecord(rec, 1, &a), "a name of garbage: not a row");
    MakeRecord(rec, "AMA1BOW", 14, 256, 6, 2);
    rec[7] = 'X';
    EXPECT(!ParseAnimRecord(rec, 1, &a), "a name that does not end: not a row");
    AnimText(a, text, sizeof text);
    EXPECT(std::strstr(text, "not readable"), "said so");

    // in the line: one shared record, or two copies of the same row, or nothing readable
    auto shot = [&](const AnimInfo& client, const AnimInfo& server, char* line, size_t n) {
        Tracker t;
        t.Yaw(0.0, 0.0f);
        t.Begin(1.0);
        t.ClientAnim(client);
        t.ClientAnim(AnimInfo{});   // (only the first counts)
        t.Created(1.3, 11, 0, server);
        const Result r = t.Finish();
        Line(r, true, line, n);
        return r;
    };
    uint8_t r1[kAnimRecordSize], r2[kAnimRecordSize];
    AnimInfo c, s;
    MakeRecord(r1, "AMA1BOW", 14, 256, 6, 2);
    MakeRecord(r2, "AMA1BOW", 14, 256, 6, 2);
    ParseAnimRecord(r1, 0x5000, &c);
    ParseAnimRecord(r1, 0x5000, &s);
    char line[1200];
    shot(c, s, line, sizeof line);
    EXPECT(std::strstr(line, "anim record: client AMA1BOW") && std::strstr(line, "server AMA1BOW") && std::strstr(line, "ONE record, shared"), "shared: %s", line);
    ParseAnimRecord(r2, 0x9000, &s);
    shot(c, s, line, sizeof line);
    EXPECT(std::strstr(line, "the same row in ANOTHER copy") && !std::strstr(line, "ONE record"), "another copy: %s", line);
    shot(AnimInfo{}, s, line, sizeof line);
    EXPECT(std::strstr(line, "client not readable") && !std::strstr(line, "ONE record") && !std::strstr(line, "ANOTHER"), "unreadable client: %s", line);
    // the first client record of a shot is the one kept
    Tracker t2;
    t2.Begin(1.0);
    EXPECT(t2.NeedClientAnim(), "needs one");
    t2.ClientAnim(c);
    EXPECT(!t2.NeedClientAnim(), "has it");
    Tracker t3;
    EXPECT(!t3.NeedClientAnim(), "no shot, nothing needed");
}

void TestKind() {
    std::puts("a bow shot and a throw");
    char line[1200];
    Tracker t;
    t.Begin(1.0);
    Result r = t.Finish();
    EXPECT(!std::strcmp(r.kind, "bow"), "the default is a bow shot");
    Line(r, true, line, sizeof line);
    EXPECT(std::strncmp(line, "bow latency: release", 20) == 0, "the line: %s", line);
    t.Begin(2.0, "throw");
    t.Created(2.1, 44, 0x420);
    r = t.Finish();
    EXPECT(!std::strcmp(r.kind, "throw") && Near(r.createdMs, 100.0, 0.1) && r.createdClass == 44, "a throw: %s, created %.0f class %d", r.kind, r.createdMs, r.createdClass);
    Line(r, true, line, sizeof line);
    EXPECT(std::strncmp(line, "throw latency: release", 22) == 0 && std::strstr(line, "missile created 100 ms") && !std::strstr(line, "arrow"), "the line: %s", line);
    t.Begin(3.0, "a-very-long-kind-name");
    r = t.Finish();
    EXPECT(std::strlen(r.kind) <= 7, "a long label is cut, not overrun: %s", r.kind);
    t.Begin(4.0, nullptr);
    EXPECT(!std::strcmp(t.Finish().kind, "bow"), "no label: a bow shot");
}

}  // namespace

int main() {
    TestKind();
    TestAnimRecord();
    TestSplit();
    TestNotSeen();
    TestWrapAndOutside();
    TestLimits();
    if (g_fails) {
        std::printf("%d FAILED\n", g_fails);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
