// walk_path.h - how BodyWalk's walking reaches the game through the plugin ([openxr] direct_bodywalk_walk). Pure: no game, no XInput,
// no clock of its own; the unit test is cleanroom/tests/walk_path_test.cpp, the wiring is vr/vrcam.cpp (PadMirrorRead) and vr/link.cpp.
//
// The path. BodyWalk turns the body's motion (steps in place, an arm swing, a lean) into the left stick of the gamepad it presents to the
// game: written to the pad report Local\BodyWalkVR_PadMirror (and, where it can, to a virtual pad). The plugin reads that report as pad 0
// in its XInput hook; the game takes the left stick for its walk through its own code (0xFEFE0 -> the player's command 0x42, "walk in
// this direction"), so the stick, the stop (the stick let go), the run threshold and the dead zone are the game's. In the plugin's native
// mode ([openxr] direct_input=1, F4) the plugin is the pad itself, and BodyWalk's report was ignored whole, and BodyWalk was not even given
// the headset and the controllers it walks by (link::PublishXr, xr.cpp's presenter request): its walking could not reach the game at all.
// With [openxr] direct_bodywalk_walk=1 (the default) BodyWalk gets the poses again (controls zeroed) and ITS LEFT STICK - nothing else of
// its pad - is mixed into the controller's left stick here:
//   - each stick counts only past the game's own dead zone (10349 of 32767: a drifting thumbstick does not skew a walk);
//   - one alone is itself; both at once: the sum clamped to the unit disc, but never weaker than the stronger of the two (pulling
//     against each other cannot cancel a walk: the stronger wins), and the same push twice (within ~25 degrees: BodyWalk may mirror the
//     thumbstick as well as walk) is the stronger of the two, not their sum;
//   - a panel or menu open, or no game running: the stick is the menu's, BodyWalk's walk is dropped;
//   - the turn into the game's frame (the camera's yaw) comes after, for the mixed stick, as for any stick: one rotation, so
//     BodyWalk's direction means what the controller's means.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace walkpath {

constexpr float kDeadZone = 10349.0f / 32767.0f;   // the game's own (0x1210DA0), no rescale
constexpr float kSameWayCos = 0.90f;                // two pushes within ~26 degrees are one push

struct Stick { float x = 0.0f, y = 0.0f; };         // x right, y ahead, the unit disc

inline float Len(Stick s) { return std::sqrt(s.x * s.x + s.y * s.y); }
inline Stick FromPad(int16_t lx, int16_t ly) {
    return {std::clamp(lx / 32767.0f, -1.0f, 1.0f), std::clamp(ly / 32767.0f, -1.0f, 1.0f)};
}
inline int16_t Axis(float v) { return (int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f); }
// The game's own test of a stick (raw units, as vrcam's BodyWalkWalk has it): pushed = past the dead zone.
inline bool PushedRaw(int lx, int ly) { return (long long)lx * lx + (long long)ly * ly > 10349ll * 10349ll; }
inline bool Pushed(Stick s) { return PushedRaw((int)Axis(s.x), (int)Axis(s.y)); }

// -- the mix ----------------------------------------------------------------------------------------------------------------
enum class How : uint8_t {
    Neither,            // neither is past the dead zone: the controller's stick as it is
    OwnAlone,           // BodyWalk's stick is at rest
    BodyWalkAlone,      // the controller's stick is at rest
    Sum,                // both: their sum, on the unit disc
    OwnStronger,        // both, pulling apart: the controller's is the stronger and stays
    BodyWalkStronger,   // ... BodyWalk's is
    SameWay,            // both the same way: the stronger of the two (not doubled)
};
struct Mixed { Stick out; How how; };

inline Mixed Combine(Stick own, Stick bw) {
    const bool o = Pushed(own), b = Pushed(bw);
    if (!b) return {own, o ? How::OwnAlone : How::Neither};
    if (!o) return {bw, How::BodyWalkAlone};
    const float la = Len(own), lb = Len(bw);
    const bool ownStronger = la >= lb;
    const Stick strong = ownStronger ? own : bw;
    const float ls = ownStronger ? la : lb;
    if (own.x * bw.x + own.y * bw.y >= kSameWayCos * la * lb) return {strong, How::SameWay};
    Stick s{own.x + bw.x, own.y + bw.y};
    float l = Len(s);
    if (l > 1.0f) { s.x /= l; s.y /= l; l = 1.0f; }
    if (l < ls) return {strong, ownStronger ? How::OwnStronger : How::BodyWalkStronger};
    return {s, How::Sum};
}

