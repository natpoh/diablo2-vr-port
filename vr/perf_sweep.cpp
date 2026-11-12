// perf_sweep.cpp - [debug] perf_sweep (vr/perf_sweep.h): an automatic A/B of the left pass's processor time, the ini untouched.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "perf_sweep.h"
#include "pipe_hunt.h"      // the game's calls of each stage and kind, counted
#include "render_bench.h"   // ... their names
#include "unleashed_far_objects.h"   // shared/: big_radius as the plugin reads it (0, or 300 - 1000)

namespace vrc::drawprof { extern std::atomic<bool> g_want; }   // (vr/diag/pair_probes.cpp: [debug] profile_draw's one 5 s profile)

namespace perfsweep {
namespace {

void (*g_log)(const char*) = nullptr;
void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char b[1024];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

struct Over { const wchar_t* sec; const wchar_t* key; float v; };
struct Step {
    std::string name;
    std::vector<Over> over;
    bool count = true;      // the calls counted (off: the step that measures the counting's own cost)
    bool profile = false;   // one 5 s profile of the draw thread while it measures (its numbers then carry the sampler's cost)
    int settleMs = 6000;
};
struct Result {
    double pairs = 0, cpu = 0, left = 0, right = 0, gap = 0, secs = 0;
    uint64_t stage[renderbench::kStages] = {}, sub[renderbench::kSubs] = {};
    bool counted = false, done = false;
};

constexpr int kMeasureMs = 12000;
constexpr ULONGLONG kNoF4AbortMs = 60000;

wchar_t g_ini[MAX_PATH] = {};
std::shared_mutex g_mu;            // g_over
std::vector<Over> g_over;          // the values in force now (the step's)
std::atomic<bool> g_overOn{false};
std::atomic<bool> g_reload{false};
std::atomic<bool> g_running{false};
bool g_keyWas = false;

// The run (the draw thread's, but Start / Stop from LoadSettings: under g_runMu)
std::mutex g_runMu;
std::vector<Step> g_steps;
std::vector<Result> g_results;
int g_step = -1;
ULONGLONG g_stepAt = 0, g_lastF4 = 0;
double g_settledMs = 0, g_measuredMs = 0;   // F4 time spent in the step (a pair outside F4 does not count)
bool g_measuring = false;
uint64_t g_stage0[renderbench::kStages] = {}, g_sub0[renderbench::kSubs] = {};
bool g_profiled = false;

float FileF(const wchar_t* sec, const wchar_t* key, float def) {
    wchar_t b[64], d[32];
    swprintf_s(d, L"%g", def);
    GetPrivateProfileStringW(sec, key, d, b, 64, g_ini);
    wchar_t* end = nullptr;
    const float v = wcstof(b, &end);
    return end != b && std::isfinite(v) ? v : def;
}

void SetOver(const std::vector<Over>& o) {
    {
        std::unique_lock lk(g_mu);
        g_over = o;
        g_overOn.store(!g_over.empty());
    }
    g_reload.store(true);
}

std::vector<Step> MakeSteps() {
    std::vector<Step> s;
    const float big = unleashed::farobjects::Normalize(FileF(L"render", L"big_radius", 0.0f));
    const float model = FileF(L"render", L"model_radius", 200.0f);
    const int rings = (int)std::lround(FileF(L"render", L"rings", 2.0f));
    const bool outline = FileF(L"render", L"game_outline", 0.0f) != 0.0f;
    char b[96];
    snprintf(b, sizeof b, "as set (big objects %.0f, models %.0f, %d room rings)", big, model, rings);
    s.push_back({b, {}});
    if (big > 300.0f) s.push_back({"big objects 300", {{L"render", L"big_radius", 300.0f}}});
    if (big > 0.0f) s.push_back({"big objects off", {{L"render", L"big_radius", 0.0f}}});
    if (model > 150.0f) s.push_back({"models 150", {{L"render", L"model_radius", 150.0f}}});
    if (model > 150.0f && big > 0.0f) s.push_back({"models 150 + big objects off", {{L"render", L"model_radius", 150.0f}, {L"render", L"big_radius", 0.0f}}});
    if (model > 100.0f) s.push_back({"models 100 + big objects off", {{L"render", L"model_radius", 100.0f}, {L"render", L"big_radius", 0.0f}}});
    if (rings > 1) s.push_back({"room rings 1", {{L"render", L"rings", 1.0f}}});
    {   // [game_patches] (vr/unleashed_game_patches.h): each of the six the other way from the file, then all six on - each goes in or out between
        // two frames, so the step's settle covers it. shadows_once / cull_once act only when the game draws both eyes ([stereo] native_replay 0, or
        // a one-pass pair paused): with one pass their steps are left out
        struct Gp { const wchar_t* key; const char* name; bool twoPassOnly; };
        static const Gp gps[] = {{L"transparent_filter", "transparent_filter", false}, {L"profiler_accounting", "profiler_accounting", false},
                                 {L"draw_debug_names", "draw_debug_names", false}, {L"selection_walk", "selection_walk", false},
                                 {L"shadows_once", "shadows_once", true}, {L"cull_once", "cull_once", true}};
        const bool twoPasses = FileF(L"stereo", L"native_replay", 0.0f) == 0.0f;
        std::vector<Over> allOn;
        bool anyOff = false;
        for (const Gp& g : gps) {
            const bool on = FileF(L"game_patches", g.key, 1.0f) != 0.0f;
            anyOff |= !on;
            allOn.push_back({L"game_patches", g.key, 1.0f});
            if (g.twoPassOnly && !twoPasses) continue;
            char n[96];
            snprintf(n, sizeof n, "game patch %s %s", g.name, on ? "OFF" : "on");
            s.push_back({n, {{L"game_patches", g.key, on ? 0.0f : 1.0f}}});
        }
        if (anyOff) s.push_back({"game patches: all six on", allOn});
    }
    if (!outline) {   // the outline's bench bit off: with no other bench key the pipe hunt's per-bind lookup is not made at all
        Step st{"the game's outline drawn (the bench's per-bind lookup idle; calls not counted)", {{L"render", L"game_outline", 1.0f}}};
        st.count = false;
        s.push_back(st);
    }
    s.push_back({"every game draw skipped (bench_skip_all: the driver's share)", {{L"render", L"bench_skip_all", 1.0f}}});
    {
        Step st{"ours off (bench_bare)", {{L"render", L"bench_bare", 1.0f}}};
        st.settleMs = 8000;
        s.push_back(st);
    }
    s.push_back({"as set again (drift)", {}});
    {
        Step st{"as set, the draw thread sampled 5 s (profile_draw: the sampler's own cost in it)", {}};
        st.profile = true;
        s.push_back(st);
    }
    return s;
}

void BeginStep(int i) {
    g_step = i;
    g_stepAt = GetTickCount64();
    g_settledMs = g_measuredMs = 0;
    g_measuring = false;
    g_profiled = false;
    const Step& st = g_steps[(size_t)i];
    SetOver(st.over);
    pipehunt::SetCounting(st.count);
    LogF("vrcam: perf sweep - step %d of %d: %s (%.0f s to settle, then %.0f s measured; F4 only)", i + 1, (int)g_steps.size(), st.name.c_str(),
         st.settleMs / 1000.0, kMeasureMs / 1000.0);
}

void Table() {
    LogF("vrcam: perf sweep - the table (a pair = both eyes; ms a pair; calls a pair of the game's, all its recording threads):");
    LogF("vrcam: perf sweep | %-82s | pairs/s |  CPU | left | right | game | lit  | models foliage terrain other | pre-pass | shadows | all", "step");
    for (size_t i = 0; i < g_steps.size() && i < g_results.size(); ++i) {
        const Result& r = g_results[i];
        if (!r.done) { LogF("vrcam: perf sweep | %-82s | not measured", g_steps[i].name.c_str()); continue; }
        const double k = r.pairs > 0 ? 0.001 / r.pairs : 0.0, per = r.pairs > 0 ? 1.0 / r.pairs : 0.0;
        char calls[256] = "| -    | -                            | -        | -       | -";
        if (r.counted) {
            uint64_t all = 0, other = 0;
            for (int s = 1; s < renderbench::kStages; ++s) all += r.stage[s];
            for (int s = renderbench::kSubAlphaTested; s < renderbench::kSubPrepass; ++s) other += r.sub[s];
            all += r.sub[renderbench::kSubPrepass];
            snprintf(calls, sizeof calls, "| %4.0f | %6.0f %7.0f %7.0f %5.0f | %8.0f | %7.0f | %4.0f", r.stage[renderbench::kLit] * per,
                     r.sub[renderbench::kSubModels] * per, r.sub[renderbench::kSubFoliage] * per, r.sub[renderbench::kSubTerrain] * per, other * per,
                     r.sub[renderbench::kSubPrepass] * per, (r.stage[renderbench::kShadows] + r.stage[renderbench::kShadowsPoint]) * per, all * per);
        }
        LogF("vrcam: perf sweep | %-82s | %7.1f | %4.2f | %4.2f | %5.2f | %4.2f %s", g_steps[i].name.c_str(), r.secs > 0 ? r.pairs / r.secs : 0.0,
             r.cpu * k, r.left * k, r.right * k, r.gap * k, calls);
    }
}

void StopLocked(const char* why) {
    if (!g_running.load()) return;
    g_running.store(false);
    SetOver({});
    pipehunt::SetCounting(false);
    LogF("vrcam: perf sweep - %s; every value back as the ini has it (the file was never written)", why);
    Table();
    g_step = -1;
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

void Configure(bool on, const wchar_t* iniPath) {
    if (iniPath) wcsncpy_s(g_ini, iniPath, _TRUNCATE);
    static bool first = true;   // the first read only remembers: a 1 left in the file does not start a run at every game start
    const bool was = first ? on : g_keyWas;
    first = false;
    g_keyWas = on;
    std::lock_guard lk(g_runMu);
    if (on && !was && !g_running.load()) {
        g_steps = MakeSteps();
        g_results.assign(g_steps.size(), Result{});
        g_running.store(true);
        g_lastF4 = GetTickCount64();
        LogF("vrcam: perf sweep ON ([debug] perf_sweep) - %d steps, ~%.0f s in VR first person: stand still, look the same way; nothing is "
             "written to d2r_vr.ini", (int)g_steps.size(), g_steps.size() * (6.5 + kMeasureMs / 1000.0));
        BeginStep(0);
    } else if (!on && was && g_running.load()) {
        StopLocked("stopped ([debug] perf_sweep turned 0)");
    }
}

bool Forced(const wchar_t* sec, const wchar_t* key, float* v) {
    if (!g_overOn.load(std::memory_order_relaxed)) return false;
    std::shared_lock lk(g_mu);
    for (const Over& o : g_over)
        if (_wcsicmp(o.sec, sec) == 0 && _wcsicmp(o.key, key) == 0) { *v = o.v; return true; }
    return false;
}

bool TakeReload() { return g_reload.exchange(false); }
bool Running() { return g_running.load(std::memory_order_relaxed); }

void OnPair(double cpuUs, double leftUs, double rightUs, double gapUs, bool f4) {
    if (!g_running.load(std::memory_order_relaxed)) return;
    std::lock_guard lk(g_runMu);
    if (!g_running.load() || g_step < 0 || g_step >= (int)g_steps.size()) return;
    const ULONGLONG now = GetTickCount64();
    if (!f4) {
        if (now - g_lastF4 > kNoF4AbortMs) StopLocked("stopped: no pair in VR first person (F4) for a minute");
        return;
    }
    const double dtMs = std::min(100.0, (double)(now - g_lastF4));   // (F4 time only: a pause outside F4 is not counted)
    g_lastF4 = now;
    const Step& st = g_steps[(size_t)g_step];
    Result& r = g_results[(size_t)g_step];
    if (!g_measuring) {
        g_settledMs += dtMs;
        if (g_settledMs < st.settleMs) return;
        g_measuring = true;
        pipehunt::Counts(g_stage0, g_sub0);
        if (st.profile && !g_profiled) { g_profiled = true; vrc::drawprof::g_want.store(true); }
        return;
    }
    g_measuredMs += dtMs;
    r.pairs += 1;
    r.cpu += cpuUs;
    r.left += leftUs;
    r.right += rightUs;
    r.gap += gapUs;
    r.secs += dtMs / 1000.0;
    if (g_measuredMs < kMeasureMs) return;
    uint64_t stage[renderbench::kStages], sub[renderbench::kSubs];
    pipehunt::Counts(stage, sub);
    for (int i = 0; i < renderbench::kStages; ++i) r.stage[i] = stage[i] - g_stage0[i];
    for (int i = 0; i < renderbench::kSubs; ++i) r.sub[i] = sub[i] - g_sub0[i];
    r.counted = st.count;
    r.done = true;
    const double k = r.pairs > 0 ? 0.001 / r.pairs : 0.0;
    LogF("vrcam: perf sweep - step %d done: %.1f pairs/s, CPU %.2f ms a pair (left pass %.2f, right %.2f, game %.2f)%s", g_step + 1,
         r.secs > 0 ? r.pairs / r.secs : 0.0, r.cpu * k, r.left * k, r.right * k, r.gap * k, r.counted ? "" : " - calls not counted");
    if (g_step + 1 < (int)g_steps.size()) BeginStep(g_step + 1);
    else StopLocked("done");
}

}  // namespace perfsweep
