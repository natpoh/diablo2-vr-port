// shot_trace_wire.cpp - [debug] shot_trace: see shot_trace.h for why. Read only: the hooks tell what they see and pass everything on.
//
// Three places: the map click 0xFE3B0 (vrcam's hook calls ClickTrace), the click's command sender 0x102490 (hooked here: the record the click
// builds - flags with Stand Still and Run, the target, the point, the walk or run id, the skill - as it comes in and as it goes out), and the
// client's funnel 0xFABE0 (stray_walk_wire.cpp calls OnCommand; the way back up the stack is told as RVAs).

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "shot_trace.h"
#include "shot_trace_wire.h"
#include "sigscan.h"

namespace shottrace {
namespace {

// the click's command sender (3.3.93787): void (command*), the record in rcx
constexpr uint64_t RVA_CLICK_COMMAND = 0x102490;
// push rdi; sub rsp, 50h; mov rdi, rcx; call 0x1019xx (its rel32 is compared where the code is where it was, not elsewhere); test eax, eax
const uint8_t kSigClickCommand[16] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0xF9, 0xE8, 0x82, 0xEB, 0xFF, 0xFF, 0x85, 0xC0};
using ClickCommandFn = void (*)(void* command);

void (*g_log)(const char*) = nullptr;
std::atomic<bool> g_wanted{false};
std::atomic<bool> g_busy{false};
std::atomic<int> g_hook{0};   // 0 not in yet, 1 in, 2 failed
ClickCommandFn g_origClick = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;
Window g_win;
uint32_t g_total = 0;   // lines said in all

#pragma optimize("", off)
bool SafeCopy(void* d, const void* s, size_t n) noexcept {
    __try { memcpy(d, s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#pragma optimize("", on)

uint64_t NowMs() { return GetTickCount64(); }

// One line of the shot: "vrcam: shot trace #3 +42 ms: ...". Nothing outside a shot, nothing past the shot's cap or the session's.
void SayV(const char* fmt, va_list args) {
    if (!g_log) return;
    char text[512];
    vsnprintf(text, sizeof text, fmt, args);
    bool last = false;
    uint32_t shot = 0, ms = 0;
    AcquireSRWLockExclusive(&g_lock);
    const bool ok = g_win.Open() && g_total < 1500 && g_win.TakeLine(&last);
    if (ok) { ++g_total; shot = g_win.Shot(); ms = g_win.SinceMs(NowMs()); }
    ReleaseSRWLockExclusive(&g_lock);
    if (!ok) return;
    char b[640];
    snprintf(b, sizeof b, "vrcam: shot trace #%u +%u ms: %s%s", shot, ms, text, last ? " (the cap for one shot: quiet until the next)" : "");
    g_log(b);
}
void Say(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    SayV(fmt, a);
    va_end(a);
}

struct Image { uintptr_t base = 0, size = 0; };
Image ImageOfGame() {
    static Image im = [] {
        Image r;
        r.base = (uintptr_t)GetModuleHandleW(nullptr);
        const auto* dos = (const IMAGE_DOS_HEADER*)r.base;
        if (r.base && dos->e_magic == IMAGE_DOS_SIGNATURE) {
            const auto* nt = (const IMAGE_NT_HEADERS*)(r.base + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE) r.size = nt->OptionalHeader.SizeOfImage;
        }
        return r;
    }();
    return im;
}

// The hero as the game keeps him: the mode at unit + 0xC, the place as 16.16 cells at the path (unit + 0x38) +0 / +4.
struct Hero { bool ok = false; uint32_t mode = 0; float x = 0.0f, y = 0.0f; };
Hero ReadHero(const void* unit) {
    Hero h;
    if (!unit) return h;
    uint32_t mode = 0;
    uintptr_t path = 0;
    if (!SafeCopy(&mode, (const uint8_t*)unit + 0xC, sizeof mode) || !SafeCopy(&path, (const uint8_t*)unit + 0x38, sizeof path) || path < 0x10000) return h;
    uint32_t xy[2];
    if (!SafeCopy(xy, (const void*)path, sizeof xy)) return h;
    h.ok = true;
    h.mode = mode;
    h.x = xy[0] / 65536.0f;
    h.y = xy[1] / 65536.0f;
    return h;
}

// The client's units, as vr/melee_touch.cpp reads them: a hash of 128 buckets per type at RVA 0x2A23910 (0x400 bytes a type), the chain through unit + 0x158;
// a unit's header is {type, class, id, mode}. Read only, bounded (a debug aid: it walks the buckets).
constexpr uint64_t RVA_UNITS = 0x2A23910;
struct Unit { bool ok = false; uint32_t type = 0, cls = 0, id = 0, mode = 0; float x = 0.0f, y = 0.0f; };
Unit ReadUnit(const void* at) {
    Unit u;
    uint32_t head[4];
    uintptr_t path = 0;
    uint32_t xy[2];
    if (!at || !SafeCopy(head, at, sizeof head) || !SafeCopy(&path, (const uint8_t*)at + 0x38, sizeof path) || path < 0x10000 || !SafeCopy(xy, (const void*)path, sizeof xy)) return u;
    u.ok = true;
    u.type = head[0]; u.cls = head[1]; u.id = head[2]; u.mode = head[3];
    u.x = xy[0] / 65536.0f;
    u.y = xy[1] / 65536.0f;
    return u;
}
uintptr_t FindUnit(uint32_t type, uint32_t id) {
    const uintptr_t table = d2rsig::Addr(RVA_UNITS);
    if (!table || type > 5) return 0;
    uintptr_t bucket[128];
    if (!SafeCopy(bucket, (const void*)(table + (uintptr_t)type * 0x400), sizeof bucket)) return 0;
    unsigned visited = 0;
    for (const uintptr_t head : bucket) {
        uintptr_t at = head;
        for (unsigned chain = 0; at && chain < 64 && visited < 4096; ++chain, ++visited) {
            uint32_t h[3];
            if (!SafeCopy(h, (const void*)at, sizeof h) || h[0] != type) break;
            if (h[2] == id) return at;
            if (!SafeCopy(&at, (const uint8_t*)at + 0x158, sizeof at)) break;
        }
    }
    return 0;
}

// The record the click builds (unit 0x102490's rcx): flags @0, the actor @8, the target @0x10, the point @0x18 / 0x1C, the walk or run id @0x20, @0x24,
// the skill @0x28, the client index @0x30. Read here by offset only; it is the game's own, read and not changed.
struct Record {
    uint32_t flags = 0;
    uintptr_t actor = 0, target = 0, skill = 0;
    int32_t x = 0, y = 0;
    uint32_t id0 = 0, id1 = 0, idx = 0;
};
bool ReadRecord(const void* rec, Record* r) {
    uint8_t b[0x34];
    if (!rec || !SafeCopy(b, rec, sizeof b)) return false;
    memcpy(&r->flags, b + 0, 4);
    memcpy(&r->actor, b + 8, 8);
    memcpy(&r->target, b + 0x10, 8);
    memcpy(&r->x, b + 0x18, 4);
    memcpy(&r->y, b + 0x1C, 4);
    memcpy(&r->id0, b + 0x20, 4);
    memcpy(&r->id1, b + 0x24, 4);
    memcpy(&r->skill, b + 0x28, 8);
    memcpy(&r->idx, b + 0x30, 4);
    return true;
}

void UnitLine(const Unit& u, const Hero& h, char* out, size_t n) {
    if (!u.ok) { snprintf(out, n, "a unit that cannot be read"); return; }
    char dist[48] = "";
    if (h.ok) snprintf(dist, sizeof dist, ", %.1f cells from the hero", std::hypot(u.x - h.x, u.y - h.y));
    snprintf(out, n, "a %s, class %u, id %u, mode %u, at (%.1f, %.1f)%s", UnitKindName(u.type), u.cls, u.id, u.mode, u.x, u.y, dist);
}

void HookClickCommand(void* command) {
    Record in;
    const bool seen = Active() && ReadRecord(command, &in);
    char fl[64];
    if (seen) {
        CommandFlagsText(in.flags, fl, sizeof fl);
        const Hero h = ReadHero((const void*)in.actor);
        char tgt[160] = "none";
        if (in.target) UnitLine(ReadUnit((const void*)in.target), h, tgt, sizeof tgt);
        Say("click command in: flags %s, target %s, skill %s, the point (%d, %d), walk/run id %u / %u, client %u%s", fl, tgt,
            in.skill ? "set" : "none", in.x, in.y, in.id0, in.id1, in.idx, h.ok ? "" : " (the hero is not readable)");
        if (h.ok) Say("  the hero: mode %u (%s), at (%.1f, %.1f), %.1f cells from the point", h.mode, ModeName(h.mode), h.x, h.y,
                      std::hypot(in.x - h.x, in.y - h.y));
    }
    g_origClick(command);
    Record out;
    if (seen && ReadRecord(command, &out)) {
        if (out.flags != in.flags || out.x != in.x || out.y != in.y || out.target != in.target) {
            CommandFlagsText(out.flags, fl, sizeof fl);
            Say("click command out (changed): flags %s, target %s, the point (%d, %d)", fl, out.target ? "YES" : "none", out.x, out.y);
        } else {
            Say("click command out: unchanged");
        }
    }
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

const char* UnitText(const void* unit, const void* hero, char* out, size_t n) {
    if (!out || !n) return "";
    if (!unit) { snprintf(out, n, "none"); return out; }
    UnitLine(ReadUnit(unit), ReadHero(hero), out, n);
    return out;
}

void SetWanted(bool on) {
    if (g_wanted.exchange(on) != on) {
        if (g_log) g_log(on ? "vrcam: shot trace - [debug] shot_trace=1: for every attack of ours, from its press until 1.5 s after it, the map clicks, the command the "
                              "click builds and the commands through the client's funnel are told (needs [gameplay] attack_in_place = 1)"
                            : "vrcam: shot trace - [debug] shot_trace=0");
        AcquireSRWLockExclusive(&g_lock);
        g_win.Reset();
        ReleaseSRWLockExclusive(&g_lock);
    }
}
bool Wanted() { return g_wanted.load(); }

void Observe(bool busy) {
    if (!g_wanted.load()) { g_busy.store(false); return; }
    g_busy.store(busy);
    bool fresh = false, open = false;
    AcquireSRWLockExclusive(&g_lock);
    open = g_win.Step(busy, NowMs(), &fresh);
    const uint32_t shot = g_win.Shot();
    ReleaseSRWLockExclusive(&g_lock);
    if (fresh && open) Say("an attack of ours is pressed - Stand Still is asked for (shot %u of the session)", shot);
}

bool Active() {
    if (!g_wanted.load()) return false;
    AcquireSRWLockExclusive(&g_lock);
    const bool open = g_win.Step(g_busy.load(), NowMs());
    ReleaseSRWLockExclusive(&g_lock);
    return open;
}

void Note(const char* fmt, ...) {
    if (!Active()) return;
    va_list a;
    va_start(a, fmt);
    SayV(fmt, a);
    va_end(a);
}

ClickTrace::ClickTrace(void* unit, int type, int x, int y, uint64_t flags) {
    if (!Active()) return;
    on_ = true;
    type_ = type;
    const Hero h = ReadHero(unit);
    char hero[160] = "the hero is not readable";
    if (h.ok) snprintf(hero, sizeof hero, "the hero: mode %u (%s), at (%.1f, %.1f), %.1f cells from the point", h.mode, ModeName(h.mode), h.x, h.y,
                       std::hypot(x - h.x, y - h.y));
    Say("map click type %d (%s) at (%d, %d), flags 0x%llX: Stand Still %d, Run %d; %s", type, ClickTypeName(type), x, y, (unsigned long long)(flags & 0xFFFF),
        ClickStandStill(flags) ? 1 : 0, ClickRun(flags) ? 1 : 0, hero);
}
ClickTrace::~ClickTrace() {
    if (on_) Say("map click type %d returns", type_);
}

void OnCommand(int cmd, void* unit, int a1, int a2, const char* ctx, int clickType) {
    if (!Active()) return;
    void* frames[10] = {};
    const USHORT n = RtlCaptureStackBackTrace(2, 10, frames, nullptr);   // not this function, not the hook that called it
    const Image im = ImageOfGame();
    char way[400];
    FramesText(frames, n, im.base, im.size, way, sizeof way);
    // the stick's direction (0x42) comes every frame while the stick is held: one a quarter of a second is told
    if (cmd == 0x42) {
        static std::atomic<uint64_t> last{0};
        const uint64_t now = NowMs();
        if (now - last.load() < 250) return;
        last.store(now);
    }
    const Hero h = ReadHero(unit);
    char hero[96] = "";
    if (h.ok) snprintf(hero, sizeof hero, "; the hero: mode %u (%s) at (%.1f, %.1f)", h.mode, ModeName(h.mode), h.x, h.y);
    // a walk or run to a unit (2, 4): a1 = its type, a2 = its id - who it is, and how far
    char who[200] = "";
    if (cmd == 2 || cmd == 4) {
        char u[160];
        const uintptr_t at = FindUnit((uint32_t)a1, (uint32_t)a2);
        if (at) UnitLine(ReadUnit((const void*)at), h, u, sizeof u);
        else snprintf(u, sizeof u, "a %s of id %d, not found in the client's units", UnitKindName((uint32_t)a1), a2);
        snprintf(who, sizeof who, "; TO %s", u);
    }
    char type[32] = "";
    if (clickType >= 0) snprintf(type, sizeof type, ", click type %d", clickType);
    Say("FUNNEL command %d (%s) a1=%d a2=%d, out of %s%s; way back: %s%s%s", cmd, CommandName(cmd), a1, a2, ctx ? ctx : "?", type, way, hero, who);
}

void Install() {
    if (g_hook.load() != 0 || !g_wanted.load()) return;
    if (!d2rsig::Check(RVA_CLICK_COMMAND, kSigClickCommand, sizeof kSigClickCommand)) return;   // its page is not decrypted yet, or another build
    const bool in = d2rsig::Hook(RVA_CLICK_COMMAND, kSigClickCommand, sizeof kSigClickCommand, (void*)&HookClickCommand, (void**)&g_origClick);
    g_hook.store(in ? 1 : 2);
    if (g_log)
        g_log(in ? "vrcam: shot trace - the game's click-command sender 0x102490 hooked (read only)"
                 : "vrcam: shot trace - the hook on the game's click-command sender FAILED: the map clicks and the funnel are still told");
}

}  // namespace shottrace
