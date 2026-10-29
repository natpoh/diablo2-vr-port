// walk_latency.h - [debug] walk_start_log: where the time goes between BodyWalk's stick leaving rest and the hero walking, told in ONE log line
// per walk start. Pure (no game, no clock of its own; times are seconds on any monotonic base, the caller's); cleanroom/tests/walk_latency_test.cpp.
//
// A walk in place is: the trackers' steps -> BodyWalk's step averages and speed curve -> its pad report's left stick -> the plugin's read of
// it (vr/walk_path.h; [input] bodywalk_walk_lift may lengthen it) -> the game's walking code takes the stick from its own call
// (HookStickGet, the walk's 0x14C642) once it is past the dead zone (10349 of 32767) -> the hero's position moves. BodyWalk's side has a line
// of its own (its Debug tab, "Walk Start Timing": "STEP START: first step ... past a game's dead zone ..."); this is the plugin's side,
// fed from the walk's stick call, every game frame in the world:
//   - when BodyWalk's stick left rest (longer than kStir after kRestS at rest) - the moment BodyWalk first answered the steps,
//   - when it passed the game's dead zone on its own (what the game would have walked by without a lift),
//   - when the game's walk was first given a vector (with the lift on this can be earlier than the line above),
//   - when the hero's position first moved kMovedUnits away from where he stood.
// The line: "walk start (BodyWalk): stick stirred 0.04 at 17:31:14.250 -> past the game's dead zone +640 ms (on its own), the game's walk
// given a vector +310 ms (0.32, lifted), the hero moved +360 ms; peak before the dead zone 0.30". A stir that never got the hero going is told
// only when it lasted kLongStirS (a walk that never started), at most one such line in kQuietEveryS.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdio>

