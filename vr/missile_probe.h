// missile_probe.h - [debug] missile_probe: what the server's missile create (CreateMissile 0x5371A0) is given for a player's missile - the whole params block,
// decoded as far as the create's own code reads it - and which creates make one shot (the fan of a Multiple Shot: their headings against each other). Read
// only. It is step 1 of the shot at release (docs/notes/shot_at_release.md): the heading rewrite and the start shift there are written from what this logs.
//
// The params block (0x98 bytes; vr/throw_born.h has the offsets the potion's rewrite needs). Read from the create's code in 3.3.93787 (0x5371A0 - 0x537B60,
// tools/recon.py dis): every field the create reads, and what it does with it:
//   +0x00 flags (below)            +0x08 owner unit*            +0x10 source unit* (the start when 0x1 is not set: its place)
//   +0x18 target unit* (a missile aimed at a unit: the create compares its room with the source's)
//   +0x20 missile class (Missiles.txt's row)    +0x24 / +0x28 start cell (0x1)    +0x2C / +0x30 target cell (0x20 absolute, 0x2 start + it)
//   +0x38 velocity (0x4; shifted 8 up unless 0x10)   +0x3C (handed to the missile, 0x3BDC70)   +0x40 the skill level (the row's VelLev, Range per level)
//   +0x44 extra range per level (0x8)   +0x50 frames taken off the range (0x200)   +0x54 frames off instead of the row's (0x800)   +0x58 (handed on, 0x13)
//   +0x5C the range in frames itself (0x8000)   +0x64 (with the flag byte +0x02 bit 0)   +0x68 (handed on; 0: the owner's own)   +0x70 (non-zero: a call with
//   the whole block, 0x3BAC90)   +0x84 a sub-block the create copies when it is valid (0x3BCDA0 / 0x3BD720)
//   0x400: the range is the distance start -> target at the missile's speed (the throwing potion's; an arrow flies its row's range past its point).
// Unverified until the log: which of these a bow's, a Multiple Shot's, a Strafe's and a throw's own creates set.
//
// Pure: no game, no Windows. The game's side is vr/throw_born.cpp (the create's hook is its); the test cleanroom/tests/missile_probe_test.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace missileprobe {

constexpr size_t kSize = 0x98;

constexpr size_t kOffFlags = 0x00, kOffOwner = 0x08, kOffSource = 0x10, kOffTargetUnit = 0x18, kOffClass = 0x20, kOffStartX = 0x24, kOffStartY = 0x28,
                 kOffTargetX = 0x2C, kOffTargetY = 0x30, kOffVelocity = 0x38, kOff3C = 0x3C, kOffLevel = 0x40, kOffRangeLevel = 0x44, kOffRangeOff = 0x50,
                 kOffRangeOffOwn = 0x54, kOff58 = 0x58, kOffRange = 0x5C, kOff64 = 0x64, kOff68 = 0x68, kOff70 = 0x70;

constexpr uint32_t kFlagStart = 0x1, kFlagTargetRel = 0x2, kFlagVelocity = 0x4, kFlagRangeLevel = 0x8, kFlagVelocityRaw = 0x10, kFlagTargetAbs = 0x20,
                   kFlagRangeOff = 0x200, kFlagLifeByDistance = 0x400, kFlagRangeOffOwn = 0x800, kFlagRange = 0x8000;

template <typename T> inline T Get(const uint8_t* b, size_t off) { T v; std::memcpy(&v, b + off, sizeof v); return v; }

struct Params {
    uint32_t flags = 0;
    uint64_t owner = 0, source = 0, targetUnit = 0;
    int32_t cls = -1;
    int32_t sx = 0, sy = 0, tx = 0, ty = 0;
    int32_t velocity = 0, f3c = 0, level = 0, rangeLevel = 0, rangeOff = 0, rangeOffOwn = 0, f58 = 0, range = 0, f64 = 0, f68 = 0, f70 = 0;
};

inline Params Decode(const uint8_t* b) {
    Params p;
    p.flags = Get<uint32_t>(b, kOffFlags);
    p.owner = Get<uint64_t>(b, kOffOwner);
    p.source = Get<uint64_t>(b, kOffSource);
    p.targetUnit = Get<uint64_t>(b, kOffTargetUnit);
    p.cls = Get<int32_t>(b, kOffClass);
    p.sx = Get<int32_t>(b, kOffStartX);
    p.sy = Get<int32_t>(b, kOffStartY);
    p.tx = Get<int32_t>(b, kOffTargetX);
    p.ty = Get<int32_t>(b, kOffTargetY);
    p.velocity = Get<int32_t>(b, kOffVelocity);
    p.f3c = Get<int32_t>(b, kOff3C);
    p.level = Get<int32_t>(b, kOffLevel);
    p.rangeLevel = Get<int32_t>(b, kOffRangeLevel);
    p.rangeOff = Get<int32_t>(b, kOffRangeOff);
    p.rangeOffOwn = Get<int32_t>(b, kOffRangeOffOwn);
    p.f58 = Get<int32_t>(b, kOff58);
    p.range = Get<int32_t>(b, kOffRange);
    p.f64 = Get<int32_t>(b, kOff64);
    p.f68 = Get<int32_t>(b, kOff68);
    p.f70 = Get<int32_t>(b, kOff70);
    return p;
}

