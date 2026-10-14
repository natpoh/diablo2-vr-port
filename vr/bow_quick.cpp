// bow_quick.cpp - [bow] quick_release, the game's side: find the game's AnimData table in memory, move the action-frame flag of the hero class's bow attack row to an
// early frame while the cut is wanted, and put it back exactly when it is not (the pure part - the shape of the row, the move and the way back, the walk over a block -
// and the reasons are in vr/bow_quick.h).
//
// Nothing is called in the game and nothing is hooked: the table sets are pointers in a global array (RVA 0x2A9A580 in 3.3.93787, found again by a signature in
// cleanroom/sigscan/game_sigs.h: AnimTables), each at +0x14D8 the AnimData table whose first qword is the block the file was read into; the block is walked bucket by
// bucket (u32 count + count rows of 160 bytes) and the rows are found by their exact NAME and then checked by their whole SHAPE. Every read and write is behind an SEH
// guard. A row that is not as it was read - another name, frames, speed or flags - is left alone, once, with the reason in the log; a row changed under us (the game
// reloaded its tables) is forgotten, not "restored". Offline: the table is the one the local game and its server read; with a remote server only this client's copy
// would change, so the option says so and is not for online play.

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

#include <D2RLPlugin/api.h>

#include "bow_quick.h"
#include "sigscan.h"

using namespace D2RL;

