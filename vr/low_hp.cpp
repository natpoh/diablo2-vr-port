// low_hp.cpp - the low-life vignette ([hud] low_hp_vignette, vr/low_hp.h) in the game and the effect.
//
//   - the hero's life, read on the game's UI thread five times a second through the game's own stat getter
//     (read only: unit 6 = life, 7 = its maximum life, kept with eight fractional bits). The getter and how the
//     local player is found (LocalIndex, ClientPlayer) are bearstonem/d2r-pcvr's (MIT, vr/native_ui.cpp), found
//     again here through our signatures (game_sigs.h, area "Low life"; the skill wheel reads the same two);
//   - the beat stepped once a pair by vr/low_hp.h's Heartbeat, from the real time;
//   - the opacity handed to reshade/D2R_DepthFog.fx (LowHpAlpha...) by vrcam, the same for both eyes;
//   - the optional haptic pulse in both controllers on each beat.
//
// Nothing of the game is written, called other than to read, or drawn over the game's own picture except by the
// effect's colour at the edges. A read that fails (no player yet, a title screen, another build) shows nothing and
// is told once; a call into the game that faults switches the read off until the game restarts.

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "low_hp_game.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <cstdio>
#include <cstring>

#include <D2RLPlugin/api.h>

#include "haptics.h"
#include "sigscan.h"
#include "ui_ticket.h"

using namespace D2RL;

namespace lowhp {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h): the index of the local player, the client's player unit by index,
// and the unit's stat getter (a jump thunk): stat(unit, id, layer) -> the value.
constexpr uint64_t kLocalIndex = 0x8B2D0, kClientPlayer = 0x9A480, kStat = 0x2F5020;
constexpr int kStatLife = 6, kStatMaxLife = 7;

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;

// the settings (Configure, the update thread; read by the draw thread)
std::atomic<bool> g_on{false}, g_gentle{false}, g_haptics{false};
std::atomic<float> g_threshold{0.30f}, g_start{0.25f}, g_end{0.85f}, g_saturation{0.8f}, g_radius{0.40f}, g_softness{0.80f}, g_round{2.5f},
    g_hapticsStrength{1.0f};
// the view's width and height as the tangent of the half-angle (xr's field of view of this pair; 0 = not known)
std::atomic<float> g_tanX{0.0f}, g_tanY{0.0f};

// the life, as the UI thread last read it: float bits | known << 32 | dead << 33, and when (GetTickCount64)
std::atomic<uint64_t> g_life{0};
std::atomic<uint64_t> g_lifeAt{0};
uiticket::Ticket g_queued;
std::atomic<bool> g_dead{false};   // a call into the game faulted: no read until the game restarts
std::atomic<int> g_toldFail{0};

// the beat (the draw thread's) and what the effect is handed (any thread)
Heartbeat g_beat;
std::atomic<float> g_alpha{0.0f};
std::atomic<uint64_t> g_frameAt{0};   // when Frame last stepped (GetTickCount64)
std::atomic<uint64_t> g_pageUntil{0};      // the settings panel is on a vignette page until then (SetPreview): nothing shows or plays for that alone
std::mutex g_previewLock;
Config g_previewCfg;                       // the panel's draft, under g_previewLock
int g_previewPage = -1;                    // which of the pages, under g_previewLock
PreviewWindow g_window;                    // the "Preview 5 s" button's five seconds (and the red's fade-out after them)
bool g_toldFirstPlay = false, g_toldFirstPreview = false;   // the first heartbeat pulse of each kind is told once, with its numbers (the draw thread's)
double g_lastSeconds = 0.0;
bool g_wasActive = false;
ULONGLONG g_toldEdge = 0;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

double Seconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER n;
    QueryPerformanceCounter(&n);
    return (double)n.QuadPart / (double)freq.QuadPart;
}

enum class Read { Ok, NoAddress, NoPlayer, Fault };

