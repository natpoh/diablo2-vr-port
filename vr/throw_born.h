// throw_born.h - [hands] throw_direct: a thrown potion BORN where the model lands. The user's EXPERIMENTAL, opt-in option, in the spirit of "Direct hit"
// (vr/melee_direct.h): everything the game can do by itself it keeps doing.
//
// The problem. A thrown potion is two things in this mod: the potion in the hand (the weapon-weight model's model: it leaves the hand at the release and flies
// the worked-out trajectory) and the game's own throw (the Throw skill: the game's missile leaves the hero at the ACTION FRAME of his throw animation, flies
// straight to the cast point at 1 cell a frame - 25 cells a second - and explodes where its life ends). With the full flight two potions fly, one after the
// other, and the explosion comes a second after the model has landed.
//
// What the game does (static reading of D2R 3.3.93787, notes: side-session-notes/throw-direct-research.md). The server's per-tick step of a straight missile
// takes one frame off its life and at life <= 0 calls its Hit with no unit; the six potions have AlwaysExplode, so Hit runs the missile's own hit function
// (damage, burn, radius, the gas cloud's sub-missile) and ends it, and the client's copy makes the explosion sprite. So an explosion at a spot is a potion-class
// missile BORN at that spot with a one-frame life; nothing needs calling but the game's own create. The potion's throw (the server's 0x432F90, called by Throw
// 0x430610 for items of type tpot) builds the create's params block with flags 0x420 - start = the hero (flag 0x1 missing), target absolute (0x20), the life
// by distance (0x400) - and has first spent the stack. The create is CreateMissile 0x5371A0 (game, params*).
//
// What this does. The game's throw is pressed AT THE RELEASE, as it always was - so the hero's throw animation plays while the model flies and the input lag is
// the least there can be; the animation, the gates (line of sight, mana, the stack) and the stack's spending are the game's. ONE hook on the create, armed for
// one throw, finds the potion's params block as it goes by (the hero's own unit as owner, a potion missile class, the potion shooter's flags) and moves the START
// along the line from the hero so that the missile's life (its distance to the cast point, one cell a frame) ends exactly as the model lands: by then the
// animation has reached its action frame and the model has hardly any flight left. The game's own explosion happens there. Every other create, and every create
// with the option off or no armed throw, passes untouched.
//
// This header is the pure part: the params block, the decision, the rewrite, the armed window, the press time. The game's side is vr/throw_born.cpp; the test
// is cleanroom/tests/throw_born_test.cpp.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace throwborn {

// -- the server's missile-create params block (CreateMissile 0x5371A0 (game, params*), 0x98 bytes). The layout is the one every skill of the game fills in and the
// one bearstonem/d2r-pcvr's VisualParams writes (MIT, vr/bow.cpp): the coordinates are the game's cells, as ints.
constexpr size_t kParamsSize = 0x98;
constexpr size_t kOffFlags = 0x00, kOffOwner = 0x08, kOffSource = 0x10, kOffClass = 0x20, kOffStartX = 0x24, kOffStartY = 0x28, kOffTargetX = 0x2C, kOffTargetY = 0x30;
constexpr uint32_t kFlagStart = 0x1;           // start = the block's own start cell, else the source unit's place
constexpr uint32_t kFlagTargetAbs = 0x20;      // the target is the block's cell, absolute (0x2: start + the block's offset)
constexpr uint32_t kFlagLifeByDistance = 0x400; // the life = the octagonal distance start -> target in frames at the missile's speed
constexpr int32_t kPotionClassFirst = 44, kPotionClassLast = 49;   // the six throwing potions' missile classes (the research's reading of missiles.txt)
constexpr int32_t kCellMax = 0x7FFF;           // a cell is a small positive int; anything else is not a cell

using Block = std::array<uint8_t, kParamsSize>;

template <typename T> inline T Get(const uint8_t* b, size_t off) { T v; std::memcpy(&v, b + off, sizeof v); return v; }
template <typename T> inline void Put(uint8_t* b, size_t off, T v) { std::memcpy(b + off, &v, sizeof v); }

// What the hook reads out of the block (and what the unit under `owner` is, which only the game's side can read).
struct Seen {
    uint32_t flags = 0;
    int32_t missileClass = 0;
    uint64_t owner = 0;
    int32_t tx = 0, ty = 0;
};
inline Seen Read(const uint8_t* b) {
    Seen s;
    s.flags = Get<uint32_t>(b, kOffFlags);
    s.missileClass = Get<int32_t>(b, kOffClass);
    s.owner = Get<uint64_t>(b, kOffOwner);
    s.tx = Get<int32_t>(b, kOffTargetX);
    s.ty = Get<int32_t>(b, kOffTargetY);
    return s;
}

