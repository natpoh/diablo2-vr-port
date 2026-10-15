// anim_cut.h - [bow] quick_release, [hands] quick_throw and [hands] quick_strike: the hero's arrow, throw or melee blow made earlier INSIDE the same animation, by moving the
// action-frame flag of the game's own animation row.
//
// The game's attack animations are data: AnimData.D2 holds, for every token (a hero class), mode and weapon class, a row of 160 bytes -
//     name[8]   "AMA1BOW" = Amazon, attack 1, bow; "AMTH1HT" = Amazon, throw, one-hand thrust; a NUL after it
//     u32 frames   the frames of one direction (14 for the bow attack, 16 for the Amazon's throw)
//     u32 speed    256 = one frame a game tick (25 a second)
//     u8 flags[144]  one per frame; the flagged frame is where the game acts - where the server makes the arrow or the thrown missile (frame 6 of the Amazon's bow
//                    attack, frame 9 of her throw: 6 and 9 ticks = 0.24 and 0.36 s after the animation starts; read from the game's own archives, nothing of it in
//                    this repository)
// The game loads the file whole into one heap block and builds its 256 hash buckets over it IN PLACE: bucket = u32 count + count rows, one after the other, so the block
// is walked from its first byte. A unit keeps a pointer to its current row (+0x70); the server and the client read the flag from it - with ONE function, the unit's
// animation step (0x3476B0), which looks at the frames AFTER the one the animation is on, so a flag on frame 0 is never read at the start and frame 1 is the earliest.
//
// The cut: the flag byte of the hero class's rows is moved from its frame to an early one (1 by default for the throw, the user's choice for the bow): the server makes the
// arrow or the missile sooner after the press, while the animation's frames, speed, the attack rate and the next action's start are what they were. It is a change of the
// game's TIMING, said so in the Settings text: the missile, its damage roll, its mana and ammunition happen earlier, and a monster's hit in those first frames no longer
// cancels the shot. It is undone exactly.
//   Kind::Bow   - <token>A1BOW and <token>A2BOW (the bow's attack).
//   Kind::Throw - every <token>TH<weapon class> row (mode 11, the throw: the Throw skill with a potion, a javelin, a throwing knife or axe, whatever weapon class the hero holds).
//   Kind::Melee - every <token>A1<weapon class> and <token>A2<weapon class> row of a melee weapon class (modes 7 and 8: the attack, and the skills that play it - Zeal, Bash, Berserk,
//                 Sacrifice, Vengeance, Power Strike ... all have `anim A1` in skills.txt) and the Paladin's S1 rows (Smite, mode 13). Not the bow's (BOW) or the crossbow's (XBW): a
//                 shot is the bow's, and Kind::Bow has them. Measured in the user's log: the game's blow lands 317 - 339 ms after our press (the Paladin's one-hand swing: the action
//                 frame 7 or 8 = 280 / 320 ms, plus the ~40 ms the press takes to start the animation); the Direct hit lands in 15 - 53 ms. The cut takes the action frame to 1.
// The bow's rows, the throw's rows and the melee rows are never in each other's reach.
//
// This header is the pure part - the shape check, the move and the way back, the walk over a block through a reader - and cleanroom/tests/anim_cut_test.cpp drives it.
// vr/anim_cut.cpp is the game's side (finding the block, the apply / restore state machine, the log).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace animcut {

constexpr size_t kRowSize = 160, kNameBytes = 8, kFramesAt = 8, kSpeedAt = 12, kFlagsAt = 16, kFlagCount = 144;
constexpr int kBuckets = 256;
constexpr uint32_t kMaxBucketRows = 1024;   // the real file has 3661 rows over 256 buckets; a count far over a bucket's share is not this file
constexpr size_t kMaxBlock = 4u << 20;      // the real file is 0.59 MB

enum class Kind : uint8_t { Bow, Throw, Melee };
constexpr int kKinds = 3;

inline uint32_t U32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

// The two-letter token of a hero class (an index into skel::kHeroClasses: amazon, sorceress, necromancer, paladin, barbarian, druid, assassin, warlock), else nullptr.
inline const char* ClassToken(int cls) {
    static const char* const t[8] = {"AM", "SO", "NE", "PA", "BA", "DZ", "AI", "WK"};
    return cls >= 0 && cls < 8 ? t[cls] : nullptr;
}

// What a row is, as far as the shape goes.
struct Row {
    char name[8] = {};
    uint32_t frames = 0, speed = 0;
    int flagIdx = -1;       // the one frame with a flag set
    uint8_t flagVal = 0;
};

