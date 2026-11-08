// vr/config/settings.cpp - the settings' one copy, the ini's path and its helpers (cut from vr/vrcam.cpp, docs/plan_modules.md M2)
#include "settings.h"
#include "../vrcam_internal.h"
#include "../auto_tune_run.h"   // [autotune]: the values a run tries, handed to the reads instead of the file's

#include <cmath>
#include <cwchar>

namespace vrc {

Settings g_set;
std::atomic<bool> g_benchBare{false};

namespace {
// [render] bench_bare: what reads as off while it is on - (section, key); "*" every key of the section (read as its default: [debug])
struct Forced { const wchar_t* sec; const wchar_t* key; float value; };
constexpr Forced kBench[] = {
    {L"fog", L"enabled", 0}, {L"fog", L"game_fog", 0}, {L"fog", L"game_fog_height", 0}, {L"fog", L"game_fog_mist", 0},
    {L"fog", L"game_fog_our_curve", 0}, {L"fog", L"game_fog_before_fx", 0}, {L"fog", L"game_fog_fx_depth", 0},
    {L"sky", L"enabled", 0}, {L"sky", L"game_sky", 0}, {L"sky", L"dome_true_colour", 0}, {L"sky", L"dome_in_game_colour", 0},
    {L"sky", L"dome_gain_once", 0}, {L"sky", L"dome_fog_horizon", 0}, {L"sky", L"game_dome_caves", 0}, {L"sky", L"arcane_sky", 0},
    {L"sky", L"dome_tint", 0}, {L"fog", L"fog_colour_measured", 0},
    {L"ceiling", L"enabled", 0}, {L"ceiling", L"game_ceiling", 0},
    {L"render", L"distance_blur", 0}, {L"render", L"effect_world", 0}, {L"render", L"fog_in_frame", 0},
    {L"hud", L"hand_vitals", 0}, {L"hud", L"ordinary_mob_bars", 0}, {L"hud", L"monster_bar_room", 0},
    {L"weight", L"knock_props", 0}, {L"weight", L"grab_props", 0}, {L"weight", L"grab_items", 0}, {L"weight", L"grab_items_empty_grip", 0}, {L"weight", L"world_contact", 0},
    {L"weight", L"world_contact_hands", 0}, {L"weight", L"monster_ragdoll", 0}, {L"weight", L"monster_bodies", 0}, {L"weight", L"npc_bodies", 0},
    {L"weight", L"knock_down", 0},
    {L"body", L"legs_rear", 0}, {L"body", L"legs_side_ragdoll", 0},
    {L"debug", L"*", 0},
};
}  // namespace

// The value bench_bare reads for a key (false: not one of them)
bool BenchForced(const wchar_t* sec, const wchar_t* key, float def, float* v) {
    if (!g_benchBare.load(std::memory_order_relaxed)) return false;
    for (const Forced& f : kBench) {
        if (_wcsicmp(f.sec, sec) != 0) continue;
        if (f.key[0] == L'*') { *v = def; return true; }
        if (_wcsicmp(f.key, key) == 0) { *v = f.value; return true; }
    }
    return false;
}

wchar_t g_iniPath[MAX_PATH];

float IniF(const wchar_t* sec, const wchar_t* key, float def) {
    if (float forced = 0.0f; autotunerun::Forced(sec, key, &forced)) return forced;   // ([autotune]: a run measures or waits for Keep)
    if (float forced = 0.0f; BenchForced(sec, key, def, &forced)) return forced;   // ([render] bench_bare)
    wchar_t buf[64], d[64];
    swprintf_s(d, L"%g", def);
    GetPrivateProfileStringW(sec, key, d, buf, 64, g_iniPath);
    wchar_t* end = nullptr;
    const float v = wcstof(buf, &end);
    return end != buf && std::isfinite(v) ? v : def;
}
bool IniB(const wchar_t* sec, const wchar_t* key, bool def) { return IniF(sec, key, def ? 1.0f : 0.0f) != 0.0f; }

}  // namespace vrc
