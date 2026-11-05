// auto_tune_run.cpp - [autotune] the "Auto" graphics tuning in the game (vr/auto_tune_run.h): vr/auto_tune.h's steps applied live
// through the plugin's own ini reads, measured at each xrEndFrame, kept or undone by the player.
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
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "auto_tune.h"
#include "auto_tune_run.h"
#include "render_size.h"   // shared/: [render] vr_resolution / vr_resolution_px read together
#include "ui_text.h"

namespace autotunerun {
namespace {

void (*g_log)(const char*) = nullptr;
void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

// -- the values handed to the ini reads instead of the file's ------------------------------------------------------------
struct Over { std::wstring sec, key; float v = 0; };
std::shared_mutex g_overMu;
std::vector<Over> g_over;                 // (under g_overMu)
std::atomic<bool> g_overOn{false};        // any at all: the reads' fast way out
std::atomic<bool> g_reload{false};

void SetOverrides(std::vector<Over> o) {
    {
        std::unique_lock lk(g_overMu);
        g_over = std::move(o);
        g_overOn.store(!g_over.empty());
    }
    g_reload.store(true);
}

// -- the knobs -------------------------------------------------------------------------------------------------------------
struct Set { const wchar_t* sec; const wchar_t* key; int v; };
struct KnobDef {
    autotune::Knob k;
    std::vector<std::vector<Set>> levels;   // the keys each level writes (the start level: the file's own - nothing forced)
    std::vector<std::string> names;         // a level's words ("off", "on", "2048 px")
    std::string what;                       // the knob's words ("Sky")
    int settleMs = 2500;                    // after a change of this knob: frames settle before they are counted
};

wchar_t g_ini[MAX_PATH] = {};
std::atomic<bool> g_enabled{false};
int g_serial = 0, g_keepSerial = 0, g_undoSerial = 0;
bool g_serialSeen = false;
std::atomic<double> g_depthFogMs{0.0}, g_distBlurMs{0.0};

// The file's own value (no override: read before any is set, or the file's on purpose).
int FileInt(const wchar_t* sec, const wchar_t* key, int def) {
    wchar_t b[64], d[32];
    swprintf_s(d, L"%d", def);
    GetPrivateProfileStringW(sec, key, d, b, 64, g_ini);
    wchar_t* end = nullptr;
    const double v = wcstod(b, &end);
    return end != b && std::isfinite(v) ? (int)std::lround(v) : def;
}

KnobDef Toggle(const char* id, const std::string& what, const wchar_t* sec, const wchar_t* key, float worth, float hint) {
    KnobDef d;
    d.k.id = id;
    d.k.levels = 2;
    d.k.start = 1;
    d.k.worth = worth;
    d.k.hintMs = hint;
    d.levels = {{{sec, key, 0}}, {{sec, key, 1}}};
    d.names = {uitext::Tr("vr.autotune.off", "off"), uitext::Tr("vr.autotune.on", "on")};
    d.what = what;
    return d;
}

std::string Px(int side) { return std::to_string(side) + " px"; }

// What may be turned down, from the file as it is now. Only what is on is a knob (a thing the player has off stays off).
std::vector<KnobDef> MakeKnobs(uint32_t recW, uint32_t recH, bool fogFxHooked, std::string* skipped) {
    std::vector<KnobDef> out;
    const double depthFog = g_depthFogMs.load(), distBlur = g_distBlurMs.load();
    const bool frameGen = FileInt(L"openxr", L"frame_generation", 0) != 0;

    // the fog laid before the game's transparent pass: the dearest of ours where it is on (72 -> 41 fps on PC1); live only if hooked at the start
    if (FileInt(L"fog", L"game_fog_before_fx", 0) && fogFxHooked)
        out.push_back(Toggle("fog_before_fx", uitext::Tr("vr.autotune.k.fog_before_fx", "Fog before effects"), L"fog", L"game_fog_before_fx", 0.7f, 8.0f));
    // our sky and our fog share one pass (PS_DepthFog): its time orders both
    if (FileInt(L"sky", L"enabled", 0))
        out.push_back(Toggle("sky", uitext::Tr("vr.autotune.k.sky", "Sky"), L"sky", L"enabled", 1.0f, depthFog > 0 ? (float)depthFog * 10.0f : 4.0f));
    if (FileInt(L"fog", L"enabled", 0)) {   // three levels: off, light, full (a light fog to start: two)
        KnobDef d;
        d.k.id = "fog";
        const bool light = FileInt(L"fog", L"light", 0) != 0;
        d.k.levels = 3;
        d.k.start = light ? 1 : 2;
        d.k.worth = 1.2f;
        d.k.hintMs = depthFog > 0 ? (float)depthFog * 8.0f : 3.0f;
        d.levels = {{{L"fog", L"enabled", 0}}, {{L"fog", L"enabled", 1}, {L"fog", L"light", 1}}, {{L"fog", L"enabled", 1}, {L"fog", L"light", 0}}};
        d.names = {uitext::Tr("vr.autotune.off", "off"), uitext::Tr("vr.autotune.fog_light", "light"), uitext::Tr("vr.autotune.fog_full", "full")};
        d.what = uitext::Tr("vr.autotune.k.fog", "Fog");
        out.push_back(d);
    }
    if (FileInt(L"ceiling", L"enabled", 0))
        out.push_back(Toggle("ceiling", uitext::Tr("vr.autotune.k.ceiling", "Cave ceiling (effect)"), L"ceiling", L"enabled", 0.8f, 2.0f));
    if (FileInt(L"render", L"distance_blur", 0))
        out.push_back(Toggle("distance_blur", uitext::Tr("vr.autotune.k.distance_blur", "Distance blur"), L"render", L"distance_blur", 0.4f,
                             distBlur > 0 ? (float)distBlur * 10.0f : 1.5f));
    if (FileInt(L"fog", L"game_fog", 0) && FileInt(L"fog", L"game_fog_height", 0))
        out.push_back(Toggle("game_fog_height", uitext::Tr("vr.autotune.k.game_fog_height", "Fog thinned with height"), L"fog", L"game_fog_height", 0.5f, 1.0f));

    // the stereo mode: both eyes a frame -> eyes by turns (D2R VR Settings' "Native OpenXR - eyes by turns" writes these). Left alone with
    // frame generation on: its settings follow the mode (xr.cpp framegenlib::Configure)
    const bool pairs = FileInt(L"stereo", L"afr", 0) && FileInt(L"stereo", L"pair_per_tick", 0);
    if (pairs && frameGen) {
        *skipped += " stereo (frame generation is on)";
    } else if (pairs && FileInt(L"openxr", L"on", 0)) {
        KnobDef d;
        d.k.id = "stereo";
        d.k.levels = 2;
        d.k.start = 1;
        d.k.worth = 3.0f;
        d.k.hintMs = 6.0f;
        const int replay = FileInt(L"stereo", L"replay_right", 0);
        d.levels = {{{L"stereo", L"pair_per_tick", 0}, {L"stereo", L"replay_right", 0}}, {{L"stereo", L"pair_per_tick", 1}, {L"stereo", L"replay_right", replay}}};
        d.names = {uitext::Tr("vr.autotune.eyes_by_turns", "eyes by turns"), uitext::Tr("vr.autotune.full_stereo", "full stereo")};
        d.what = uitext::Tr("vr.autotune.k.stereo", "3D in the headset");
        d.settleMs = 6000;   // (replay waits for the picture to settle after a change)
        out.push_back(d);
    }

    // the render size, last in the probes (each change rebuilds the picture: a few seconds frozen). A square side, or the headset's
    // recommendation as its longer side; the window's own size (Desktop) is left as it is
    const int mode = FileInt(L"render", L"vr_resolution", 0), pxKey = FileInt(L"render", L"vr_resolution_px", 0);
    const rendersize::Pick pick = rendersize::FromIni(mode, pxKey);
    int side = pick.kind == rendersize::Kind::Square ? pick.side : pick.kind == rendersize::Kind::Headset ? (int)std::max(recW, recH) : 0;
    if (side >= rendersize::kSideMin) {
        std::vector<int> sides;
        for (const double f : {0.7, 0.8, 0.9}) {
            const int s = std::max(rendersize::kSideMin, (int)(side * f) / rendersize::kSideStep * rendersize::kSideStep);
            if (s < side && (sides.empty() || sides.back() != s)) sides.push_back(s);
        }
        if (!sides.empty()) {
            KnobDef d;
            d.k.id = "scale";
            d.k.levels = (int)sides.size() + 1;
            d.k.start = (int)sides.size();
            d.k.worth = 2.0f;
            d.k.hintMs = 0.5f;
            for (const int s : sides) {
                d.levels.push_back({{L"render", L"vr_resolution", 0}, {L"render", L"vr_resolution_px", s}});
                d.names.push_back(Px(s));
            }
            d.levels.push_back({{L"render", L"vr_resolution", mode}, {L"render", L"vr_resolution_px", pxKey}});
            d.names.push_back(pick.kind == rendersize::Kind::Headset ? uitext::Tr("vr.autotune.headset_size", "headset's size") : Px(side));
            d.what = uitext::Tr("vr.autotune.k.scale", "Render size");
            d.settleMs = 9000;   // (the game freezes about 7 s while the picture is rebuilt)
            out.push_back(d);
        }
    } else {
        *skipped += " render size (the window's own size)";
    }
    return out;
}

// -- the run ---------------------------------------------------------------------------------------------------------------
std::mutex g_mu;
std::atomic<int> g_state{(int)State::Idle};
std::vector<KnobDef> g_knobs;              // (under g_mu)
std::unique_ptr<autotune::Tuner> g_tuner;  // (under g_mu)
std::vector<int> g_applied;                // the levels in force now (under g_mu)
std::atomic<uint64_t> g_rev{1};
std::atomic<uint32_t> g_recW{0}, g_recH{0};
std::atomic<bool> g_fogFxHooked{false};
std::atomic<bool> g_writing{false};

// the measure (XR thread, under g_mu)
enum class Step { Settle, Window };
Step g_step = Step::Settle;
int g_settlePending = -1;                // ms to settle, from the next frame on (-1: none asked)
double g_settleUntil = 0, g_windowFrom = 0, g_windowMs = 3000, g_lastUs = 0, g_waitSum = 0, g_maxGap = 0, g_periodNs = 0;
uint32_t g_frames = 0;
bool g_spoilt = false;

std::vector<Over> OverridesOf(const std::vector<int>& lv) {
    std::vector<Over> o;
    for (size_t i = 0; i < g_knobs.size() && i < lv.size(); ++i) {
        if (lv[i] == g_knobs[i].k.start) continue;   // the player's own level: the file's value
        for (const Set& s : g_knobs[i].levels[lv[i]]) o.push_back({s.sec, s.key, (float)s.v});
    }
    return o;
}

void Apply(const std::vector<int>& lv) {
    int settle = 1500;
    for (size_t i = 0; i < lv.size(); ++i)
        if (g_applied.size() != lv.size() || g_applied[i] != lv[i]) settle = std::max(settle, g_knobs[i].settleMs);
    g_applied = lv;
    SetOverrides(OverridesOf(lv));
    g_step = Step::Settle;
    g_settlePending = settle;   // (timed on the XR clock from the next frame)
}

std::string LevelsText(const std::vector<int>& lv) {
    std::string s;
    for (size_t i = 0; i < g_knobs.size() && i < lv.size(); ++i) {
        if (!s.empty()) s += ", ";
        s += g_knobs[i].k.id;
        s += ' ';
        s += g_knobs[i].names[lv[i]];
    }
    return s.empty() ? "nothing to turn down" : s;
}

void Finish();

void Begin() {
    std::string skipped;
    g_knobs = MakeKnobs(g_recW.load(), g_recH.load(), g_fogFxHooked.load(), &skipped);
    std::vector<autotune::Knob> ks;
    for (const auto& d : g_knobs) ks.push_back(d.k);
    g_tuner = std::make_unique<autotune::Tuner>(std::move(ks));
    g_applied.clear();
    g_state.store((int)State::Running);
    LogF("vrcam: auto tune - started: %s%s%s; frame generation left as it is", LevelsText(g_tuner->Want()).c_str(), skipped.empty() ? "" : "; not tuned:",
         skipped.c_str());
    Apply(g_tuner->Want());
    g_rev.fetch_add(1);
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

void Configure(const wchar_t* iniPath, bool enabled, int runSerial, int keepSerial, int undoSerial) {
    if (iniPath && iniPath[0]) wcscpy_s(g_ini, iniPath);
    const bool was = g_enabled.exchange(enabled);
    if (was && !enabled && g_state.load() != (int)State::Idle) Undo();
    if (!g_serialSeen) {
        g_serialSeen = true;
        g_serial = runSerial;
        g_keepSerial = keepSerial;
        g_undoSerial = undoSerial;
        return;
    }
    const bool run = runSerial != g_serial, keep = keepSerial != g_keepSerial, undo = undoSerial != g_undoSerial;
    g_serial = runSerial;
    g_keepSerial = keepSerial;
    g_undoSerial = undoSerial;
    if (!enabled) return;
    if (undo) { LogF("vrcam: auto tune - Undo asked from D2R VR Settings"); Undo(); }
    else if (keep) { LogF("vrcam: auto tune - Keep asked from D2R VR Settings"); Keep(); }
    else if (run) { LogF("vrcam: auto tune - asked from D2R VR Settings"); Start(); }
}

bool Enabled() { return g_enabled.load(); }

bool Forced(const wchar_t* sec, const wchar_t* key, float* v) {
    if (!g_overOn.load(std::memory_order_relaxed)) return false;
    std::shared_lock lk(g_overMu);
    for (const Over& o : g_over)
        if (_wcsicmp(o.sec.c_str(), sec) == 0 && _wcsicmp(o.key.c_str(), key) == 0) { *v = o.v; return true; }
    return false;
}

bool TakeReload() { return g_reload.exchange(false); }

void NoteGpu(double depthFogMs, double distBlurMs, bool fogBeforeFxHooked) {
    g_depthFogMs.store(depthFogMs);
    g_distBlurMs.store(distBlurMs);
    g_fogFxHooked.store(fogBeforeFxHooked);
}

void SetRecommended(uint32_t w, uint32_t h) {
    g_recW.store(w);
    g_recH.store(h);
}

void Start() {
    if (!g_enabled.load() || g_writing.load()) return;
    std::lock_guard lk(g_mu);
    if (g_state.load() != (int)State::Idle) return;
    Begin();
}

void OnFrame(double nowUs, double waitUs, double periodNs, bool counts) {
    if (g_state.load(std::memory_order_relaxed) != (int)State::Running) return;
    std::lock_guard lk(g_mu);
    if (g_state.load() != (int)State::Running || !g_tuner) return;
    if (g_settlePending >= 0) { g_settleUntil = nowUs + g_settlePending * 1000.0; g_settlePending = -1; }
    const double gap = g_lastUs > 0 ? nowUs - g_lastUs : 0;
    g_lastUs = nowUs;
    if (periodNs > 0) g_periodNs = periodNs;
    if (g_step == Step::Settle) {
        if (nowUs < g_settleUntil) return;
        g_step = Step::Window;
        g_windowFrom = nowUs;
        g_windowMs = g_tuner->phase() == autotune::Phase::Final ? 5000.0 : 3000.0;
        g_frames = 0;
        g_waitSum = 0;
        g_maxGap = 0;
        g_spoilt = false;
        return;
    }
    ++g_frames;
    g_waitSum += waitUs;
    g_maxGap = std::max(g_maxGap, gap);
    if (!counts) g_spoilt = true;
    const double spanUs = nowUs - g_windowFrom;
    if (spanUs < g_windowMs * 1000.0) return;

    autotune::Measure m;
    const double s = spanUs / 1e6;
    m.valid = !g_spoilt && g_frames >= 15 && g_maxGap < 250e3 && g_periodNs > 0;   // a load, a menu, a freeze in the window: measured again
    m.fps = (float)(g_frames / s);
    m.busyMs = (float)(spanUs / g_frames / 1000.0 - g_waitSum / g_frames / 1000.0);
    m.hz = g_periodNs > 0 ? (float)(1e9 / g_periodNs) : 0.0f;
    const autotune::Phase ph = g_tuner->phase();
    const std::vector<int> was = g_tuner->Want();
    g_tuner->Feed(m);
    LogF("vrcam: auto tune - %s %s: %.1f frames/s, %.2f ms a frame of our own (headset %.0f Hz)%s", autotune::PhaseName(ph), LevelsText(was).c_str(),
         m.fps, m.busyMs, m.hz, m.valid ? "" : g_spoilt ? " - not counted (out of the world / F4), again" : " - not counted (a freeze or too few frames), again");
    g_rev.fetch_add(1);
    if (g_tuner->Done()) {
        Finish();
        return;
    }
    Apply(g_tuner->Want());
}

namespace {
void Finish() {   // (under g_mu)
    const autotune::Tuner& t = *g_tuner;
    if (t.Aborted()) {
        LogF("vrcam: auto tune - given up (no frames could be counted): everything as it was");
        SetOverrides({});
        g_state.store((int)State::Idle);
        g_rev.fetch_add(1);
        return;
    }
    const std::vector<Over> o = OverridesOf(t.Result());
    SetOverrides(o);   // the result stays in force while it waits for Keep or Undo
    LogF("vrcam: auto tune - %s %.0f frames/s (headset %.0f Hz): %s - waits for Keep or Undo", t.Reached() ? "reached" : "NOT reached", t.Target(), t.Hz(),
         LevelsText(t.Result()).c_str());
    g_state.store((int)State::Waiting);
    g_rev.fetch_add(1);
}

// The ini's folder + name.
std::wstring Beside(const wchar_t* name) {
    std::wstring p = g_ini;
    const size_t slash = p.find_last_of(L"\\/");
    p.resize(slash == std::wstring::npos ? 0 : slash + 1);
    return p + name;
}
}  // namespace

void Keep() {
    std::vector<Over> o;
    std::vector<std::string> report;
    {
        std::lock_guard lk(g_mu);
        if (g_state.load() != (int)State::Waiting || !g_tuner || g_writing.load()) return;
        o = OverridesOf(g_tuner->Result());
        const autotune::Tuner& t = *g_tuner;
        char b[256];
        snprintf(b, sizeof b, "Target %.0f frames/s (headset %.0f Hz): %s", t.Target(), t.Hz(), t.Reached() ? "reached" : "not reached");
        report.push_back(b);
        for (const auto& r : t.Log()) {
            const char* what = r.knob >= 0 ? g_knobs[r.knob].k.id : "all";
            snprintf(b, sizeof b, "%-8s %-16s %s -> %s  %.1f -> %.1f frames/s  %.2f -> %.2f ms%s", autotune::PhaseName(r.phase), what,
                     r.knob >= 0 ? g_knobs[r.knob].names[r.from].c_str() : "", r.knob >= 0 ? g_knobs[r.knob].names[r.to].c_str() : "", r.fpsBefore,
                     r.fpsAfter, r.busyBefore, r.busyAfter, r.kept ? "  kept" : "");
            report.push_back(b);
        }
        report.push_back("Result: " + LevelsText(t.Result()));
        g_writing.store(true);
    }
    // the file on its own thread (a copy, a few keys): the frame is not held up
    std::thread([o = std::move(o), report = std::move(report)] {
        const std::wstring ini = g_ini, backup = Beside(L"d2r_vr.ini.before_autotune");
        const bool copied = CopyFileW(ini.c_str(), backup.c_str(), FALSE) != 0;
        int written = 0;
        if (copied) {
            for (const Over& v : o) {
                wchar_t b[32];
                swprintf_s(b, L"%d", (int)std::lround(v.v));
                if (WritePrivateProfileStringW(v.sec.c_str(), v.key.c_str(), b, ini.c_str())) ++written;
            }
            if (FILE* f = _wfopen(Beside(L"d2r_vr_autotune.txt").c_str(), L"wb")) {
                for (const auto& line : report) fprintf(f, "%s\r\n", line.c_str());
                fclose(f);
            }
        }
        LogF("vrcam: auto tune - Keep: %d of %zu key(s) written to d2r_vr.ini%s", written, o.size(),
             copied ? " (the old one copied to d2r_vr.ini.before_autotune first; d2r_vr_autotune.txt has the steps)" : " - NOT written: the copy failed");
        std::lock_guard lk(g_mu);
        SetOverrides({});   // the file has them now (or kept what it had: the copy failed)
        g_state.store((int)State::Idle);
        g_writing.store(false);
        g_rev.fetch_add(1);
    }).detach();
}

void Undo() {
    std::lock_guard lk(g_mu);
    if (g_state.load() == (int)State::Idle || g_writing.load()) return;
    LogF("vrcam: auto tune - %s: the settings as they were (d2r_vr.ini was not touched)", g_state.load() == (int)State::Running ? "stopped" : "Undo");
    SetOverrides({});
    g_state.store((int)State::Idle);
    g_rev.fetch_add(1);
}

uint64_t Revision() { return g_rev.load(); }

View Now() {
    View v;
    std::lock_guard lk(g_mu);
    v.state = (State)g_state.load();
    v.revision = g_rev.load();
    if (!g_tuner || v.state == State::Idle) return v;
    const autotune::Tuner& t = *g_tuner;
    v.measured = t.Measured();
    v.left = t.Remaining();
    v.reached = t.Reached();
    v.aborted = t.Aborted();
    v.targetFps = t.Target();
    v.hz = t.Hz();
    v.nowFps = t.Last().fps;
    for (const auto& r : t.Log()) {
        if (r.phase == autotune::Phase::Baseline) v.startFps = r.fpsAfter;
        Row w;
        w.phase = (int)r.phase;
        w.kept = r.kept;
        w.fpsBefore = r.fpsBefore;
        w.fpsAfter = r.fpsAfter;
        if (r.knob >= 0 && r.knob < (int)g_knobs.size()) {
            w.what = g_knobs[r.knob].what;
            w.from = g_knobs[r.knob].names[r.from];
            w.to = g_knobs[r.knob].names[r.to];
        }
        v.rows.push_back(w);
    }
    const std::vector<int>& res = v.state == State::Waiting ? t.Result() : t.Want();
    for (size_t i = 0; i < g_knobs.size() && i < res.size(); ++i) {
        if (res[i] == g_knobs[i].k.start) continue;
        Row w;
        w.what = g_knobs[i].what;
        w.from = g_knobs[i].names[g_knobs[i].k.start];
        w.to = g_knobs[i].names[res[i]];
        w.kept = true;
        v.changes.push_back(w);
    }
    return v;
}

}  // namespace autotunerun
