// patch_hunt.cpp - [debug] patch_hunt, the plugin's side (vr/patch_hunt_wire.h; the steps and the game's places: vr/patch_hunt.h).
//
// The game's settings: found from their accessors (their bytes checked, their own names read in them), each accessor called on the UI thread for
// its object (as the game does before it reads one), the value put through the game's own setter 0xD2F020 there - the call its key commands and
// /players make - and given back when the step is left, unless the player changed it in the game's menu meanwhile. A fault in the game's code
// turns this part off for the session. Our own parts are left out by LoadSettings (vr/config/load_settings.cpp), the pipeline by vr/pipe_hunt.cpp.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <D2RLPlugin/api.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "banner.h"   // a message that must be seen (the capture key's), big in the middle
#include "log_async.h"
#include "patch_hunt.h"
#include "patch_hunt_wire.h"
#include "pipe_hunt.h"
#include "sigscan.h"
#include "ui_ticket.h"

namespace patchhunt {
namespace {

using namespace D2RL;
namespace ov = D2RL::Overlay;

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
const OverlayService* g_overlay = nullptr;
std::atomic<ov::CallbackHandle> g_frameCb{ov::InvalidCallbackHandle};
std::wstring g_ini;

std::atomic<int> g_step{0}, g_pipeK{0};
std::atomic<bool> g_configured{false};
std::atomic<ULONGLONG> g_changedAt{0};
std::atomic<bool> g_gameDue{false};     // the game's settings to be put to the step (the UI thread)
std::atomic<bool> g_gameFault{false};   // the game's code faulted once: off for the session
uiticket::Ticket g_queued;

void Say(const char* fmt, ...) {
    if (!g_ctx) return;
    char b[900];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    logasync::Info(g_ctx, b);
}

bool Peek(void* d, uintptr_t s, size_t n) noexcept {
    __try { memcpy(d, (const void*)s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- the game's settings ----------------------------------------------------------------------------------------------------------------------
struct Setting {
    uintptr_t accessor = 0, object = 0;   // checked; object: what the accessor's lea rax points to
    bool called = false;                  // its accessor called once (the object made): its value is the game's
    bool held = false;                    // we put the step's value there ...
    int was = -1;                         // ... over this one (given back when the step is left)
};
std::mutex g_gameLock;   // the UI thread's (Run); the overlay reads the published copies below, never this lock (the setter may wait on the picture)
Setting g_set[kGfxCount];
std::atomic<uintptr_t> g_pubObj[kGfxCount] = {};   // an object once its accessor was called, 0 not (yet)
std::atomic<int> g_pubWas[kGfxCount] = {};
std::atomic<bool> g_pubMissing{false};             // looked for and a piece not found (another game build)
uintptr_t g_setter = 0;
bool g_toldFound = false;
ULONGLONG g_lastLook = 0;

// Under g_gameLock: the setter and each accessor, checked - looked for again twice a second while one is missing.
void Find() {
    bool all = g_setter != 0;
    for (int g = 0; g < kGfxCount; ++g) all = all && g_set[g].object;
    if (all) return;
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastLook < 500) return;
    g_lastLook = now;
    if (!g_setter && d2rsig::Check(RVA_SETTING_SET, kSigSet, sizeof kSigSet)) g_setter = d2rsig::Addr(RVA_SETTING_SET);
    for (int g = 0; g < kGfxCount; ++g) {
        Setting& s = g_set[g];
        if (s.object) continue;
        const uintptr_t a = d2rsig::Addr(kGfx[g].accessor);
        uint8_t b[kAccessorScan] = {};
        if (!a || !Peek(b, a, sizeof b) || memcmp(b, kSigAccessorHead, sizeof kSigAccessorHead) != 0 || (b[3] != 0x58 && b[3] != 0x78) ||
            memcmp(b + 4, kSigAccessorGs, sizeof kSigAccessorGs) != 0)
            continue;
        uintptr_t object = 0;
        bool named = false;
        for (uint32_t i = 0; i + 10 <= sizeof b; ++i) {
            int32_t disp = 0;
            memcpy(&disp, b + i + 3, 4);
            const uintptr_t t = (uintptr_t)RipTarget(a + i, disp, 7);
            // lea rax, [rip + the object]; add rsp, ... - the accessor's return
            if (!object && b[i] == 0x48 && b[i + 1] == 0x8D && b[i + 2] == 0x05 && b[i + 7] == 0x48 && b[i + 8] == 0x83 && b[i + 9] == 0xC4) object = t;
            // lea rdx, [rip + its name] - its first call names it
            if (!named && b[i] == 0x48 && b[i + 1] == 0x8D && b[i + 2] == 0x15) {
                char n[40] = {};
                const size_t len = strlen(kGfx[g].name) + 1;
                named = len <= sizeof n && Peek(n, t, len) && memcmp(n, kGfx[g].name, len) == 0;
            }
        }
        if (object && named) { s.accessor = a; s.object = object; }
    }
    all = g_setter != 0;
    for (int g = 0; g < kGfxCount; ++g) all = all && g_set[g].object;
    if (all && !g_toldFound) {
        g_toldFound = true;
        Say("vrcam: patch_hunt - the game's graphics settings and their setter found (AO, shadows, lights, anti aliasing, DLSS, resolution scale)");
    }
}
int ValueAt(uintptr_t object, int g) {   // (any thread) the game's value now, -1 not known
    int32_t v = 0;
    return object && Peek(&v, object + kValueAt, 4) ? Sane(g, v) : -1;
}
int ValueOf(const Setting& s, int g) { return s.called ? ValueAt(s.object, g) : -1; }
int PublishedValue(int g) { return ValueAt(g_pubObj[g].load(), g); }

using AccessorFn = void* (*)();
using SetterFn = void (*)(void* object, int value);
bool CallAccessor(uintptr_t fn, void** out) noexcept {
    __try { *out = ((AccessorFn)fn)(); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallSetter(uintptr_t fn, void* object, int value) noexcept {
    __try { ((SetterFn)fn)(object, value); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// UI thread: every setting where the step wants it - the step's value over the game's, or the game's given back.
void __cdecl Run(const PluginContext*, void* user) noexcept {
    if (!g_queued.Claim(uiticket::FromUser(user))) return;
    if (g_gameFault.load()) return;
    g_gameDue.store(false);   // (a change from here on queues another run)
    const int step = g_step.load();
    std::lock_guard<std::mutex> lock(g_gameLock);
    g_lastLook = 0;
    Find();
    char changes[400] = "";
    for (int g = 0; g < kGfxCount; ++g) {
        Setting& s = g_set[g];
        if (!s.object || !g_setter) continue;
        void* obj = nullptr;
        if (!CallAccessor(s.accessor, &obj)) {
            g_gameFault.store(true);
            Say("vrcam: patch_hunt - the game's accessor of \"%s\" faulted: the game's settings are left alone for this session", kGfx[g].name);
            return;
        }
        if ((uintptr_t)obj != s.object) {
            Say("vrcam: patch_hunt - the game's accessor of \"%s\" gave another object (%p, not %p): not touched", kGfx[g].name, obj, (void*)s.object);
            s.object = 0;
            continue;
        }
        s.called = true;
        const int now = ValueOf(s, g);
        int to = -1;
        if (Touches(step, g)) {
            if (!s.held) { s.was = now; s.held = true; }
            if (now != kGfx[g].hunt) to = kGfx[g].hunt;
        } else if (s.held) {
            s.held = false;
            // given back only when it is still the step's (the player's own change in the game's menu kept)
            if (now == kGfx[g].hunt && s.was >= 0 && s.was != now) to = s.was;
        }
        g_pubObj[g].store(s.object);
        g_pubWas[g].store(s.was);
        if (to < 0) continue;
        if (!CallSetter(g_setter, obj, to)) {
            g_gameFault.store(true);
            Say("vrcam: patch_hunt - the game's setter faulted on \"%s\": the game's settings are left alone for this session", kGfx[g].name);
            return;
        }
        char one[96];
        snprintf(one, sizeof one, "%s\"%s\" %d -> %d (now %d)", changes[0] ? ", " : "", kGfx[g].name, now, to, ValueOf(s, g));
        strncat_s(changes, one, _TRUNCATE);
    }
    bool missing = !g_setter;
    for (int g = 0; g < kGfxCount; ++g) missing = missing || !g_set[g].object;
    g_pubMissing.store(missing);
    char vals[200];
    snprintf(vals, sizeof vals, "AO %d, shadows %d, lights %d, anti aliasing %d, DLSS %d, resolution scale %d", ValueOf(g_set[kAo], kAo),
             ValueOf(g_set[kShadow], kShadow), ValueOf(g_set[kLight], kLight), ValueOf(g_set[kAa], kAa), ValueOf(g_set[kDlss], kDlss),
             ValueOf(g_set[kResScale], kResScale));
    Say("vrcam: patch_hunt %d - the game's settings %s%s; now %s (-1 not known)", step, changes[0] ? "set through its own setter: " : "unchanged",
        changes, vals);
}

// ---- the overlay line ------------------------------------------------------------------------------------------------------------------------
ov::Color C(int r, int g, int b, float a = 1.0f) { return {r / 255.0f, g / 255.0f, b / 255.0f, a}; }
void Fill(const ov::Frame* f, float x, float y, float w, float h, ov::Color c) {
    ov::FilledRectangleRequest q{};
    q.structSize = ov::FilledRectangleRequestSize;
    q.canvas = f->canvas;
    q.rect = {x, y, x + w, y + h};
    q.color = c;
    q.rounding = 4.0f;
    g_overlay->drawFilledRectangle(g_ctx, &q);
}
float Width(const ov::Frame* f, float size, const std::string& s) {
    if (s.empty() || !g_overlay->measureText) return (float)s.size() * size * 0.5f;
    ov::MeasureTextRequest q{};
    q.structSize = ov::MeasureTextRequestSize;
    q.canvas = f->canvas;
    q.textSize = size;
    q.text = s.data();
    q.textLength = (uint32_t)std::min<size_t>(s.size(), ov::MaxTextBytes);
    ov::TextMetrics m{};
    m.structSize = ov::TextMetricsSize;
    return g_overlay->measureText(g_ctx, &q, &m) == ov::Result::Success ? m.width : (float)s.size() * size * 0.5f;
}
void Text(const ov::Frame* f, float x, float y, float size, ov::Color c, const std::string& s) {
    ov::TextRequest q{};
    q.structSize = ov::TextRequestSize;
    q.canvas = f->canvas;
    q.position = {x, y};
    q.color = c;
    q.textSize = size;
    q.text = s.data();
    q.textLength = (uint32_t)std::min<size_t>(s.size(), ov::MaxTextBytes);
    g_overlay->drawText(g_ctx, &q);
}

// The second line: what the step did, as known now.
std::string Detail(int step) {
    char b[300] = "";
    if (step >= kGameAo && step <= kGameUpscale || step == kAllAtOnce) {
        if (g_gameFault.load()) return "the game's code faulted - set it yourself in Options > Video";
        std::string s;
        for (int g = 0; g < kGfxCount; ++g) {
            if (!Touches(step, g)) continue;
            char one[96];
            if (!g_pubObj[g].load()) snprintf(one, sizeof one, g_pubMissing.load() ? "%s NOT found: set it in Options > Video" : "%s ...", kGfx[g].name);
            else snprintf(one, sizeof one, "%s %d (was %d)", kGfx[g].name, PublishedValue(g), g_pubWas[g].load());
            if (!s.empty()) s += ", ";
            s += one;
        }
        if (step == kAllAtOnce) s += " + depth_fix, light_bounds, our sky/fog/ceiling off";
        return s;
    }
    if (step == kPipeline) {
        const pipehunt::Now n = pipehunt::State();
        if (n.recording && !n.count) return "recording one frame of the game's pipelines ...";
        if (!n.count) return "no pipe trace yet - Ctrl+F10";
        if (!n.index) snprintf(b, sizeof b, "%d pipelines in the trace: Ctrl+Alt+F8 skips the first, then the next", n.count);
        else snprintf(b, sizeof b, "pipe %d / %d skipped: %c %016llx (%u calls a frame; %u left out last second) - Ctrl+Alt+F8 next", n.index, n.count,
                      n.kind, (unsigned long long)n.hash, n.draws, n.skippedLastSecond);
        return b;
    }
    if (step == kNormal) return "everything as you have it";
    return "";
}

void __cdecl OnFrame(const PluginContext*, const ov::Frame* f, void*) noexcept {
    if (!f || !g_overlay) return;
    const int step = g_step.load();
    const ULONGLONG now = GetTickCount64();
    banner::Message m;
    if (banner::Now(now, &m)) {   // vr/banner.h: big, in the middle, a third down - it must be seen
        const float s = std::max(20.0f, f->screenHeight / 26.0f);
        const float w = Width(f, s, m.text) + 2.0f * s, h = s * 2.0f, x = (f->screenWidth - w) * 0.5f, y = f->screenHeight * 0.30f;
        Fill(f, x, y, w, h, C(10, 14, 18, 0.88f));
        Fill(f, x, y + h - s * 0.12f, w, s * 0.12f, m.ok ? C(90, 200, 120) : C(240, 150, 60));
        Text(f, x + s, y + s * 0.5f, s, m.ok ? C(150, 240, 170) : C(255, 190, 110), m.text);
    }
    if (!step && now - g_changedAt.load() > 6000) return;   // 0: said for 6 s after a change, then nothing
    const float size = std::max(14.0f, f->screenHeight / 42.0f), sizeSub = size * 0.78f;
    char head[160];
    snprintf(head, sizeof head, "patch_hunt %d: %s   (Ctrl+F8 next, Ctrl+Shift+F8 back)", step, What(step));
    const std::string a = head, b = Detail(step);
    const float w = std::max(Width(f, size, a), Width(f, sizeSub, b)) + size, h = size + (b.empty() ? 0.0f : sizeSub * 1.25f) + size * 0.6f;
    const float x = (f->screenWidth - w) * 0.5f, y = size * 0.4f;
    Fill(f, x, y, w, h, C(10, 14, 18, 0.78f));
    Text(f, x + size * 0.5f, y + size * 0.3f, size, step ? C(255, 214, 120) : C(150, 220, 160), a);
    if (!b.empty()) Text(f, x + size * 0.5f, y + size * 0.3f + size * 1.15f, sizeSub, C(225, 232, 236), b);
}

void Register() {
    if (!g_overlay || !g_ctx || g_frameCb.load() != ov::InvalidCallbackHandle) return;
    ov::CallbackRegistration reg{};
    reg.structSize = ov::CallbackRegistrationSize;
    reg.phase = ov::Phase::AfterGameUi;
    reg.callback = &OnFrame;
    ov::CallbackHandle h = ov::InvalidCallbackHandle;
    const ov::Result r = g_overlay->registerFrameCallback(g_ctx, &reg, &h);
    if (r == ov::Result::Success) g_frameCb.store(h);
    else Say("vrcam: patch_hunt - D2RLoader's overlay NOT taken (result %u): no line on the picture, the log still says each step", (unsigned)r);
}

template <class S> const S* Query(const PluginContext* ctx) {
    const S* s = nullptr;
    return ctx->QueryService(&s) == ServiceQueryResult::Success ? s : nullptr;
}

// A key's edge (the update thread).
struct Key {
    int vk;
    bool was = false;
    bool Pressed(bool focus) {
        const bool now = focus && (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool edge = now && !was;
        was = now;
        return edge;
    }
};
void WriteIni(const wchar_t* key, int v) {
    wchar_t b[16];
    swprintf_s(b, L"%d", v);
    if (!g_ini.empty() && !WritePrivateProfileStringW(L"debug", key, b, g_ini.c_str()))
        Say("vrcam: patch_hunt - d2r_vr.ini could not be written (error %lu): set [debug] %ls=%d by hand", GetLastError(), key, v);
}

}  // namespace

void Init(const PluginContext* ctx, const wchar_t* iniPath) {
    g_ctx = ctx;
    g_ini = iniPath ? iniPath : L"";
    if (!ctx) return;
    g_threads = Query<ThreadService>(ctx);
    if (ctx->QueryService(&g_overlay) != ServiceQueryResult::Success || !HasOverlayServiceField(g_overlay, OverlayServiceRequiredSize)) g_overlay = nullptr;
    else Register();
}

void Configure(int step, int pipeK, const Suspects& s) {
    step = Clamp(step);
    pipeK = std::max(0, pipeK);
    const int was = g_step.exchange(step), wasK = g_pipeK.exchange(pipeK);
    const bool first = !g_configured.exchange(true);
    pipehunt::SetHunt(PipelineSkip(step), pipeK);
    if (step == kPipeline && !pipehunt::State().count && !pipehunt::State().recording && !g_ini.empty()) {   // step 8 needs a list: one frame recorded
        std::wstring path = g_ini;
        path.resize(path.find_last_of(L"\\/") + 1);
        pipehunt::Arm(path + L"d2r_vr_pipetrace.txt");
    }
    if (step == was && pipeK == wasK && !first) return;
    if (first && !step) return;   // the start with 0: nothing to say
    g_changedAt.store(GetTickCount64());
    if (step != was || first) g_gameDue.store(true);
    const pipehunt::Now p = pipehunt::State();
    Say("vrcam: patch_hunt %d - %s | as the ini has the rest: depth_fix_mode %d%s, light_bounds_fix %d / _flat %d%s, our fog %d sky %d ceiling %d blur %d, "
        "game sky %d game ceiling %d%s | skip_pipeline %llx, skip_shaders %d, patch_hunt_pipe %d (%d in the last pipe trace) | platform %d, flat_shift %.3f / %.3f",
        step, What(step), s.depthFixMode, DepthFixOff(step) ? " (-> 0 now)" : "", s.lightBoundsFix ? 1 : 0, s.lightBoundsFlat ? 1 : 0,
        LightBoundsOff(step) ? " (not bound now)" : "", s.fog ? 1 : 0, s.sky ? 1 : 0, s.ceiling ? 1 : 0, s.blur ? 1 : 0, s.gameSky ? 1 : 0,
        s.gameCeiling ? 1 : 0, OwnEffectsOff(step) ? " (all off now)" : "", (unsigned long long)p.skipLegacy, p.shaders, pipeK, p.count, s.platform,
        s.flatShiftX, s.flatShiftY);
}

int Step() { return g_step.load(std::memory_order_relaxed); }

void GameValues(int out[6]) {
    std::lock_guard<std::mutex> lock(g_gameLock);
    Find();
    for (int g = 0; g < kGfxCount && g < 6; ++g) out[g] = g_set[g].object ? ValueAt(g_set[g].object, g) : -1;
}

void Notice(const char* text, unsigned ms) { banner::Show(text ? text : "", true, GetTickCount64(), ms); }

void KeyTick(bool focus) {
    static Key f8{VK_F8};
    if (!f8.Pressed(focus) || !(GetAsyncKeyState(VK_CONTROL) & 0x8000)) return;
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0, alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (alt) {   // step 8's pipeline: the next / the last of the trace's list
        const int n = pipehunt::State().count;
        int k = g_pipeK.load();
        k = n ? (shift ? (k <= 1 ? n : k - 1) : (k >= n ? 1 : k + 1)) : 0;
        WriteIni(L"patch_hunt_pipe", k);
        if (g_step.load() != kPipeline) WriteIni(L"patch_hunt", kPipeline);
        Say("vrcam: patch_hunt - Ctrl+Alt%s+F8: patch_hunt_pipe %d of %d (d2r_vr.ini)", shift ? "+Shift" : "", k, n);
        return;
    }
    const int to = shift ? Prev(g_step.load()) : Next(g_step.load());
    WriteIni(L"patch_hunt", to);
    Say("vrcam: patch_hunt - Ctrl%s+F8: %d (d2r_vr.ini)", shift ? "+Shift" : "", to);
}

void Tick() {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || g_gameFault.load()) return;
    if (!g_gameDue.load()) return;
    if (const uint32_t token = g_queued.Start(GetTickCount64()))
        if (g_threads->runOnUiThread(g_ctx, &Run, uiticket::ToUser(token)) != Threads::Result::Success) g_queued.Claim(token);
}

}  // namespace patchhunt