enum class Verdict : uint8_t {
    Relocate,       // this is the armed throw's potion: born at the cast point
    NotArmed,       // no throw is waiting for it
    NotPotion,      // not the potion shooter's block (the flags, or a class that is no throwing potion)
    NotHero,        // the owner is not the hero's own player unit
    BadCell,        // the cast point is no cell
    NoRoom,         // the game's room test says there is no room at the cast point: the game's own flight is left alone
};

inline const char* Why(Verdict v) {
    switch (v) {
        case Verdict::Relocate: return "born at the landing";
        case Verdict::NotArmed: return "no throw armed";
        case Verdict::NotPotion: return "not the potion shooter's params";
        case Verdict::NotHero: return "the owner is not the hero";
        case Verdict::BadCell: return "the cast point is no cell";
        case Verdict::NoRoom: return "no room at the cast point";
    }
    return "";
}

inline bool IsPotionFlags(uint32_t flags) {
    return (flags & kFlagLifeByDistance) != 0 && (flags & kFlagTargetAbs) != 0 && (flags & kFlagStart) == 0;
}
inline bool IsPotionClass(int32_t c) { return c >= kPotionClassFirst && c <= kPotionClassLast; }
inline bool IsCell(int32_t v) { return v > 0 && v <= kCellMax; }

// armed: a throw is waiting; ownerIsHero: the unit under owner is the hero's (type 0, the local one); roomAtCast: the game says there is room at the cast point.
inline Verdict Decide(const Seen& s, bool armed, bool ownerIsHero, bool roomAtCast) {
    if (!armed) return Verdict::NotArmed;
    if (!IsPotionFlags(s.flags) || !IsPotionClass(s.missileClass)) return Verdict::NotPotion;
    if (!ownerIsHero) return Verdict::NotHero;
    if (!IsCell(s.tx) || !IsCell(s.ty)) return Verdict::BadCell;
    if (!roomAtCast) return Verdict::NoRoom;
    return Verdict::Relocate;
}

// -- where the potion is born. The game's throw is pressed AT THE RELEASE (the hero's throw animation plays while the model flies - the least input lag); its
// potion missile is created at the animation's action frame (about a third of a second on), so by then the model has `remainingS` of its flight left. The
// missile lives its octagonal distance in frames (1 cell a frame, 25 a second), so it is born that many frames' distance BEFORE the cast point on the line
// from the hero, and the game's own explosion comes as the model lands. Born past the hero's own distance it would have to start behind the hero: then the
// game's own flight is left alone (it lands earlier than the model - nothing can make it later). With no time left it is born at the cast point itself
// with a one-frame life.
inline constexpr double kGameTickHz = 25.0;
inline int OctFrames(int dx, int dy) {   // the game's life by distance: (2 * longer + shorter) / 2 of the two axes in cells
    const long ax = dx < 0 ? -(long)dx : (long)dx, ay = dy < 0 ? -(long)dy : (long)dy;
    return (int)((2 * (std::max)(ax, ay) + (std::min)(ax, ay)) / 2);
}
struct Start {
    enum class Kind : uint8_t { Natural, AtCast, Shifted, NoDirection };
    Kind kind = Kind::Natural;
    int sx = 0, sy = 0;   // the start cell (Shifted, AtCast)
    int tx = 0, ty = 0;   // the target cell to write
    int frames = 0;       // the life it will have, by the game's own count
};
inline const char* Why(Start::Kind k) {
    switch (k) {
        case Start::Kind::Natural: return "the game's own flight is as long as the model's (or longer)";
        case Start::Kind::AtCast: return "born at the cast point with a one-frame life";
        case Start::Kind::Shifted: return "born on the way, its life ends at the cast point as the model lands";
        case Start::Kind::NoDirection: return "the hero stands on the cast point";
    }
    return "";
}
// hero (hx, hy) and cast point (tx, ty) in cells; remainingS: how long the model still flies when the missile is created.
inline Start PlanStart(int hx, int hy, int tx, int ty, double remainingS) {
    Start p;
    p.tx = tx; p.ty = ty;
    const int dx = tx - hx, dy = ty - hy;
    const int full = OctFrames(dx, dy);
    if (full <= 0) { p.kind = Start::Kind::NoDirection; return p; }
    const double r = std::isfinite(remainingS) ? remainingS : 0.0;
    const int want = (int)std::llround(std::clamp(r, 0.0, 10.0) * kGameTickHz);
    if (want >= full) { p.kind = Start::Kind::Natural; p.frames = full; return p; }
    if (want <= 1) {
        p.kind = Start::Kind::AtCast;
        p.sx = tx; p.sy = ty; p.tx = tx + 1; p.ty = ty; p.frames = 1;
        return p;
    }
    // the unit direction hero -> cast point; its octagonal length is hi + lo / 2 for a vector of length 1
    const double len = std::sqrt((double)dx * dx + (double)dy * dy);
    const double ux = dx / len, uy = dy / len;
    const double oct = (std::max)(std::fabs(ux), std::fabs(uy)) + (std::min)(std::fabs(ux), std::fabs(uy)) * 0.5;
    const double cells = want / oct;
    p.sx = tx - (int)std::llround(ux * cells);
    p.sy = ty - (int)std::llround(uy * cells);
    p.frames = OctFrames(tx - p.sx, ty - p.sy);
    if (p.frames <= 1) { p.kind = Start::Kind::AtCast; p.sx = tx; p.sy = ty; p.tx = tx + 1; p.frames = 1; return p; }
    if (p.frames >= full) { p.kind = Start::Kind::Natural; p.frames = full; return p; }
    p.kind = Start::Kind::Shifted;
    return p;
}

