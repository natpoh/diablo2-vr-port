#pragma once
// quality_presets.h - the Quality presets, one table for D2R VR Settings (Performance and Home, tools/settings/settings.cpp) and the headset
// settings window's Video page (vr/hset_model.h). Pure: no Windows, no ini; each side reads and writes the keys its own way.
//
// Serg 2026-11-08 - three presets, Light and High gone ("режим Light надо удалить", "пресет High можно удалить", "оставить Medium максимальный"),
// and between them "по сути там меняется только драв дистанс монстров": the fog and the sky drawn by the game ("рисуется игрой, небо рисуется
// игрой"), the game's fog with our numbers and none of our tweaks on it ("никаких подгонок"), the same fog everywhere ("размер тумана одинаковый
// на всех": 55 / 140, in caves 65 / 200), 2 room rings everywhere ("плиток столько же на Potato, Low, Medium"), Native one pass (Potato measured
// 90 against 70 eyes by turns), the lens mask and the shadows once a pair. What differs:
//   Potato  the Potato stages (no sun's shadows, no torch lights) and the Potato light; models 150 (the game's own), big objects 200
//   Low     the Low stages (the lit world, the sun's shadows, the tone map); models 150, big objects 200
//   Medium  the game's whole frame (game_stages 0); models 210, big objects 600 ("он отличается только повышенным draw distance")
// ... and the cave ceiling's relief (8 / 8 / 12 steps), kept as it was for Serg to decide. 0.208 (Serg: new players start on Medium, of today's performance features only one pass
// ships on): Medium leaves the lens mask and the shadows once a pair as the player has them (kAsIs: neither written nor matched), so a fresh
// ini - both 0 - shows as Medium. Never in a preset: the wrist medallions ([hud]
// bar_place / wrist_look / hand_vitals: "не влияют на производительность, оставляем во всех режимах"), the render size.
// Tested by tools/hset_test.cpp (the headset side) and used as is by Settings.

namespace qualitypreset {

constexpr int kCount = 3;   // Potato, Low, Medium; kCount itself = Custom (the keys match none)
inline const char* Name(int p) {
    static const char* const n[kCount + 1] = {"Potato", "Low", "Medium", "Custom"};
    return p >= 0 && p <= kCount ? n[p] : "Custom";
}

constexpr float kAsIs = -987654.0f;   // a preset that leaves this key as the player has it: neither written nor matched
constexpr bool IsAsIs(float v) { return v == kAsIs; }

// "@stereo3d" "source": the render type (shared/render_type.h: 1 = Native one pass), not an ini key - Settings' own item, the headset's
// Render type row's keys
struct Key { const wchar_t* section; const wchar_t* key; float v[kCount]; };
inline constexpr Key kKeys[] = {
    {L"render", L"native_effects", {1, 1, 1}},
    // the game's own render stages (vr/render_bench.h StagesMask: 2 Potato, 1 Low, 0 all) and the Potato light; the outline out in every one
    {L"render", L"game_stages", {2, 1, 0}},
    {L"render", L"game_outline", {0, 0, 0}},
    {L"render", L"potato_light", {1, 0, 0}},
    {L"render", L"lens_mask", {1, 1, kAsIs}},
    {L"render", L"shadows_once_a_pair", {1, 1, kAsIs}},
    // the draw distance - what really differs (room rings read at the game's start, as the models' radius)
    {L"render", L"rings", {2, 2, 2}},
    {L"render", L"model_radius", {150, 150, 210}},
    {L"render", L"big_radius", {200, 200, 600}},
    // the sky drawn by the game: its dome ([sky] enabled 1 with game_sky 1 - "Sky drawn by: the game's dome")
    {L"sky", L"enabled", {1, 1, 1}},
    {L"sky", L"game_sky", {1, 1, 1}},
    // the fog drawn by the game with our numbers ([fog] game_fog alone: enabled 0 is our effect's fog off - vr/sky/sky_choice.cpp GameFogWanted)
    {L"fog", L"enabled", {0, 0, 0}},
    {L"fog", L"light", {0, 0, 0}},
    {L"fog", L"game_fog", {1, 1, 1}},
    {L"fog", L"start", {55, 55, 55}},
    {L"fog", L"end", {140, 140, 140}},
    {L"fog", L"caves", {1, 1, 1}},
    {L"fog", L"caves_start", {65, 65, 65}},
    {L"fog", L"caves_end", {200, 200, 200}},
    {L"fog", L"game_fog_mist", {0, 0, 0}},
    {L"fog", L"game_fog_height", {0, 0, 0}},
    {L"fog", L"game_fog_before_fx", {0, 0, 0}},
    {L"fog", L"game_fog_fx_depth", {0, 0, 0}},
    {L"fog", L"game_fog_our_curve", {0, 0, 0}},
    {L"fog", L"game_fog_caves_black", {0, 0, 0}},
    {L"ceiling", L"enabled", {1, 1, 1}},
    {L"ceiling", L"steps", {8, 8, 12}},
    {L"@stereo3d", L"source", {1, 1, 1}},
    {L"stereo", L"headset_rate", {3, 3, 3}},   // 90 Hz (the game's cap follows: Settings' ApplyFrameCap)
};
inline constexpr int kKeyCount = (int)(sizeof kKeys / sizeof kKeys[0]);

constexpr bool Same(const wchar_t* a, const wchar_t* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
constexpr bool IsRenderType(const Key& k) { return Same(k.section, L"@stereo3d"); }

// The preset the keys match, else kCount (Custom). read(key) gives a key's value as the reader has it now (the render type's row: its type).
template <class Read>
int Match(Read read) {
    for (int p = 0; p < kCount; ++p) {
        bool all = true;
        for (const Key& k : kKeys) {
            if (IsAsIs(k.v[p])) continue;   // (this preset leaves it as it is)
            const float v = read(k);
            if (v - k.v[p] > 0.001f || k.v[p] - v > 0.001f) { all = false; break; }
        }
        if (all) return p;
    }
    return kCount;
}

// A press of < (-1) or > (+1) on the preset row: Potato .. Medium, stopping at the ends; from Custom > is Potato, < Medium.
inline int Step(int now, int delta) {
    if (now < 0 || now >= kCount) return delta > 0 ? 0 : kCount - 1;
    const int to = now + (delta > 0 ? 1 : -1);
    return to < 0 ? 0 : to >= kCount ? kCount - 1 : to;
}

}  // namespace qualitypreset
