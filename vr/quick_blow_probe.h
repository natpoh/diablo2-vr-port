// quick_blow_probe.h - [debug] quick_blow_probe: one log line per attack of OUR hero on our server - what became of its blow. View only: nothing
// is changed in the game, the hooks only note. Pure: no game, no Windows, no clock (cleanroom/tests/quick_blow_probe_test.cpp feeds it).
//
// Why (Serg's Amazon, 0.206.5, 2026-11-07): 32 touches pressed the game's attack, quick blow moved each blow to the first frame - and the server's
// frame trigger never came with the action (no "blow is out", no "counted in reach": no roll at all). Yesterday the same hooks saw every blow.
// The line tells, per attack:
//   - the scheduler (0x491240): the mode it scheduled, whether the blow was moved (by how many frames), the hero's pending MODECHANGE event frame
//     (event type 0, read with 0x48B230) and the animation's end (type 1) right after it - where the moved blow sits;
//   - every frame trigger (0x42A940) the hero got in the attack: its code and frame (1 / 2 the blow; our attack path's step is 0x7A);
//   - every time our code re-armed the hero's MODECHANGE event (the attack path's start and step, 0x48B720) - with what was pending before;
//   - the attack-mode gate (0x42D840) asked for another mode in the attack (a walk): the game's answer and ours;
//   - the scheduler called for another mode before the end (the attack replaced);
//   - the verdict: BLOW FIRED at frame N, or "quick blow LOST" with the reason (replaced by X at frame N / the mode changed / the animation ended).
// 0.206.7 found with it: "MODECHANGE pending for 0" = the scheduler queued NO event - the blow had been moved below the attack's start frame
// (vr/quick_blow.h); such a line now says "NO MODECHANGE event queued by the scheduler".
#pragma once

#include <cstdint>
#include <cstdio>

