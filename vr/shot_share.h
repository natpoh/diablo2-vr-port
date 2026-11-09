// shot_share.h - a player's bow shot to the other players (docs/notes/shot_at_release.md, items 4 and 7): the game sends its arrow missile to every PC,
// but only as the game's flat arrow from the hero's body - the line the shooter aimed (from the bow, up or down, to what it met) was the shooter's PC's alone,
// so the others saw the partner's arrows "fly as usual" (Serg, 2026-11-06). The line goes now with D2RLoader's plugin network (pose_share's channel,
// vr/pose_share_wire.cpp): to the host, which hands it on to every other player at once, and on each receiver vr/arrow_flight_wire.cpp draws the partner's
// arrow along it (NoteRemote). The host also keeps each player's line for the create of his arrows (vr/shot_release.h): a joined flat player's arrow is
// turned there to where his crosshair was at the release, as the host's own is.
//
// One message a press, then the same press's line again while the attack goes on (the crosshair moving; at most the wire's rate), so the line a receiver
// holds when the partner's arrow appears is the one of the release. Everything is in the game's world units (the render world: twice the cell, raised ground
// pushed by its height): the same on every PC, so nothing is turned or moved on the way.
//
// Pure: no game, no Windows, no network; the test cleanroom/tests/shot_share_test.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "pose_share.h"

namespace shotshare {

enum Flags : uint8_t { kTarget = 1, kMonster = 2, kStick = 4, kPress = 8 };

struct Shot {
    uint16_t seq = 0;          // the sender's press (the line's updates during one attack carry the same)
    uint8_t flags = 0;         // kTarget: the line met something (`at`); kMonster: a body; kStick: the game's floor; kPress: the press's own message
    float theta = 0.0f;        // the heading the game is given, atan2(z, x) on the ground (radians)
    float start[3] = {};       // the arrow's release point (world units)
    float dir[3] = {1.0f, 0.0f, 0.0f};   // the line (a unit vector)
    float at[3] = {};          // kTarget: the point met (world units)
    float hero[3] = {};        // the shooter's feet as drawn (world units)
    float gridDist = 0.0f;     // kTarget: the cell met from the hero's (world units)
    float upm = 1.0f;          // the shooter's world units a metre
};

// version, 0, the sender's unit id, seq, flags, theta (65536 a turn), start, dir (1/32767), at, hero, gridDist (1/100), upm (1/1000)
constexpr uint32_t kShotMsgBytes = 2 + 4 + 2 + 1 + 2 + 12 + 6 + 12 + 12 + 2 + 2;   // 57

inline void PutF(uint8_t* b, float v) { uint32_t u; std::memcpy(&u, &v, 4); poseshare::Put32(b, u); }
inline float GetF(const uint8_t* b) { const uint32_t u = poseshare::Get32(b); float v; std::memcpy(&v, &u, 4); return v; }
inline bool Finite3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

inline uint32_t BuildShotMsg(uint32_t unit, const Shot& s, uint8_t* out, uint32_t cap) {
    if (!out || cap < kShotMsgBytes || !unit || !std::isfinite(s.theta) || !Finite3(s.start) || !Finite3(s.dir) || !Finite3(s.at) || !Finite3(s.hero)) return 0;
    uint8_t* b = out;
    b[0] = poseshare::kVersion;
    b[1] = 0;
    poseshare::Put32(b + 2, unit);
    poseshare::Put16(b + 6, s.seq);
    b[8] = s.flags;
    poseshare::Put16(b + 9, poseshare::QuantAngle(s.theta));
    for (int i = 0; i < 3; ++i) PutF(b + 11 + 4 * i, s.start[i]);
    for (int i = 0; i < 3; ++i) poseshare::Put16(b + 23 + 2 * i, (uint16_t)(int16_t)std::lround(std::clamp(s.dir[i], -1.0f, 1.0f) * 32767.0f));
    for (int i = 0; i < 3; ++i) PutF(b + 29 + 4 * i, s.at[i]);
    for (int i = 0; i < 3; ++i) PutF(b + 41 + 4 * i, s.hero[i]);
    poseshare::Put16(b + 53, (uint16_t)std::lround(std::clamp(s.gridDist, 0.0f, 655.0f) * 100.0f));
    poseshare::Put16(b + 55, (uint16_t)std::lround(std::clamp(s.upm, 0.0f, 65.0f) * 1000.0f));
    return kShotMsgBytes;
}

inline bool ParseShotMsg(const void* data, uint32_t size, uint32_t* unit, Shot* s) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || size < kShotMsgBytes || b[0] != poseshare::kVersion || !unit || !s) return false;
    Shot o;
    *unit = poseshare::Get32(b + 2);
    o.seq = poseshare::Get16(b + 6);
    o.flags = b[8];
    o.theta = poseshare::DequantAngle(poseshare::Get16(b + 9));
    for (int i = 0; i < 3; ++i) o.start[i] = GetF(b + 11 + 4 * i);
    float len = 0.0f;
    for (int i = 0; i < 3; ++i) { o.dir[i] = (float)(int16_t)poseshare::Get16(b + 23 + 2 * i) / 32767.0f; len += o.dir[i] * o.dir[i]; }
    for (int i = 0; i < 3; ++i) o.at[i] = GetF(b + 29 + 4 * i);
    for (int i = 0; i < 3; ++i) o.hero[i] = GetF(b + 41 + 4 * i);
    o.gridDist = poseshare::Get16(b + 53) / 100.0f;
    o.upm = poseshare::Get16(b + 55) / 1000.0f;
    len = std::sqrt(len);
    if (!*unit || !(len > 0.5f) || !Finite3(o.start) || !Finite3(o.at) || !Finite3(o.hero) || !(o.upm > 0.01f)) return false;
    for (int i = 0; i < 3; ++i) o.dir[i] /= len;
    *s = o;
    return true;
}

