// debug_press.h - a Settings button's press seen by the running plugin: one action per press, none at the start.
//
// 2026-11-12, Serg's 0.213.9 run: [debug] replay_scan_go was a tick "turned on for each" scan. Left at 1 in his ini, it fired at every start (the
// last value seen began as 0) and the scan it started read all of the game's upload memory on the draw thread: the game froze for ~24 s. A
// Settings Button writes a count instead (each press +1, nothing stays "on"); the plugin acts when the count it reads CHANGES while it runs. The
// first read (the start, or the first time the key is read) only remembers the count: a value left in the ini never fires.
// Pure (tools/native_replay_test.cpp).
#pragma once

namespace debugpress {

struct Press {
    bool seen = false;
    int last = 0;
    // The key's value now; true once for each change after the first read.
    bool Pressed(int now) {
        if (!seen) { seen = true; last = now; return false; }
        const bool changed = now != last;
        last = now;
        return changed;
    }
};

}  // namespace debugpress
