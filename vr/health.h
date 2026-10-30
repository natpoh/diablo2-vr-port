// [debug] health: one line per feature every 10 s, and a mark where a chain is broken (docs/plan_modules.md, section 4).
//
//   vrcam: health body: pushes 5 | poses asked 1180 | poses given 0 (!) pushed, never given to the skinning
//   vrcam: health sky: presents 600 | post sky 0 | game dome 600 | post fog 600 | game fog 0 | both skies 0 | both fogs 0 | both ceilings 0
//
// Counters are relaxed atomics, counted only while the key is on (one relaxed load when off) and taken each window. The rule for the mark
// "(!)": a producer counted and its consumer not in the same window (pushes, but no pose given to the skinning), or two drawers of one
// thing in the same present (both skies, both fogs, both ceilings). It only counts and writes lines; it changes nothing in play.
// The pure part (the counters, the rule, the line) - tested by cleanroom/tests/health_test.cpp; the counts are taken in vr/vrcam.cpp.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

namespace health {

// The key's state: read by every counter (relaxed), set by the settings' load.
inline std::atomic<bool>& OnFlag() { static std::atomic<bool> on{false}; return on; }
inline bool On() { return OnFlag().load(std::memory_order_relaxed); }
inline void SetOn(bool on) { OnFlag().store(on, std::memory_order_relaxed); }

// One count of a window.
struct Count {
    std::atomic<uint32_t> n{0};
    void Add(uint32_t k = 1) { if (On()) n.fetch_add(k, std::memory_order_relaxed); }
    uint32_t Take() { return n.exchange(0, std::memory_order_relaxed); }
};

// A chain is broken when what was made never reached what uses it.
inline bool Broken(uint32_t produced, uint32_t consumed) { return produced > 0 && consumed == 0; }

struct Item { const char* label; uint32_t value; };

// "vrcam: health <feature>: a 1 | b 2 (!) why" - the mark and its why only when `why` is given. Empty when nothing was counted at all
// (a feature that did nothing in the window writes no line).
template <size_t N>
std::string Line(const char* feature, const Item (&items)[N], const char* why = nullptr) {
    bool any = false;
    for (const Item& it : items) any = any || it.value != 0;
    if (!any && !why) return std::string();
    std::string s = "vrcam: health ";
    s += feature;
    s += ':';
    char b[32];
    for (size_t i = 0; i < N; ++i) {
        s += i ? " | " : " ";
        s += items[i].label;
        std::snprintf(b, sizeof b, " %u", items[i].value);
        s += b;
    }
    if (why) {
        s += " (!) ";
        s += why;
    }
    return s;
}

// The body: the weapon's pushes into the bodies (weight -> meleetouch::PushBody) and the turned poses the skeleton hook and the
// skinning are given back (meleetouch::MonsterPose, MonsterPosesForSkin) - the chain the NPCs went still on in 0.188.6.
struct Body {
    Count pushes, posesAsked, posesGiven, skinAsked, skinGiven;
    std::string Take() {
        const uint32_t p = pushes.Take(), pa = posesAsked.Take(), pg = posesGiven.Take(), sa = skinAsked.Take(), sg = skinGiven.Take();
        const Item items[] = {{"pushes", p}, {"poses asked", pa}, {"poses given", pg}, {"skinning asked", sa}, {"skinning given", sg}};
        return Line("body", items, Broken(p, pg + sg) ? "pushed, never given to the hook or the skinning" : nullptr);
    }
};

// The sky, the fog and the ceiling as each present of our effect sets them (who draws each): ours (the post pass) or the game's.
struct Sky {
    Count presents, postSky, gameDome, postFog, gameFog, postCeil, gameCeil, bothSkies, bothFogs, bothCeils;
    // One present: what our effect draws and what the game's own passes draw in it.
    void Present(bool ourSky, bool dome, bool ourFog, bool theirFog, bool ourCeil, bool theirCeil) {
        if (!On()) return;
        presents.Add();
        if (ourSky) postSky.Add();
        if (dome) gameDome.Add();
        if (ourFog) postFog.Add();
        if (theirFog) gameFog.Add();
        if (ourCeil) postCeil.Add();
        if (theirCeil) gameCeil.Add();
        if (ourSky && dome) bothSkies.Add();
        if (ourFog && theirFog) bothFogs.Add();
        if (ourCeil && theirCeil) bothCeils.Add();
    }
    std::string Take() {
        const uint32_t pr = presents.Take(), s = postSky.Take(), d = gameDome.Take(), f = postFog.Take(), g = gameFog.Take(), c = postCeil.Take(),
                       gc = gameCeil.Take(), bs = bothSkies.Take(), bf = bothFogs.Take(), bc = bothCeils.Take();
        const Item items[] = {{"presents", pr}, {"post sky", s}, {"game dome", d}, {"post fog", f}, {"game fog", g}, {"post ceiling", c},
                              {"game ceiling", gc}, {"both skies", bs}, {"both fogs", bf}, {"both ceilings", bc}};
        const char* why = bs ? "two skies drawn in one present" : bf ? "two fogs drawn in one present" : bc ? "two ceilings drawn in one present" : nullptr;
        return Line("sky", items, why);
    }
};

}  // namespace health
