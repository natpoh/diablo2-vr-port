// shot_trace.h - [debug] shot_trace: what the game does with a shot of ours (a bow's arrow, a blow, a throw - an attack the plugin presses
// with Stand Still), told line by line in the log. Pure (no game, no clock of its own); unit test cleanroom/tests/shot_trace_test.cpp.
//
// The fault it is for: in a log of 2026-10-14 18:05 four bow shots of twenty were followed by the game running the hero off - a run to a
// point (command 3 of the client's funnel 0xFABE0) 3 ms after the pad's A went down, out of the map click's pad types (7 .. 10), and a
// second one at A's release, with Stand Still "held" by the plugin's own account, in the same direction and from the same spot as shots
// that did not run. The log has no line that tells the two apart: not the Stand Still flag the click carried, not the command the click
// built, not the hero's mode, not the way from the funnel back to the click. So, for each shot - from the press of an attack of ours
// until 1.5 s after it is let go - every map click, the command the game's click builds from it, and every command that goes through the
// funnel is told, with the funnel's way back up the stack (return addresses as RVAs).
//
// Nothing here touches the game; vr/shot_trace_wire.cpp feeds it and prints.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace shottrace {

constexpr uint64_t kTailMs = 1500;     // a shot is watched this long after its press is let go
constexpr uint32_t kMaxLines = 120;    // lines for one shot (a held attack repeats)
constexpr uint32_t kMaxShots = 60;     // shots a session

// The window of one shot: opens when an attack of ours is pressed, closes kTailMs after it is let go. Fed with the same `busy` every
// time it is asked (the stand-still machine's); cheap and idempotent for one instant.
class Window {
public:
    // True while a shot is being watched. `newShot`: this call opened it.
    bool Step(bool busy, uint64_t nowMs, bool* newShot = nullptr) {
        if (newShot) *newShot = false;
        if (busy) {
            openUntil_ = 0;
            if (!open_) {
                if (shots_ >= kMaxShots) return false;
                open_ = true;
                ++shots_;
                since_ = nowMs;
                lines_ = 0;
                if (newShot) *newShot = true;
            }
            return true;
        }
        if (open_) {
            if (!openUntil_) openUntil_ = nowMs + kTailMs;
            if (nowMs >= openUntil_) { open_ = false; openUntil_ = 0; }
        }
        return open_;
    }
    bool Open() const { return open_; }
    uint32_t Shot() const { return shots_; }
    uint32_t SinceMs(uint64_t nowMs) const { return nowMs >= since_ ? (uint32_t)(nowMs - since_) : 0; }
    // One more line allowed for this shot? (The cap says itself once, with the line that goes over.)
    bool TakeLine(bool* last = nullptr) {
        if (last) *last = false;
        if (lines_ >= kMaxLines) return false;
        ++lines_;
        if (last) *last = lines_ == kMaxLines;
        return true;
    }
    void Reset() { *this = Window(); }

private:
    bool open_ = false;
    uint32_t shots_ = 0, lines_ = 0;
    uint64_t since_ = 0, openUntil_ = 0;
};

// The client's funnel, cmd = the D2 packet id (vr/stray_walk.h).
inline const char* CommandName(int cmd) {
    switch (cmd) {
    case 1: return "walk to a point";
    case 2: return "walk to a unit";
    case 3: return "run to a point";
    case 4: return "run to a unit";
    case 0x42: return "the stick's direction";
    default: return cmd >= 5 && cmd < 0x42 ? "a skill (or another packet)" : "another command";
    }
}

// A unit's type (unit + 0), the D2 unit kinds; the funnel's commands 2 and 4 (walk / run to a unit) carry it as their first argument, the unit's id as the second.
inline const char* UnitKindName(uint32_t type) {
    switch (type) {
    case 0: return "player";
    case 1: return "monster";
    case 2: return "object";
    case 3: return "missile";
    case 4: return "item";
    case 5: return "tile";
    default: return "unit?";
    }
}

// The hero's mode (unit + 0xC), the D2 player modes.
inline const char* ModeName(uint32_t mode) {
    static const char* const n[] = {"death", "neutral", "walk", "run", "get hit", "town neutral", "town walk", "attack 1", "attack 2", "block",
                                    "cast", "throw", "kick", "skill 1", "skill 2", "skill 3", "skill 4", "dead", "sequence", "knocked back"};
    return mode < sizeof n / sizeof n[0] ? n[mode] : "?";
}

// The map click's 5th argument (0x8AFD0's word): the low byte is Stand Still, the high byte Run, each 0 or 1.
inline bool ClickStandStill(uint64_t flags) { return (flags & 0xFF) != 0; }
inline bool ClickRun(uint64_t flags) { return (flags & 0xFF00) != 0; }

// The click's type, as stray_walk.h names the groups.
inline const char* ClickTypeName(int type) {
    switch (type) {
    case 0: return "left press";
    case 1: return "left held";
    case 2: return "left release";
    case 3: return "right press";
    case 4: return "right held";
    case 5: return "right release";
    case 6: return "the stick's stop";
    case 7: return "pad cast, press (hand 1)";
    case 8: return "pad cast, held (hand 1)";
    case 9: return "pad cast, press (hand 2)";
    case 10: return "pad cast, held (hand 2)";
    case 11: case 12: case 13: return "Force Move";
    case 14: case 15: case 16: case 17: return "keyboard-layout cast";
    default: return "another type";
    }
}

// The command the game's click builds (the record 0x102490 gets) as bits of its first dword: the click sets 1 / 2 (the button), 4 / 8 / 0x10 (press,
// held, release), 0x20 Stand Still, 0x40 Run (only with stamina), 0x80 set just before the send, 0x200 / 0x400 the keyboard layout's.
inline int CommandFlagsText(uint32_t f, char* out, size_t n) {
    return snprintf(out, n, "0x%X%s%s%s", f, (f & 0x20) ? " [Stand Still]" : "", (f & 0x40) ? " [Run]" : "", (f & 0x80) ? " [sent]" : "");
}

// Return addresses as RVAs, "0x101F78 <- 0x102A95 <- 0xFE69F": frames outside the game's image ("?") are told as they are. `base` / `size` = the image.
inline int FramesText(void* const* frames, uint32_t count, uintptr_t base, uintptr_t size, char* out, size_t n) {
    size_t at = 0;
    if (n) out[0] = 0;
    for (uint32_t i = 0; i < count && at + 24 < n; ++i) {
        const uintptr_t a = (uintptr_t)frames[i];
        const int w = a >= base && a - base < size ? snprintf(out + at, n - at, "%s0x%llX", i ? " <- " : "", (unsigned long long)(a - base))
                                                   : snprintf(out + at, n - at, "%s?%llX", i ? " <- " : "", (unsigned long long)a);
        if (w < 0) break;
        at += (size_t)w;
    }
    return (int)at;
}

}  // namespace shottrace