// The flags by name ("start-cell target-abs life-by-distance +0x10000"), the ones not known as hex.
inline void FlagNames(uint32_t f, char* out, size_t n) {
    if (!out || !n) return;
    out[0] = 0;
    static const struct { uint32_t bit; const char* name; } kNames[] = {
        {kFlagStart, "start-cell"}, {kFlagTargetRel, "target-rel"}, {kFlagVelocity, "velocity"}, {kFlagRangeLevel, "range-per-level"},
        {kFlagVelocityRaw, "velocity-raw"}, {kFlagTargetAbs, "target-abs"}, {kFlagRangeOff, "range-off"}, {kFlagLifeByDistance, "life-by-distance"},
        {kFlagRangeOffOwn, "range-off-own"}, {kFlagRange, "range-given"},
    };
    uint32_t left = f;
    size_t len = 0;
    for (const auto& k : kNames) {
        if (!(f & k.bit)) continue;
        left &= ~k.bit;
        const int w = std::snprintf(out + len, n - len, "%s%s", len ? " " : "", k.name);
        if (w < 0 || (size_t)w >= n - len) return;
        len += (size_t)w;
    }
    for (uint32_t bit = 1; bit; bit <<= 1) {
        if (!(left & bit)) continue;
        const int w = std::snprintf(out + len, n - len, "%s+0x%X", len ? " " : "", bit);
        if (w < 0 || (size_t)w >= n - len) return;
        len += (size_t)w;
    }
    if (!len) std::snprintf(out, n, "none");
}

// The target cell as the create takes it: absolute (0x20), the start plus the block's (0x2), else the start itself (a missile with no point of its own).
inline void TargetCell(const Params& p, int startX, int startY, int* tx, int* ty) {
    if (p.flags & kFlagTargetRel) { *tx = startX + p.tx; *ty = startY + p.ty; }
    else if (p.flags & kFlagTargetAbs) { *tx = p.tx; *ty = p.ty; }
    else { *tx = startX; *ty = startY; }
}

// The heading start -> target on the grid, degrees (atan2(dy, dx)); NaN when they are one cell.
inline float HeadingDeg(int sx, int sy, int tx, int ty) {
    const int dx = tx - sx, dy = ty - sy;
    if (dx == 0 && dy == 0) return NAN;
    return std::atan2((float)dy, (float)dx) * 57.2957795f;
}
inline float WrapDeg(float d) {
    if (!std::isfinite(d)) return d;
    d = std::fmod(d + 180.0f, 360.0f);
    if (d < 0.0f) d += 360.0f;
    return d - 180.0f;
}

// The missile classes this work is about (Missiles.txt rows; the arrows' as vr/arrow_flight.h has them, the potions' as vr/throw_born.h). The javelin's,
// the throwing knife's and the throwing axe's are for this probe to tell: nullptr = not named here.
inline const char* ClassName(int32_t c) {
    switch (c) {
        case 0: return "arrow";
        case 12: return "Fire Arrow";
        case 27: return "Magic Arrow";
        case 28: return "Ice Arrow";
        case 31: return "bolt";
        case 40: return "Cold Arrow";
        case 41: return "Exploding Arrow";
        case 85: return "Immolation Arrow";
        case 86: return "Guided Arrow";
        case 87: return "Freezing Arrow";
        case 213: return "Multiple Shot arrow";
        case 214: return "Multiple Shot bolt";
        case 682: return "Strafe arrow";
        case 683: return "Strafe bolt";
        default: return c >= 44 && c <= 49 ? "throwing potion" : nullptr;
    }
}

// -- one shot: the creates of one owner from one call site of the game that come within kGapS of each other (a Multiple Shot's fan is made in one frame,
// 40 ms at the server's 25; a Strafe's arrows come frames apart: each is its own group, which is what it is).
inline constexpr double kGapS = 0.03;
inline constexpr size_t kMaxFan = 32;

struct Create {
    double t = 0.0;            // seconds, any monotonic base
    uint32_t ownerId = 0;      // the owner unit's id
    uint64_t caller = 0;       // the create's return address, as an RVA: which of the game's shooters made it
    int32_t cls = -1;
    float headingDeg = NAN;    // start -> target on the grid (NaN: no heading)
    bool targetUnit = false;   // the block names a target unit
};

struct Shot {
    uint32_t ownerId = 0;
    uint64_t caller = 0;
    int32_t cls = -1;          // the first create's (another in the group: mixed)
    bool mixed = false;
    double t0 = 0.0, t1 = 0.0;
    unsigned count = 0, withUnit = 0, noHeading = 0;
    float heading[kMaxFan] = {};   // each create's heading in order (up to kMaxFan; NaN where it had none)
    unsigned kept = 0;
};