// -- which stick the game's pad poll gets ----------------------------------------------------------------------------------
// What the plugin knows when it builds the report the game's XInput hook and walking hooks read (vrcam's PadMirrorRead).
struct Inputs {
    bool directFeeding = false;   // [openxr] direct_input, F4, headset frames arriving: the plugin's own pad is the pad
    bool settingOn = true;        // [openxr] direct_bodywalk_walk
    bool inWorld = true;          // a game is running (not the title or character screens)
    bool menuOpen = false;        // a panel or menu is open: the pad is the game's menu pad
    bool bodywalkPadOn = true;    // [input] bodywalk_pad (only for the old, non-direct path)
    bool reportLive = false;      // BodyWalk's pad report moved in the last second and is a version we know
    int16_t ownLX = 0, ownLY = 0; // the plugin's own pad's left stick
    int16_t bwLX = 0, bwLY = 0;   // BodyWalk's report's left stick
};

// What the log says (one line per change, vr/vrcam.cpp NoteWalk).
enum class State : uint8_t {
    NotDirect,        // direct mode is not feeding: BodyWalk's pad is the pad as it always was (or none) - nothing to say
    Off,              // direct mode, [openxr] direct_bodywalk_walk=0: BodyWalk's walk IGNORED
    NoBodyWalk,       // direct mode, its pad report is not moving: ignored (BodyWalk not running, or it does not write one)
    Menu,             // a panel is open: BodyWalk's walk DROPPED (the stick is the menu's)
    NoGame,           // no game running: dropped
    Live,             // BodyWalk's pad is live, its stick at rest, and so is the controller's
    OwnAlone,
    BodyWalkAlone,    // BodyWalk's stick walks, the controller's is at rest
    Sum,
    OwnStronger,
    BodyWalkStronger,
    SameWay,
    FlatOwn,          // VR F1-F3 without BodyWalk ([direct] walk_flat_views, vr/walk_flat.h): the controller's left stick walks, nothing else of the pad
};

enum class Source : uint8_t {
    None,           // no pad of BodyWalk's, no plugin pad: the real XInput's answer stays
    BodyWalkPad,    // not direct: BodyWalk's report is the pad, whole (as before direct input existed)
    OwnPad,         // direct: the plugin's own pad, BodyWalk's walk not in it
    Mixed,          // direct: the plugin's own pad with the left stick of both
};

struct Out {
    Source source = Source::None;
    int16_t lx = 0, ly = 0;   // the left stick of the report (BodyWalkPad: BodyWalk's, as it is)
    State state = State::NotDirect;
    How how = How::Neither;
};

inline Out Resolve(const Inputs& in) {
    Out o;
    if (!in.directFeeding) {
        o.state = State::NotDirect;
        if (in.bodywalkPadOn && in.reportLive) { o.source = Source::BodyWalkPad; o.lx = in.bwLX; o.ly = in.bwLY; }
        return o;
    }
    o.source = Source::OwnPad;
    o.lx = in.ownLX; o.ly = in.ownLY;
    if (!in.settingOn) { o.state = State::Off; return o; }
    if (!in.reportLive) { o.state = State::NoBodyWalk; return o; }
    if (!in.inWorld) { o.state = State::NoGame; return o; }
    if (in.menuOpen) { o.state = State::Menu; return o; }
    const Mixed m = Combine(FromPad(in.ownLX, in.ownLY), FromPad(in.bwLX, in.bwLY));
    o.how = m.how;
    switch (m.how) {
    case How::Neither: o.state = State::Live; break;
    case How::OwnAlone: o.state = State::OwnAlone; break;
    case How::BodyWalkAlone: o.state = State::BodyWalkAlone; break;
    case How::Sum: o.state = State::Sum; break;
    case How::OwnStronger: o.state = State::OwnStronger; break;
    case How::BodyWalkStronger: o.state = State::BodyWalkStronger; break;
    case How::SameWay: o.state = State::SameWay; break;
    }
    // (the stick of the report only where BodyWalk's walk is in it: the others keep the controller's own value to the bit)
    if (m.how == How::BodyWalkAlone || m.how == How::Sum || m.how == How::OwnStronger || m.how == How::BodyWalkStronger || m.how == How::SameWay) {
        o.source = Source::Mixed;
        o.lx = Axis(m.out.x); o.ly = Axis(m.out.y);
    }
    return o;
}