namespace bowquick {
namespace {

constexpr uint64_t RVA_ANIM_TABLES = 0x2A9A580;   // the key in game_sigs.h
constexpr size_t kSets = 4, kSetStep = 16, kAnimTableAt = 0x14D8;
constexpr int kMaxPatches = 8;

const PluginContext* g_ctx = nullptr;
std::atomic<bool> g_wanted{false};
std::atomic<int> g_frame{2};
SRWLOCK g_lock = SRWLOCK_INIT;

struct Patch {
    uintptr_t row = 0;
    char name[8] = {};
    uint32_t frames = 0, speed = 0;
    int from = 0, at = 0;
    uint8_t val = 0;
};
Patch g_p[kMaxPatches];
int g_np = 0;
int g_patchedClass = -1, g_patchedFrame = -1;
ULONGLONG g_retryAt = 0, g_verifyAt = 0, g_waitSince = 0;
unsigned g_tries = 0;
std::string g_lastSaid;

void Log(const char* text) { if (g_ctx) g_ctx->LogInfo(text); }
template <typename... A> void LogF(const char* fmt, A... a) {
    char b[520];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}
// One reason, once: the same text is not said again until something else has been said.
void SayOnce(const char* text) {
    if (g_lastSaid == text) return;
    g_lastSaid = text;
    Log(text);
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
void RestoreAll(const char* why) {
    if (!g_np) return;
    int back = 0, gone = 0, other = 0;
    char name[9] = {};
    int from = 0;
    for (int i = 0; i < g_np; ++i) {
        const Patch& p = g_p[i];
        std::memcpy(name, p.name, 7);
        from = p.from;
        uint8_t row[kRowSize];
        if (!ReadMem(row, p.row, sizeof row)) { ++gone; continue; }
        if (!InPatchedState(row, p.from, p.at, p.val) || std::memcmp(row, p.name, kNameBytes) != 0) { ++other; continue; }
        if (WriteByte(p.row + kFlagsAt + (size_t)p.at, 0) && WriteByte(p.row + kFlagsAt + (size_t)p.from, p.val)) ++back; else ++gone;
    }
    LogF("bow quick release: off (%s) - %s's action flag is back at frame %d in %d row%s%s%s", why, name, from, back, back == 1 ? "" : "s",
         other ? " (a row had been changed by the game since: left as it is)" : "", gone ? " (a row was gone: nothing to put back)" : "");
    g_np = 0;
    g_patchedClass = g_patchedFrame = -1;
}

// Finds the hero class's bow rows and moves their flags. Returns true if any row was moved.
bool Apply(int cls, int wantedFrame) {
    const char* token = ClassToken(cls);
    if (!token) return false;
    uintptr_t blocks[kSets];
    const char* why = "";
    const size_t nb = FindBlocks(blocks, kSets, &why);
    if (!nb) {
        char t[300];
        snprintf(t, sizeof t, "bow quick release: nothing done - %s", why);
        SayOnce(t);
        return false;
    }
    int moved = 0, rowsFound = 0;
    std::string refused;
    for (size_t b = 0; b < nb; ++b) {
        for (const char mode : {'1', '2'}) {
            char name[8] = {token[0], token[1], 'A', mode, 'B', 'O', 'W', 0};
            size_t offs[4];
            size_t nHit = 0;
            auto read = [&](size_t off, void* dst, size_t n) { return ReadMem(dst, blocks[b] + off, n); };
            size_t rows = 0;
            const bool walked = Walk(read, name, [&](size_t off) { if (nHit < 4) offs[nHit++] = off; }, &rows);
            if (!walked) { refused = "the table block is not AnimData-shaped after all"; continue; }
            for (size_t h = 0; h < nHit; ++h) {
                ++rowsFound;
                const uintptr_t at = blocks[b] + offs[h];
                uint8_t row[kRowSize];
                if (!ReadMem(row, at, sizeof row)) { refused = "a row could not be read"; continue; }
                Row r;
                const Verdict v = Check(row, token, &r);
                if (v == Verdict::Nothing) { refused = std::string(name) + ": " + Why(v); continue; }
                if (v != Verdict::Ok) { refused = std::string(name) + " looks different from what was read - " + Why(v) + " (frames " + std::to_string(r.frames) + ", speed " + std::to_string(r.speed) + ")"; continue; }
                const int to = Target(wantedFrame, r.flagIdx);
                if (to >= r.flagIdx) { refused = std::string(name) + ": the action frame is already at " + std::to_string(r.flagIdx) + ", not later than the wished " + std::to_string(wantedFrame); continue; }
                if (g_np >= kMaxPatches) continue;
                // cleared first, then set: never two flags at once
                if (!WriteByte(at + kFlagsAt + (size_t)r.flagIdx, 0)) { refused = "a row could not be written"; continue; }
                if (!WriteByte(at + kFlagsAt + (size_t)to, r.flagVal)) { WriteByte(at + kFlagsAt + (size_t)r.flagIdx, r.flagVal); refused = "a row could not be written"; continue; }
                Patch& p = g_p[g_np++];
                p.row = at;
                std::memcpy(p.name, r.name, kNameBytes);
                p.frames = r.frames; p.speed = r.speed; p.from = r.flagIdx; p.at = to; p.val = r.flagVal;
                ++moved;
                LogF("bow quick release: on - the hero's bow row %s (%u frames, speed %u): the action flag moved from frame %d to %d (%.0f ms -> %.0f ms after the animation starts), "
                     "in table copy %zu of %zu. The animation's length and the attack rate stay; the arrow is made earlier inside the attack",
                     name, r.frames, r.speed, r.flagIdx, to, MsOf(r.flagIdx, r.speed), MsOf(to, r.speed), b + 1, nb);
            }
        }
    }
    if (moved) {
        g_patchedClass = cls;
        g_patchedFrame = wantedFrame;
        g_lastSaid.clear();
        return true;
    }
    char t[420];
    if (!rowsFound) snprintf(t, sizeof t, "bow quick release: nothing done - the %s class's bow attack row (%sA1BOW) is not in the table%s%s", ClassToken(cls), token, refused.empty() ? "" : ": ", refused.c_str());
    else snprintf(t, sizeof t, "bow quick release: nothing done - %s", refused.empty() ? "no row could be moved" : refused.c_str());
    SayOnce(t);
    return false;
}

}  // namespace

void Init(const PluginContext* ctx) { g_ctx = ctx; }

void Configure(bool on, int frame) {
    g_frame.store(std::clamp(frame, 1, 40));
    g_wanted.store(on);
}

bool Active() { return g_np > 0; }

void Tick(bool want, int cls, bool acting, uint64_t nowMs) {
    if (!TryAcquireSRWLockExclusive(&g_lock)) return;
    const bool wish = g_wanted.load() && want && ClassToken(cls) != nullptr;
    const int frame = g_frame.load();
    if (g_np) {
        // put back: the cut is not wanted any more, or the class or the frame is not what was moved
        const char* why = !g_wanted.load() ? "the option is off" : !want ? "not in VR first person with a bow in hand" : cls != g_patchedClass ? "another class" : frame != g_patchedFrame ? "the frame was changed" : nullptr;
        if (why) {
            if (acting && g_waitSince == 0) g_waitSince = nowMs;
            if (!acting || nowMs - g_waitSince > 3000) { RestoreAll(why); g_waitSince = 0; g_retryAt = 0; }
        } else {
            g_waitSince = 0;
            if (nowMs >= g_verifyAt) {   // once a second: still as we left it? (the game may have reloaded its tables)
                g_verifyAt = nowMs + 1000;
                for (int i = 0; i < g_np; ++i) {
                    uint8_t row[kRowSize];
                    if (!ReadMem(row, g_p[i].row, sizeof row) || !InPatchedState(row, g_p[i].from, g_p[i].at, g_p[i].val) || std::memcmp(row, g_p[i].name, kNameBytes) != 0) {
                        Log("bow quick release: the table is not as it was left (the game reloaded or changed it) - forgotten, looked for again");
                        g_np = 0;
                        g_patchedClass = g_patchedFrame = -1;
                        g_retryAt = 0;
                        break;
                    }
                }
            }
        }
    } else if (wish && !acting && nowMs >= g_retryAt) {
        if (Apply(cls, frame)) g_tries = 0;
        else g_retryAt = nowMs + (++g_tries < 6 ? 3000 : 30000);   // not found / different: looked at again now and then, said once
    } else if (!wish) {
        g_tries = 0;
        g_retryAt = 0;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void Shutdown() {
    AcquireSRWLockExclusive(&g_lock);
    g_wanted.store(false);
    RestoreAll("the plugin unloads");
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace bowquick
