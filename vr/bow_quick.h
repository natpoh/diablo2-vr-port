// bow_quick.h - [bow] quick_release: the hero's bow shot made earlier INSIDE the same attack, by moving the action-frame flag of the game's own animation row.
//
// The game's attack animations are data: AnimData.D2 holds, for every token (a hero class), mode (A1 = attack 1) and weapon class (BOW), a row of 160 bytes -
//     name[8]   "AMA1BOW" = Amazon, attack 1, bow, a NUL after it
//     u32 frames   the frames of one direction (14)
//     u32 speed    256 = one frame a game tick (25 a second)
//     u8 flags[144]  one per frame; the flagged frame is where the game acts - for a bow, where the server makes the arrow (frame 6 for the Amazon: 6 ticks = 0.24 s
//                    after the animation starts; read from the game's own archives, nothing of it in this repository)
// The game loads the file whole into one heap block and builds its 256 hash buckets over it IN PLACE: bucket = u32 count + count rows, one after the other, so the block
// is walked from its first byte. A unit keeps a pointer to its current row (+0x70); the server and the client read the flag from it.
//
// The cut: the flag byte of the hero class's bow attack row is moved from its frame (6) to an early one (2): the server makes the arrow ~0.16 s sooner after the press,
// while the animation's frames, speed, the attack rate and the next attack's start are what they were. It is a change of the game's TIMING, said so in the Settings text:
// the arrow, its damage roll, its mana and ammo happen earlier in the attack, and a monster's hit in those first frames no longer cancels the shot. It is undone exactly.
//
// This header is the pure part - the shape check, the move and the way back, the walk over a block through a reader - and cleanroom/tests/bow_quick_test.cpp drives it.
// vr/bow_quick.cpp is the game's side (finding the block, the apply / restore state machine, the log).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bowquick {

constexpr size_t kRowSize = 160, kNameBytes = 8, kFramesAt = 8, kSpeedAt = 12, kFlagsAt = 16, kFlagCount = 144;
constexpr int kBuckets = 256;
constexpr uint32_t kMaxBucketRows = 1024;   // the real file has 3661 rows over 256 buckets; a count far over a bucket's share is not this file
constexpr size_t kMaxBlock = 4u << 20;      // the real file is 0.59 MB

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
    Name,        // not the token's A1 / A2 bow attack row
    Frames,      // a frame count no attack has
    Speed,       // a speed no attack has
    Flags,       // not exactly one flagged frame, a flag past the last frame, or a flag value that is not 1 or 2
    Nothing,     // the flag is already at the first frames: nothing to cut
};
inline const char* Why(Verdict v) {
    switch (v) {
        case Verdict::Ok: return "as read";
        case Verdict::Name: return "not the class's bow attack row (its name)";
        case Verdict::Frames: return "a frame count no bow attack has";
        case Verdict::Speed: return "a speed no bow attack has";
        case Verdict::Flags: return "its flags are not the one-action-frame shape that was read";
        case Verdict::Nothing: return "its action frame is already at the start";
    }
    return "";
}

// Is `name` (the row's 8 bytes) "<token>A1BOW" or "<token>A2BOW" with the NUL after it?
inline bool IsBowAttackName(const uint8_t* name, const char* token) {
    if (!token || std::strlen(token) != 2) return false;
    if (name[0] != (uint8_t)token[0] || name[1] != (uint8_t)token[1]) return false;
    if (name[2] != 'A' || (name[3] != '1' && name[3] != '2')) return false;
    return name[4] == 'B' && name[5] == 'O' && name[6] == 'W' && name[7] == 0;
}

// A hero's bow attack row of this class, as it was read: one flagged frame (value 1 or 2) among 6..40 frames at a normal speed, none past the last frame.
inline Verdict Check(const uint8_t* row, const char* token, Row* out = nullptr) {
    Row r;
    std::memcpy(r.name, row, kNameBytes);
    r.frames = U32(row + kFramesAt);
    r.speed = U32(row + kSpeedAt);
    if (out) *out = r;
    if (!IsBowAttackName(row, token)) return Verdict::Name;
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

// The frame the flag goes to: the wanted one, at least 1, and never later than where it is (a cut never delays).
inline int Target(int wanted, int original) {
    if (wanted < 1) wanted = 1;
    return wanted < original ? wanted : original;
}

// The flag byte moved from its frame to `to` (cleared there first, then set: never two flags at once). False, and the row untouched, unless the row checks out and
// `to` is a real earlier frame.
inline bool Move(uint8_t* row, const char* token, int to, Row* before = nullptr) {
    Row r;
    if (Check(row, token, &r) != Verdict::Ok) return false;
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
// `found(offset)` for every row whose name is exactly `name` (7 characters and the NUL). False when the block does not look like AnimData (a count that is
// negative or far too big, an offset past kMaxBlock, a read that fails); `rows` gets the number of rows seen.
template <class Reader, class Found>
bool Walk(Reader&& read, const char* name, Found&& found, size_t* rows = nullptr) {
    uint8_t want[kNameBytes] = {};
    std::memcpy(want, name, std::strlen(name) < 7 ? std::strlen(name) : 7);
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
            if (std::memcmp(head, want, kNameBytes) == 0) found(off);
            off += kRowSize;
            ++seen;
        }
    }
    if (rows) *rows = seen;
    return true;
}

}  // namespace bowquick

// The game's side (vr/bow_quick.cpp).
namespace D2RL {
struct PluginContext;
}
namespace bowquick {

void Init(const D2RL::PluginContext* ctx);
// [bow] quick_release and quick_release_frame (live): the wish. Nothing is touched by this call; Tick does it.
void Configure(bool on, int frame);
// Often, from the update thread (about 20 times a second). `want`: the conditions of the cut hold now (VR first person F4, a bow in hand, in a game area);
// `cls`: skel::HeroClass(); `acting`: the hero's unit is in an attack or cast mode (a flag is never moved under a running attack, for a few seconds at most).
// Finds the row once the conditions hold and moves its flag; puts it back the moment they do not, the class or the frame changes, or the row is not as left.
void Tick(bool want, int cls, bool acting, uint64_t nowMs);
// The plugin unloads: whatever is moved goes back at once.
void Shutdown();
// A row is moved now.
bool Active();

}  // namespace bowquick
