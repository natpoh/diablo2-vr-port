// [weight] clash_sound: see clash_sound.h.

#include "clash_sound.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>

#include <D2RLPlugin/api.h>

#include "sigscan.h"
#include "ui_ticket.h"
#include "view_gate.h"

using namespace D2RL;

namespace clashsound {
namespace {

// Addresses in 3.3.93787 under D2RLoader 1.3.1 (not in game_sigs.h: the bytes are checked where that build has them).
constexpr uint64_t RVA_PLAY = 0x1A0C00, RVA_SETVOL = 0x19F900, RVA_SETVOL_STORE = 0x19F933;
// mov [rsp+8],rbx; mov [rsp+10h],rsi; push rdi; sub rsp,30h; mov ebx,r9d; mov edi,r8d; mov rsi,rdx (then the name's lookup)
const uint8_t kSigPlay[24] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x41, 0x8B, 0xD9, 0x41, 0x8B, 0xF8, 0x48, 0x8B, 0xF2};
// mov r10d,edx; test rcx,rcx; je ... (the instances searched for the handle)
const uint8_t kSigSetVol[8] = {0x44, 0x8B, 0xD2, 0x48, 0x85, 0xC9, 0x74, 0x2F};
// mov [rdx+24h],r10d; ret (the instance's volume, 255 when it was made)
const uint8_t kSigSetVolStore[5] = {0x44, 0x89, 0x52, 0x24, 0xC3};
constexpr const char* kSound = "block_weapon_1";   // sounds.txt: the game's block sound (group of 3; HD: impact_block_blade_hd_1)

using PlayFn = uint64_t (*)(const char* name, void* unit, int delay, int flag);
using SetVolFn = void (*)(uint64_t handle, uint32_t volume);

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
std::atomic<LogFn> g_log{nullptr};
std::atomic<HeroFn> g_hero{nullptr};
std::atomic<bool> g_wanted{true};
std::atomic<int> g_state{0};   // 0 not looked yet, 1 ready, 2 not this build / faulted: nothing played this session

void LogF(const char* fmt, ...) {
    const LogFn log = g_log.load();
    if (!log) return;
    char b[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    log(b);
}

void Log(const char* s) {
    if (const LogFn log = g_log.load()) log(s);
}

constexpr size_t kQueue = 4;
constexpr ULONGLONG kStaleMs = 150;
struct Queued {
    float speed = 0.0f;
    uint32_t volume = 0;
    const char* who = "";
    ULONGLONG at = 0;
};
std::mutex g_mutex;   // the queue and the ticket's Start (the weight thread, melee touch's timer and the shield's pose all knock)
std::array<Queued, kQueue> g_queue;
size_t g_count = 0;
uiticket::Ticket g_ticket;
unsigned g_told = 0, g_toldNone = 0;

bool CallPlay(uintptr_t play, uintptr_t setVol, void* hero, uint32_t volume, uint64_t* handle) noexcept {
    __try {
        *handle = reinterpret_cast<PlayFn>(play)(kSound, hero, 0, 0);
        if (*handle) reinterpret_cast<SetVolFn>(setVol)(*handle, volume);   // (before the sound thread starts it: it starts this loud)
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Ready() {
    int s = g_state.load();
    if (s == 0) {
        const bool ok = d2rsig::Check(RVA_PLAY, kSigPlay, sizeof kSigPlay) && d2rsig::Check(RVA_SETVOL, kSigSetVol, sizeof kSigSetVol) &&
                        d2rsig::Check(RVA_SETVOL_STORE, kSigSetVolStore, sizeof kSigSetVolStore);
        static unsigned tries = 0;
        if (ok) {
            g_state.store(s = 1);
            LogF("clash sound: the game's SoundPlay and SoundSetVolume are where 3.3.93787 under D2RLoader 1.3.1 has them - a knock of steel plays the game's block sound (%s)", kSound);
        } else if (++tries == 20) {
            g_state.store(s = 2);
            Log("clash sound: the game's SoundPlay / SoundSetVolume are not where 3.3.93787 under D2RLoader 1.3.1 has them (another build?) - no clash sound (the pulse stays)");
        }
    }
    return s == 1;
}

void Sound(const Queued& q) noexcept {
    if (!Ready()) return;
    const HeroFn heroFn = g_hero.load();
    void* hero = heroFn ? heroFn() : nullptr;
    if (!hero) {
        if (++g_toldNone <= 5) LogF("clash sound: %s at %.1f m/s - not played: the hero's unit is not known yet (a 3D sound at no place is not heard)", q.who, q.speed);
        return;
    }
    uint64_t handle = 0;
    if (!CallPlay(d2rsig::Addr(RVA_PLAY), d2rsig::Addr(RVA_SETVOL), hero, q.volume, &handle)) {
        g_state.store(2);
        Log("clash sound: the game's SoundPlay faulted - OFF for this session (the pulse stays)");
        return;
    }
    if (++g_told <= 12)
        LogF("clash sound: %s at %.1f m/s - the game's block sound (%s) at %u/255 (%.0f %% of its own loudness)%s", q.who, q.speed, kSound, q.volume, q.volume * 100.0f / 255.0f,
             handle ? "" : " - the game did not start it (its own limits: no voice free, ...)");
}

void __cdecl Apply(const PluginContext*, void* user) noexcept {
    if (!g_ticket.Claim(uiticket::FromUser(user))) return;
    for (;;) {
        Queued q;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!g_count) return;
            q = g_queue[0];
            for (size_t i = 1; i < g_count; ++i) g_queue[i - 1] = g_queue[i];
            --g_count;
        }
        if (!g_wanted.load() || !viewgate::F4() || GetTickCount64() - q.at > kStaleMs) continue;   // (F4 only; never a late clang)
        Sound(q);
    }
}

}  // namespace

void SetLog(LogFn fn) { g_log.store(fn); }

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    const ThreadService* t = nullptr;
    g_threads = ctx && ctx->QueryService(&t) == ServiceQueryResult::Success ? t : nullptr;
}

void SetWanted(bool on) {
    if (g_wanted.exchange(on) != on)
        Log(on ? "clash sound: on - a knock of steel on steel (the two weapons of a pair, a parry) plays the game's block sound, as loud as the knock was hard"
               : "clash sound: off - a knock of steel is felt, not heard");
}
bool Wanted() { return g_wanted.load(); }

void SetHero(HeroFn fn) { g_hero.store(fn); }

bool Play(float speedMps, uint32_t gameVolume, const char* who) {
    if (!g_wanted.load() || !viewgate::F4() || g_state.load() == 2 || !g_threads || !g_threads->runOnGameThread || !g_ctx || !gameVolume) return false;
    const ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_count >= kQueue) return false;
    g_queue[g_count++] = Queued{speedMps, gameVolume > 255 ? 255u : gameVolume, who ? who : "", now};
    if (const uint32_t token = g_ticket.Start(now)) {   // (a run already queued takes this one too)
        if (g_threads->runOnGameThread(g_ctx, &Apply, uiticket::ToUser(token)) != Threads::Result::Success) g_ticket.Claim(token);
    }
    return true;
}

}  // namespace clashsound
