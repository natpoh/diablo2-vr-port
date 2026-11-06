// finite_hold.h - nothing non-finite leaves the plugin for FlatVR / BodyWalk. Serg (2026-11-06): "when the camera is not found,
// inside FlatVR everything spins madly in random directions on nan - it must stand still". A NaN or an infinity in a pose FlatVR
// places something by (the head the plugin builds its views from, a panel's forearm, the pointer's depth, the depth's camera) is
// passed on as it is by the reader and spins or throws what it places; a NaN kept in a filter (a speed from two samples) stays there.
//
// Hold<N>: N floats written together. Take() keeps them when every one is finite and puts the last good ones in their place when
// any is not (or `neutral`, or zeros, when there never were good ones) - so what goes out stands still where it was. Due() says
// when a log line about it is due: at the first miss of a run, then at most once every period - never a line a frame.
// Pure (no Windows), so cleanroom/tests/finite_hold_test.cpp runs this very code.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace finitehold {

inline bool Finite(const float* v, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (!std::isfinite(v[i])) return false;
    return true;
}
inline float Or(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

template <size_t N>
struct Hold {
    float good[N] = {};
    bool ok = false;               // a good set was seen
    uint64_t misses = 0;           // sets replaced, all told
    uint64_t missesSinceTold = 0;  // since the last line
    unsigned long long toldAt = 0;
    bool told = false;

    // true: v is finite, kept as the last good; false: v now holds the last good (or neutral / zeros: see HadGood).
    bool Take(float* v, const float* neutral = nullptr) {
        if (Finite(v, N)) {
            memcpy(good, v, sizeof good);
            ok = true;
            return true;
        }
        ++misses;
        ++missesSinceTold;
        if (ok) memcpy(v, good, sizeof good);
        else if (neutral) memcpy(v, neutral, sizeof good);
        else memset(v, 0, sizeof good);
        return false;
    }
    bool HadGood() const { return ok; }
    // A line is due: misses since the last one, and none told yet or `periodMs` gone by. Marks it told.
    bool Due(unsigned long long nowMs, unsigned long long periodMs = 10000) {
        if (!missesSinceTold) return false;
        if (told && nowMs - toldAt < periodMs) return false;
        told = true;
        toldAt = nowMs;
        return true;
    }
    // The count the line reports (those since the last line), reset by it.
    uint64_t TakeCount() {
        const uint64_t n = missesSinceTold;
        missesSinceTold = 0;
        return n;
    }
};

}  // namespace finitehold