namespace qbprobe {

constexpr int kTriggers = 8, kRearms = 6, kGates = 4;
constexpr int kTickCode = 0x7A;   // vr/attack_path.h: the path's own MODECHANGE code
constexpr int kNoAction = -1000;  // = quickblow::kNoAction: no action in the record, nothing moved
constexpr int kNotJudged = -2000; // quick blow did not judge the attack (the game's own frames)

enum class Who : uint8_t { PathStart = 1, PathStep = 2 };
inline const char* WhoText(Who w) { return w == Who::PathStart ? "the attack path's start" : w == Who::PathStep ? "the attack path's step" : "?"; }

struct Trig { int code = 0; int32_t frame = 0; uint32_t mode = 0; };
struct Rearm { Who who = Who::PathStep; int32_t frame = 0, pendingBefore = 0, to = 0, pendingAfter = 0; };
struct Gate { uint32_t asked = 0; int32_t frame = 0; int game = 0, ours = 0; };

struct Attack {
    bool active = false;
    uint32_t number = 0;       // our count of the hero's attacks this session
    uint32_t mode = 0;
    uint32_t weapon = 0;       // D2RVR_TYPE_*
    int shift = kNotJudged;    // quick blow's: the frames the blow moved (> 0 earlier, < 0 later, 0 on the start frame already), kNoAction, kNotJudged
    int animStart = -1;        // the attack's start frame (the game's 0x34B760) quick blow read
    int32_t start = 0;         // the server frame the scheduler ran on
    int32_t pending = 0;       // the hero's MODECHANGE event frame right after the scheduler (where the first event - the moved blow - sits)
    int32_t end = 0;           // the animation's end (ENDANIM) right after the scheduler
    uint64_t startMs = 0;
    Trig trig[kTriggers];
    int nTrig = 0, moreTrig = 0;
    Rearm re[kRearms];
    int nRe = 0, moreRe = 0;
    Gate gate[kGates];
    int nGate = 0, moreGate = 0;
    bool blow = false;
    int32_t blowFrame = 0;
    bool replacedByMode = false;   // the scheduler ran for another mode before the end
    uint32_t newMode = 0;
    int32_t newModeAt = 0;
    bool told = false;
};

enum class Verdict : uint8_t { Fired, Replaced, ModeChanged, Ended, NoBlowInRecord, Unknown };

// What became of the blow. Replaced: no blow, and a re-arm of ours came on or before the frame the blow was pending for (the line shows what was
// pending before and after it, to tell a replaced event from one added beside it).
inline Verdict Judge(const Attack& a, int* rearmIdx = nullptr) {
    if (a.blow) return Verdict::Fired;
    if (a.shift == kNoAction) return Verdict::NoBlowInRecord;
    for (int i = 0; i < a.nRe; ++i) {
        const Rearm& r = a.re[i];
        if (r.frame <= a.pending) {
            if (rearmIdx) *rearmIdx = i;
            return Verdict::Replaced;
        }
    }
    if (a.replacedByMode) return Verdict::ModeChanged;
    for (int i = 0; i < a.nTrig; ++i)
        if (a.trig[i].mode != a.mode) return Verdict::ModeChanged;
    return a.nTrig || a.nRe || a.nGate ? Verdict::Unknown : Verdict::Ended;
}

class Recorder {
public:
    // The scheduler ran for the hero. An attack mode starts a new record (the last one handed back in *done if it was not told yet); another
    // mode during an attack ends it (handed back). True when *done holds a record to tell.
    bool Scheduled(uint32_t mode, bool attackMode, uint32_t weapon, int shift, int animStart, int32_t now, int32_t pending, int32_t end, uint64_t nowMs,
                   Attack* done) {
        bool give = false;
        if (a_.active && !a_.told) {
            if (!attackMode) { a_.replacedByMode = now < a_.end; a_.newMode = mode; a_.newModeAt = now; }
            *done = a_;
            a_.told = true;
            give = true;
        }
        a_.active = false;
        if (attackMode) {
            const uint32_t n = a_.number + 1;
            a_ = Attack{};
            a_.active = true;
            a_.number = n;
            a_.mode = mode;
            a_.weapon = weapon;
            a_.shift = shift;
            a_.animStart = animStart;
            a_.start = now;
            a_.pending = pending;
            a_.end = end;
            a_.startMs = nowMs;
        }
        return give;
    }
    void Triggered(int code, int32_t frame, uint32_t mode, bool action) {
        if (!a_.active) return;
        if (a_.nTrig < kTriggers) a_.trig[a_.nTrig++] = {code, frame, mode};
        else ++a_.moreTrig;
        if (action && !a_.blow) { a_.blow = true; a_.blowFrame = frame; }
    }
    void Rearmed(Who who, int32_t frame, int32_t pendingBefore, int32_t to, int32_t pendingAfter) {
        if (!a_.active) return;
        if (a_.nRe < kRearms) a_.re[a_.nRe++] = {who, frame, pendingBefore, to, pendingAfter};
        else ++a_.moreRe;
    }
    void GateAsked(uint32_t asked, int32_t frame, int game, int ours) {
        if (!a_.active) return;
        if (a_.nGate < kGates) a_.gate[a_.nGate++] = {asked, frame, game, ours};
        else ++a_.moreGate;
    }
    // Past its end (a server frame of a hook, or our clock: frames x 40 ms + slack): handed back once.
    bool DueByFrame(int32_t now, Attack* done) { return Due(a_.active && !a_.told && now > a_.end, done); }
    bool DueByClock(uint64_t nowMs, uint64_t slackMs, Attack* done) {
        const int32_t frames = a_.end > a_.start ? a_.end - a_.start : 0;
        return Due(a_.active && !a_.told && nowMs > a_.startMs + (uint64_t)frames * 40u + slackMs, done);
    }
    const Attack& Now() const { return a_; }

private:
    bool Due(bool due, Attack* done) {
        if (!due) return false;
        *done = a_;
        a_.told = true;
        a_.active = false;
        return true;
    }
    Attack a_;
};

// The line. Returns the length written (snprintf's, clamped).
inline int Describe(const Attack& a, char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    size_t n = 0;
    auto put = [&](const char* fmt, auto... v) {
        if (n >= cap) return;
        const int w = std::snprintf(out + n, cap - n, fmt, v...);
        if (w > 0) n += (size_t)w < cap - n ? (size_t)w : cap - n - 1;
    };
    int ri = -1;
    const Verdict v = Judge(a, &ri);
    switch (v) {
    case Verdict::Fired: put("quick blow probe %u: BLOW FIRED at frame %d (%+d)", a.number, (int)a.blowFrame, (int)(a.blowFrame - a.start)); break;
    case Verdict::Replaced:
        put("quick blow probe %u: quick blow LOST - re-armed by %s at frame %d before the blow's frame %d (pending %d, set %d, pending after %d)", a.number,
            WhoText(a.re[ri].who), (int)a.re[ri].frame, (int)a.pending, (int)a.re[ri].pendingBefore, (int)a.re[ri].to, (int)a.re[ri].pendingAfter);
        break;
    case Verdict::ModeChanged:
        if (a.replacedByMode) put("quick blow probe %u: quick blow LOST - the mode changed to %u at frame %d (the scheduler ran for it)", a.number, a.newMode, (int)a.newModeAt);
        else put("quick blow probe %u: quick blow LOST - the mode changed (a trigger came in another mode)", a.number);
        break;
    case Verdict::Ended: put("quick blow probe %u: quick blow LOST - the animation ended with no trigger at all", a.number); break;
    case Verdict::NoBlowInRecord: put("quick blow probe %u: no blow in this animation's record", a.number); break;
    case Verdict::Unknown: put("quick blow probe %u: quick blow LOST - triggers came, none was the blow", a.number); break;
    }
    put(" | mode %u weapon kind %u, frames %d..%d, ", a.mode, a.weapon, (int)a.start, (int)a.end);
    if (a.shift == kNotJudged) put("the game's own frames");
    else if (a.shift == kNoAction) put("no action in the record (start frame %d)", a.animStart);
    else if (a.shift == 0) put("blow on the start frame %d already", a.animStart);
    else put("blow moved from frame %d to the start frame %d", a.animStart + a.shift, a.animStart);
    if (a.pending > 0) put(", MODECHANGE pending for %d (%+d) after the scheduler", (int)a.pending, (int)(a.pending - a.start));
    else put(", NO MODECHANGE event queued by the scheduler");
    put(" | triggers:");
    if (!a.nTrig) put(" none");
    for (int i = 0; i < a.nTrig; ++i) put(" 0x%X@%+d%s", a.trig[i].code, (int)(a.trig[i].frame - a.start), a.trig[i].mode != a.mode ? "(other mode)" : "");
    if (a.moreTrig) put(" +%d", a.moreTrig);
    put(" | our re-arms:");
    if (!a.nRe) put(" none");
    for (int i = 0; i < a.nRe; ++i)
        put(" %s@%+d (pending %+d, set %+d, after %+d)", a.re[i].who == Who::PathStart ? "path start" : "path step", (int)(a.re[i].frame - a.start),
            (int)(a.re[i].pendingBefore - a.start), (int)(a.re[i].to - a.start), (int)(a.re[i].pendingAfter - a.start));
    if (a.moreRe) put(" +%d", a.moreRe);
    put(" | gate:");
    if (!a.nGate) put(" none");
    for (int i = 0; i < a.nGate; ++i) put(" mode %u asked @%+d: game %d, ours %d", a.gate[i].asked, (int)(a.gate[i].frame - a.start), a.gate[i].game, a.gate[i].ours);
    if (a.moreGate) put(" +%d", a.moreGate);
    if (a.replacedByMode) put(" | then mode %u @%+d", a.newMode, (int)(a.newModeAt - a.start));
    return (int)n;
}

}  // namespace qbprobe
