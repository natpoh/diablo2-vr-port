// anim_cut.cpp - [bow] quick_release, [hands] quick_throw and [hands] quick_strike, the game's side: find the game's AnimData table in memory, move the action-frame flag of the hero class's rows of
// one kind (the bow's attack, or every throw) to an early frame while the cut is wanted, and put it back exactly when it is not (the pure part - the shape of a row, the move
// and the way back, the walk over a block - and the reasons are in vr/anim_cut.h).
//
// Nothing is called in the game and nothing is hooked: the table sets are pointers in a global array (RVA 0x2A9A580 in 3.3.93787, found again by a signature in
// cleanroom/sigscan/game_sigs.h: AnimTables), each at +0x14D8 the AnimData table whose first qword is the block the file was read into; the block is walked bucket by
// bucket (u32 count + count rows of 160 bytes) and the rows are found by their NAME (the class's token and the kind's mode) and then checked by their whole SHAPE. Every
// read and write is behind an SEH guard. A row that is not as it was read - another name, frames, speed or flags - is left alone, once, with the reason in the log; a row
// changed under us (the game reloaded its tables) is forgotten, not "restored". Offline: the table is the one the local game and its server read; with a remote server only
// this client's copy would change, so the options say so and are not for online play. The three kinds are independent (three switches, three frames, three sets of rows).

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <D2RLPlugin/api.h>

#include "anim_cut.h"
#include "sigscan.h"

using namespace D2RL;