// The mean heading of a fan (the circular mean of those it has), and each one's against it: false when none had a heading.
inline bool Fan(const Shot& s, float* meanDeg, float* rel, size_t relMax, size_t* nRel) {
    double c = 0.0, si = 0.0;
    unsigned n = 0;
    for (unsigned i = 0; i < s.kept; ++i) {
        if (!std::isfinite(s.heading[i])) continue;
        c += std::cos(s.heading[i] * 0.0174532925);
        si += std::sin(s.heading[i] * 0.0174532925);
        ++n;
    }
    if (nRel) *nRel = 0;
    if (!n) return false;
    const float mean = (float)(std::atan2(si, c) * 57.2957795);
    if (meanDeg) *meanDeg = mean;
    size_t k = 0;
    for (unsigned i = 0; i < s.kept && k < relMax; ++i)
        if (std::isfinite(s.heading[i])) rel[k++] = WrapDeg(s.heading[i] - mean);
    if (nRel) *nRel = k;
    return true;
}

// Groups the creates into shots. Not thread-safe: the caller holds its own lock.
class Grouper {
public:
    // A create: when it does not belong to the open shot, the open one is finished first (*done, true returned).
    bool Add(const Create& c, Shot* done) {
        bool finished = false;
        if (open_ && !(c.ownerId == cur_.ownerId && c.caller == cur_.caller && c.t >= cur_.t1 && c.t - cur_.t1 <= kGapS)) {
            if (done) *done = cur_;
            finished = true;
            open_ = false;
        }
        if (!open_) {
            cur_ = Shot{};
            cur_.ownerId = c.ownerId;
            cur_.caller = c.caller;
            cur_.cls = c.cls;
            cur_.t0 = c.t;
            open_ = true;
        }
        cur_.t1 = c.t;
        if (c.cls != cur_.cls) cur_.mixed = true;
        ++cur_.count;
        if (c.targetUnit) ++cur_.withUnit;
        if (!std::isfinite(c.headingDeg)) ++cur_.noHeading;
        if (cur_.kept < kMaxFan) cur_.heading[cur_.kept++] = c.headingDeg;
        return finished;
    }
    // The open shot when nothing has come for kGapS: finished (true, *done).
    bool Flush(double now, Shot* done) {
        if (!open_ || now - cur_.t1 <= kGapS) return false;
        if (done) *done = cur_;
        open_ = false;
        return true;
    }
    bool Open() const { return open_; }

private:
    Shot cur_;
    bool open_ = false;
};

// "missile probe: shot by player 3 from 0x45A007: 5 creates in 0 ms, class 213 (Multiple Shot arrow), heading 12.0 deg; the fan -20.0 -10.0 +0.0 +10.0 +20.0
// deg against it; a target unit in 0 of 5"
inline void ShotLine(const Shot& s, char* out, size_t n) {
    if (!out || !n) return;
    const char* name = ClassName(s.cls);
    char cls[64];
    if (s.mixed) std::snprintf(cls, sizeof cls, "classes mixed, the first %d%s%s%s", s.cls, name ? " (" : "", name ? name : "", name ? ")" : "");
    else std::snprintf(cls, sizeof cls, "class %d%s%s%s", s.cls, name ? " (" : "", name ? name : "", name ? ")" : "");
    float mean = 0.0f, rel[kMaxFan];
    size_t nRel = 0;
    char fan[kMaxFan * 8 + 64] = "";
    if (Fan(s, &mean, rel, kMaxFan, &nRel)) {
        size_t len = (size_t)std::snprintf(fan, sizeof fan, "heading %.1f deg", mean);
        if (nRel > 1) {
            len += (size_t)std::snprintf(fan + len, sizeof fan - len, "; the fan");
            for (size_t i = 0; i < nRel && len < sizeof fan; ++i) len += (size_t)std::snprintf(fan + len, sizeof fan - len, " %+.1f", rel[i]);
            if (len < sizeof fan) std::snprintf(fan + len, sizeof fan - len, " deg against it%s", s.count > s.kept ? " (the first ones)" : "");
        }
    } else {
        std::snprintf(fan, sizeof fan, "no heading (start and target one cell)");
    }
    std::snprintf(out, n, "missile probe: shot by player %u from 0x%llX: %u create%s in %.0f ms, %s, %s; a target unit in %u of %u%s", s.ownerId,
                  (unsigned long long)s.caller, s.count, s.count == 1 ? "" : "s", (s.t1 - s.t0) * 1000.0, cls, fan, s.withUnit, s.count,
                  s.noHeading ? " (some without a heading)" : "");
}

// The block as hex, `perLine` bytes from `from`: "+0x00: 21 04 00 00 ...".
inline void HexLine(const uint8_t* b, size_t from, size_t perLine, char* out, size_t n) {
    if (!out || !n) return;
    size_t len = (size_t)std::snprintf(out, n, "+0x%02X:", (unsigned)from);
    for (size_t i = from; i < from + perLine && i < kSize && len < n; ++i) len += (size_t)std::snprintf(out + len, n - len, " %02X", b[i]);
}

}  // namespace missileprobe