namespace walklat {

inline constexpr float kStir = 0.04f;         // BodyWalk's stick this long has left rest (its standing noise is ~0.02 - 0.06)
inline constexpr double kRestS = 0.5;         // ... after at least this long under it
inline constexpr double kWindowS = 3.0;       // how long a start is watched
inline constexpr float kMovedUnits = 0.05f;   // the hero has moved (the game's world units: the camera's look-at)
inline constexpr float kDeadZone = 10349.0f / 32767.0f;
inline constexpr double kLongStirS = 1.0;     // a stir this long that never walked is told ...
inline constexpr double kQuietEveryS = 10.0;  // ... at most once in this
inline constexpr double kNone = -1.0;

struct Sample {
    double t = 0.0;
    float bw = 0.0f;          // BodyWalk's own stick, as its report has it (before any lift), 0..1
    bool ownPushed = false;   // the controller's (or the room walk's) stick is past the dead zone: not BodyWalk's walk
    bool given = false;       // the game's walk got a vector from the call
    float givenLen = 0.0f;
    bool lifted = false;      // [input] bodywalk_walk_lift lengthened BodyWalk's stick for it
    bool heroOk = false;
    float heroX = 0.0f, heroZ = 0.0f;
};

struct Result {
    float stirLen = 0.0f;
    double dz = kNone, given = kNone, moved = kNone;   // ms after the stir
    float givenLen = 0.0f, peakBeforeDz = 0.0f;
    bool lifted = false, ownPushed = false;
    double spanMs = 0.0;
    enum class End { Moved, Window, BackToRest } end = End::Moved;
};

enum class Event { None, Started, Done };

class Watcher {
public:
    // One call per walk-stick call. Started: a walk start began now (the caller notes the wall time for the line). Done: *out is ready.
    Event Feed(const Sample& s, Result* out) {
        if (!watching_) {
            if (s.bw < kStir && !s.given) {
                if (restSince_ < 0.0) restSince_ = s.t;
                return Event::None;
            }
            const bool rested = restSince_ >= 0.0 && s.t - restSince_ >= kRestS;
            restSince_ = -1.0;
            if (!rested || s.ownPushed || s.bw < kStir) return Event::None;
            watching_ = true;
            t0_ = s.t;
            r_ = Result{};
            r_.stirLen = s.bw;
            heroOk0_ = s.heroOk;
            hx0_ = s.heroX; hz0_ = s.heroZ;
            lowSince_ = -1.0;
            Track(s);
            return Event::Started;
        }
        Track(s);
        const double ms = (s.t - t0_) * 1000.0;
        if (s.bw < kStir && !s.given) { if (lowSince_ < 0.0) lowSince_ = s.t; }
        else lowSince_ = -1.0;
        // done once the hero moved and the stick has passed the dead zone on its own too (with a lift the hero goes first: the line tells
        // both), or the window ran out, or BodyWalk's stick went back to rest
        const bool back = lowSince_ >= 0.0 && s.t - lowSince_ >= 0.3;
        if (!(r_.moved >= 0.0 && r_.dz >= 0.0) && s.t - t0_ < kWindowS && !back) return Event::None;
        const Result::End end = r_.moved >= 0.0 ? Result::End::Moved : back ? Result::End::BackToRest : Result::End::Window;
        watching_ = false;
        restSince_ = end == Result::End::BackToRest ? lowSince_ : -1.0;
        r_.spanMs = ms;
        r_.end = end;
        // a stir that never got the hero going: only when it was long, and then rarely
        if (end != Result::End::Moved && r_.given < 0.0) {
            if (ms < kLongStirS * 1000.0 || (lastQuiet_ >= 0.0 && s.t - lastQuiet_ < kQuietEveryS)) return Event::None;
            lastQuiet_ = s.t;
        }
        *out = r_;
        return Event::Done;
    }
    bool Watching() const { return watching_; }

private:
    void Track(const Sample& s) {
        const double ms = (s.t - t0_) * 1000.0;
        if (s.ownPushed) r_.ownPushed = true;
        if (r_.dz < 0.0) {
            if (s.bw > kDeadZone) r_.dz = ms;
            else if (s.bw > r_.peakBeforeDz) r_.peakBeforeDz = s.bw;
        }
        if (r_.given < 0.0 && s.given) { r_.given = ms; r_.givenLen = s.givenLen; r_.lifted = s.lifted; }
        if (r_.moved < 0.0 && s.heroOk) {
            if (!heroOk0_) { heroOk0_ = true; hx0_ = s.heroX; hz0_ = s.heroZ; }
            const float dx = s.heroX - hx0_, dz = s.heroZ - hz0_;
            if (dx * dx + dz * dz > kMovedUnits * kMovedUnits) r_.moved = ms;
        }
    }

    bool watching_ = false;
    double restSince_ = -1.0, t0_ = 0.0, lowSince_ = -1.0, lastQuiet_ = -1.0;
    bool heroOk0_ = false;
    float hx0_ = 0.0f, hz0_ = 0.0f;
    Result r_;
};

// The line, after "walk start (BodyWalk): stick stirred 0.04 at HH:MM:SS.mmm" (the caller's part, with the wall time of the start).
inline size_t Text(const Result& r, char* out, size_t n) {
    auto at = [](double v, char* o, size_t m) { if (v < 0.0) snprintf(o, m, "never"); else snprintf(o, m, "+%.0f ms", v); };
    char dz[24], given[24], moved[24];
    at(r.dz, dz, sizeof dz);
    at(r.given, given, sizeof given);
    at(r.moved, moved, sizeof moved);
    char givenLen[48] = "";
    if (r.given >= 0.0) snprintf(givenLen, sizeof givenLen, " (%.2f%s)", r.givenLen, r.lifted ? ", lifted" : "");
    const char* end = r.end == Result::End::Moved ? "" : r.end == Result::End::Window ? "; watched 3 s, the hero never moved" : "; BodyWalk's stick back at rest";
    return (size_t)snprintf(out, n,
                            " -> past the game's dead zone %s (on its own), the game's walk given a vector %s%s, the hero moved %s; "
                            "peak before the dead zone %.2f%s%s",
                            dz, given, givenLen, moved, r.peakBeforeDz, r.ownPushed ? "; the controller's stick was pushed too" : "", end);
}

}  // namespace walklat
