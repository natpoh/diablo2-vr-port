// pad_presence.h - the game must never think its pad left. Pure: no game, no XInput.
//
// Why (found in the log of 2026-10-14 15:06, and in the game's own code): the game polls XInputGetState for four pads in
// OsControllerUpdateControllerMap (0x1210940, Storm\Os\src\W32\Controller.cpp). A poll that does not return ERROR_SUCCESS is a
// DISCONNECT: the pad's "connected" byte (0x35620D0 + 0x30 * n) goes to 0, the game raises its controller event and the UI opens
// the panel "PLEASE RECONNECT CONTROLLER - OK (A)" (ControllerReconnectPanel, handler 0x13B740), which blocks the game until it
// is dismissed; and a pad that is not connected is polled again only after 0xEE6B2800 clock units (about 4 s). One failed poll is
// therefore a panel and four blind seconds.
// The plugin's own pad (direct input) used to let go after a second without headset frames (kStaleMs): a draw-thread stall of
// 1.36 s (an area change, 15:06:24) made the first poll after it come back "not connected". The same for a BodyWalk that stalls
// for a second, or a session that ends, with the real XInput answering "no pad".
//
// So in VR, once the plugin is the pad (direct input on, or it ever gave one), pad 0 is ALWAYS answered ERROR_SUCCESS: with its
// own state when it has one, else a neutral one with a number that keeps moving.
#pragma once

#include <atomic>
#include <cstdint>

namespace padpresence {

// Who answered pad 0 (the game's index 0) this poll.
enum class Source : uint8_t {
    Absent,      // ERROR_DEVICE_NOT_CONNECTED went to the game (the fault, unless nothing is meant to be there)
    Real,        // the real XInput's answer (a pad plugged in, or a virtual one)
    BodyWalk,    // BodyWalk's own pad report
    Direct,      // the plugin's own pad (direct input)
    Kept,        // a neutral pad made to keep the pad present: the plugin's source is missing for now
    Keys,        // the keyboard's W A S D as a pad (shooter controls)
    Walk,        // VR F1-F3 without BodyWalk: a pad with the controller's left stick and nothing else ([direct] walk_flat_views)
};

inline const char* Name(Source s) {
    switch (s) {
    case Source::Absent: return "ABSENT (not connected)";
    case Source::Real: return "the real XInput pad";
    case Source::BodyWalk: return "BodyWalk's pad";
    case Source::Direct: return "the plugin's own pad";
    case Source::Kept: return "a neutral pad kept present";
    case Source::Keys: return "the keyboard as a pad";
    case Source::Walk: return "the plugin's left stick only (F1-F3)";
    }
    return "?";
}

// Must pad 0 be answered present whatever the source says? In VR, when the plugin is meant to be the pad (direct input is on) or
// has ever been (BodyWalk's report, or its own, was given this run). A flat game, or VR with no pad of the plugin's, is not touched.
inline bool MustStayPresent(bool vrPlatform, bool directOn, bool pluginPadSeen) {
    return vrPlatform && (directOn || pluginPadSeen);
}

// Tells when the answer changed (for the log). Any thread.
class Tracker {
public:
    // True if `now` differs from the last one seen (the first call too); *was is the one before (Absent at first).
    bool Step(Source now, Source* was) {
        const uint8_t before = last_.exchange((uint8_t)now);
        if (was) *was = (Source)before;
        const bool first = !seen_.exchange(true);
        return first || before != (uint8_t)now;
    }

private:
    std::atomic<uint8_t> last_{(uint8_t)Source::Absent};
    std::atomic<bool> seen_{false};
};

// Lines for a thing that may flap: the first `kHead` all, then every `kEvery`th, so a pad that comes and goes cannot flood the log.
class Said {
public:
    static constexpr uint32_t kHead = 80, kEvery = 50;
    bool Next() {
        const uint32_t n = n_.fetch_add(1) + 1;
        return n <= kHead || n % kEvery == 0;
    }
    uint32_t Count() const { return n_.load(); }

private:
    std::atomic<uint32_t> n_{0};
};

// The state of the neutral pad: no buttons, no sticks, a number that moves with every poll (a pad that never changes its packet
// reads as a stuck device to some code).
struct NeutralPacket {
    uint32_t Next() { return n_.fetch_add(1) + 1; }

private:
    std::atomic<uint32_t> n_{0x30000000};
};

}  // namespace padpresence
