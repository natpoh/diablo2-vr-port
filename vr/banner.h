// banner.h - a short message that must be SEEN (the capture key's "D2R VR capture saved ..." / why it failed): big in the middle of the flat picture
// (vr/patch_hunt.cpp's overlay line) and, in VR on native OpenXR, on a panel before the face (vr/xr_banner.h) - for a few seconds. One at a time; a
// newer one replaces it. Any thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace banner {

struct Message {
    std::string text;
    bool ok = true;           // green (done) / orange (failed)
    uint64_t until = 0;       // GetTickCount64
    uint32_t serial = 0;      // changes with every Show: a painter paints again
};

inline std::mutex g_lock;
inline Message g_msg;
inline std::atomic<uint64_t> g_until{0};

inline void Show(const std::string& text, bool ok, uint64_t now, unsigned ms) {
    std::lock_guard<std::mutex> l(g_lock);
    g_msg.text = text;
    g_msg.ok = ok;
    g_msg.until = now + ms;
    ++g_msg.serial;
    g_until.store(g_msg.until);
}
// The message in force at `now` (false: none - one relaxed load).
inline bool Now(uint64_t now, Message* out) {
    if (now >= g_until.load(std::memory_order_relaxed)) return false;
    std::lock_guard<std::mutex> l(g_lock);
    *out = g_msg;
    return now < out->until;
}

}  // namespace banner
