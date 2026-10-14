// bow_latency.h - [debug] bow_latency: where the second between a bow's release and its arrow goes, told in ONE log line per shot. Pure (no game, no clock
// of its own; times are seconds on any monotonic base, the caller's); cleanroom/tests/bow_latency_test.cpp.
//
// A shot by the draw gesture is: the release (the draw's wire presses the game's attack) -> the press reaches the game's pad poll (a Stand Still held for it first)
// -> the hero's unit goes into its attack mode (the game starts the animation, turning him to the aim first) -> the animation reaches its action frame ->
// the server creates the arrow's missile -> the client shows it. Which of these takes the time was a guess; this watches them, read only:
//   - the wire's press (bowdrawwire), and the pad's A as the game gets it (down and up),
//   - the hero's unit mode (Diablo II's unit modes: 1 neutral, 7 and 8 the attacks, 11 the throw), every change of it,
//   - the hero's model heading: when it begins to turn and when it has turned (the body in F4 does not follow the head),
//   - the server's missile create for the hero (throw_born.cpp's passive look at CreateMissile) and the first missile the client shows that was not there at
//     the release (meleetouch::MissileScan).
// The line is: "bow latency: release->wire press 0 ms, ->pad A down 42 (up 136), ->attack mode 7 at 180 (neutral again 910), ->turn began 20, done 750
// (146 -> -50 deg), ->arrow created 810 (missile class 11, hero's; 2 creates), ->client shows a missile 830 (class 11)", the times in ms after the release.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace bowlatency {

inline constexpr double kWindowS = 2.5;      // how long a shot is watched
inline constexpr float kMoveDeg = 5.0f;      // the heading has begun to turn when it is this far from where it was at the release
inline constexpr float kSettleDeg = 8.0f;    // ... and has turned when it stays within this of where it ends
inline constexpr size_t kMaxModes = 8, kMaxCreates = 6, kMaxYaws = 256;
inline constexpr double kNone = -1.0;

// Diablo II's unit modes: 7 and 8 the attacks, 10 a cast, 11 a throw, 12 a kick, 13 - 16 the specials: the hero is acting.
inline bool IsActionMode(int m) { return m == 7 || m == 8 || (m >= 10 && m <= 16); }