namespace animcut {
namespace {

constexpr uint64_t RVA_ANIM_TABLES = 0x2A9A580;   // the key in game_sigs.h
constexpr size_t kSets = 4, kSetStep = 16, kAnimTableAt = 0x14D8;
constexpr int kMaxPatches = 96;   // a class has up to 7 throw rows (one per weapon class) or 17 melee rows (A1 and A2 of up to 8 weapon classes, Smite's S1), in up to 4 table copies

const PluginContext* g_ctx = nullptr;

struct Patch {
    uintptr_t row = 0;
    size_t copy = 0;
    char name[8] = {};
    uint32_t frames = 0, speed = 0;
    int from = 0, at = 0;
    uint8_t val = 0;
};

struct Feature {
    Feature(Kind k, const char* l, const char* w) : kind(k), label(l), what(w) {}
    const Kind kind;
    const char* const label;   // for the log: "bow quick release" / "quick throw"
    const char* const what;    // "the arrow" / "the thrown missile"
    std::atomic<bool> wanted{false};
    std::atomic<int> frame{2};
    std::atomic<uint32_t> cutMs{0};
    SRWLOCK lock = SRWLOCK_INIT;
    Patch p[kMaxPatches];
    int np = 0;
    int patchedClass = -1, patchedFrame = -1;
    ULONGLONG retryAt = 0, verifyAt = 0, waitSince = 0;
    unsigned tries = 0;
    std::string lastSaid;
};
Feature g_bow(Kind::Bow, "bow quick release", "the arrow");
Feature g_throw(Kind::Throw, "quick throw", "the thrown missile");
Feature g_melee(Kind::Melee, "quick strike", "the blow");
Feature& Of(Kind k) { return k == Kind::Bow ? g_bow : k == Kind::Throw ? g_throw : g_melee; }

void Log(const char* text) { if (g_ctx) g_ctx->LogInfo(text); }
template <typename... A> void LogF(const char* fmt, A... a) {
    char b[640];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}
// One reason, once: the same text is not said again until something else has been said.
void SayOnce(Feature& f, const std::string& text) {
    if (f.lastSaid == text) return;
    f.lastSaid = text;
    Log(text.c_str());
}

bool ReadMem(void* d, uintptr_t s, size_t n) noexcept {
    if (s < 0x10000) return false;
    __try { memcpy(d, reinterpret_cast<const void*>(s), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool WriteByte(uintptr_t d, uint8_t v) noexcept {
    if (d < 0x10000) return false;
    __try { *reinterpret_cast<volatile uint8_t*>(d) = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

double MsOf(int frame, uint32_t speed) { return speed ? (double)frame * 40.0 * 256.0 / (double)speed : 0.0; }   // a frame is a game tick (40 ms) at speed 256

// The distinct blocks the table sets hold the file in. why: what was missing when there are none.
size_t FindBlocks(uintptr_t* out, size_t max, const char** why) {
    *why = "";
    const uintptr_t arr = d2rsig::Addr(RVA_ANIM_TABLES);
    if (!arr) { *why = "the game's table sets are not found in this build"; return 0; }
    size_t n = 0;
    for (size_t i = 0; i < kSets && n < max; ++i) {
        uintptr_t set = 0, tbl = 0, base = 0, b0 = 0, b1 = 0;
        if (!ReadMem(&set, arr + i * kSetStep, sizeof set) || set < 0x10000) continue;
        if (!ReadMem(&tbl, set + kAnimTableAt, sizeof tbl) || tbl < 0x10000) continue;
        if (!ReadMem(&base, tbl, sizeof base) || base < 0x10000) continue;
        // the shape of the table: its first two bucket pointers are the block's start and what follows the first bucket
        int32_t count0 = 0;
        if (!ReadMem(&b0, tbl + 8, sizeof b0) || !ReadMem(&b1, tbl + 16, sizeof b1) || !ReadMem(&count0, base, sizeof count0)) continue;
        if (b0 != base || count0 < 0 || (uint32_t)count0 > kMaxBucketRows || b1 != base + 4 + (uintptr_t)count0 * kRowSize) continue;
        bool dup = false;
        for (size_t k = 0; k < n; ++k) dup = dup || out[k] == base;
        if (!dup) out[n++] = base;
    }
    if (!n) *why = "no table set holds an AnimData table of the shape that was read";
    return n;
}

// Puts everything back that is still as we left it. why: for the log.
void RestoreAll(Feature& f, const char* why) {
    if (!f.np) return;
    int back = 0, gone = 0, other = 0;
    std::string names;
    for (int i = 0; i < f.np; ++i) {
        const Patch& p = f.p[i];
        char nm[9] = {};
        std::memcpy(nm, p.name, 7);
        if (names.find(nm) == std::string::npos) names += (names.empty() ? "" : ", ") + std::string(nm) + " frame " + std::to_string(p.from);
        uint8_t row[kRowSize];
        if (!ReadMem(row, p.row, sizeof row)) { ++gone; continue; }
        if (!InPatchedState(row, p.from, p.at, p.val) || std::memcmp(row, p.name, kNameBytes) != 0) { ++other; continue; }
        if (WriteByte(p.row + kFlagsAt + (size_t)p.at, 0) && WriteByte(p.row + kFlagsAt + (size_t)p.from, p.val)) ++back; else ++gone;
    }
    LogF("%s: off (%s) - the action flag is back (%s) in %d row%s%s%s", f.label, why, names.c_str(), back, back == 1 ? "" : "s",
         other ? "; a row had been changed by the game since: left as it is" : "", gone ? "; a row was gone: nothing to put back" : "");
    f.np = 0;
    f.patchedClass = f.patchedFrame = -1;
    f.cutMs.store(0);
}

// Finds the hero class's rows of this kind and moves their flags. Returns true if any row was moved.
bool Apply(Feature& f, int cls, int wantedFrame) {
    const char* token = ClassToken(cls);
    if (!token) return false;
    uintptr_t blocks[kSets];
    const char* why = "";
    const size_t nb = FindBlocks(blocks, kSets, &why);
    if (!nb) { SayOnce(f, std::string(f.label) + ": nothing done - " + why); return false; }
    struct Hit { uintptr_t at; size_t copy; };
    std::vector<Hit> hits;
    std::string refused;
    for (size_t b = 0; b < nb; ++b) {
        auto read = [&](size_t off, void* dst, size_t n) { return ReadMem(dst, blocks[b] + off, n); };
        const bool walked = WalkRows(read, [&](const uint8_t* head) { return IsRowName(head, token, f.kind); },
                                     [&](size_t off) { if (hits.size() < (size_t)kMaxPatches) hits.push_back({blocks[b] + off, b}); });
        if (!walked) refused = "the table block is not AnimData-shaped after all";
    }
    int moved = 0, nameCount = 0;
    uint32_t cut = 0;
    std::string names;
    int toFrame = 0;
    for (const Hit& h : hits) {
        uint8_t row[kRowSize];
        if (!ReadMem(row, h.at, sizeof row)) { refused = "a row could not be read"; continue; }
        Row r;
        const Verdict v = Check(row, token, f.kind, &r);
        char nm[9] = {};
        std::memcpy(nm, r.name, 7);
        if (v == Verdict::Nothing) { refused = std::string(nm) + ": " + Why(v); continue; }
        if (v != Verdict::Ok) { refused = std::string(nm) + " looks different from what was read - " + Why(v) + " (frames " + std::to_string(r.frames) + ", speed " + std::to_string(r.speed) + ")"; continue; }
        const int to = Target(wantedFrame, r.flagIdx);
        if (to >= r.flagIdx) { refused = std::string(nm) + ": the action frame is already at " + std::to_string(r.flagIdx) + ", not later than the wished " + std::to_string(wantedFrame); continue; }
        if (f.np >= kMaxPatches) continue;
        // cleared first, then set: never two flags at once
        if (!WriteByte(h.at + kFlagsAt + (size_t)r.flagIdx, 0)) { refused = "a row could not be written"; continue; }
        if (!WriteByte(h.at + kFlagsAt + (size_t)to, r.flagVal)) { WriteByte(h.at + kFlagsAt + (size_t)r.flagIdx, r.flagVal); refused = "a row could not be written"; continue; }
        Patch& p = f.p[f.np++];
        p.row = h.at;
        p.copy = h.copy;
        std::memcpy(p.name, r.name, kNameBytes);
        p.frames = r.frames; p.speed = r.speed; p.from = r.flagIdx; p.at = to; p.val = r.flagVal;
        ++moved;
        toFrame = to;
        cut = std::max(cut, CutMs(r.flagIdx, to, r.speed));
        if (h.copy == 0) {
            if (++nameCount <= 4) names += (names.empty() ? "" : ", ") + std::string(nm) + " (" + std::to_string(r.frames) + " frames) from frame " + std::to_string(r.flagIdx) + " = " + std::to_string((int)(MsOf(r.flagIdx, r.speed) + 0.5)) + " ms";
        }
    }
    if (nameCount > 4) names += " and " + std::to_string(nameCount - 4) + " more";
    if (moved) {
        f.patchedClass = cls;
        f.patchedFrame = wantedFrame;
        f.cutMs.store(cut);
        f.lastSaid.clear();
        LogF("%s: on - the hero's rows %s: the action flag moved to frame %d (%d ms after the animation starts), %d row%s in %zu table cop%s. The animation's length, speed and the attack rate stay; "
             "%s is made earlier inside the animation (up to %u ms sooner)",
             f.label, names.c_str(), toFrame, (int)(MsOf(toFrame, 256) + 0.5), moved, moved == 1 ? "" : "s", nb, nb == 1 ? "y" : "ies", f.what, cut);
        return true;
    }
    if (hits.empty()) refused = std::string("the ") + ClassToken(cls) + " class has no row of this kind in the table" + (refused.empty() ? "" : ": " + refused);
    SayOnce(f, std::string(f.label) + ": nothing done - " + (refused.empty() ? std::string("no row could be moved") : refused));
    return false;
}

}  // namespace

void Init(const PluginContext* ctx) { g_ctx = ctx; }

void Configure(Kind kind, bool on, int frame) {
    Feature& f = Of(kind);
    f.frame.store(std::clamp(frame, 1, 40));
    f.wanted.store(on);
}

bool Active(Kind kind) { return Of(kind).np > 0; }
uint32_t CutMs(Kind kind) { return Of(kind).cutMs.load(); }

void Tick(Kind kind, bool want, int cls, bool acting, uint64_t nowMs) {
    Feature& f = Of(kind);
    if (!TryAcquireSRWLockExclusive(&f.lock)) return;
    const bool wish = f.wanted.load() && want && ClassToken(cls) != nullptr;
    const int frame = f.frame.load();
    if (f.np) {
        // put back: the cut is not wanted any more, or the class or the frame is not what was moved
        const char* why = !f.wanted.load() ? "the option is off" : !want ? (kind == Kind::Bow ? "not in VR first person with a bow in hand" : "not in VR first person") :
                          cls != f.patchedClass ? "another class" : frame != f.patchedFrame ? "the frame was changed" : nullptr;
        if (why) {
            if (acting && f.waitSince == 0) f.waitSince = nowMs;
            if (!acting || nowMs - f.waitSince > 3000) { RestoreAll(f, why); f.waitSince = 0; f.retryAt = 0; }
        } else {
            f.waitSince = 0;
            if (nowMs >= f.verifyAt) {   // once a second: still as we left it? (the game may have reloaded its tables)
                f.verifyAt = nowMs + 1000;
                for (int i = 0; i < f.np; ++i) {
                    uint8_t row[kRowSize];
                    if (!ReadMem(row, f.p[i].row, sizeof row) || !InPatchedState(row, f.p[i].from, f.p[i].at, f.p[i].val) || std::memcmp(row, f.p[i].name, kNameBytes) != 0) {
                        LogF("%s: the table is not as it was left (the game reloaded or changed it) - forgotten, looked for again", f.label);
                        f.np = 0;
                        f.patchedClass = f.patchedFrame = -1;
                        f.cutMs.store(0);
                        f.retryAt = 0;
                        break;
                    }
                }
            }
        }
    } else if (wish && !acting && nowMs >= f.retryAt) {
        if (Apply(f, cls, frame)) f.tries = 0;
        else f.retryAt = nowMs + (++f.tries < 6 ? 3000 : 30000);   // not found / different: looked at again now and then, said once
    } else if (!wish) {
        f.tries = 0;
        f.retryAt = 0;
    }
    ReleaseSRWLockExclusive(&f.lock);
}

void Shutdown() {
    for (Feature* f : {&g_bow, &g_throw, &g_melee}) {
        AcquireSRWLockExclusive(&f->lock);
        f->wanted.store(false);
        RestoreAll(*f, "the plugin unloads");
        ReleaseSRWLockExclusive(&f->lock);
    }
}

}  // namespace animcut
