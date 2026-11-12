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
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

#include "auto_tune.h"
#include "auto_tune_run.h"
#include "frame_cap.h"     // shared/: [stereo] headset_rate 0..4 as Hz (the rate when no display period was seen)
#include "foveation.h"   // [render] foveated live: hooked at the start and VRS tier 2 (the ladder's foveation rung)
#include "hset_defs.h"     // a missing key's default (the headset window's table: the plugin's own defaults)
#include "quality_presets.h"   // shared/: the Quality presets, the ladder's middle rungs
#include "render_size.h"   // shared/: [render] vr_resolution / vr_resolution_px read together
#include "render_type.h"   // shared/: the render type a preset names (Auto never changes it)
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

std::string Px(int side) { return std::to_string(side) + " px"; }

// Auto's ladder (Serg 2026-11-08, "если разрешением не спасти, то менять пресет" - vr/auto_tune.h Options::ladder): frames fine - nothing; else
// the render size down to 1600 (from wherever above it the player had it, 3000 or the headset's own), then the Quality preset Medium -> Low ->
// Potato, then the size down to 1300, then foveated rendering Medium -> Maximum, then 1200 ("опускаем разрешение до 1600 на 1600", "т.е.
// опускаем до 1300", "а потом фоверацию", "а потом 1200 на 1200"; autotune::SergLadder). A size the player set below a rung is never raised to
// it. Foveation only where it works live: its hooks in from the game's start ([render] foveated on then) and VRS tier 2 (foveation::Usable).
// Then eyes by turns with the fill ("для Auto уменьшаем до 1300, если недостаточно fps, то делаем проход по очереди": the picture is worse than
// one pass, so late) - from one pass only, live (pair_per_tick; the replay's hooks stay), frame generation off - and the size to 1200 last
// (kTurnsBefore1200 swaps the two). Never a rung: our effects one by one, a lower headset rate, two passes / FlatVR, the render type a preset
// names, the wrist medallions ([hud] bar_place / wrist_look / hand_vitals - "оставляем во всех режимах").
// Before the presets go down to Low / Potato, eyes by turns is TRIED at the preset and size as they stand (Serg 2026-11-10, "перед переключением
// на лоу и потато он должен переключить режим на глаза по очереди": Medium 1280x720 one pass 107 fps, by turns 114): kept and done if it
// meets the target; short, the presets go down with whichever measured better (the log and d2r_vr_autotune.txt say which). Keep writes it
// with the rest, Undo drops it (autotune::LadderStep::trial). The order (Serg 2026-11-10, "сперва уменьшаем до 1600 размера потом по очереди
// глаза"): the size to 1600 first, then the trial at 1600, then the presets, the size under 1600 last.
// A rung that meets with room to spare climbs back ("если много кадров то можно поднять обратно немного"; "на глазах по очереди будет 90, но
// можно поднять графику до 2100"): the size on its 64 px grid up to the player's own while the target holds with 7 % of the frame's budget to
// spare, then the preset a level at a time (autotune::Options::climbKnob); each step logged ("climb ... holds / not kept").
constexpr int kSideFloor = 1600, kSideMid = 1300, kSideLast = 1200;
constexpr bool kTurnsBefore1200 = false;   // eyes by turns after the last drop to 1200 (Serg 2026-11-08: "после 1200 пусть будет по очереди")

// A key's value in the file, missing: the plugin's own default (the headset window's table has it)
int FileOrDefault(const wchar_t* sec, const wchar_t* key) {
    const int i = hset::IndexOf(sec, key);
    return FileInt(sec, key, i >= 0 ? (int)std::lround(hset::kDefs[i].def) : 0);
}
int FileRenderType() {
    rendertype::Keys k;
    k.openxrOn = FileInt(L"openxr", L"on", 0);
    k.afr = FileInt(L"stereo", L"afr", 0);
    k.pairPerTick = FileInt(L"stereo", L"pair_per_tick", 0);
    k.nativeReplay = FileInt(L"stereo", L"native_replay", 0);   // (absent: two passes, the default since 0.214)
    k.source = FileInt(L"stereo", L"source", -1);
    return rendertype::FromKeys(k);
}
// The preset the file matches (qualitypreset::kCount: none - Custom)
int FilePreset() {
    return qualitypreset::Match([](const qualitypreset::Key& k) -> float {
        if (qualitypreset::IsRenderType(k)) return (float)FileRenderType();
        return (float)FileOrDefault(k.section, k.key);
    });
}

