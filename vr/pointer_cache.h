// pointer_cache.h - the mouse pointer's images over the flat picture (vr/xr.cpp MakePointerImage), kept by what they show. Pure C++ (tools/pointer_cache_test.cpp).
//
// 2026-10-27 10:37:06, PC1 on Virtual Desktop: in an area change the game made its pointer anew every ~500 ms - a new HCURSOR each time, the same picture or
// one of two. Each new handle made a new static swapchain and retired the old one; their destroys came while the runtime still read one (DRED: a page fault where
// five "Swap Chain" resources had just been freed - those five), and the device was removed (vr/chain_reaper.h). Here an image is known by its pixels, its size
// and its hot spot: the same picture under a new handle takes the swapchain it already has, nothing made, nothing destroyed. Up to kKeep pictures are kept for
// the session; a new one past that puts the least lately used one out (to the reaper), never the one in use.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ptrcache {

constexpr std::size_t kKeep = 8;

// FNV-1a over the pixels, then the size and the hot spot
inline uint64_t Key(const uint32_t* rgba, std::size_t count, int n, int hotX, int hotY) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint32_t v) {
        for (int b = 0; b < 4; ++b) { h ^= (v >> (8 * b)) & 0xFF; h *= 1099511628211ull; }
    };
    for (std::size_t i = 0; i < count; ++i) mix(rgba[i]);
    mix((uint32_t)n);
    mix((uint32_t)hotX);
    mix((uint32_t)hotY);
    return h ? h : 1;   // (0 is no key)
}

template <class H>
class Cache {
  public:
    explicit Cache(std::size_t keep = kKeep) : keep_(keep ? keep : 1) {}
    // The handle kept for this picture (it is now the latest used), or H{} when there is none.
    H Find(uint64_t key) {
        for (Entry& e : e_)
            if (e.key == key) { e.used = ++tick_; return e.h; }
        return H{};
    }
    // A new picture's handle. Past keep_: the least lately used one is taken out and returned, for the caller to retire; H{} when none went.
    H Put(uint64_t key, H h) {
        H out{};
        if (e_.size() >= keep_) {
            std::size_t old = 0;
            for (std::size_t i = 1; i < e_.size(); ++i) if (e_[i].used < e_[old].used) old = i;
            out = e_[old].h;
            e_.erase(e_.begin() + (std::ptrdiff_t)old);
        }
        e_.push_back(Entry{key, h, ++tick_});
        return out;
    }
    std::size_t Size() const { return e_.size(); }
    // The session's end: each handle to f, none kept.
    template <class F>
    void All(F&& f) {
        for (const Entry& e : e_) f(e.h);
        e_.clear();
    }

  private:
    struct Entry { uint64_t key; H h; uint64_t used; };
    std::vector<Entry> e_;
    std::size_t keep_;
    uint64_t tick_ = 0;
};

}  // namespace ptrcache
