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
//   Medium  the game's whole frame (game_stages 0); models 200, big objects 600 ("он отличается только повышенным draw distance"; 210 until
//           0.209 - 200 since the Draw distance slider came back, Serg 2026-11-09 "комнаты 200 дистанция": Medium is the slider's stop 2, its
//           default, Low and Potato its stop 1 - shared/draw_distance.h kReach)
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

// "@stereo3d" "source": the render type (shared/render_type.h: 0 = Native two passes, 1 = Native one pass), not an ini key - Settings' own item, the headset's
// Render type row's keys
struct Key { const wchar_t* section; const wchar_t* key; float v[kCount]; };
inline constexpr Key kKeys[] = {
    {L"render", L"native_effects", {1, 1, 1}},
    // the game's own render stages (vr/render_bench.h StagesMask: 2 Potato, 1 Low, 0 all) and the Potato light; the outline out in every one
    {L"render", L"game_stages", {2, 1, 0}},
    {L"render", L"game_outline", {0, 0, 0}},
    {L"render", L"potato_light", {1, 0, 0}},
    // the Potato light in the game's own ambient and sun / moon colours (Serg 2026-11-09 "свет игры выглядит отлично на потато надо его оставлять");
    // only read with potato_light on, so Low / Medium leave it as it is
    {L"render", L"potato_light_game", {1, kAsIs, kAsIs}},   // on in Potato (Serg 2026-11-10 "на потато надо включить по умолчанию")
    // [render] lens_mask: Potato turns it on, Low and Medium leave it as the player has it (Serg 2026-11-09 "маска должна быть включена только на
    // режиме потато" / "оставляем её"): his A/B on PS VR2, F4, standing still: on 44-46 pairs/s, off 41-44
    {L"render", L"lens_mask", {1, kAsIs, kAsIs}},
    {L"render", L"shadows_once_a_pair", {1, 1, kAsIs}},
    // the draw distance - what really differs (room rings read at the game's start, as the models' radius)
    {L"render", L"rings", {2, 2, 2}},
    {L"render", L"model_radius", {150, 150, 200}},
    {L"render", L"big_radius", {200, 200, 600}},
    // the sky drawn by the game: its dome ([sky] enabled 1 with game_sky 1 - "Sky drawn by: the game's dome")
    {L"sky", L"enabled", {1, 1, 1}},
    {L"sky", L"game_sky", {1, 1, 1}},
    // the fog drawn by the game with our numbers ([fog] game_fog alone: enabled 0 is our effect's fog off - vr/sky/sky_choice.cpp GameFogWanted);
    // its distances its own keys (game_fog_start ... game_fog_caves: Serg 2026-11-09, "у каждого свои ползунки"), the old fog's left as set
    {L"fog", L"enabled", {0, 0, 0}},
    {L"fog", L"light", {0, 0, 0}},
    {L"fog", L"game_fog", {1, 1, 1}},
    {L"fog", L"game_fog_start", {55, 55, 55}},
    {L"fog", L"game_fog_end", {140, 140, 140}},
    {L"fog", L"game_fog_caves", {1, 1, 1}},
    {L"fog", L"game_fog_caves_start", {65, 65, 65}},
    {L"fog", L"game_fog_caves_end", {200, 200, 200}},
    {L"fog", L"game_fog_mist", {0, 0, 0}},
    {L"fog", L"game_fog_height", {0, 0, 0}},
    {L"fog", L"game_fog_before_fx", {kAsIs, kAsIs, kAsIs}},   // (0.210.1 ships it off - far trees speckled; no preset turns it back on, the player's tick stays)
    {L"fog", L"game_fog_fx_depth", {0, 0, 0}},
    {L"fog", L"game_fog_our_curve", {0, 0, 0}},
    {L"fog", L"game_fog_caves_black", {0, 0, 0}},
    {L"ceiling", L"enabled", {1, 1, 1}},
    {L"ceiling", L"steps", {8, 8, 12}},
    // the render type: Native two passes in all three since 0.214 (Serg 2026-11-12 "по умолчанию ставь 2 прохода", "чтобы у людей не зависало":
    // one pass hung the graphics card inside the replayed right eye - the top crash; the game render patches made two passes nearly as fast);
    // Native one pass stays a choice in "3D in the headset" for a processor too slow for two passes
    {L"@stereo3d", L"source", {0, 0, 0}},
    {L"stereo", L"headset_rate", {3, 3, 3}},   // 90 Hz (the game's cap follows: Settings' ApplyFrameCap)
    // Serg 2026-11-09: "на графике нормал по умолчанию должно быть включено monitor_eye=1 а так же при записи движений чтобы слой рук был
    // поверх меню на всех режимах" - the monitor shows the left eye alone (one small copy a frame), and the hands over the Gestures tab while
    // it is up (nothing of it runs elsewhere), in every preset
    {L"render", L"monitor_eye", {1, 1, 1}},
    {L"openxr", L"hands_over_panels", {1, 1, 1}},
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

// [render] preset_game_options (Serg 2026-11-09: "режимы графики так же должны в игре менять с лоу на медиум и хич графику"): a preset also puts
// the GAME's own graphics options (its Options > Video, kept in its Settings.json) - Potato the game's low, Low its medium, Medium its high.
// name: the game's own name of the option, as vr/patch_hunt.h kGfx has it (its accessor and the setter found there - never a second copy of
// the places); v: its value per preset, the game's own scale (AO 0 off .. 4, shadows 1 low .. 4, lights 0 low .. 2). Only the three that cost
// the graphics card and draw the same picture in both eyes; anti aliasing, DLSS and the resolution scale stay the player's (they feed our
// own stereo path: TAA / DLSS history and motion vectors, the render size). Put once a preset is picked (vr/patch_hunt.cpp PresetGame), never
// matched: a change the player makes in the game's menu afterwards stays and the preset still shows as it was.
struct GameOption { const char* name; int v[kCount]; };
inline constexpr GameOption kGameOptions[] = {
    {"Ambient Occlusion Quality", {0, 2, 3}},   // off / medium / high
    {"Shadow Quality", {1, 2, 3}},              // low / medium / high
    {"Light Quality", {0, 1, 2}},               // low / medium / high
};
inline constexpr int kGameOptionCount = (int)(sizeof kGameOptions / sizeof kGameOptions[0]);
// Is a preset to be put into the game now: one of the three (not Custom) and not the one put last (served: -1 none yet). The plugin keeps
// `served` in d2r_vr_game_options.txt beside the ini, so a game start puts nothing unless the preset changed meanwhile.
constexpr bool GameOptionsDue(int preset, int served) { return preset >= 0 && preset < kCount && preset != served; }

// A press of < (-1) or > (+1) on the preset row: Potato .. Medium, stopping at the ends; from Custom > is Potato, < Medium.
inline int Step(int now, int delta) {
    if (now < 0 || now >= kCount) return delta > 0 ? 0 : kCount - 1;
    const int to = now + (delta > 0 ? 1 : -1);
    return to < 0 ? 0 : to >= kCount ? kCount - 1 : to;
}

}  // namespace qualitypreset