std::vector<KnobDef> MakeKnobs(uint32_t recW, uint32_t recH, std::vector<autotune::LadderStep>* ladder, std::string* skipped, int* climbSize = nullptr,
                               int* climbPreset = nullptr) {
    std::vector<KnobDef> out;
    ladder->clear();
    // knob 0: the render size, its levels from 1200 up to the player's own (the start) on the slider's 64 px grid - the climb's steps (Serg
    // 2026-11-10: "можно поднять графику до 2100"); the ladder's drops are some of them (sizeRungs: a tenth less each, down to 1600)
    int sizeKnob = -1, sizeFloorLevel = -1, sizeMidLevel = -1, sizeStart = 0;
    std::vector<int> sizeRungs;
    const int mode = FileInt(L"render", L"vr_resolution", 0), pxKey = FileInt(L"render", L"vr_resolution_px", 0);
    const rendersize::Pick pick = rendersize::FromIni(mode, pxKey);
    const int side = pick.kind == rendersize::Kind::Square ? pick.side : pick.kind == rendersize::Kind::Headset ? (int)std::max(recW, recH) : 0;
    if (side > kSideLast) {
        std::vector<int> down;   // the sides below the player's, largest first
        for (int s = side; s > kSideFloor;) {   // above 1600: a tenth less a step, on the slider's grid, the last one 1600 itself
            s = std::max(kSideFloor, (int)(s * 0.9) / rendersize::kSideStep * rendersize::kSideStep);
            down.push_back(s);
        }
        for (const int s : {kSideMid, kSideLast})   // then 1300 and 1200 (below a player's own smaller size only)
            if (s < (down.empty() ? side : down.back())) down.push_back(s);
        std::vector<int> grid(down.begin(), down.end());   // the rungs' sides and every 64 px between 1200 and the player's own
        for (int s = (kSideLast / rendersize::kSideStep + 1) * rendersize::kSideStep; s < side; s += rendersize::kSideStep) grid.push_back(s);
        std::sort(grid.begin(), grid.end());
        grid.erase(std::unique(grid.begin(), grid.end()), grid.end());
        grid.erase(std::remove_if(grid.begin(), grid.end(), [&](int s) { return s >= side || s < (down.empty() ? side : down.back()); }), grid.end());
        KnobDef d;
        d.k.id = "scale";
        d.k.levels = (int)grid.size() + 1;
        d.k.start = (int)grid.size();
        d.k.worth = 2.0f;
        for (const int s : grid) {   // level 0 the smallest
            d.levels.push_back({{L"render", L"vr_resolution", 0}, {L"render", L"vr_resolution_px", s}});
            d.names.push_back(Px(s));
            if (s == kSideFloor) sizeFloorLevel = (int)d.levels.size() - 1;
            if (s == kSideMid) sizeMidLevel = (int)d.levels.size() - 1;
        }
        for (const int s : down)   // the first drop's rungs (largest first): their levels on the grid
            if (s >= kSideFloor)
                for (size_t g = 0; g < grid.size(); ++g)
                    if (grid[g] == s) sizeRungs.push_back((int)g);
        d.levels.push_back({{L"render", L"vr_resolution", mode}, {L"render", L"vr_resolution_px", pxKey}});
        d.names.push_back(pick.kind == rendersize::Kind::Headset ? uitext::Tr("vr.autotune.headset_size", "headset's size") : Px(side));
        d.what = uitext::Tr("vr.autotune.k.scale", "Render size");
        d.settleMs = 9000;   // (the game freezes about 7 s while the picture is rebuilt)
        sizeKnob = (int)out.size();
        sizeStart = d.k.start;
        out.push_back(d);
    } else {
        *skipped += side <= 0 ? " render size (the window's own size)" : " render size (1200 or less already)";
    }
    // knob 1: the Quality preset, its levels Potato .. the player's own (a preset he is on, or his own settings: Custom)
    int presetKnob = -1, presetStart = 0;
    const int now = FilePreset();
    if (now > 0) {
        KnobDef d;
        d.k.id = "preset";
        d.k.levels = now + 1;   // (Custom = kCount: Potato, Low, Medium and his own)
        d.k.start = now;
        d.k.worth = 3.0f;
        static const char* const kKeys[] = {"vr.preset.potato", "vr.preset.low", "vr.preset.medium"};
        static const char* const kEn[] = {"Potato", "Low", "Medium"};
        for (int p = 0; p < now; ++p) {
            std::vector<Set> set;
            for (const qualitypreset::Key& k : qualitypreset::kKeys) {
                if (qualitypreset::IsRenderType(k) || qualitypreset::Same(k.key, L"headset_rate") || qualitypreset::IsAsIs(k.v[p])) continue;   // (the render type, the rate, an as-is key: kept)
                set.push_back({k.section, k.key, (int)std::lround(k.v[p])});
            }
            d.levels.push_back(set);
            d.names.push_back(uitext::Tr(kKeys[p], kEn[p]));
        }
        d.levels.push_back({});   // his own: the file's
        d.names.push_back(now < qualitypreset::kCount ? uitext::Tr(kKeys[now], kEn[now]) : uitext::Tr("vr.autotune.own_settings", "your own settings"));
        d.what = uitext::Tr("vr.autotune.k.preset", "Quality preset");
        d.settleMs = 4000;   // (the game's stages and the Potato light's pipelines change: a few frames)
        presetKnob = (int)out.size();
        presetStart = d.k.start;
        out.push_back(d);
    } else {
        *skipped += " preset (Potato already)";
    }
    // knob 2: foveated rendering - Maximum, Medium, the player's own (only the levels coarser than his: off, or a lighter strength)
    int fovKnob = -1, fovStart = 0;
    if (foveation::Usable()) {
        const bool on = FileInt(L"render", L"foveated", 0) != 0;
        const int strength = std::clamp(FileInt(L"render", L"foveated_strength", 1), 0, 3);
        std::vector<int> cheaper;   // the strengths to try, the coarsest first: Maximum (3), Medium (1)
        for (const int st : {3, 1})
            if (!on || st > strength) cheaper.push_back(st);
        if (!cheaper.empty()) {
            KnobDef d;
            d.k.id = "foveated";
            d.k.levels = (int)cheaper.size() + 1;
            d.k.start = (int)cheaper.size();
            d.k.worth = 1.5f;
            for (const int st : cheaper) {
                d.levels.push_back({{L"render", L"foveated", 1}, {L"render", L"foveated_strength", st}});
                d.names.push_back(st == 3 ? uitext::Tr("vr.render.strength_max", "Maximum") : uitext::Tr("vr.render.strength_medium", "Medium"));
            }
            d.levels.push_back({});
            d.names.push_back(on ? uitext::Tr("vr.autotune.fov_own", "as you set it") : uitext::Tr("vr.autotune.off", "off"));
            d.what = uitext::Tr("vr.autotune.k.foveated", "Foveated rendering");
            d.settleMs = 2500;
            fovKnob = (int)out.size();
            fovStart = d.k.start;
            out.push_back(d);
        } else {
            *skipped += " foveated rendering (at Maximum already)";
        }
    } else {
        *skipped += foveation::VrsTier2() == 0 ? " foveated rendering (no variable rate shading tier 2 on this graphics card: it cannot work here)"
                    : !foveation::Hooked()      ? " foveated rendering (off at the game's start: it works from the next start - tick it and run Auto again)"
                                                : " foveated rendering (not ready yet)";
    }
    // knob 3: eyes by turns with the fill - from both eyes a pair (one pass or two) on the native path, frame generation off
    int turnsKnob = -1, turnsStart = 0;
    const bool native = FileInt(L"openxr", L"on", 0) != 0, pairs = FileInt(L"stereo", L"afr", 0) != 0 && FileInt(L"stereo", L"pair_per_tick", 0) != 0;
    if (native && pairs && FileInt(L"openxr", L"frame_generation", 0) == 0) {
        KnobDef d;
        d.k.id = "stereo";
        d.k.levels = 2;
        d.k.start = 1;
        d.k.worth = 2.5f;
        d.levels = {{{L"stereo", L"pair_per_tick", 0}, {L"stereo", L"replay_right", 0}, {L"stereo", L"turn_warp", 1}, {L"stereo", L"turns_fill", 1}}, {}};
        d.names = {uitext::Tr("vr.autotune.eyes_by_turns", "eyes by turns"), uitext::Tr("vr.autotune.one_pass", "both eyes")};
        d.what = uitext::Tr("vr.autotune.k.stereo", "3D in the headset");
        d.settleMs = 6000;   // (the picture settles after a change of the pair)
        turnsKnob = (int)out.size();
        turnsStart = d.k.start;
        out.push_back(d);
    } else {
        *skipped += !native ? " eyes by turns (not native OpenXR)" : !pairs ? " eyes by turns (already)" : " eyes by turns (frame generation is on)";
    }
    // the rungs (autotune::SergLadder): the size to 1600, the presets down, the size to 1300, foveation, eyes by turns, the size to 1200
    *ladder = autotune::SergLadder(sizeKnob, sizeStart, sizeFloorLevel, sizeMidLevel, presetKnob, presetStart, fovKnob, fovStart, turnsKnob, turnsStart,
                                   kTurnsBefore1200, sizeKnob >= 0 ? &sizeRungs : nullptr);
    if (climbSize) *climbSize = sizeKnob;       // the climb: the size back up on its grid, then the preset
    if (climbPreset) *climbPreset = presetKnob;
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
bool g_belowMin = false;   // the last run: short at its last rung (under g_mu)
int g_limit = 0;           // ... and what holds it back: 1 the graphics card, 2 the processor

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

// The headset's own rate (autotune::HeadsetRate): the shortest display period seen this session, every frame (XR thread) - SteamVR stretches
// the period while the game is slow, the target must not follow it. [stereo] headset_rate as Hz, read at each run's start (0: none in the ini).
autotune::PeriodFloor g_floor;
std::atomic<float> g_iniHz{0.0f};
bool g_rateTold = false;

void Begin() {
    {
        const int rate = FileInt(L"stereo", L"headset_rate", -1);
        g_iniHz.store(rate >= 0 ? (float)framecap::HzOfRate(rate) : 0.0f);
        g_rateTold = false;
    }
    std::string skipped;
    autotune::Options o;
    g_knobs = MakeKnobs(g_recW.load(), g_recH.load(), &o.ladder, &skipped, &o.climbKnob, &o.climbPresetKnob);
    o.halfRate = false;   // (the ladder: short at its last rung is "below the minimum", never half the rate)
    std::vector<autotune::Knob> ks;
    for (const auto& d : g_knobs) ks.push_back(d.k);
    g_tuner = std::make_unique<autotune::Tuner>(std::move(ks), o);
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
    g_floor.Note(periodNs);   // (every frame, run or not: the session's first frames are at the headset's own rate)
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
    const autotune::Rate rate = autotune::HeadsetRate(g_floor.Shortest(), g_iniHz.load());
    m.valid = !g_spoilt && g_frames >= 15 && g_maxGap < 250e3 && rate.hz > 0;   // a load, a menu, a freeze in the window: measured again
    m.fps = (float)(g_frames / s);
    m.busyMs = (float)(spanUs / g_frames / 1000.0 - g_waitSum / g_frames / 1000.0);
    m.hz = rate.hz;   // the headset's own - not 1e9 / g_periodNs, which SteamVR stretches while the game is slow
    if (!g_rateTold && rate.hz > 0) {   // once a run: where the target comes from, and the period the runtime gives now
        g_rateTold = true;
        LogF("vrcam: auto tune - headset %.1f Hz from %s (shortest display period %.2f ms, now %.2f ms%s)", rate.hz, autotune::RateFromName(rate.from),
             g_floor.Shortest() / 1e6, g_periodNs / 1e6, g_periodNs > g_floor.Shortest() * 1.2 ? " - stretched by the runtime: the game is slower than the headset" : "");
    }
    const autotune::Phase ph = g_tuner->phase();
    const std::vector<int> was = g_tuner->Want();
    const size_t rowsWas = g_tuner->Log().size();
    g_tuner->Feed(m);
    LogF("vrcam: auto tune - %s %s: %.1f frames/s, %.2f ms a frame of our own (headset %.0f Hz)%s", autotune::PhaseName(ph), LevelsText(was).c_str(),
         m.fps, m.busyMs, m.hz, m.valid ? "" : g_spoilt ? " - not counted (out of the world / F4), again" : " - not counted (a freeze or too few frames), again");
    for (size_t r = rowsWas; r < g_tuner->Log().size(); ++r) {   // a tried rung (eyes by turns before Low / Potato): what stayed, and why
        const autotune::LogRow& row = g_tuner->Log()[r];
        if (row.phase == autotune::Phase::Up && row.knob >= 0 && row.knob < (int)g_knobs.size()) {   // the climb (room to spare: back up a little)
            const KnobDef& d = g_knobs[row.knob];
            LogF("vrcam: auto tune - climb %s %s -> %s: %.1f frames/s, %.2f ms of the %.2f ms budget (at most %.2f with the margin) - %s", d.k.id,
                 d.names[row.from].c_str(), d.names[row.to].c_str(), row.fpsAfter, row.busyAfter, 1000.0f / std::max(1.0f, g_tuner->Target()),
                 1000.0f / std::max(1.0f, g_tuner->Target()) * 0.93f, row.kept ? "holds" : "too much, not kept");
            continue;
        }
        if (!row.trial || row.knob < 0 || row.knob >= (int)g_knobs.size()) continue;
        const KnobDef& d = g_knobs[row.knob];
        LogF("vrcam: auto tune - tried %s before the presets go down: %.1f frames/s (%.2f ms) against %s's %.1f (%.2f ms) - %s", d.names[row.to].c_str(),
             row.fpsAfter, row.busyAfter, d.names[row.from].c_str(), row.fpsBefore, row.busyBefore,
             row.kept ? (row.fpsAfter >= g_tuner->Target() * 0.97f ? "kept, the target is met" : "kept: the presets go down with it")
                      : "back to it: the presets go down without it");
    }
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
    g_belowMin = t.BelowMinimum();
    g_limit = 0;
    if (g_belowMin) {   // which part holds it back: the render size's rungs won a share of the frame - the graphics card; next to nothing - the processor
        float resFrom = 0, resTo = 0, allFrom = 0, allTo = 0;
        for (const auto& r : t.Log()) {
            if (r.phase != autotune::Phase::Down) continue;
            if (allFrom <= 0) allFrom = r.busyBefore;
            allTo = r.busyAfter;
            if (r.knob >= 0 && r.knob < (int)g_knobs.size() && !std::strcmp(g_knobs[r.knob].k.id, "scale")) {
                if (resFrom <= 0) resFrom = r.busyBefore;
                resTo = r.busyAfter;
            }
        }
        const float share = resFrom > 0 ? (resFrom - resTo) / resFrom : allFrom > 0 ? (allFrom - allTo) / allFrom : 0.0f;
        g_limit = share >= 0.12f ? 1 : 2;
        LogF("vrcam: auto tune - BELOW THE MINIMUM: down to Potato, 1200 px, foveation (where it works) and eyes by turns it is still short of %.0f frames/s (%.1f now) - the %s is the limit "
             "(the render size's rungs won %.0f%% of a frame)", t.Target(), t.Last().fps, g_limit == 1 ? "graphics card (GPU)" : "processor (CPU)", share * 100.0f);
    }
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
                     r.fpsAfter, r.busyBefore, r.busyAfter, r.trial ? (r.kept ? "  tried before Low / Potato: kept" : "  tried before Low / Potato: undone") : r.kept ? "  kept" : "");
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
    v.belowMin = v.state == State::Waiting && g_belowMin;
    v.limit = g_limit;
    v.targetFps = t.Target();
    v.hz = t.Hz();
    v.nowFps = t.Last().fps;
    for (const auto& r : t.Log()) {
        if (r.phase == autotune::Phase::Baseline) v.startFps = r.fpsAfter;
        Row w;
        w.phase = (int)r.phase;
        w.kept = r.kept;
        w.trial = r.trial;
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