inline float WrapDeg(float d) {
    if (!std::isfinite(d)) return 0.0f;
    d = std::fmod(d, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

// The animation record a unit points at (its +0x70): the game's AnimData.D2 row, read in place - name[8] ("AMA1BOW" = Amazon, attack 1, bow), the frames of one
// direction, the speed (256 = one frame a game tick, 25 a second) and 144 frame flags, the one that is set marking the frame the game acts on (the arrow's release).
// Read only: it tells which record the hero's attack really uses, and whether the client's unit and the server's share one copy of it.
inline constexpr size_t kAnimRecordSize = 160;
struct AnimInfo {
    bool ok = false;
    char name[9] = {};
    uint32_t frames = 0, speed = 0;
    int flagIdx = -1, flagVal = 0;   // the first frame with a flag set, and the flag
    uint64_t ptr = 0;                // where the record lives (for 'same record?')
};
inline bool ParseAnimRecord(const uint8_t* rec, uint64_t ptr, AnimInfo* out) {
    AnimInfo a;
    a.ptr = ptr;
    for (int i = 0; i < 7; ++i) {
        const uint8_t c = rec[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) { *out = a; return false; }
        a.name[i] = (char)c;
    }
    if (rec[7] != 0) { *out = a; return false; }
    a.frames = (uint32_t)rec[8] | ((uint32_t)rec[9] << 8) | ((uint32_t)rec[10] << 16) | ((uint32_t)rec[11] << 24);
    a.speed = (uint32_t)rec[12] | ((uint32_t)rec[13] << 8) | ((uint32_t)rec[14] << 16) | ((uint32_t)rec[15] << 24);
    if (a.frames < 1 || a.frames > 144 || a.speed < 1 || a.speed > 4096) { *out = a; return false; }
    for (uint32_t i = 0; i < a.frames; ++i)
        if (rec[16 + i]) { a.flagIdx = (int)i; a.flagVal = rec[16 + i]; break; }
    a.ok = true;
    *out = a;
    return true;
}
inline size_t AnimText(const AnimInfo& a, char* out, size_t n) {
    if (!a.ok) return (size_t)snprintf(out, n, "not readable");
    return (size_t)snprintf(out, n, "%s (%u frames, speed %u, the flag %d at frame %d)", a.name, a.frames, a.speed, a.flagVal, a.flagIdx);
}

struct ModeChange { double t; int from, to; };
struct Create { double t; int cls; uint32_t flags; AnimInfo anim; };

// What a finished shot comes to, ms after the release (negative = not seen).
struct Result {
    double wirePressMs = kNone, padDownMs = kNone, padUpMs = kNone;
    double attackModeMs = kNone, neutralAgainMs = kNone;
    int attackMode = 0;
    double turnBeganMs = kNone, turnDoneMs = kNone;
    float yawFrom = 0.0f, yawTo = 0.0f;
    double createdMs = kNone;
    int createdClass = 0;
    size_t creates = 0;
    double clientMs = kNone;
    int clientClass = 0;
    size_t modeChanges = 0;
    AnimInfo clientAnim, serverAnim;   // the animation record the hero's unit points at on the client (when the attack began) and on the server (at the create)
};

class Tracker {
public:
    // Always (every tick, shot or not): the heading is kept so that a release has the heading it began from.
    void Yaw(double t, float deg) {
        if (!std::isfinite(deg)) return;
        if (!active_) { baseYaw_ = deg; haveBase_ = true; return; }
        if (nYaws_ < kMaxYaws) { yawT_[nYaws_] = t; yawDeg_[nYaws_] = deg; ++nYaws_; }
    }
    // Always: the hero's mode, so that a release has the mode it began from.
    void HeroMode(double t, int mode) {
        if (mode < 0) return;
        if (!active_) { lastMode_ = mode; return; }
        if (mode != lastMode_ && nModes_ < kMaxModes) { modes_[nModes_++] = {t, lastMode_, mode}; }
        lastMode_ = mode;
    }

    // A shot is released at t: the clock starts. A release inside a watched shot ends that one first (the caller asked Done/Take before).
    void Begin(double t) {
        active_ = true;
        t0_ = t;
        wirePress_ = padDown_ = padUp_ = clientAt_ = kNone;
        clientClass_ = 0;
        clientAnim_ = AnimInfo{};
        clientAnimTried_ = false;
        nModes_ = nCreates_ = nYaws_ = 0;
        if (haveBase_) { yawT_[0] = t; yawDeg_[0] = baseYaw_; nYaws_ = 1; }
    }
    void WirePress(double t) { if (active_ && wirePress_ < 0.0) wirePress_ = t; }
    void PadA(double t, bool down) {
        if (!active_) return;
        if (down && padDown_ < 0.0) padDown_ = t;
        else if (!down && padDown_ >= 0.0 && padUp_ < 0.0) padUp_ = t;
    }
    void Created(double t, int cls, uint32_t flags, const AnimInfo& anim = AnimInfo{}) {
        if (active_ && nCreates_ < kMaxCreates) creates_[nCreates_++] = {t, cls, flags, anim};
    }
    // The client's unit record, once the attack mode is seen (the first call in a shot counts).
    void ClientAnim(const AnimInfo& a) { if (active_ && !clientAnimTried_) { clientAnimTried_ = true; clientAnim_ = a; } }
    bool NeedClientAnim() const { return active_ && !clientAnimTried_; }
    void ClientMissile(double t, int cls) {
        if (active_ && clientAt_ < 0.0) { clientAt_ = t; clientClass_ = cls; }
    }

    bool Active() const { return active_; }
    double Started() const { return t0_; }
    // The watch is over at `now`: time to tell it.
    bool Over(double now) const { return active_ && now - t0_ >= kWindowS; }
    void Cancel() { active_ = false; }

    Result Finish() {
        Result r;
        if (!active_) return r;
        active_ = false;
        auto ms = [&](double t) { return t < 0.0 ? kNone : (t - t0_) * 1000.0; };
        r.wirePressMs = ms(wirePress_);
        r.padDownMs = ms(padDown_);
        r.padUpMs = ms(padUp_);
        r.modeChanges = nModes_;
        for (size_t i = 0; i < nModes_; ++i) {
            const ModeChange& m = modes_[i];
            if (r.attackModeMs < 0.0 && IsActionMode(m.to)) { r.attackModeMs = ms(m.t); r.attackMode = m.to; }
            else if (r.attackModeMs >= 0.0 && r.neutralAgainMs < 0.0 && m.to == 1) r.neutralAgainMs = ms(m.t);
        }
        r.creates = nCreates_;
        if (nCreates_) { r.createdMs = ms(creates_[0].t); r.createdClass = creates_[0].cls; r.serverAnim = creates_[0].anim; }
        r.clientAnim = clientAnim_;
        r.clientMs = ms(clientAt_);
        r.clientClass = clientClass_;
        // the heading: from where it was at the release to where it ended
        if (nYaws_ >= 2) {
            const float from = yawDeg_[0], to = yawDeg_[nYaws_ - 1];
            r.yawFrom = from;
            r.yawTo = to;
            for (size_t i = 1; i < nYaws_; ++i)
                if (std::fabs(WrapDeg(yawDeg_[i] - from)) >= kMoveDeg) { r.turnBeganMs = ms(yawT_[i]); break; }
            if (r.turnBeganMs >= 0.0) {
                size_t last = 0;   // the last sample still away from where it ends
                for (size_t i = 0; i < nYaws_; ++i) if (std::fabs(WrapDeg(yawDeg_[i] - to)) > kSettleDeg) last = i;
                r.turnDoneMs = last + 1 < nYaws_ ? ms(yawT_[last + 1]) : kNone;
            }
        }
        return r;
    }

private:
    bool active_ = false, haveBase_ = false;
    double t0_ = 0.0, wirePress_ = kNone, padDown_ = kNone, padUp_ = kNone, clientAt_ = kNone;
    int clientClass_ = 0, lastMode_ = -1;
    AnimInfo clientAnim_;
    bool clientAnimTried_ = false;
    float baseYaw_ = 0.0f;
    ModeChange modes_[kMaxModes];
    Create creates_[kMaxCreates];
    double yawT_[kMaxYaws];
    float yawDeg_[kMaxYaws];
    size_t nModes_ = 0, nCreates_ = 0, nYaws_ = 0;
};

// The one line. `hook` says whether the server's create is being watched at all (else "not watched" rather than "none").
inline size_t Line(const Result& r, bool hook, char* out, size_t n) {
    char b[1100];
    int k = snprintf(b, sizeof b, "bow latency: release");
    auto add = [&](const char* fmt, auto... a) { if (k > 0 && (size_t)k < sizeof b) k += snprintf(b + k, sizeof b - (size_t)k, fmt, a...); };
    auto when = [&](const char* what, double ms) {
        if (ms < 0.0) add(", ->%s not seen", what); else add(", ->%s %.0f ms", what, ms);
    };
    when("wire press", r.wirePressMs);
    if (r.padDownMs < 0.0) add(", ->pad A down not seen");
    else if (r.padUpMs < 0.0) add(", ->pad A down %.0f ms", r.padDownMs);
    else add(", ->pad A down %.0f ms (up %.0f)", r.padDownMs, r.padUpMs);
    if (r.attackModeMs < 0.0) add(", ->attack mode not seen");
    else if (r.neutralAgainMs < 0.0) add(", ->attack mode %d at %.0f ms", r.attackMode, r.attackModeMs);
    else add(", ->attack mode %d at %.0f ms (neutral again %.0f)", r.attackMode, r.attackModeMs, r.neutralAgainMs);
    if (r.turnBeganMs < 0.0) add(", ->no turn of the body (%.0f -> %.0f deg)", r.yawFrom, r.yawTo);
    else if (r.turnDoneMs < 0.0) add(", ->body turn began %.0f ms (%.0f -> %.0f deg), not done", r.turnBeganMs, r.yawFrom, r.yawTo);
    else add(", ->body turn began %.0f ms, done %.0f ms (%.0f -> %.0f deg)", r.turnBeganMs, r.turnDoneMs, r.yawFrom, r.yawTo);
    if (r.createdMs < 0.0) add(hook ? ", ->arrow created: none by the hero" : ", ->arrow created: create not watched");
    else add(", ->arrow created %.0f ms (missile class %d, the hero's; %u create%s)", r.createdMs, r.createdClass, (unsigned)r.creates, r.creates == 1 ? "" : "s");
    if (r.clientMs < 0.0) add(", ->client shows a missile: none");
    else add(", ->client shows a missile %.0f ms (class %d)", r.clientMs, r.clientClass);
    {   // the animation record the attack runs on, from the client's unit and from the server's (the owner of the create)
        char c[120], s[120];
        AnimText(r.clientAnim, c, sizeof c);
        AnimText(r.serverAnim, s, sizeof s);
        const bool same = r.clientAnim.ok && r.serverAnim.ok && r.clientAnim.ptr == r.serverAnim.ptr;
        const bool alike = r.clientAnim.ok && r.serverAnim.ok && !same && !std::strcmp(r.clientAnim.name, r.serverAnim.name);
        add("; anim record: client %s, server %s%s", c, s, same ? " - ONE record, shared" : alike ? " - the same row in ANOTHER copy" : "");
    }
    if (k < 0) k = 0;
    if (n) { snprintf(out, n, "%s", b); }
    return (size_t)k;
}

}  // namespace bowlatency