inline const char* StateName(State s) {
    switch (s) {
    case State::NotDirect: return "direct input is not feeding (BodyWalk's pad is the pad)";
    case State::Off: return "IGNORED - direct input is on and [openxr] direct_bodywalk_walk=0";
    case State::NoBodyWalk: return "IGNORED - BodyWalk's pad report is not moving (BodyWalk is not running, or it writes no pad report): the controller walks alone";
    case State::Menu: return "DROPPED - a panel or menu is open (the stick is the menu's)";
    case State::NoGame: return "DROPPED - no game is running (the stick is the menu's)";
    case State::Live: return "passes - BodyWalk's pad is live, nobody pushes a stick";
    case State::OwnAlone: return "passes - the controller's stick walks, BodyWalk's is at rest";
    case State::BodyWalkAlone: return "COMBINED - BodyWalk's stick walks, the controller's is at rest";
    case State::Sum: return "COMBINED - BodyWalk's stick and the controller's added (clamped to the unit disc)";
    case State::OwnStronger: return "COMBINED - the two pull apart: the controller's stick is the stronger and stays";
    case State::BodyWalkStronger: return "COMBINED - the two pull apart: BodyWalk's stick is the stronger and stays";
    case State::SameWay: return "COMBINED - the two push the same way (BodyWalk may mirror the thumbstick): the stronger of the two, not doubled";
    case State::FlatOwn: return "VR F1-F3 without BodyWalk - the controller's left stick walks (movement only)";
    }
    return "?";
}

// Whose stick the game's walking code is given in this state, for the log of the walking hook.
inline const char* WhoWalks(State s) {
    switch (s) {
    case State::NotDirect: return "BodyWalk's pad";
    case State::BodyWalkAlone: return "BodyWalk's stick";
    case State::Sum: case State::OwnStronger: case State::BodyWalkStronger: case State::SameWay: return "both sticks (the controller's and BodyWalk's)";
    default: return "the controller's stick";   // direct mode with BodyWalk's walk ignored, dropped, at rest or not there
    }
}

// The turn into the game's frame (vrcam's ViewStickToGame, the approximate branch): a stick in our view (x right, y ahead) by the
// camera's yaw, the length kept. False = left as it was (no length).
inline bool TurnByYaw(float* px, float* py, float yawDeg, float sign, float angleScale, float squash) {
    const float x = *px, y = *py;
    const float mag = std::sqrt(x * x + y * y);
    if (mag < 1e-4f) return false;
    const float t = sign * angleScale * yawDeg * 3.14159265f / 180.0f;
    float rx = x * std::cos(t) - y * std::sin(t), ry = x * std::sin(t) + y * std::cos(t);
    ry *= squash;
    const float rm = std::sqrt(rx * rx + ry * ry);
    if (rm < 1e-6f) return false;
    *px = rx * mag / rm; *py = ry * mag / rm;
    return true;
}

// What BodyWalk is given of a hand in direct mode (link::PublishXr): where it is, and no control - its Mapping presses nothing a second time
// and its own thumbstick locomotion does not double the plugin's; its walking is the body's motion, read from the poses. Any struct with
// these fields (Game Link's BW_LinkXrHand).
template <class Hand> inline void ZeroControls(Hand& h) {
    h.trigger = 0.0f; h.squeeze = 0.0f;
    h.stickX = 0.0f; h.stickY = 0.0f;
    h.buttons = 0;
}