// The rewrite: the start flag set, the start and target cells as planned. Nothing else in the block changes.
inline void Rewrite(uint8_t* b, const Start& p) {
    const Seen s = Read(b);
    Put<uint32_t>(b, kOffFlags, s.flags | kFlagStart);
    Put<int32_t>(b, kOffStartX, p.sx);
    Put<int32_t>(b, kOffStartY, p.sy);
    Put<int32_t>(b, kOffTargetX, p.tx);
    Put<int32_t>(b, kOffTargetY, p.ty);
}

// -- the armed window: one throw, a short time. The draw thread arms it when it presses the game's throw; the game thread's hook takes it. -----------------------
class Window {
public:
    // landAtMs: when the model lands (GetTickCount64), the time the game's explosion is aimed at.
    void Arm(uint64_t nowMs, uint64_t ttlMs, uint64_t landAtMs = 0) {
        landAt_.store(landAtMs, std::memory_order_release);
        until_.store(nowMs + ttlMs, std::memory_order_release);
    }
    void Cancel() { until_.store(0, std::memory_order_release); }
    bool Armed(uint64_t nowMs) const { const uint64_t u = until_.load(std::memory_order_acquire); return u != 0 && nowMs <= u; }
    // One taker: true once while armed, then it is spent.
    bool Take(uint64_t nowMs) {
        const uint64_t u = until_.exchange(0, std::memory_order_acq_rel);
        return u != 0 && nowMs <= u;
    }
    uint64_t Until() const { return until_.load(std::memory_order_acquire); }
    uint64_t LandAt() const { return landAt_.load(std::memory_order_acquire); }

private:
    std::atomic<uint64_t> until_{0}, landAt_{0};
};

// How long the hook waits for the potion's create after the press: the model's flight, the animation's action frame and the game's own gates, with a margin.
inline uint64_t ArmTtlMs(double flightS) {
    const double f = std::isfinite(flightS) ? std::clamp(flightS, 0.0, 10.0) : 0.0;
    return (uint64_t)std::llround((f + 1.5) * 1000.0);
}

}  // namespace throwborn

// The game's side (vr/throw_born.cpp).
namespace D2RL {
struct PluginContext;
}
namespace throwborn {

void Init(const D2RL::PluginContext* ctx);
// [hands] throw_direct: live.
void Configure(bool wanted);
bool Wanted();
// Hooks the create once it is found (tried again now and then: the server's code is decrypted as it runs). Cheap to call every frame.
void Install();
// True when the hook is in, nothing faulted, and the option is on: the throw wire may rely on it.
bool Ready();
// The wire pressed the game's throw of a potion (GetTickCount64 times): the next potion create of the hero is born so that it explodes at landAtMs, where the
// model lands. Returns false when the option is not ready.
bool Arm(uint64_t nowMs, uint64_t landAtMs, double flightS);
void Disarm();
// The game's potion of the armed throw has been created (its action frame has passed) since the last Arm: weight::SetActionFrameSource - the thrown model
// hands over to it there. False once a few seconds have gone.
bool ActionFramePassed();

// -- a passive look at the server's create ([debug] bow_latency) ------------------------------------------------------------------------------------------
// The same hook, read only: every create whose owner is a player's unit (the hero's: offline, the only one) is written down - when, which missile class, the
// params' flags and target cell - and nothing is rewritten, whatever [hands] throw_direct says. The hook goes in for this alone too. A ring of the last 32.
struct Made {
    double at = 0.0;          // NowS() when the game called the create
    int32_t missileClass = 0;
    uint32_t flags = 0;
    int32_t tx = 0, ty = 0;
    // the animation record the owner's unit points at (its +0x70: the game's AnimData row, 160 bytes, read in place) as the create is made - the attack's own
    bool animRead = false;
    uint64_t animPtr = 0;
    uint8_t anim[160] = {};
};
// The steady clock the creates are stamped with, in seconds (std::chrono::steady_clock).
double NowS();
void Observe(bool on);
// The hook is in, nothing faulted, and the look is on.
bool Observing();
// The hero's creates made at or after `sinceS` (NowS() time), oldest first; the count copied.
size_t CreatesSince(double sinceS, Made* out, size_t max);

}  // namespace throwborn