enum class Verdict : uint8_t {
    Ok,
    Name,        // not one of the token's rows of this kind
    Frames,      // a frame count no such animation has
    Speed,       // a speed no such animation has
    Flags,       // not exactly one flagged frame, a flag past the last frame, or a flag value that is not 1 or 2
    Nothing,     // the flag is already at the first frames: nothing to cut
};
inline const char* Why(Verdict v) {
    switch (v) {
        case Verdict::Ok: return "as read";
        case Verdict::Name: return "not the class's row of this kind (its name)";
        case Verdict::Frames: return "a frame count no such animation has";
        case Verdict::Speed: return "a speed no such animation has";
        case Verdict::Flags: return "its flags are not the one-action-frame shape that was read";
        case Verdict::Nothing: return "its action frame is already at the start";
    }
    return "";
}

inline bool IsAlnum(uint8_t c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

// Is `name` (the row's 8 bytes) a row of this kind for this token: Bow "<token>A1BOW" / "<token>A2BOW"; Throw "<token>TH" + a three-character weapon class ("1HS", "1HT", "HTH", "1JT"...);
// Melee "<token>A1" / "<token>A2" + a three-character weapon class that is not BOW or XBW ("1HS", "2HT", "STF", "HTH", "HT1", "1JS"...), and for the Paladin "PAS1" + a weapon class (Smite).
// The NUL must follow.
inline bool IsRowName(const uint8_t* name, const char* token, Kind kind) {
    if (!token || std::strlen(token) != 2) return false;
    if (name[0] != (uint8_t)token[0] || name[1] != (uint8_t)token[1]) return false;
    if (name[7] != 0) return false;
    const bool wc = IsAlnum(name[4]) && IsAlnum(name[5]) && IsAlnum(name[6]);
    if (kind == Kind::Bow) return name[2] == 'A' && (name[3] == '1' || name[3] == '2') && name[4] == 'B' && name[5] == 'O' && name[6] == 'W';
    if (kind == Kind::Melee) {
        const bool ranged = (name[4] == 'B' && name[5] == 'O' && name[6] == 'W') || (name[4] == 'X' && name[5] == 'B' && name[6] == 'W');
        if (name[2] == 'A' && (name[3] == '1' || name[3] == '2')) return wc && !ranged;
        return name[0] == 'P' && name[1] == 'A' && name[2] == 'S' && name[3] == '1' && wc && !ranged;   // Smite: the Paladin's special 1 (the other classes' S1 are not melee)
    }
    return name[2] == 'T' && name[3] == 'H' && wc;
}

// A hero's row of this kind and class, as it was read: one flagged frame (value 1 or 2) among 6..40 frames at a normal speed, none past the last frame.
inline Verdict Check(const uint8_t* row, const char* token, Kind kind, Row* out = nullptr) {
    Row r;
    std::memcpy(r.name, row, kNameBytes);
    r.frames = U32(row + kFramesAt);
    r.speed = U32(row + kSpeedAt);
    if (out) *out = r;
    if (!IsRowName(row, token, kind)) return Verdict::Name;
    if (r.frames < 6 || r.frames > 40) return Verdict::Frames;
    if (r.speed < 64 || r.speed > 1024) return Verdict::Speed;
    int idx = -1, flagged = 0;
    for (size_t i = 0; i < kFlagCount; ++i)
        if (row[kFlagsAt + i]) {
            ++flagged;
            if (idx < 0) idx = (int)i;
        }
    if (flagged != 1 || idx < 0 || (uint32_t)idx >= r.frames) return Verdict::Flags;
    const uint8_t val = row[kFlagsAt + (size_t)idx];
    if (val != 1 && val != 2) return Verdict::Flags;
    r.flagIdx = idx;
    r.flagVal = val;
    if (out) *out = r;
    return idx < 2 ? Verdict::Nothing : Verdict::Ok;
}

// The frame the flag goes to: the wanted one, at least 1 (frame 0 is never read at the start: nothing is faster than 1), and never later than where it is (a cut never delays).
inline int Target(int wanted, int original) {
    if (wanted < 1) wanted = 1;
    return wanted < original ? wanted : original;
}

// How many milliseconds sooner the action comes: the frames moved, a game tick (40 ms) each at speed 256.
inline uint32_t CutMs(int from, int to, uint32_t speed) {
    if (to >= from || speed == 0) return 0;
    return (uint32_t)((double)(from - to) * 40.0 * 256.0 / (double)speed + 0.5);
}

// The flag byte moved from its frame to `to` (cleared there first, then set: never two flags at once). False, and the row untouched, unless the row checks out and
// `to` is a real earlier frame.
inline bool Move(uint8_t* row, const char* token, Kind kind, int to, Row* before = nullptr) {
    Row r;
    if (Check(row, token, kind, &r) != Verdict::Ok) return false;
    if (before) *before = r;
    if (to < 1 || to >= r.flagIdx) return false;
    row[kFlagsAt + (size_t)r.flagIdx] = 0;
    row[kFlagsAt + (size_t)to] = r.flagVal;
    return true;
}

// The way back: only a row exactly as Move left it (the one flag at `at` with `val`, nothing at `from`) is put back; anything else is somebody else's and is left.
inline bool InPatchedState(const uint8_t* row, int from, int at, uint8_t val) {
    int flagged = 0;
    for (size_t i = 0; i < kFlagCount; ++i) if (row[kFlagsAt + i]) ++flagged;
    return flagged == 1 && row[kFlagsAt + (size_t)at] == val && row[kFlagsAt + (size_t)from] == 0;
}
inline bool Back(uint8_t* row, int from, int at, uint8_t val) {
    if (!InPatchedState(row, from, at, val)) return false;
    row[kFlagsAt + (size_t)at] = 0;
    row[kFlagsAt + (size_t)from] = val;
    return true;
}

// The walk: the block's bucket after bucket, `read(offset, dst, n)` giving the bytes (a plain memcpy in the test, a guarded read of the game's memory in the game).
// `found(offset)` for every row whose first 8 bytes `wanted(name)` accepts. False when the block does not look like AnimData (a count that is negative or far too big, an
// offset past kMaxBlock, a read that fails); `rows` gets the number of rows seen.
template <class Reader, class Wanted, class Found>
bool WalkRows(Reader&& read, Wanted&& wanted, Found&& found, size_t* rows = nullptr) {
    size_t off = 0, seen = 0;
    for (int b = 0; b < kBuckets; ++b) {
        uint8_t cnt[4];
        if (off + 4 > kMaxBlock || !read(off, cnt, 4)) return false;
        const int32_t n = (int32_t)U32(cnt);
        off += 4;
        if (n < 0 || (uint32_t)n > kMaxBucketRows) return false;
        for (int i = 0; i < n; ++i) {
            uint8_t head[kNameBytes];
            if (off + kRowSize > kMaxBlock || !read(off, head, sizeof head)) return false;
            if (wanted(head)) found(off);
            off += kRowSize;
            ++seen;
        }
    }
    if (rows) *rows = seen;
    return true;
}
// ... the rows of one exact name (7 characters and the NUL).
template <class Reader, class Found>
bool Walk(Reader&& read, const char* name, Found&& found, size_t* rows = nullptr) {
    uint8_t want[kNameBytes] = {};
    std::memcpy(want, name, std::strlen(name) < 7 ? std::strlen(name) : 7);
    return WalkRows(read, [&](const uint8_t* head) { return std::memcmp(head, want, kNameBytes) == 0; }, found, rows);
}

// [hands] quick_throw leaves a THROWING POTION alone unless [hands] quick_throw_potions says otherwise. A potion has its own timing: the game's missile is made at the throw's action frame and
// flies 25 cells a second, so its explosion comes about when the weapon-weight model's potion (which flies the thrown trajectory) lands - cut the frame to the start and the explosion comes
// ~0.3 s BEFORE the potion lands, hidden or not. The rows are the class's for every weapon it holds, so the cut is on while no potion is in the hand and off (put back) while one is.
inline constexpr bool ThrowCutWanted(bool inF4, bool potionInHand, bool potionsToo) { return inF4 && (!potionInHand || potionsToo); }

}  // namespace animcut

// The game's side (vr/anim_cut.cpp).
namespace D2RL {
struct PluginContext;
}
namespace animcut {

void Init(const D2RL::PluginContext* ctx);
// [bow] quick_release / [hands] quick_throw and their frame settings (live): the wish. Nothing is touched by this call; Tick does it.
void Configure(Kind kind, bool on, int frame);
// Often, from the update thread (about 20 times a second). `want`: the conditions of the cut hold now (VR first person F4, in a game area; for the bow also a bow in hand);
// `cls`: skel::HeroClass(); `acting`: the hero's unit is in an attack, cast or throw mode (a flag is never moved under a running animation, for a few seconds at most).
// Finds the rows once the conditions hold and moves their flags; puts them back the moment they do not, the class or the frame changes, or a row is not as left.
void Tick(Kind kind, bool want, int cls, bool acting, uint64_t nowMs);
// The plugin unloads: whatever is moved goes back at once, both kinds.
void Shutdown();
// A row of this kind is moved now.
bool Active(Kind kind);
// The most milliseconds sooner any moved row of this kind acts (0 when none is moved): what other timings that follow the action frame may take off.
uint32_t CutMs(Kind kind);

}  // namespace animcut