// The hero's life and maximum life, raw. The unit must be a player (its first dword, the unit type, is 0).
// Plain data only here: __try cannot sit in a function that unwinds C++ objects.
Read ReadLife(int32_t* life, int32_t* maxLife) noexcept {
    const uintptr_t indexFn = d2rsig::Addr(kLocalIndex), playerFn = d2rsig::Addr(kClientPlayer), statFn = d2rsig::Addr(kStat);
    if (!indexFn || !playerFn || !statFn) return Read::NoAddress;
    __try {
        const int index = reinterpret_cast<int (*)()>(indexFn)();
        if (index < 0 || index >= 8) return Read::NoPlayer;
        const uintptr_t player = reinterpret_cast<uintptr_t (*)(int)>(playerFn)(index);
        if (player < 0x10000) return Read::NoPlayer;
        if (*reinterpret_cast<const volatile uint32_t*>(player) != 0) return Read::NoPlayer;
        // the getter is a jump thunk through a slot the loader fills in: a slot still empty is not a getter
        const uint8_t* thunk = reinterpret_cast<const uint8_t*>(statFn);
        if (thunk[0] == 0xFF && thunk[1] == 0x25) {
            int32_t disp;
            memcpy(&disp, thunk + 2, sizeof disp);
            if (*reinterpret_cast<const volatile uintptr_t*>(statFn + 6 + (intptr_t)disp) < 0x10000) return Read::NoAddress;
        }
        const auto stat = reinterpret_cast<int (*)(uintptr_t, int, int)>(statFn);
        *life = stat(player, kStatLife, 0);
        *maxLife = stat(player, kStatMaxLife, 0);
        return Read::Ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return Read::Fault;
    }
}

void StoreLife(const Life& l) {
    uint32_t bits;
    memcpy(&bits, &l.frac, sizeof bits);
    g_life.store((uint64_t)bits | (l.known ? 1ull << 32 : 0ull) | (l.dead ? 1ull << 33 : 0ull));
    g_lifeAt.store(GetTickCount64());
}

void __cdecl ReadJob(const PluginContext*, void* user) noexcept {
    if (!g_queued.Claim(uiticket::FromUser(user))) return;
    int32_t life = 0, maxLife = 0;
    const Read r = ReadLife(&life, &maxLife);
    if (r == Read::Ok) {
        const Life l = LifeFromStats(life, maxLife);
        StoreLife(l);
        static bool told = false;
        if (l.known && !told) {
            told = true;
            char b[200];
            snprintf(b, sizeof b, "vrcam: low life vignette - the hero's life is read: %.2f of %.2f (%.0f%%)", life / kStatScale, maxLife / kStatScale,
                     l.frac * 100.0f);
            Log(b);
        } else if (!l.known && g_toldFail.fetch_add(1) < 2) {
            char b[200];
            snprintf(b, sizeof b, "vrcam: low life vignette - the life numbers made no sense (%d, maximum %d): no vignette until they do", life, maxLife);
            Log(b);
        }
        return;
    }
    StoreLife(Life{});   // unknown: the vignette goes out
    if (r == Read::Fault) {
        g_dead.store(true);
        Log("vrcam: low life vignette - reading the hero's life faulted: the vignette is off until the game restarts");
    } else if (g_toldFail.fetch_add(1) < 2) {
        Log(r == Read::NoAddress ? "vrcam: low life vignette - the game's stat getter or player lookup was not found (another build?): no vignette"
                                 : "vrcam: low life vignette - no hero to read yet (a title or character screen, or a load): no vignette until there is");
    }
}

Config CurrentConfig() {
    Config c;
    c.on = g_on.load(std::memory_order_relaxed);
    c.gentle = g_gentle.load(std::memory_order_relaxed);
    c.haptics = g_haptics.load(std::memory_order_relaxed);
    c.threshold = g_threshold.load(std::memory_order_relaxed);
    c.intensityStart = g_start.load(std::memory_order_relaxed);
    c.intensityEnd = g_end.load(std::memory_order_relaxed);
    c.saturation = g_saturation.load(std::memory_order_relaxed);
    c.radius = g_radius.load(std::memory_order_relaxed);
    c.softness = g_softness.load(std::memory_order_relaxed);
    c.roundness = g_round.load(std::memory_order_relaxed);
    c.hapticsStrength = g_hapticsStrength.load(std::memory_order_relaxed);
    return c;
}