// ---- the players' lines, by unit (the host's for the create; a receiver's for the drawn arrows) ----------------------------------------------------------

constexpr int kMaxOwners = 8;
constexpr uint64_t kLineMs = 1500;    // a line this old is no shot's any more (the window arrow_flight takes an arrow in)
constexpr uint64_t kPressMs = 3000;   // ... and a press this old none of its attack's (a slow bow: about a second)

struct Line {
    uint32_t unit = 0;
    Shot shot;                 // the latest
    uint64_t gotMs = 0;        // when the latest came
    uint16_t pressSeq = 0;
    float pressTheta = 0.0f;   // the heading the press gave (the first message of its seq)
    bool pressKnown = false;
    uint64_t pressMs = 0;
};

class Owners {
public:
    // A line came. A new seq (or a press's own message) starts its press: the heading the create's turn is counted from.
    void Note(uint32_t unit, const Shot& s, uint64_t nowMs) {
        if (!unit) return;
        Line* l = Find(unit);
        if (!l) {
            l = Free(nowMs);
            *l = Line{};
            l->unit = unit;
        }
        const bool newPress = !l->pressKnown || l->pressSeq != s.seq || (s.flags & kPress);
        if (newPress) {
            l->pressSeq = s.seq;
            l->pressTheta = s.theta;
            l->pressKnown = true;
            l->pressMs = nowMs;
        }
        l->shot = s;
        l->gotMs = nowMs;
    }
    // The unit's line now, if it is fresh (kLineMs) and its press not too old (kPressMs).
    bool Get(uint32_t unit, uint64_t nowMs, Line* out) const {
        for (const Line& l : l_)
            if (l.unit && l.unit == unit) {
                if (nowMs < l.gotMs || nowMs - l.gotMs > kLineMs || nowMs - l.pressMs > kPressMs) return false;
                *out = l;
                return true;
            }
        return false;
    }

private:
    Line* Find(uint32_t unit) {
        for (Line& l : l_) if (l.unit == unit) return &l;
        return nullptr;
    }
    Line* Free(uint64_t nowMs) {   // an empty one, else the oldest
        Line* best = &l_[0];
        for (Line& l : l_) {
            if (!l.unit) return &l;
            if (nowMs - l.gotMs > nowMs - best->gotMs) best = &l;
        }
        return best;
    }
    Line l_[kMaxOwners];
};

}  // namespace shotshare