// -- the log ---------------------------------------------------------------------------------------------------------------
// A line when the state changes - the first kHead of them at once, then at most one in kEveryMs (and a change swallowed in between is
// said when its time comes, with how many changes it stands for). Not thread-safe: the caller holds a lock.
class StateLog {
public:
    static constexpr uint32_t kHead = 40;
    static constexpr uint64_t kEveryMs = 5000;
    // `now`: this call's state; NotDirect is no state (nothing is said and the memory of the last one is let go).
    // True when a line is due: *state is what to say, *changes how many changes it stands for.
    bool Step(State now, uint64_t nowMs, State* state, uint32_t* changes) {
        if (now == State::NotDirect) { last_ = State::NotDirect; pending_ = 0; return false; }
        if (now != last_) { last_ = now; ++pending_; ++total_; }
        if (pending_ == 0) return false;
        if (lines_ < kHead || nowMs - lastAt_ >= kEveryMs) {
            ++lines_;
            lastAt_ = nowMs;
            *state = last_;
            *changes = pending_;
            pending_ = 0;
            return true;
        }
        return false;
    }
    uint32_t Lines() const { return lines_; }
    uint32_t Changes() const { return total_; }

private:
    State last_ = State::NotDirect;
    uint32_t pending_ = 0, total_ = 0, lines_ = 0;
    uint64_t lastAt_ = 0;
};

// -- [input] bodywalk_walk_lift: BodyWalk's stick from `from` up counts as just past the game's dead zone ---------------------------
// BodyWalk's walk in place reaches its stick through two moving averages and a speed curve, so from rest its stick climbs for the
// better part of a second, and the game walks only once it is past 0.32 (2026-10-28, SlimeVR legs: 0.7 - 1.5 s after the first step).
// The climb itself starts within ~0.3 s; this lets the hero start there: a BodyWalk stick at least `from` long but still inside the
// dead zone is lengthened to just past it, its direction kept. A stick past the dead zone and one shorter than `from` are left as they
// are (the first walks anyway, the second is BodyWalk at rest - its noise sits under ~0.1). 0 (or >= the dead zone) = off, as before.
inline constexpr int16_t kLiftedAxisLen = 10400;   // just past 10349: the game's own test passes, the walk is the slowest it has
inline bool Lift(int16_t* lx, int16_t* ly, float from) {
    if (!(from > 0.0f) || from * 32767.0f >= 10349.0f) return false;
    const float x = (float)*lx, y = (float)*ly;
    const float len = std::sqrt(x * x + y * y);
    if (len < from * 32767.0f || PushedRaw(*lx, *ly)) return false;
    const float k = (float)kLiftedAxisLen / len;
    int16_t nx = (int16_t)std::lround(std::clamp(x * k, -32767.0f, 32767.0f));
    int16_t ny = (int16_t)std::lround(std::clamp(y * k, -32767.0f, 32767.0f));
    if (!PushedRaw(nx, ny)) return false;   // (rounding on a diagonal: never claim a lift the game would not take)
    *lx = nx; *ly = ny;
    return true;
}

// -- BodyWalk's walk through Game Link (its Output "Game Link": bodywalk/game_link.h Walk, link::Walk) ------------------------------
// With that Output BodyWalk puts no walk on its pad: the report's left stick is the Mapping's alone (at rest unless a gesture holds
// it), and the walk comes in the Walk block - the very stick its Xbox output would have pushed, /32767 (the same dead zone, curve and
// speed). It goes back onto the report's left stick the way BodyWalk's own pad does it (VirtualJoystickService::update: a stick the
// Mapping holds wins, else the walk; vigem_set_joystick truncates x * 32767), so from here on nothing can tell it from the Xbox
// output's walk: the mix above in direct mode, the report whole elsewhere, the walking hooks, the game's walk and run.
inline int16_t LinkAxis(float v) { return (int16_t)(int)(std::clamp(v, -1.0f, 1.0f) * 32767.0f); }
// True = the walk is now the report's left stick. `live`: link::Walk had one (BodyWalk's Output is Game Link, the block fresh).
inline bool WithLinkWalk(int16_t* lx, int16_t* ly, bool live, float wx, float wy) {
    if (!live || *lx != 0 || *ly != 0) return false;
    *lx = LinkAxis(wx);
    *ly = LinkAxis(wy);
    return true;
}

}  // namespace walkpath