bool PageLeased() { return GetTickCount64() < g_pageUntil.load(std::memory_order_relaxed); }
// The preview's five seconds are running (the life shown as half down, the pulse), and the picture is the preview's (those five seconds and the red's fade-out after).
bool PreviewRunning() { return PageLeased() && g_window.Active(GetTickCount64()); }
bool PreviewShown() { return PageLeased() && g_window.Showing(GetTickCount64()); }

// The settings the picture is drawn with: the ini's, or - while the preview is up - the panel's draft.
Config ActiveConfig() {
    if (PreviewShown()) {
        std::lock_guard<std::mutex> lock(g_previewLock);
        return PreviewConfig(g_previewCfg);
    }
    return CurrentConfig();
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    if (ctx->QueryService(&g_threads) != ServiceQueryResult::Success) g_threads = nullptr;
    if (!g_threads || !g_threads->runOnUiThread) Log("vrcam: low life vignette - no UI thread service: the hero's life cannot be read");
}

void Configure(const Config& config) {
    const Config c = Sanitized(config);
    const bool was = g_on.exchange(c.on);
    g_gentle.store(c.gentle);
    g_haptics.store(c.haptics);
    g_threshold.store(c.threshold);
    g_start.store(c.intensityStart);
    g_end.store(c.intensityEnd);
    g_saturation.store(c.saturation);
    g_radius.store(c.radius);
    g_softness.store(c.softness);
    g_round.store(c.roundness);
    g_hapticsStrength.store(c.hapticsStrength);
    if (c.on != was) {
        char b[300];
        snprintf(b, sizeof b, "vrcam: low life vignette %s ([hud] low_hp_vignette; from %.0f%% life, intensity %.2f up to %.2f, red %.2f, reaches %.0f%% in%s%s)",
                 c.on ? "on" : "off", c.threshold * 100.0f, c.intensityStart, c.intensityEnd, c.saturation, c.radius * 100.0f, c.gentle ? ", gentle" : "",
                 c.haptics ? ", with haptics" : "");
        Log(b);
        if (!c.on) g_alpha.store(0.0f);   // unticked: the picture is as it was, now (the draw thread's next step clears the rest)
    }
}

bool Enabled() { return g_on.load(std::memory_order_relaxed) || PreviewShown(); }

void SetPreview(bool on, const Config& draft, int page) {
    {
        std::lock_guard<std::mutex> lock(g_previewLock);
        if (on) {
            g_previewCfg = Sanitized(draft);
            if (page != g_previewPage) g_window.Stop();   // another page: the preview of the one before is over
            g_previewPage = page;
        } else {
            g_window.Stop();   // left the page, or closed the panel
            g_previewPage = -1;
        }
    }
    g_pageUntil.store(on ? GetTickCount64() + 300 : 0, std::memory_order_relaxed);
}

void PlayPreview() {
    if (!PageLeased()) return;
    g_window.Start(GetTickCount64());
    Log("vrcam: low life vignette - preview: the vignette and the beat in the controllers for 5 s, with the settings panel's rows as they stand");
}

void Tick(bool wantRead) {
    if (!wantRead || !g_on.load(std::memory_order_relaxed) || !g_ctx || !g_threads || !g_threads->runOnUiThread || g_dead.load()) return;
    static ULONGLONG next = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 200;
    if (const uint32_t token = g_queued.Start(now))
        if (g_threads->runOnUiThread(g_ctx, &ReadJob, uiticket::ToUser(token)) != Threads::Result::Success) g_queued.Claim(token);
}

void Frame(const Where& where, bool menuOpen, float tanX, float tanY) {
    g_tanX.store(tanX);
    g_tanY.store(tanY);
    if (!PageLeased()) g_window.Stop();   // the panel is gone (a host that stopped calling): a preview does not wait for it to come back
    const bool running = PreviewRunning(), shown = PreviewShown();
    Config cfg = CurrentConfig();   // (off: Step keeps nothing and gives nothing)
    if (shown) {
        std::lock_guard<std::mutex> lock(g_previewLock);
        cfg = PreviewConfig(g_previewCfg);
    }
    const double now = Seconds();
    Heartbeat::In in;
    in.inView = InView(where);
    in.menu = menuOpen;
    in.dt = g_lastSeconds > 0.0 ? (float)(now - g_lastSeconds) : 0.0f;
    g_lastSeconds = now;
    const uint64_t life = g_life.load(), at = g_lifeAt.load();
    const double age = at ? (double)(GetTickCount64() - at) : -1.0;
    in.lifeKnown = ((life >> 32) & 1) != 0 && Fresh(age) && !g_dead.load();
    in.dead = ((life >> 33) & 1) != 0;
    const uint32_t bits = (uint32_t)life;
    memcpy(&in.frac, &bits, sizeof in.frac);
    if (running) {   // the preview's five seconds: as if the life were half way down
        in.lifeKnown = true;
        in.dead = false;
        in.menu = false;
        in.frac = PreviewFrac(Sanitized(cfg).threshold);
    }
    const Heartbeat::Out out = g_beat.Step(cfg, in);
    g_alpha.store(out.alpha);
    g_frameAt.store(GetTickCount64());

    if (out.active != g_wasActive) {   // told as it comes and goes (not more than once a second)
        g_wasActive = out.active;
        const ULONGLONG t = GetTickCount64();
        if (t - g_toldEdge > 1000) {
            g_toldEdge = t;
            char b[200];
            snprintf(b, sizeof b, "vrcam: low life vignette - life %.0f%%: %s (%.0f beats a minute at most %.2f opacity)", in.frac * 100.0f,
                     out.active ? "coming up" : "going out", Bpm(out.severity, cfg.gentle), PeakAlpha(out.severity, Sanitized(cfg)));
            Log(b);
        }
    }
    // The pulse in the hands. In play: the tick, the beat of the life, as ever. In the settings panel: only the preview's five seconds (the draft's strength, whatever
    // the tick says); the red's fade-out after them is quiet, it is not the life's beat. On a weak motor (the PlayStation VR2 Sense under SteamVR) the gentle pulse is lifted
    // to what it shows (haptics::ForMotor); on any other the numbers go through as they are.
    const bool afterPreview = shown && !running;
    if ((out.beat.lub || out.beat.dub) && haptics::Enabled() && PulseAllowed(cfg.haptics && out.amount >= kHapticFrom, afterPreview)) {
        const Haptic h = HapticFor(!out.beat.lub, out.severity, cfg);
        const haptics::Motor motor = haptics::CurrentMotor();
        const haptics::Request r = haptics::ForMotor(motor, h.amplitude, (uint32_t)h.ms);
        bool& told = running ? g_toldFirstPreview : g_toldFirstPlay;
        if (!told) {   // the numbers, once for the life's beat and once for the preview's
            told = true;
            const Haptic lub = HapticFor(false, out.severity, cfg), dub = HapticFor(true, out.severity, cfg);
            const haptics::Request rl = haptics::ForMotor(motor, lub.amplitude, (uint32_t)lub.ms), rd = haptics::ForMotor(motor, dub.amplitude, (uint32_t)dub.ms);
            char b[420];
            snprintf(b, sizeof b,
                     "vrcam: low life vignette - the first %s beat in the controllers: lub %.2f for %u ms, dub %.2f for %u ms (the setting makes %.2f for %d ms and %.2f for %d ms; "
                     "%s motor; strength %.2f, [openxr] haptics_scale on top; no frequency asked, the runtime's)",
                     running ? "preview" : "real", rl.amplitude, (unsigned)rl.durationMs, rd.amplitude, (unsigned)rd.durationMs, lub.amplitude, lub.ms, dub.amplitude, dub.ms,
                     motor == haptics::Motor::Weak ? "weak" : "plain", cfg.hapticsStrength);
            Log(b);
        }
        haptics::Pulse(haptics::kLeft, r.amplitude, (int)r.durationMs);
        haptics::Pulse(haptics::kRight, r.amplitude, (int)r.durationMs);
    }
}

bool Current(Uniforms* out) {
    if (!Enabled()) return false;
    // No pair stepped it lately (the view changed, a load, the headset away): nothing, rather than a frozen tint.
    if (GetTickCount64() - g_frameAt.load(std::memory_order_relaxed) > 250) return false;
    const float a = g_alpha.load(std::memory_order_relaxed);
    if (!(a > 0.0f)) return false;
    *out = MakeUniforms(a, ActiveConfig(), g_tanX.load(std::memory_order_relaxed), g_tanY.load(std::memory_order_relaxed));
    return true;
}

}  // namespace lowhp
