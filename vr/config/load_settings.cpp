// vr/config/load_settings.cpp - d2r_vr.ini read into g_set and handed to every feature (LoadSettings), again whenever the file changes
// (ReloadIfChanged) - cut from vr/vrcam.cpp (docs/plan_modules.md M2). It reaches into most features: it sees vrcam.cpp's headers
// (vr/vrcam_headers.h) and the names of vrcam.cpp it sets (vr/vrcam_internal.h). Its place in the update thread is the same.
#include "../vrcam_headers.h"
#include "../../shared/draw_distance.h"
#include "../../shared/render_type.h"   // the render type the keys make: one log line at each change
#include "../bone_cap.h"
#include "../flat_weapon.h"   // [body] flat_weapon_adjust: the defaults and the range of [weapon_<kind>] flat_*
#include "../aim_tilt.h"      // [body] flat_aim_tilt: the defaults and the range of its share and its cap
#include "../view_keys.h"
#include "../quick_blow.h"   // [gameplay] quick_blow_min_ms: its default
#include "../render_bench.h"   // [render] bench_skip_*: the keys, one per game stage
#include "../trigger_roles_wire.h"   // [gestures] trigger_left / trigger_right
#include "../bench_lights.h"   // [render] bench_skip_point_lights

using namespace D2RL;

namespace vrc {

FILETIME g_iniTime{};

void LoadSettings() {
    {   // [render] bench_bare: read first - every key below it forces reads as off (settings.cpp BenchForced)
        const bool bench = IniB(L"render", L"bench_bare", false);
        if (g_benchBare.exchange(bench) != bench)
            LogF("vrcam: bench_bare %s - %s", bench ? "ON" : "OFF",
                 bench ? "stopped: our whole effect (D2R_DepthFog.fx, its overlays and interface passes - medallions, labels, toolbar, "
                         "phantoms), our sky, fog, ceiling and distance blur; the game's fog as the game ships it (game_fog, its height and "
                         "mist patch, the step before the effects and the effects' depth); the game's dome, its bake and both "
                         "colour measures; the hand vitals' orbs, the monsters' bars; knock_props, grab_props / items, world_contact, "
                         "monster_bodies / ragdolls / knock-down, npc_bodies, legs_rear and the side gait; every [debug] key. NOT live (as they "
                         "were at the start, until the game restarts): room rings, the models' radius, big objects; the native hooks stay "
                         "installed and idle. Kept: the picture to the headset, head and hands, input"
                       : "everything back as set in the ini");
    }
    // [debug] patch_hunt (vr/patch_hunt.h): one suspect of the floor patches off a step - read first, the reads below leave our own parts out by it
    const int hunt = patchhunt::Clamp((int)IniF(L"debug", L"patch_hunt", 0.0f));
    patchhunt::Suspects suspects;
    g_set.platform.store(IniF(L"mode", L"platform", 1.0f) != 0.0f ? 1 : 0);
#if D2RVR_FIRST_PERSON
    g_set.flatView.store(std::clamp((int)IniF(L"mode", L"flat_view", 3.0f), 1, 3));
#else
    g_set.flatView.store(std::clamp((int)IniF(L"mode", L"flat_view", 1.0f), 1, 2));
#endif
    // [mode] vr_mode: 1 above, 2 third person, 3 the game on the floor, 4 first person
    // with the body. It replaced vr_view (2026-10-05), which also had the mouse-and-
    // keyboard first person as 3 - an ini with only that is carried over. 3 and 4 were
    // the other way round until vr_mode_order=2 (2026-10-05: the body, not ready for
    // everyone, went last); an older ini's is swapped once. Nothing set: from above.
    {
        int v = (int)IniF(L"mode", L"vr_mode", 0.0f);
        bool write = false;
        if (v < 1 || v > 4) {
            static const int kFromOld[6] = {1, 1, 2, 4, 4, 3};
            v = kFromOld[std::clamp((int)IniF(L"mode", L"vr_view", 0.0f), 0, 5)];
            write = true;
        } else if (IniF(L"mode", L"vr_mode_order", 1.0f) < 2.0f) {
            v = v == 3 ? 4 : v == 4 ? 3 : v;
            write = true;
        }
        if (write) {
            wchar_t b[8]; swprintf_s(b, L"%d", v);
            WritePrivateProfileStringW(L"mode", L"vr_mode", b, g_iniPath);
            WritePrivateProfileStringW(L"mode", L"vr_mode_order", L"2", g_iniPath);
        }
#if !D2RVR_FIRST_PERSON
        if (v > 3) v = 1;
#endif
        g_set.vrView.store(v);
    }
    // [view] keys (Settings > Home > View keys; vr/view_keys.h): which F keys pick the view
    if (const int keys = viewkeys::ModeFromIni(IniF(L"view", L"keys", 0.0f)); keys != g_set.viewKeys.exchange(keys))
        LogF("vrcam: view keys %d - %s ([view] keys; F12 next view, the controller and Settings in every mode)", keys, viewkeys::ModeName(keys));
    // [view] own_hotkeys (Settings > Home, under View keys; vr/view_keys.h OwnGate): F9 - F12 kept from the game
    if (const int own = IniB(L"view", L"own_hotkeys", false) ? 1 : 0; own != g_set.ownHotkeys.exchange(own))
        LogF("vrcam: own hotkeys %d - %s ([view] own_hotkeys)", own,
             own ? "F9 - F12 (mouse look, skeleton dump, recentre, next view) are the plugin's only: the game does not get them - its "
                   "default keys there (Center / Fade Automap, Show Party, Show Party Names) are not flipped with each press"
                 : "the game gets F9 - F12 too, as before - with its default keys each F11 / F12 also flips its automap's Show Party / Party Names");
    g_set.fov.store(std::clamp(IniF(L"camera", L"fov", 60.0f), 20.0f, 140.0f));
    g_set.fovFromFlatVR.store(IniB(L"camera", L"fov_from_flatvr", true) && g_set.platform.load() == 1);   // no FlatVR on a monitor
    g_set.distance.store(std::clamp(IniF(L"camera", L"distance", -1.0f), -20.0f, 1500.0f));
    g_set.height.store(IniF(L"camera", L"height", 8.0f));
    g_set.heightAuto.store(IniB(L"camera", L"height_auto", true));
    g_set.eyeOffset.store(std::clamp(IniF(L"camera", L"eye_offset", 0.0f), -5.0f, 5.0f));
    g_set.followJump.store(IniB(L"camera", L"follow_jump", true));
    g_set.jumpFrom.store(std::clamp(IniF(L"camera", L"jump_from", 0.3f), 0.0f, 3.0f));
    g_set.pitch.store(std::clamp(IniF(L"camera", L"pitch", -60.0f), -150.0f, 60.0f));
    g_set.flatShiftX.store(flatshift::Clamp(IniF(L"camera", L"flat_shift_x", 0.0f)));
    g_set.flatShiftY.store(flatshift::Clamp(IniF(L"camera", L"flat_shift_y", 0.0f)));
    g_set.stickPitchF2.store(IniB(L"camera", L"stick_pitch_f2", false));
    g_set.stickPitchFlat.store(IniB(L"camera", L"stick_pitch_flat", true));
    g_set.stickPitchSpeed.store(stickpitch::Speed(IniF(L"camera", L"stick_pitch_speed", stickpitch::kSpeedDefault)));
    {
        const stickpitch::Limits l = stickpitch::FromIni(IniF(L"camera", L"stick_pitch_min", stickpitch::kMinDefault), IniF(L"camera", L"stick_pitch_max", stickpitch::kMaxDefault));
        g_set.stickPitchMin.store(l.min); g_set.stickPitchMax.store(l.max);
    }
    g_set.stickPitchSign.store(IniF(L"camera", L"stick_pitch_sign", 1.0f) < 0 ? -1.0f : 1.0f);
    g_set.rings.store(std::clamp((int)IniF(L"render", L"rings", 4.0f), 1, 8));
    g_set.modelRadius.store(std::clamp(IniF(L"render", L"model_radius", 1000.0f), 10.0f, 10000.0f));
    g_set.bigRadius.store(unleashed::farobjects::Normalize(IniF(L"render", L"big_radius", 0.0f)));
    g_set.renderPoolMb.store(std::clamp((int)IniF(L"render", L"render_pool_mb", 64.0f), 0, 4096));
    owneffects::Configure(IniB(L"render", L"dim_own_effects", false), IniF(L"render", L"dim_own_effects_pct", 30.0f), IniF(L"render", L"dim_own_auras_pct", 50.0f));   // the hero's own effects fainter (vr/own_effects.h), F4 only
    owneffects::ConfigureSpeed(IniB(L"render", L"slow_own_effects", false), IniF(L"render", L"own_effects_speed_pct", 20.0f));   // Bone Armor (the solid effects) turning slower, F4 only
    owneffects::ConfigureHeight(IniB(L"render", L"lower_own_effects", false), IniF(L"render", L"own_effects_height_cm", 0.0f));   // Bone Armor (the solid effects) drawn lower, F4 only
    auraview::Configure(IniB(L"render", L"aura_centre", false), IniF(L"render", L"aura_lift_pct", 0.0f));   // the Paladin's aura rings centred under the hero (vr/aura_view.h), every VR view
    owneffects::ConfigureAuraPlace(IniB(L"render", L"move_own_auras", false), IniF(L"render", L"own_auras_up_cm", 0.0f), IniF(L"render", L"own_auras_ahead_cm", 0.0f),
                                   IniF(L"render", L"own_auras_side_cm", 0.0f));   // the hero's Paladin aura rings moved (vr/own_effects.h AuraShift), every VR view
    g_set.entityPoolMb.store(std::clamp((int)IniF(L"render", L"entity_pool_mb", 280.0f), 0, 4096));
    g_set.mouseLook.store(IniB(L"input", L"mouse_look", false));
    g_set.hudHide.store(std::clamp((int)IniF(L"hud", L"hide", 0.0f), 0, 2));
    hud::SetHide(g_set.hudHide.load());
    mobbars::SetEnabled(IniB(L"hud", L"ordinary_mob_bars", false), (int)IniF(L"debug", L"mob_bars_style", 0.0f));   // the hook goes in from the tick
    plateworld::SetLift(IniB(L"hud", L"monster_over_head", false));   // the game's own plate lifted over the monster's head (the anchor call, vr/mob_bars.cpp)
    plateworld::Configure(IniB(L"hud", L"monster_bar_room", false), IniF(L"hud", L"monster_bar_gap_cm", 15.0f), IniF(L"hud", L"monster_size", 100.0f), IniF(L"hud", L"monster_alpha", 1.0f));   // the monster's health bar over its head, in the room (F4, native OpenXR): PlateWorldTick
    mobbars::SetHeight(IniF(L"hud", L"monster_height", 0.0f));   // the same hook; the plate higher or lower in F4 (% of the screen's height)
    noticecard::Configure(IniB(L"hud", L"notice_card", false), IniF(L"hud", L"notice_card_seconds", noticecard::kShowDefaultS));   // the quest / level-up notice on a card in the room (F4, native OpenXR): NoticeCardTick
    {   // [hud] low_hp_vignette (vr/low_hp.h): a red heartbeat vignette at low life, VR F4 on native OpenXR only
        lowhp::Config lh;
        lh.on = IniB(L"hud", L"low_hp_vignette", false);
        lh.threshold = lowhp::ThresholdFromPercent(IniF(L"hud", L"low_hp_threshold", lh.threshold * 100.0f));
        lh.intensityStart = IniF(L"hud", L"low_hp_intensity_start", lh.intensityStart);
        lh.intensityEnd = lowhp::ResolveIntensityEnd(IniF(L"hud", L"low_hp_intensity_end", -1.0f), IniF(L"hud", L"low_hp_strength", -1.0f));   // (the old single strength is the end)
        lh.saturation = IniF(L"hud", L"low_hp_saturation", lh.saturation);
        lh.radius = lowhp::RadiusFromPercent(IniF(L"hud", L"low_hp_radius", lh.radius * 100.0f));
        lh.softness = IniF(L"hud", L"low_hp_softness", lh.softness);
        lh.roundness = IniF(L"hud", L"low_hp_roundness", IniF(L"debug", L"low_hp_roundness", lh.roundness));
        lh.gentle = IniB(L"hud", L"low_hp_gentle", false);
        lh.haptics = IniB(L"hud", L"low_hp_haptics", false);
        lh.hapticsStrength = IniF(L"hud", L"low_hp_haptics_strength", lh.hapticsStrength);
        lh.coverEq.on = IniB(L"hud", L"low_hp_cover_eq", false);   // how far the red closes in at points of the life (vr/low_hp_eq.h); off = low_hp_radius
        for (int k = 0; k < lowhp::eq::kBands; ++k) lh.coverEq.cover[k] = IniF(L"hud", lowhp::kCoverKeys[k], lowhp::Config::kCoverDefault[k]);
        lowhp::Configure(lh);
    }
    {   // [hud] low_mp_vignette (vr/low_mp.h): a blue band at the bottom at low mana, its height by its own equalizer if ticked; VR F4 on native OpenXR only
        lowmp::Config mp;
        mp.on = IniB(L"hud", L"low_mp_vignette", false);
        mp.threshold = IniF(L"hud", L"low_mp_threshold", mp.threshold * 100.0f) * 0.01f;
        mp.intensityStart = IniF(L"hud", L"low_mp_intensity_start", mp.intensityStart);
        mp.intensityEnd = IniF(L"hud", L"low_mp_intensity_end", mp.intensityEnd);
        mp.height = IniF(L"hud", L"low_mp_height", mp.height * 100.0f) * 0.01f;
        mp.softness = IniF(L"hud", L"low_mp_softness", mp.softness);
        mp.coverEq.on = IniB(L"hud", L"low_mp_cover_eq", false);   // how far up the blue reaches at points of the mana; off = low_mp_height
        for (int k = 0; k < lowhp::eq::kBands; ++k) mp.coverEq.cover[k] = IniF(L"hud", lowmp::kCoverKeys[k], lowmp::Config::kCoverDefault[k]);
        lowhp::ConfigureMana(mp);
    }
    {   // [hud] merc_tint (vr/merc_tint.h): the wounded merc tinted red by the effect, VR F4 only; off by default
        merctint::Config mt;
        mt.on = IniB(L"hud", L"merc_tint", false);
        mt.threshold = merctint::ThresholdFromPercent(IniF(L"hud", L"merc_tint_threshold", mt.threshold * 100.0f));
        mt.strength = IniF(L"hud", L"merc_tint_strength", mt.strength);
        merctint::Configure(mt);
    }
    // [hud] npc_dialog_no_bar (hud.cpp SetPanelNoBar): 1 = an NPC's Talk / Trade / Cancel box goes to the headset ([openxr] menu_3d) without
    // the toolbar; 0 = the whole interface with it, as before
    g_set.npcDialogNoBar.store(IniB(L"hud", L"npc_dialog_no_bar", true));
    {   // [hud] hand_vitals (vr/hand_vitals.h): life and mana on the hand when the palm turns to the face, VR F4 on native OpenXR only
        handvitals::Config hv;
        // The toolbar's placement list has a fifth entry, "Wrists" ([hud] bar_place = 5): life on the left wrist, mana on the right, drawn by the plugin
        // (FlatVR is given "not shown", below). Its look is [hud] wrist_look (0 the glass orbs, 1 the game's own globes). The older keys (hand_vitals, hand_vitals_style)
        // still work without it.
        const bool wrists = (int)IniF(L"hud", L"bar_place", 5.0f) == 5;
        hv.on = (wrists || IniB(L"hud", L"hand_vitals", false)) && !g_benchBare.load();   // ([render] bench_bare: none)
        hv.hand = (int)IniF(L"hud", L"hand_vitals_hand", 0.0f);
        hv.sizeCm = IniF(L"hud", L"hand_vitals_size_cm", 8.0f);
        hv.heightCm = IniF(L"hud", L"hand_vitals_height_cm", 6.0f);
        hv.opacity = IniF(L"hud", L"hand_vitals_opacity", 1.0f);
        hv.coneDeg = IniF(L"hud", L"hand_vitals_cone", 35.0f);
        hv.pulse = IniB(L"hud", L"hand_vitals_pulse", false);
        hv.pulseEq = IniB(L"hud", L"hand_vitals_pulse_eq", false);   // the balls beat with the low life / low mana vignettes (their equalizers); off = as before
        hv.lowFrac = IniF(L"hud", L"low_hp_threshold", 30.0f) * 0.01f;   // the low-life vignette's threshold: under it the pulse starts
        hv.flip = IniB(L"debug", L"hand_vitals_flip", false);
        hv.style = wrists ? (IniF(L"hud", L"wrist_look", 0.0f) >= 0.5f ? handvitals::kStyleGlobes : handvitals::kStyleOrbs)
                          : (int)IniF(L"hud", L"hand_vitals_style", 0.0f);   // 0 the glass orbs on the wrists, 1 the palm panel, 2 the game's own globes
        hv.orbHands = (int)IniF(L"hud", L"hand_vitals_orb_hands", 0.0f);
        {   // the way the liquid fills: hand_vitals_orb_fill 0 - 5 (a fixed direction of the wrist's frame, the default out of the hand); the older hand_vitals_orb_liquid 0 / 1 is 0 / 5
            const float oldLiquid = IniF(L"hud", L"hand_vitals_orb_liquid", -1.0f);
            hv.orbFill = (int)IniF(L"hud", L"hand_vitals_orb_fill", oldLiquid < 0.0f ? (float)handvitals::kFillOutOfHand : oldLiquid >= 0.5f ? (float)handvitals::kFillWorldUp : 0.0f);
        }
        hv.orbShape = (int)IniF(L"hud", L"hand_vitals_orb_shape", 0.0f);               // 0 a medallion (a dome on a metal base), 1 the full glass ball
        hv.orbPatina = IniF(L"hud", L"hand_vitals_orb_patina", 55.0f) * 0.01f;         // percent
        hv.orbMetal = (int)IniF(L"hud", L"hand_vitals_orb_metal", 0.0f);               // 0 gold, 1 bronze, 2 silver
        hv.orbDiscFaces = (int)IniF(L"hud", L"hand_vitals_orb_disc", 1.0f);            // the game's globe's disc as a ball: 1 fixed on the wrist, 0 turns to the eyes
        hv.orbSizeCm = IniF(L"hud", L"hand_vitals_orb_size_cm", 5.0f);
        // The ball's place on each wrist, in the wrist's own frame (cm): along the arm, across (+ toward the thumb), up out of the back of the hand. The older single
        // hand_vitals_orb_up_cm / _along_cm are what a wrist starts from while its own key is not in the ini.
        const float oldUp = IniF(L"hud", L"hand_vitals_orb_up_cm", 4.0f), oldAlong = IniF(L"hud", L"hand_vitals_orb_along_cm", 3.0f);
        hv.orbLeft.alongCm = IniF(L"hud", L"hand_vitals_orb_left_along_cm", oldAlong);
        hv.orbLeft.sideCm = IniF(L"hud", L"hand_vitals_orb_left_side_cm", 0.0f);
        hv.orbLeft.upCm = IniF(L"hud", L"hand_vitals_orb_left_up_cm", oldUp);
        hv.orbRight.alongCm = IniF(L"hud", L"hand_vitals_orb_right_along_cm", oldAlong);
        hv.orbRight.sideCm = IniF(L"hud", L"hand_vitals_orb_right_side_cm", 0.0f);
        hv.orbRight.upCm = IniF(L"hud", L"hand_vitals_orb_right_up_cm", oldUp);
        // the medallion's turn about the wrist's three axes, degrees (anatomical: the same numbers on both hands are the mirror image)
        hv.orbLeft.rotXDeg = IniF(L"hud", L"hand_vitals_orb_left_rot_x_deg", 0.0f);
        hv.orbLeft.rotYDeg = IniF(L"hud", L"hand_vitals_orb_left_rot_y_deg", 0.0f);
        hv.orbLeft.rotZDeg = IniF(L"hud", L"hand_vitals_orb_left_rot_z_deg", 0.0f);
        hv.orbRight.rotXDeg = IniF(L"hud", L"hand_vitals_orb_right_rot_x_deg", 0.0f);
        hv.orbRight.rotYDeg = IniF(L"hud", L"hand_vitals_orb_right_rot_y_deg", 0.0f);
        hv.orbRight.rotZDeg = IniF(L"hud", L"hand_vitals_orb_right_rot_z_deg", 0.0f);
        for (int k = 0; k < handvitals::kHeroCount; ++k) {   // the depth offset of each hero class (hand_vitals_orb_z_cm_<class>, cm): added to both wrists' Z while that hero plays
            wchar_t key[64];
            swprintf_s(key, L"hand_vitals_orb_z_cm_%hs", handvitals::HeroKey(k));
            hv.orbClassZCm[k] = IniF(L"hud", key, 0.0f);
        }
        // [shapeshift] the druid's werebear as your body: its own depth offset on top of the druid's (hand_vitals_orb_z_cm_werebear, cm), and the auto lift by its forearm's mesh
        // (the werewolf keeps the druid's place)
        hv.orbBearZCm = IniF(L"hud", L"hand_vitals_orb_z_cm_werebear", 0.0f);
        hv.orbFormAuto = IniB(L"hud", L"hand_vitals_orb_form_auto", false);
        for (int k = 0; k < handvitals::kHeroCount; ++k) {   // the depth and the along offset of each hero class under a shield (hand_vitals_orb_shield_up_cm_<class> / _along_cm_<class>, cm), on top of the shield's own
            wchar_t key[80];
            swprintf_s(key, L"hand_vitals_orb_shield_up_cm_%hs", handvitals::HeroKey(k));
            hv.orbShieldClassUpCm[k] = IniF(L"hud", key, 0.0f);
            swprintf_s(key, L"hand_vitals_orb_shield_along_cm_%hs", handvitals::HeroKey(k));
            hv.orbShieldClassAlongCm[k] = IniF(L"hud", key, 0.0f);
            swprintf_s(key, L"hand_vitals_orb_shield_rot_x_deg_%hs", handvitals::HeroKey(k));   // ... and the medallion's turn about X / Y / Z there, degrees
            hv.orbShieldClassRotXDeg[k] = IniF(L"hud", key, 0.0f);
            swprintf_s(key, L"hand_vitals_orb_shield_rot_y_deg_%hs", handvitals::HeroKey(k));
            hv.orbShieldClassRotYDeg[k] = IniF(L"hud", key, 0.0f);
            swprintf_s(key, L"hand_vitals_orb_shield_rot_z_deg_%hs", handvitals::HeroKey(k));
            hv.orbShieldClassRotZDeg[k] = IniF(L"hud", key, 0.0f);
        }
        hv.orbMirror = IniB(L"hud", L"hand_vitals_orb_mirror", false);   // the right wrist takes the left's three numbers (and the three angles)
        // the wrist that wears a shield (the left arm, or the right one after a hand swap) turns its ball to the other side of the wrist (VR F4 only); the coefficients of that place
        hv.orbUnderShield = IniB(L"hud", L"hand_vitals_orb_under_shield", true);
        hv.orbOnBone = IniB(L"hud", L"hand_vitals_orb_on_bone", false);   // on the forearm bone's own mesh box (handorbs::OnBone), no spot numbers - off by default
        // since 0.192.39: a hero's skeleton (more than 160 bones) never read before 0.192.36's 256-bone cap, so the box never placed a medallion until then -
        // the sliders did; read, it put the medallion 10-15 cm off the wrist (Serg's bisect 0.192.34 -> 0.192.36; 0.192.37's anchor on the bone line did not mend it)
        hv.orbShield.turnDeg = IniF(L"hud", L"hand_vitals_orb_shield_turn_deg", 180.0f);
        hv.orbShield.alongCm = IniF(L"hud", L"hand_vitals_orb_shield_along_cm", 0.0f);
        hv.orbShield.sideCm = IniF(L"hud", L"hand_vitals_orb_shield_side_cm", 0.0f);
        hv.orbShield.upCm = IniF(L"hud", L"hand_vitals_orb_shield_up_cm", 0.0f);
        hv.orbShield.rotXDeg = IniF(L"hud", L"hand_vitals_orb_shield_rot_x_deg", 0.0f);
        hv.orbShield.rotYDeg = IniF(L"hud", L"hand_vitals_orb_shield_rot_y_deg", 0.0f);
        hv.orbShield.rotZDeg = IniF(L"hud", L"hand_vitals_orb_shield_rot_z_deg", 0.0f);
        hv.orbOpacity = IniF(L"hud", L"hand_vitals_orb_opacity", 0.95f);
        handvitals::Configure(hv);
    }
    {   // [hud] wrist_status (vr/wrist_status.h): summons, the mercenary, buffs, auras and curses over a wrist turned to the eyes, VR F4 on native OpenXR only; off by default
        wriststatus::Config ws;
        ws.on = IniB(L"hud", L"wrist_status", false);
        ws.companions = false;   // the summons' and the merc's counters: gone - the allies are shown where [hud] allies_place says (on the wrist: our tiles)
        ws.effects = IniB(L"hud", L"wrist_status_effects", true);
        ws.sides = (int)IniF(L"hud", L"wrist_status_sides", 0.0f);   // 0 the effects on the left wrist and the companions on the right, 1 the other way, 2 both on both
        ws.sizeCm = IniF(L"hud", L"wrist_status_size_cm", 10.0f);
        ws.heightCm = IniF(L"hud", L"wrist_status_height_cm", 7.0f);
        ws.alongCm = IniF(L"hud", L"wrist_status_along_cm", 0.0f);
        ws.coneDeg = IniF(L"hud", L"wrist_status_cone", 40.0f);
        ws.opacity = IniF(L"hud", L"wrist_status_opacity", 1.0f);
        ws.flip = IniB(L"debug", L"wrist_status_flip", false);
        wriststatus::Configure(ws);
    }
    {   // [hud] allies_place (shared/allies_place.h, vr/ally_portraits.h): where the allies are shown in VR F4 - the game's row, under the map, on the wrist
        allyportraits::Config ap;
        const int place = (int)IniF(L"hud", L"allies_place", -1.0f);
        ap.place = place >= 0 ? place   // (not in the file yet: the three ticks of 0.205.5 - 0.205.6, read for it)
                              : alliesplace::FromOldTicks((int)IniF(L"hud", L"ally_portraits", -1.0f), (int)IniF(L"hud", L"allies_game_hide", -1.0f),
                                                          (int)IniF(L"hud", L"wrist_status", -1.0f), (int)IniF(L"hud", L"wrist_status_companions", -1.0f));
        ap.sizePct = IniF(L"hud", L"ally_portraits_size", 125.0f);
        ap.mapCols = (int)IniF(L"hud", L"allies_map_cols", (float)alliesplace::kMapColsDefault);
        ap.wristCols = (int)IniF(L"hud", L"allies_wrist_cols", (float)alliesplace::kWristColsDefault);
        allyportraits::Configure(ap);
    }
    g_set.hudMap.store(std::clamp((int)IniF(L"hud", L"map", 0.0f), 0, 1));
    hud::SetMapMode(g_set.hudMap.load());
    hud::SetInterfaceScale(GameInterfaceScale());
    // [hud] hide = 1 takes the toolbar's strip out of the game's interface layer: that strip is worked out from the layer's size and Safe Screen Percent and fits the game's art only as
    // far as the 1920 x 1920 picture it was measured on - at the user's 2048 x 2048 (80 %) the horns, the statues' hands and the globes' tops stayed 10 - 55 px over the cut. So the strip
    // can be made taller above (hide_up_pct) and below (hide_down_pct), percent of the picture's height; [debug] hide_strip_tint paints the strip a see-through magenta to see its edges.
    hud::SetToolbarCut(IniF(L"hud", L"hide_up_pct", 0.0f), IniF(L"hud", L"hide_down_pct", 0.0f), IniB(L"debug", L"hide_strip_tint", false));
    g_set.mapCorner.store(std::clamp((int)IniF(L"hud", L"map_corner", 0.0f), 0, 2));
    g_set.mapPlaceFlat.store(std::clamp((int)IniF(L"hud", L"flat_map_place", 0.0f), 0, 3));   // left / middle / right in the flat game: MapRotateFlatTick
    // (the flat game's map put left or right by flat_map_place: its box looked for in either corner, whatever map_corner says)
    hud::SetMapCorner(g_set.platform.load() == 0 && g_set.mapPlaceFlat.load() != 0 ? 0 : g_set.mapCorner.load());
    g_set.mapDirection.store(IniB(L"hud", L"map_direction", true));   // the arrow on the game's map in our camera's views: MapDirectionTick
    g_set.mapTurn.store(IniB(L"hud", L"map_rotate", false) || IniB(L"hud", L"map_compass", false));   // the head's way for the map in the hand (xr.cpp reads the keys itself)
    g_set.mapRotateFlat.store(IniB(L"hud", L"map_rotate_flat", false));
    g_set.mapCompassFlat.store(IniB(L"hud", L"map_compass_flat", true));   // the compass over the turned map on the screen: MapRotateFlatTick
    g_set.flatMapZoom.store(IniB(L"hud", L"flat_map_zoom", true));   // + / - zoom the flat game's map: FlatMapZoomTick, FlatMapZoomKey
    g_set.flatMapZoomLevel.store(flatzoom::Clamped((int)std::lround(IniF(L"hud", L"flat_map_zoom_level", 0.0f))));   // the corner map on the screen turned with the camera: MapRotateFlatTick
    // The same while the game runs: the game's map put back in the middle (its options) is put in a corner again, by the game's own
    // setter (vr/minimap_side.h) - the start's Settings.json rewrite (MiniMapForHand) holds only when the plugin runs before the game reads it.
    gamestate::SetMapSide(g_set.platform.load() == 1 && g_set.hudMap.load() == 1 ? std::clamp((int)IniF(L"hud", L"map_corner", 0.0f), 0, 2) : -1);
    g_set.hudClassic.store(std::clamp((int)IniF(L"hud", L"classic", 0.0f), 0, 1));
    g_set.barNear.store(std::clamp(IniF(L"hud", L"bar_near", 0.0f), -20.0f, 20.0f));
    g_set.classicTop.store(std::clamp((int)IniF(L"hud_top", L"classic", (float)g_set.hudClassic.load()), 0, 1));
    g_set.classicThird.store(std::clamp((int)IniF(L"hud_third", L"classic", (float)g_set.hudClassic.load()), 0, 1));
    g_set.barNearTop.store(std::clamp(IniF(L"hud_top", L"bar_near", g_set.barNear.load()), -20.0f, 20.0f));
    g_set.barNearThird.store(std::clamp(IniF(L"hud_third", L"bar_near", g_set.barNear.load()), -20.0f, 20.0f));
    g_set.barNearInside.store(std::clamp(IniF(L"hud_inside", L"bar_near", g_set.barNear.load()), -20.0f, 20.0f));
    g_set.barSizeTop.store(std::clamp(IniF(L"hud_top", L"bar_size", 100.0f), 30.0f, 300.0f) * 0.01f);
    g_set.barSizeThird.store(std::clamp(IniF(L"hud_third", L"bar_size", 100.0f), 30.0f, 300.0f) * 0.01f);
    g_set.mapZoomInside.store(std::clamp(IniF(L"hud_inside", L"map_zoom", 1.0f), 0.3f, 3.0f));
    g_set.classicFloor.store(std::clamp((int)IniF(L"hud_floor", L"classic", 0.0f), 0, 1));
    g_set.barNearFloor.store(std::clamp(IniF(L"hud_floor", L"bar_near", g_set.barNear.load()), -20.0f, 20.0f));
    g_set.barSizeFloor.store(std::clamp(IniF(L"hud_floor", L"bar_size", 100.0f), 20.0f, 300.0f) * 0.01f);
    // bar_x: % of the screen's width, + right; bar_y: % of its height, + up (0, 0 = where the game puts it)
    g_set.barXTop.store(std::clamp(IniF(L"hud_top", L"bar_x", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.barYTop.store(-std::clamp(IniF(L"hud_top", L"bar_y", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.barXThird.store(std::clamp(IniF(L"hud_third", L"bar_x", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.barYThird.store(-std::clamp(IniF(L"hud_third", L"bar_y", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.barXFloor.store(std::clamp(IniF(L"hud_floor", L"bar_x", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.barYFloor.store(-std::clamp(IniF(L"hud_floor", L"bar_y", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.mapSizeFloor.store(std::clamp(IniF(L"hud_floor", L"map_size", 100.0f), 30.0f, 300.0f) * 0.01f);
    g_set.mapNearFloor.store(std::clamp(IniF(L"hud_floor", L"map_near", 0.0f), -20.0f, 20.0f));
    g_set.mapXFloor.store(std::clamp(IniF(L"hud_floor", L"map_x", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.mapYFloor.store(-std::clamp(IniF(L"hud_floor", L"map_y", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.labelsAlphaFloor.store(std::clamp(IniF(L"hud_floor", L"labels_alpha", 0.5f), 0.0f, 1.0f));
    g_set.labelsSizeFloor.store(std::clamp(IniF(L"hud_floor", L"labels_size", 100.0f), 20.0f, 150.0f) * 0.01f);
    g_set.labelsNativeFloor.store(IniF(L"hud_floor", L"labels_native", 1.0f) != 0.0f);
    g_set.labelsWholeEye.store(IniB(L"hud", L"labels_whole_eye", false));
    g_set.labelsAlphaTop.store(std::clamp(IniF(L"hud_top", L"labels_alpha", 1.0f), 0.0f, 1.0f));
    g_set.labelsAlphaThird.store(std::clamp(IniF(L"hud_third", L"labels_alpha", 1.0f), 0.0f, 1.0f));
    g_set.labelsAlphaBody.store(std::clamp(IniF(L"hud", L"labels_alpha", 1.0f), 0.0f, 1.0f));
    g_set.plateAlpha.store(std::clamp(IniF(L"hud", L"monster_alpha", 1.0f), 0.0f, 1.0f));
    g_set.plateSize.store(std::clamp(IniF(L"hud", L"monster_size", 100.0f), 30.0f, 200.0f) * 0.01f);
    g_set.alliesX.store(std::clamp(IniF(L"hud", L"allies_x", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.alliesY.store(-std::clamp(IniF(L"hud", L"allies_y", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.durHide.store(IniB(L"hud", L"durability_hide", false));
    g_set.durX.store(std::clamp(IniF(L"hud", L"durability_x", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.durY.store(-std::clamp(IniF(L"hud", L"durability_y", 0.0f), -100.0f, 100.0f) * 0.01f);
    g_set.durScale.store(std::clamp(IniF(L"hud", L"durability_scale", 100.0f), 30.0f, 300.0f) * 0.01f);
    g_set.labelsSizeTop.store(std::clamp(IniF(L"hud_top", L"labels_size", 100.0f), 20.0f, 150.0f) * 0.01f);
    g_set.labelsSizeThird.store(std::clamp(IniF(L"hud_third", L"labels_size", 100.0f), 20.0f, 150.0f) * 0.01f);
    g_set.labelsSizeBody.store(std::clamp(IniF(L"hud", L"labels_size", 100.0f), 20.0f, 150.0f) * 0.01f);
    g_set.labelsNear.store(std::clamp(IniF(L"hud", L"labels_near", 0.0f), -20.0f, 20.0f));
    g_set.labelsTilt.store(std::clamp(IniF(L"hud", L"labels_tilt", 0.0f), -40.0f, 40.0f));
    g_set.labelsNearBody.store(std::clamp(IniF(L"hud", L"labels_near_body", 0.0f), -20.0f, 20.0f));
    g_set.plateFar.store(std::clamp(IniF(L"hud", L"monster_far", 100.0f), 20.0f, 100.0f) * 0.01f);
    g_set.plateDepthBody.store(std::clamp(IniF(L"hud", L"monster_depth_body", 0.0f), 0.0f, 150.0f) * 0.01f);
    g_set.plateNearBody.store(std::clamp(IniF(L"hud", L"monster_near_body", g_set.labelsNearBody.load()), -20.0f, 20.0f));   // (no key: where the labels' depth has always put it)
    g_set.plateStereoByDistance.store(IniB(L"hud", L"monster_stereo_by_distance", false));   // (vr/plate_look.h DepthAt)
    plateworld::SetSteady(IniB(L"hud", L"monster_plate_smooth", false));   // the plate hung on a head held steady, the distance too (vr/plate_world.h SteadyHead)
    g_set.plateStereoNear.store(std::clamp(IniF(L"hud", L"monster_stereo_near", 2.0f), -20.0f, 20.0f));
    g_set.plateStereoFar.store(std::clamp(IniF(L"hud", L"monster_stereo_far", 1.0f), -20.0f, 20.0f));
    g_set.plateStereoNearM.store(std::clamp(IniF(L"hud", L"monster_stereo_near_m", platelook::kNearM), 0.0f, 30.0f));
    g_set.plateStereoFarM.store(std::clamp(IniF(L"hud", L"monster_stereo_far_m", platelook::kFarM), 0.5f, 60.0f));
    plateworld::SetTrack(g_set.plateFar.load() < 0.995f || g_set.plateDepthBody.load() > 0.005f || g_set.plateStereoByDistance.load() || plateworld::Steadying() ||
                         IniB(L"hud", L"monster_names_own", false));   // (monster_names_own: our names' depth and size by distance need the target's distance)   // the hook notes the targeted monster for them
    g_set.ownSizeNear.store(std::clamp(IniF(L"hud", L"labels_own_near_pct", 100.0f), 10.0f, 300.0f));   // our labels' size by distance (ownlabels::CurveK), live
    g_set.ownSizeFar.store(std::clamp(IniF(L"hud", L"labels_own_far_pct", 40.0f), 10.0f, 300.0f));
    g_set.ownNearM.store(std::clamp(IniF(L"hud", L"labels_own_near_m", 1.5f), 0.1f, 20.0f));
    g_set.ownFarM.store(std::clamp(IniF(L"hud", L"labels_own_far_m", 20.0f), 1.0f, 60.0f));
    g_set.ownMinPx.store(std::clamp(IniF(L"hud", L"labels_own_min_px", 24.0f), 0.0f, 80.0f));   // our labels and names never smaller than this (px of the eye), live
    g_set.monsterNamesOwn.store(IniB(L"hud", L"monster_names_own", false));
    g_set.npcNamesOwn.store(IniB(L"hud", L"npc_names_own", false));
    g_set.ownDropGame.store(IniB(L"hud", L"own_labels_drop_game", false));   // the game's own layer not drawn under ours (labels.cpp DropGameLayer), live
    g_set.ownLabelsDump.store(IniB(L"debug", L"own_labels_dump", false));   // our labels' atlas to a picture once (view only), live
    g_set.namesSize.store(std::clamp(IniF(L"hud", L"names_own_size_pct", 100.0f), 20.0f, 300.0f));   // our names' own size, by distance, backdrop (ownnames::NameLook), live
    g_set.namesFar.store(std::clamp(IniF(L"hud", L"names_own_far_pct", 60.0f), 10.0f, 150.0f));
    g_set.namesAlpha.store(std::clamp(IniF(L"hud", L"names_own_alpha", 0.0f), 0.0f, 1.0f));   // F4: the hover names over NPCs, objects, portals drawn by us (vr/own_names.h), live   // F4: the names in the target's plate drawn by us, sharp (vr/own_names.h), live
    plateworld::SetFollow(platelook::PlateNeedsBox(true, PlateDepthForBox(), g_set.labelsNearBody.load(), g_set.plateAlpha.load(), g_set.plateSize.load(),
                                                   g_set.plateFar.load(), g_set.plateDepthBody.load()) ||
                          g_set.monsterNamesOwn.load());   // (monster_names_own: our names stand in the plate's box)   // F4 needs the plate's box: the hook says where the plate hangs (PlateBoxUv)
    g_set.labelsDepthBody.store(std::clamp(IniF(L"hud", L"labels_depth_body", 0.0f), 0.0f, 150.0f) * 0.01f);
    g_set.labelsFarBody.store(std::clamp(IniF(L"hud", L"labels_far", 100.0f), 20.0f, 100.0f) * 0.01f);
    g_set.labelsOwn.store(IniB(L"hud", L"labels_own", false));   // F4: our own sharp item labels in the game's places (vr/own_labels.h), live
    ownlabels::SetWanted(g_set.labelsOwn.load());
    for (int i = 0; i < 2; ++i) {
        const wchar_t* sec = i == 0 ? L"hud_top" : L"hud_third";
        g_set.pointerNear[i].store(std::clamp(IniF(sec, L"pointer_near", 0.0f), -20.0f, 20.0f));
        g_set.pointerTilt[i].store(std::clamp(IniF(sec, L"pointer_tilt", 0.0f), -40.0f, 40.0f));
        const float n = g_set.pointerNear[i].load(), t = g_set.pointerTilt[i].load();
        g_set.pointerAt[i][0].store(std::clamp(IniF(sec, L"pointer_top", n - 0.5f * t), -40.0f, 40.0f));
        g_set.pointerAt[i][1].store(std::clamp(IniF(sec, L"pointer_mid", n), -40.0f, 40.0f));
        g_set.pointerAt[i][2].store(std::clamp(IniF(sec, L"pointer_bottom", n + 0.5f * t), -40.0f, 40.0f));
        g_set.pointerTopSize[i].store(std::clamp(IniF(sec, L"pointer_top_size", 100.0f), 10.0f, 200.0f));
    }
    g_set.crosshairDepth.store(std::clamp(IniF(L"hud_third", L"crosshair_depth", 0.0f), -40.0f, 40.0f));
    g_set.crosshairOn.store(IniB(L"hud_third", L"crosshair", true));
    g_set.crosshairSize.store(std::clamp(IniF(L"hud_third", L"crosshair_size", 100.0f), 25.0f, 400.0f));
    g_set.crosshairDepthDown.store(std::clamp(IniF(L"hud_third", L"crosshair_depth_down", g_set.crosshairDepth.load()), -40.0f, 40.0f));
    {   // [hud] how FlatVR shows the toolbar and the map (game_hud_shared.h)
        FlatVRGameHudLook look = FlatVRGameHudDefaultLook();
        const int place = (int)IniF(L"hud", L"bar_place", 5.0f);
        look.bar_anchor = place == 5 ? 0u : (uint32_t)std::clamp(place, 0, 4);   // 5 = the wrists: the plugin draws them, FlatVR hangs no toolbar
        // A set of its own on a forearm (bar_*_arm) and on the body (bar_*_body); the
        // one key of before (bar_*) stands in for both until they are set.
        const wchar_t* set = look.bar_anchor == 1 || look.bar_anchor == 2 ? L"_arm" : L"_body";
        auto bar = [&](const wchar_t* name, float def) {
            wchar_t key[64];
            swprintf_s(key, L"bar_%s%s", name, set);
            wchar_t old[64];
            swprintf_s(old, L"bar_%s", name);
            return IniF(L"hud", key, IniF(L"hud", old, def));
        };
        look.bar_roll_deg = std::clamp(bar(L"roll", 0.0f), -180.0f, 180.0f);
        look.bar_tip_deg = std::clamp(bar(L"tip", 0.0f), -90.0f, 90.0f);
        look.bar_spin_deg = std::clamp(bar(L"spin", 0.0f), -180.0f, 180.0f);
        look.bar_lift_cm = std::clamp(bar(L"lift", 0.0f), -10.0f, 20.0f);
        look.bar_side_cm = std::clamp(bar(L"side", 0.0f), -20.0f, 20.0f);
        look.bar_width_m = std::clamp(bar(L"width", look.bar_width_m), 0.05f, 1.5f);
        look.bar_along_cm = std::clamp(bar(L"along", look.bar_along_cm), -30.0f, 40.0f);
        look.bar_split = (uint32_t)std::clamp((int)bar(L"split", 0.0f), 0, 2);   // two halves side by side, both orbs at one end
        look.map_anchor = (uint32_t)std::clamp((int)IniF(L"hud", L"map_place", (float)look.map_anchor), 0, 4);
        look.map_orb = IniB(L"hud", L"map_orb", look.map_orb != 0) ? 1u : 0u;
        look.map_width_m = std::clamp(IniF(L"hud", L"map_width", look.map_width_m), 0.05f, 1.5f);
        look.orb_size_m = std::clamp(IniF(L"hud", L"orb_size", look.orb_size_m), 0.03f, 0.6f);
        look.orb_zoom = std::clamp(IniF(L"hud", L"orb_zoom", look.orb_zoom), 0.5f, 4.0f);
        look.orb_glow = std::clamp(IniF(L"hud", L"orb_glow", look.orb_glow), 0.0f, 1.0f);
        look.orb_alpha = std::clamp(IniF(L"hud", L"orb_alpha", look.orb_alpha), 0.02f, 1.0f);
        look.encoded = IniB(L"hud", L"colors_as_monitor", look.encoded != 0) ? 1u : 0u;
        wchar_t col[64];
        GetPrivateProfileStringW(L"hud", L"orb_color", L"", col, (DWORD)std::size(col), g_iniPath);
        float rgb[3];
        if (ParseHexColor(col, rgb))
            look.orb_rgb = ((uint32_t)std::lround(rgb[0] * 255.0f) << 16) | ((uint32_t)std::lround(rgb[1] * 255.0f) << 8) |
                           (uint32_t)std::lround(rgb[2] * 255.0f);
        hud::SetLook(look);
    }
    g_set.mouseSpeed.store(std::clamp(IniF(L"input", L"mouse_speed", 0.15f), 0.01f, 2.0f));
    g_set.nearClip.store(std::clamp(IniF(L"camera", L"near", 1.5f), 0.0f, 20.0f));
    g_set.camSide.store(std::clamp(IniF(L"camera", L"side", 0.0f), -10.0f, 10.0f));
    g_set.camForward.store(std::clamp(IniF(L"camera", L"forward", 0.0f), -10.0f, 10.0f));
    g_set.neckModel.store(IniB(L"camera", L"neck_model", true));
    g_set.neckUpCm.store(std::clamp(IniF(L"camera", L"neck_up_cm", 10.0f), 0.0f, 30.0f));
    g_set.neckFwdCm.store(std::clamp(IniF(L"camera", L"neck_forward_cm", 9.0f), 0.0f, 30.0f));
    g_set.headYaw.store(IniB(L"head", L"yaw_from_head", true));
    g_set.yawSign.store(IniF(L"head", L"yaw_sign", 1.0f) < 0 ? -1.0f : 1.0f);
    g_set.yawScale.store(IniF(L"head", L"yaw_scale", 1.0f));
    g_set.headPitch.store(IniB(L"head", L"pitch_from_head", true));
    g_set.headPredict.store(IniB(L"head", L"predict", false));
    g_set.flatSync.store(std::clamp((int)IniF(L"flatvr", L"frame_sync", 0.0f), 0, 2));
    g_set.flatSyncLate.store(std::clamp(IniF(L"flatvr", L"frame_sync_late", 0.5f), 0.05f, 1.0f));
    g_set.flatLevel.store(IniB(L"flatvr", L"head_level", false));
    g_set.flatPosition.store(IniB(L"flatvr", L"head_position", false));
    g_set.flatRoomCentre.store(IniB(L"flatvr", L"room_centre", false));
    g_set.flatEnterCentre.store(IniB(L"flatvr", L"recenter_on_enter", false));
    g_set.pitchSign.store(IniF(L"head", L"pitch_sign", 1.0f) < 0 ? -1.0f : 1.0f);
    g_set.headRoll.store(IniB(L"head", L"roll_from_head", true));
    g_set.rollSign.store(IniF(L"head", L"roll_sign", 1.0f) < 0 ? -1.0f : 1.0f);
    g_set.stickRotate.store(IniB(L"stick", L"rotate", true));
    g_set.stickSign.store(IniF(L"stick", L"sign", 1.0f) < 0 ? -1.0f : 1.0f);
    g_set.stickSquash.store(std::clamp(IniF(L"stick", L"squash", 1.0f), 0.1f, 4.0f));
    g_set.stickExact.store(IniB(L"stick", L"exact", true));
    g_set.stickAngleScale.store(std::clamp(IniF(L"stick", L"angle_scale", 1.0f), 0.0f, 4.0f));
    g_set.rightTurn.store(IniB(L"turn", L"right_stick", true));
    g_set.turnSpeed.store(turnmode::Speed(IniF(L"turn", L"speed", turnmode::kSpeedDefault)));
    // [turn] smooth is its own key (1 smooth at `speed`, 0 a snap of `snap` degrees); an ini without it keeps its mode: snap=0 is smooth turning
    g_set.snapAngle.store(turnmode::EffectiveSnapAngle(IniF(L"turn", L"snap", 0.0f), IniF(L"turn", L"smooth", -1.0f)));
    g_set.turnSign.store(IniF(L"turn", L"sign", 1.0f) < 0 ? -1.0f : 1.0f);
    g_set.afr.store(IniB(L"stereo", L"afr", false) && g_set.platform.load() == 1);   // on a monitor alternate eyes only flicker
    g_set.ipd.store(std::clamp(IniF(L"stereo", L"ipd", 0.27f), 0.0f, 5.0f));
    g_set.afrSwap.store(IniB(L"stereo", L"swap", false));
    g_set.dlssPerEye.store(IniB(L"render", L"dlss_per_eye", true));
    g_set.pairPerTick.store(IniB(L"stereo", L"pair_per_tick", false));
    g_set.rightDtMs.store(std::clamp(IniF(L"stereo", L"right_dt_ms", 0.01f), 0.0f, 5.0f));
    g_set.pairFxSync.store(IniB(L"stereo", L"pair_fx_sync", true));
    g_set.slowPairWorld.store(IniB(L"stereo", L"slow_pair_world", true));
    g_set.trueScale.store(IniB(L"stereo", L"true_scale", true));
    g_set.eyeMm.store(std::clamp(IniF(L"stereo", L"eye_mm", 63.0f), 45.0f, 80.0f));
    // From the user's height: the eyes of an adult sit at ~0.936 of it.
    g_set.eyeHeightM.store(0.936f * std::clamp(IniF(L"stereo", L"user_height_m", 1.75f), 1.1f, 2.3f));
    g_set.topStereo.store(IniB(L"stereo", L"top_down", false) && g_set.platform.load() == 1);
    g_set.topAngle.store(std::clamp(IniF(L"stereo", L"top_angle", 3.0f), 0.0f, 20.0f));
    g_set.convergence.store(std::clamp(IniF(L"stereo", L"convergence", 15.0f), 0.0f, 1000.0f));
    g_set.bgFullSpeed.store(IniB(L"render", L"background_full_speed", true));
    g_set.gameHeightFog.store(IniB(L"render", L"game_height_fog", false));
    g_set.gameDepthFog.store(IniB(L"render", L"game_depth_fog", false));
    g_set.gameFogScanSafe.store(IniB(L"render", L"game_fog_scan_safe", true));
    g_ownDepth.store(IniB(L"render", L"own_depth", true));
    g_squareWin.store(IniB(L"openxr", L"square_window", false));
    {   // the game's light culling at non-16:10 sizes: made whenever the key is on (read at the game's start), bound in VR, and in flat mode with light_bounds_flat
        const bool fix = IniB(L"render", L"light_bounds_fix", true), flat = g_set.platform.load() == 0, boundFlat = IniB(L"render", L"light_bounds_flat", true);
        // ([debug] patch_hunt 6 / 9: made as ever, not bound - the game's own light bins)
        lightbounds::Configure(fix, !patchhunt::LightBoundsOff(hunt) && (!flat || boundFlat), IniB(L"debug", L"log_compute_shaders", false), flat);
        suspects.lightBoundsFix = fix;
        suspects.lightBoundsFlat = boundFlat;
        // the game's reflections in puddles, blood and water by each eye's own position (vr/reflections_eye.h): the copies made when the key
        // is on at the game's start, bound live in VR
        refleye::Configure(IniB(L"render", L"reflections_per_eye", false), !flat);
    }
    {   // [render] foveated (vr/foveation.h): fixed foveated rendering in F4 - live (its hooks go in only when it was on at the start)
        foveation::Config fc;
        fc.on = IniB(L"render", L"foveated", false);
        fc.strength = (int)IniF(L"render", L"foveated_strength", 1.0f);
        fc.radius = IniF(L"render", L"foveated_radius", 0.0f);
        fc.unseen = IniB(L"render", L"foveated_unseen", true);
        fc.abSeconds = (int)IniF(L"render", L"foveated_ab", 0.0f);
        fc.gaze = IniB(L"render", L"foveated_gaze", true);   // (the gaze itself is asked of the runtime at the session start)
        fc.fullscreen = (int)IniF(L"render", L"foveated_fullscreen", 0.0f);   // the heavy full-screen passes too (vr/fullscreen_passes.h)
        fc.grassCoarse = (int)IniF(L"render", L"grass_lighting_coarse", 0.0f);  // the grass lighting at 2x1 / 2x2
        foveation::Configure(fc);
    }
    {   // [render] lens_mask (vr/lens_mask.h): the hidden area of each eye cleared to the nearest depth after the game's scene depth clear - live
        lensmask::Config lc;
        lc.on = IniB(L"render", L"lens_mask", false);
        lc.turnWarp = IniB(L"stereo", L"turn_warp", true);   // (eyes by turns: the eye made is moved by the depth - no mask with it)
        lensmask::Configure(lc);
    }
    g_set.solidWalls.store(std::clamp((int)IniF(L"render", L"solid_walls", 1.0f), 0, 2));
    g_set.keepRoofs.store(IniB(L"render", L"keep_roofs", false));
    g_set.roofsTwoSided.store(IniB(L"render", L"roofs_two_sided", false));
    g_set.propsOncePair.store(IniB(L"stereo", L"props_once_a_pair", false));
    g_set.stampPixels.store(IniB(L"stereo", L"stamp_pixels", true));
    g_set.stamps.store(IniB(L"stereo", L"stamps", true));
    g_set.pictureRing.store(IniB(L"stereo", L"picture_ring", false));
    g_set.pace.store(IniB(L"stereo", L"pace", true));
    g_set.paceFlatVrOnly.store(IniB(L"stereo", L"pace_flatvr_only", true));
    g_set.flatvrFeed.store(IniB(L"render", L"flatvr_feed", true));
    g_set.vcache.store(IniB(L"render", L"vcache", false));
    {
        static const int kHz[5] = {72, 75, 80, 90, 120};
        g_set.headsetHz.store(kHz[std::clamp((int)IniF(L"stereo", L"headset_rate", 3.0f), 0, 4)]);
    }
    // [stereo] replay_right: the ReShade path's; [stereo] native_replay: the plugin's own path's (vr/native_replay.h) - each its own key
    // (native_replay's default 1: one pass, Serg 2026-11-08 - shared/render_type.h reads an ini without it the same way)
    replay::SetWant((int)IniF(L"stereo", L"replay_right", 0.0f), (int)IniF(L"stereo", L"native_replay", 1.0f));
    {   // the render type (shared/render_type.h) as these keys make it: one line at the start and at every change, and whether it takes effect now
        // (Serg's 0.207.7 report, 2026-11-08: "глаза по очереди - ничего не поменялось" - the log said nothing of what the file asked for)
        rendertype::Keys k;
        k.openxrOn = (int)IniF(L"openxr", L"on", 0.0f);
        k.afr = (int)IniF(L"stereo", L"afr", 0.0f);
        k.pairPerTick = (int)IniF(L"stereo", L"pair_per_tick", 0.0f);
        k.nativeReplay = (int)IniF(L"stereo", L"native_replay", 1.0f);
        k.source = (int)IniF(L"stereo", L"source", k.afr ? 2.0f : 1.0f);
        const int type = rendertype::FromKeys(k);
        static int told = -1;
        if (type != told) {
            const bool first = told < 0;
            told = type;
            const char* when = type == rendertype::kNativeOne && !replay::NativeAtStart()
                                   ? "AFTER A RESTART - one pass's hooks go in at the game's start ([stereo] native_replay was 0 then): two passes until then"
                               : type == rendertype::kNativeOne ? "now (once an area has settled, F4; a paused pair is two passes)"
                               : rendertype::Native(type) ? "now"
                                                          : "now - native OpenXR ends and FlatVR (BodyWalk) shows the picture";
            LogF("vrcam: render type %s %s ([openxr] on=%d, [stereo] afr=%d pair_per_tick=%d native_replay=%d source=%d): %s", first ? "at the start:" : "changed to",
                 rendertype::Name(type), k.openxrOn, k.afr, k.pairPerTick, k.nativeReplay, k.source, when);
        }
    }
    shadowsonce::SetOn(IniB(L"render", L"shadows_once_a_pair", false));   // (live once its hooks went in at the start; vr/shadows_once.h)
    // [render] sun_shadows / sun_shadow_radius_m / sun_shadow_every (vr/simpler_sun.h): the game's sun shadow map filled for less in F4 - live
    // ([render] game_stages 2, the Potato stages: the Potato light reads no sun map, so none of its casters is collected either - sun_shadows 0's
    // processor saving; the stage skip alone only drops the ~1,800 draws a present after the game has collected them. Serg's 0.207.8 run)
    simplersun::Configure(std::clamp((int)IniF(L"render", L"game_stages", 0.0f), 0, 2) >= 2 ? 0 : (int)IniF(L"render", L"sun_shadows", 2.0f),
                          IniF(L"render", L"sun_shadow_radius_m", 0.0f), (int)IniF(L"render", L"sun_shadow_every", 1.0f));
    replay::g_ahead.store(std::clamp((int)IniF(L"stereo", L"replay_ahead", 1.0f), 1, 3));
    replay::g_depthCopy.store(IniB(L"stereo", L"replay_depth", false));
    replay::g_rightDepth.store(IniB(L"stereo", L"replay_right_depth", true));
    replay::g_holdSignalsOn.store(IniB(L"stereo", L"replay_hold_signals", false));
    g_projFix.store((int)IniF(L"stereo", L"replay_proj", 3.0f));
    g_shiftMul.store(IniF(L"stereo", L"replay_shift_mul", 1.0f));
    g_replayViews.store(std::clamp((int)IniF(L"stereo", L"replay_views", 8.0f), 0, 8));
    g_viewMask.store((int)IniF(L"stereo", L"replay_view_mask", 0.0f));
    g_replayPositions.store((int)IniF(L"stereo", L"replay_positions", 2.0f));
    g_replayPrev.store((int)IniF(L"stereo", L"replay_prev", 1.0f));
    replay::g_fxSync.store(IniB(L"stereo", L"replay_fx_sync", true));
    replay::g_marksOn.store(std::clamp((int)IniF(L"debug", L"replay_marks", 1.0f), 0, 4));   // how the replayed lists are submitted (replay_groups.h): 0 batched, 1 one by one with a signal after each, 2 one by one, 3 as the game's calls, 4 the same with a signal after each
    replay::g_fxFence.store(IniB(L"render", L"fx_fence", false));   // our effects' constants kept apart by fences (F4)
    replay::g_resizeDrain.store(IniB(L"render", L"resize_drain", true));   // no replay of a pass the game resized / applied video options in, the GPU drained first (vr/resize_drain.h)
    replay::g_dlssBoth.store(IniB(L"stereo", L"replay_dlss", false));
    replay::g_replayCopy.store(IniB(L"stereo", L"replay_copy", true));
    replay::g_fxAtHold.store(IniB(L"stereo", L"replay_fx", true));
    // [render] monitor_eye: the window one eye (0 as before, 1 left, 2 right), cropped to the window's shape; by turns the eye made or only drawn ones
    monitoreye::Configure((int)IniF(L"render", L"monitor_eye", 1.0f), IniB(L"render", L"monitor_eye_fit", true), IniB(L"render", L"monitor_eye_drawn_only", false));
    // [render] monitor_panels: the panels the headset shows (the menu's panel, the toolbar, the map, the laser...) over the window's picture too
    monitorlayers::Configure(IniB(L"render", L"monitor_panels", false));
    xr::LoadSettings(g_iniPath);   // [openxr]: the game in the headset itself (vr/xr.cpp)
    calib::LoadSettings(g_iniPath);   // [calibration]: the measured height and arm span (vr/calibration.cpp)
    headshot::LoadSettings(g_iniPath);   // [headset_shot]: on, its key, Settings' button (a change of take = one shot)
    headclip::LoadSettings(g_iniPath);   // [headset_shot] video, video_key, video_take: the 10 s clip, apart from the screenshot
    headgrid::LoadSettings(g_iniPath);   // [headset_shot] grid: the calibration grid in the eyes' pictures (live)
    firststart::LoadSettings(g_iniPath);   // [onboarding]: the first-start guide (vr/first_start.cpp)
    g_xrThirdGaze.store(IniB(L"openxr", L"third_gaze", false));
    g_inputLog.store((int)IniF(L"debug", L"input_log", 1.0f));   // presses and their sources (InputLogF)
    {   // [debug] profile_draw: one 5 s profile of the draw thread each time it turns 1
        static bool was = false;
        const bool on = IniB(L"debug", L"profile_draw", false);
        if (on && !was) drawprof::g_want.store(true);
        was = on;
        static bool wasGo = false;
        const bool go = IniB(L"debug", L"cb_compare_go", false);
        if (go && !wasGo) cbcmp::g_go.store(true);
        wasGo = go;
        static bool wasScan = false;   // [debug] replay_scan_go 0 -> 1: copies of the left camera in upload memory
        const bool scan = IniB(L"debug", L"replay_scan_go", false);
        if (scan && !wasScan) camfix::g_scanWant.store(1);
        wasScan = scan;
        static bool wasPeek = false;   // [debug] replay_peek_go 0 -> 1: one pair's two pictures compared
        const bool peek = IniB(L"debug", L"replay_peek_go", false);
        if (peek && !wasPeek) replay::g_peekWant.store(1);
        wasPeek = peek;
        static bool wasRt = false;
        const bool rt = IniB(L"debug", L"replay_trace_go", false);
        if (rt && !wasRt) rtrace::g_go.store(true);
        wasRt = rt;
        // [debug] diag_go: D2R VR Settings' Collect logs writes a new value - 10 s of the frame
        // log (and FlatVR's pose trace beside it) for the logs it zips. The value at start is
        // only noted: a capture begins on a change while the game runs.
        // [autotune] enabled: the "Auto" graphics tuning (its button in the headset's Video page and in D2R VR Settings); run / keep / undo:
        // Settings' buttons write a new value - a run starts, its result is kept / undone on a change while the game runs (vr/auto_tune_run.h)
        autotunerun::Configure(g_iniPath, IniB(L"autotune", L"enabled", false), (int)IniF(L"autotune", L"run", 0.0f), (int)IniF(L"autotune", L"keep", 0.0f),
                               (int)IniF(L"autotune", L"undo", 0.0f));
        static int lastDiag = INT_MIN;
        const int diag = (int)IniF(L"debug", L"diag_go", 0.0f);
        if (lastDiag != INT_MIN && diag != lastDiag) flog::StartDiag();
        lastDiag = diag;
    }
    g_set.pipelineDepth.store(std::clamp((int)IniF(L"stereo", L"pipeline_depth", 0.0f), 0, 3));
    g_set.turnFxFence.store(IniB(L"stereo", L"turn_fx_fence", true));
    g_set.thirdDistance.store(std::clamp(IniF(L"third", L"distance", 7.0f), -5.0f, 200.0f));
    g_set.tableFloor.store(std::clamp(IniF(L"table", L"floor", 0.05f), 0.0f, 0.3f));
    // [table] hero_cm: how tall the hero stands on the floor - the size of the whole
    // game field, as a number one can picture. A hero is ~6 world units tall.
    g_set.tableScale.store(6.0f / (std::clamp(IniF(L"table", L"hero_cm", 40.0f), 5.0f, 250.0f) * 0.01f));
    g_set.topPersp.store(IniB(L"top", L"perspective", false));
    g_set.topTilt.store(std::clamp(IniF(L"top", L"tilt", 0.0f), -40.0f, 60.0f));
    g_set.tableHeightM.store(std::clamp(IniF(L"table", L"height_m", 0.0f), -2.0f, 2.0f));
    g_set.tableTurnDeg.store(std::clamp(IniF(L"table", L"turn", 0.0f), -180.0f, 180.0f));
    g_set.tablePlace.store((int)IniF(L"table", L"place", 0.0f));
    g_set.tableBounds.store(std::clamp(IniF(L"table", L"bounds", 1.0f), 0.0f, 5.0f));
    g_set.tableAheadM.store(std::clamp(IniF(L"table", L"ahead_m", 1.0f), 0.0f, 5.0f));
    LoadTableBackground();
    g_set.thirdHeight.store(std::clamp(IniF(L"third", L"height", 6.5f), 0.0f, 40.0f));
    g_set.uiShift.store(std::clamp(IniF(L"stereo", L"ui_near", 0.0f), 0.0f, 20.0f));
    g_set.hudTop.store(std::clamp(IniF(L"stereo", L"ui_top", 0.70f), 0.0f, 1.0f));
    g_set.fogOn.store(IniB(L"fog", L"enabled", false));
    fogspare::SetWanted(IniB(L"fog", L"spare_effects", false));   // the game's pipelines are looked at only with it on at the game's start
    // (the same: read at the game's start). [fog] game_fog_before_fx asks for it too: with ReShade our effect's fog goes before the game's
    // transparent resolve this way (VR F4); without ReShade it is vr/fog_before_fx.cpp's step, and this one is never registered
    fogframe::SetWanted(IniB(L"render", L"fog_in_frame", false) || IniB(L"fog", L"game_fog_before_fx", false));
    fx::g_fifPaint.store(std::clamp((int)IniF(L"debug", L"fog_in_frame_paint", 0.0f), 0, 4));   // live
    g_set.fogStart.store(std::clamp(IniF(L"fog", L"start", 150.0f), 0.0f, 100000.0f));
    g_set.fogEnd.store(std::clamp(IniF(L"fog", L"end", 600.0f), g_set.fogStart.load() + 1.0f, 200000.0f));
    // caves: their own range, the open air's when not set (caves are smaller, their fog nearer)
    g_set.fogCaveStart.store(std::clamp(IniF(L"fog", L"caves_start", g_set.fogStart.load()), 0.0f, 100000.0f));
    g_set.fogCaveEnd.store(std::clamp(IniF(L"fog", L"caves_end", g_set.fogEnd.load()), g_set.fogCaveStart.load() + 1.0f, 200000.0f));
    g_set.fogCaveStrength.store(std::clamp(IniF(L"fog", L"caves_strength", 1.0f), 0.0f, 1.0f));
    g_set.fogCurve.store(std::clamp(IniF(L"fog", L"curve", 1.6f), 0.3f, 4.0f));
    // [fog] arcane_own: the Arcane Sanctuary's own numbers (missing: the caves' and the curve above - its look as it was)
    g_set.arcaneOwnFog.store(IniB(L"fog", L"arcane_own", false));
    g_set.arcaneFogStart.store(std::clamp(IniF(L"fog", L"arcane_start", g_set.fogCaveStart.load()), 0.0f, 100000.0f));
    g_set.arcaneFogEnd.store(std::clamp(IniF(L"fog", L"arcane_end", g_set.fogCaveEnd.load()), g_set.arcaneFogStart.load() + 1.0f, 200000.0f));
    // [fog] game_fog=1 (GameFogNumbersNow): a full fog nearer than its start (the slider lets "Full fog" go below "Fog start") is taken as the
    // near end - the two swapped. Held to start + 1 the whole fog came in over one unit - Serg on 0.192.34, start 40 / end 10: a flat grey
    // wall from 41 units with a hard edge where it met the sky (121.webp). Our effect's fog keeps start + 1 above (Serg 2026-10-31: "старый
    // туман пожалуйста не трогай")
    {
        const auto fogRange = [](std::atomic<float>& start, std::atomic<float>& end, float s, float e, const char* name) {
            s = std::clamp(s, 0.0f, 100000.0f);
            e = std::clamp(e, 0.0f, 200000.0f);
            if (e < s) {
                LogF("vrcam: [fog] %s: full fog %.0f nearer than its start %.0f - the game's fog takes them as %.0f to %.0f", name, e, s, e, s);
                std::swap(s, e);
            }
            start.store(s);
            end.store(std::max(e, s + 1.0f));
        };
        fogRange(g_set.gameFogStart, g_set.gameFogEnd, IniF(L"fog", L"start", 150.0f), IniF(L"fog", L"end", 600.0f), "start / end");
        fogRange(g_set.gameFogCaveStart, g_set.gameFogCaveEnd, IniF(L"fog", L"caves_start", g_set.gameFogStart.load()),
                 IniF(L"fog", L"caves_end", g_set.gameFogEnd.load()), "caves_start / caves_end");
        fogRange(g_set.gameArcaneFogStart, g_set.gameArcaneFogEnd, IniF(L"fog", L"arcane_start", g_set.gameFogCaveStart.load()),
                 IniF(L"fog", L"arcane_end", g_set.gameFogCaveEnd.load()), "arcane_start / arcane_end");
    }
    g_set.arcaneFogStrength.store(std::clamp(IniF(L"fog", L"arcane_strength", g_set.fogCaveStrength.load()), 0.0f, 1.0f));
    g_set.arcaneFogCurve.store(std::clamp(IniF(L"fog", L"arcane_curve", g_set.fogCurve.load()), 0.3f, 4.0f));
    g_set.arcaneFogIntoSky.store(IniB(L"fog", L"arcane_into_sky", true));
    g_set.fogBlur.store(std::clamp(IniF(L"fog", L"blur", 3.0f), 0.0f, 12.0f));
    g_set.fogLight.store(IniB(L"fog", L"light", false));
    {   // [render] distance_blur: its distances in metres (shared/draw_distance.h: 4 world units a metre), the full blur past the start
        g_set.distanceBlur.store(IniB(L"render", L"distance_blur", false));
        const float startM = std::clamp(IniF(L"render", L"distance_blur_start_m", 15.0f), 1.0f, 2000.0f);
        const float fullM = std::clamp(IniF(L"render", L"distance_blur_full_m", 50.0f), startM * 1.05f, 4000.0f);
        g_set.distanceBlurStart.store(drawdistance::ToUnits(startM));
        g_set.distanceBlurFull.store(drawdistance::ToUnits(fullM));
        g_set.distanceBlurStrength.store(std::clamp(IniF(L"render", L"distance_blur_strength", 0.4f), 0.0f, 1.0f));
    }
    g_set.gameFog.store(IniB(L"fog", L"game_fog", false));   // [fog] game_fog: the game's own fog with our numbers (VR F4)
    g_set.fogHeight.store(IniB(L"fog", L"game_fog_height", false));
    g_set.fogTop.store(std::clamp(IniF(L"fog", L"game_fog_top", 60.0f), -100.0f, 2000.0f));
    g_set.fogSoft.store(std::clamp(IniF(L"fog", L"game_fog_soft", 50.0f), 1.0f, 1000.0f));
    g_set.fogTopFromFloor.store(IniB(L"fog", L"game_fog_top_from_floor", false));
    g_set.fogMist.store(IniB(L"fog", L"game_fog_mist", true));
    g_set.fogCavesBlack.store(IniB(L"fog", L"game_fog_caves_black", true));
    g_set.fogBeforeFx.store(IniB(L"fog", L"game_fog_before_fx", false));
    g_set.fogFxDepth.store(IniB(L"fog", L"game_fog_fx_depth", false));
    g_set.fogOurCurve.store(IniB(L"fog", L"game_fog_our_curve", false));
    // ([fog] game_fog_match removed 2026-11-08 - Serg: "она плохо работает и ломает туман - можно вообще её удалить, чтобы кто-то случайно не
    // включил". It kept our effect's whole world pass running to move the game's fogged pixels (Potato 95 -> 83 pairs/s); a leftover key in an
    // old ini is read by nothing)
    g_set.fogMeasured.store(IniB(L"fog", L"fog_colour_measured", false));
    g_set.fogIntoSky.store(IniB(L"fog", L"fog_into_sky", false));
    g_set.fogKeepsColour.store(IniB(L"fog", L"fog_keeps_colour", false));
    g_set.gameDrawnFlat.store(IniB(L"render", L"game_drawn_flat", false));
    g_set.fogCaves.store(IniB(L"fog", L"caves", true));
    g_set.fogEaseIn.store(IniB(L"fog", L"ease_in", false));
    g_set.fogNoRiver.store(IniB(L"fog", L"no_fog_river", false));
    {
        wchar_t poke[256];
        GetPrivateProfileStringW(L"debug", L"fog_poke", L"", poke, (DWORD)std::size(poke), g_iniPath);
        gamefog::SetPoke(poke);
        wchar_t unit[260];
        GetPrivateProfileStringW(L"debug", L"engine_unit", L"", unit, (DWORD)std::size(unit), g_iniPath);
        engitem::SetWant(unit);
        // [hands] belt_models: the belt's potions as the game's own models on the hero (F4); where: up and the arc's radius in
        // parts of his eye height, degrees between columns, size, side; and shifted across him (parts of his eye height, + to his right)
        // and swung round him (degrees, + to his right) - the headset settings' Belt page and D2R VR Settings write all of them
        engitem::SetBelt(IniB(L"hands", L"belt_models", true), std::clamp(IniF(L"hands", L"belt_models_up", 0.57f), 0.0f, 1.5f),
                         std::clamp(IniF(L"hands", L"belt_models_ahead", 0.13f), -1.0f, 1.0f),
                         std::clamp(IniF(L"hands", L"belt_models_arc", 35.0f), 0.0f, 90.0f),
                         std::clamp(IniF(L"hands", L"belt_models_scale", 0.56f), 0.05f, 10.0f), IniF(L"hands", L"belt_models_side", -1.0f),
                         std::clamp(IniF(L"hands", L"belt_models_across", 0.0f), -0.5f, 0.5f), std::clamp(IniF(L"hands", L"belt_models_turn", 0.0f), -180.0f, 180.0f),
                         std::clamp(IniF(L"hands", L"belt_grab_cm", 12.0f), 4.0f, 30.0f),
                         IniB(L"hands", L"belt_grab", true), [] {
                             static float h[6];
                             const wchar_t* const k[6] = {L"belt_hold_x", L"belt_hold_y", L"belt_hold_z", L"belt_hold_pitch", L"belt_hold_yaw", L"belt_hold_roll"};
                             for (int i = 0; i < 6; ++i) h[i] = std::clamp(IniF(L"hands", k[i], 0.0f), i < 3 ? -30.0f : -180.0f, i < 3 ? 30.0f : 180.0f);
                             return (const float*)h;
                         }());
        {   // [hands] belt_ball_*: the lit ball's colour for each kind of potion (#RRGGBB; empty = the default)
            const wchar_t* const key[6] = {L"belt_ball_health", L"belt_ball_mana", L"belt_ball_stamina", L"belt_ball_rejuv",
                                           L"belt_ball_antidote", L"belt_ball_thawing"};
            const wchar_t* const def[6] = {L"#F21F1A", L"#4DA6FF", L"#F2F2F2", L"#C78CFF", L"#4DE64D", L"#FF8C26"};
            float rgb[6][3];
            for (int i = 0; i < 6; ++i) {
                wchar_t b[32];
                GetPrivateProfileStringW(L"hands", key[i], L"", b, (DWORD)std::size(b), g_iniPath);
                if (!b[0] || !ParseHexColor(b, rgb[i])) ParseHexColor(def[i], rgb[i]);
            }
            engitem::SetBallColours(rgb);
        }
        // [hands] belt_inner_glow: the game's own highlight on a bottle in reach (instead of the ball); belt_glow: its value (the game's own: -0.3 an item, 0.5 a hero)
        beltglow::Configure(IniB(L"hands", L"belt_inner_glow", false), std::clamp(IniF(L"hands", L"belt_glow", 1.0f), -0.9f, 4.0f));
        // [hands] belt_empty_dark / belt_empty_alpha: the emptied bottle darker and see-through (the game's highlight and transparency on its model entity)
        beltglow::ConfigureEmpty(IniB(L"hands", L"belt_empty_bottle", false), std::clamp(IniF(L"hands", L"belt_empty_dark", -0.35f), -0.9f, 0.0f),
                                 std::clamp(IniF(L"hands", L"belt_empty_alpha", 0.45f), 0.05f, 1.0f));
        beltglow::SetTrace(IniB(L"debug", L"belt_bottle_trace", false));
        // [debug] skip_pipeline: a game pipeline (hex, as Ctrl+F10's trace prints "last pipe") whose draws are
        // skipped - which pass draws something, found by elimination; nothing in the game's memory changes
        wchar_t sp[64];
        GetPrivateProfileStringW(L"debug", L"skip_pipeline", L"0", sp, (DWORD)std::size(sp), g_iniPath);
        static uint64_t toldSkip = 0;
        const uint64_t skip = wcstoull(sp, nullptr, 16);
        uitrace::SetSkip(skip);
        pipehunt::SetSkip(skip);   // (without ReShade: the plugin's own hooks, vr/pipe_hunt.h - a pixel / compute shader's hash, as Ctrl+F10's pipe trace prints it)
        if (skip != toldSkip) { toldSkip = skip; LogF("vrcam: [debug] skip_pipeline %llx", (unsigned long long)skip); }
        // [render] skip_shaders: the game's passes left out for good, by their pixel shader (Ctrl+F10's "ps"),
        // space-separated hex. Empty = none; the default is filled in once one is confirmed.
        wchar_t ss[512];
        GetPrivateProfileStringW(L"render", L"skip_shaders", L"", ss, (DWORD)std::size(ss), g_iniPath);
        uint64_t hs[8];
        int nh = 0;
        for (const wchar_t* q = ss; *q && nh < 8;) {
            wchar_t* end = nullptr;
            const uint64_t h = wcstoull(q, &end, 16);
            if (end == q) { ++q; continue; }
            if (h) hs[nh++] = h;
            q = end;
        }
        uitrace::SetSkipShaders(hs, nh);
        pipehunt::SetSkipShaders(hs, nh);   // (without ReShade, vr/pipe_hunt.h)
        static std::wstring toldShaders;
        if (toldShaders != ss) { toldShaders = ss; LogF("vrcam: [render] skip_shaders - %d shader(s) left out", nh); }
        {   // [render] bench_skip_<stage> / bench_skip_all: the game's own render stages left out, live, to measure each one's cost (vr/render_bench.h)
            uint32_t mask = IniB(L"render", L"bench_skip_all", false) ? renderbench::kAll : 0u;
            for (int k = 0; k < renderbench::kKeys; ++k) {
                wchar_t key[64];
                swprintf_s(key, L"bench_skip_%hs", renderbench::KeyName(k));
                if (IniB(L"render", key, false)) mask |= 1u << k;
            }
            // the players' [render] game_stages (0 as the game, 1 Low, 2 Potato) and [render] game_outline (0: the game's hover outline left out) on
            // top of the bench's keys - the Quality presets' (vr/render_bench.h StagesMask; F4 only, as every stage skip)
            const int stages = std::clamp((int)IniF(L"render", L"game_stages", 0.0f), 0, 2);
            // (the game's fog pass kept with [fog] game_fog=1: the game's own fog is the fog - Serg 2026-11-08)
            mask |= renderbench::StagesMask(stages, IniB(L"render", L"game_outline", true), IniB(L"fog", L"game_fog", false));
            pipehunt::SetBench(mask);
            // the local lights out: the sun alone (vr/bench_lights.h) - the bench's key, or the Potato stages
            benchlights::SetOn(IniB(L"render", L"bench_skip_point_lights", false) || stages >= 2);
            {   // the lit pass in its own colours times one light (vr/lit_flat.h): [render] potato_light (Performance > Render cost, the player's: its
                // _brightness 0.3 - 2, _colour 0 neutral / 1 warm / 2 cool / 3 the area's own), else the bench's [render] bench_lit_flat (_level,
                // _colour #RRGGBB, empty white) - kept as it was
                if (IniB(L"render", L"potato_light", false) || stages >= 2) {   // (the Potato stages take the sun's shadows away: never without it)
                    const int choice = std::clamp((int)IniF(L"render", L"potato_light_colour", 0.0f), 0, 3);
                    // (0.3 - 5 since Serg 2026-11-08: "яркость надо сделать ползунок больше 2"; underground its own, "для пещер отдельный ползунок
                    // яркости" - without the key, the open's level)
                    const float open = std::clamp(IniF(L"render", L"potato_light_brightness", 1.0f), 0.3f, 5.0f);
                    litflat::Configure(true, open, litflat::PresetColour(choice), choice == 3,
                                       std::clamp(IniF(L"render", L"potato_light_brightness_caves", open), 0.3f, 5.0f));
                } else {
                    wchar_t c[32];
                    GetPrivateProfileStringW(L"render", L"bench_lit_flat_colour", L"", c, (DWORD)std::size(c), g_iniPath);
                    const wchar_t* h = c[0] == L'#' ? c + 1 : c;
                    wchar_t* end = nullptr;
                    const unsigned long rgb = wcstoul(h, &end, 16);
                    litflat::Configure(IniB(L"render", L"bench_lit_flat", false), IniF(L"render", L"bench_lit_flat_level", 1.0f),
                                       (end && end != h && wcslen(h) == 6) ? (uint32_t)rgb : 0xFFFFFFu);
                }
            }
        }
        // [render] depth_fix: the game's linear depth (fog, height fog, AO, reflections, water) over 0 .. depth_range
        // world units from our camera instead of its own camera's 550 .. 1060 (uitrace.cpp, 2026-10-13; on since the
        // headset test the same day: the act 2 town's veil gone); 0 = the game's
        // [render] depth_fix_mode: 0 off (the game's), 1 each b10 as a list binds it (uitrace.cpp, D2R VR's), 2 once a frame at the
        // game's frame builder (depthsrc, after bearstonem/d2r-pcvr). Without the key: depth_fix says (1 = mode 1). Live.
        int depthMode = std::clamp((int)IniF(L"render", L"depth_fix_mode", IniB(L"render", L"depth_fix", true) ? 2.0f : 0.0f), 0, 2);   // (2 by default since 0.188)
        // [fog] game_fog: the game's fog pass reads its distances by this range - mode 2 (every pass alike, with or without ReShade;
        // mode 1 is ReShade's binds only, and may miss the fog pass's b10). Off (0) stays off: the game's fog then reads its own range.
        // (Mode 1 does nothing without ReShade - no binds of ReShade's to fix: the game's AO, reflections, water, lights and height fog stay on
        // its own camera's range there. game_fog raised it to 2, so "Sky and fog drawn by" changed the lighting too - Serg on 0.188.5,
        // 2026-10-29, 64 / 65.webp. Mode 2 is the player's choice in Settings: "How that depth is set" > "Once a frame where the game makes it".)
        if (g_set.gameFog.load() && depthMode == 1) depthMode = 2;
        suspects.depthFixMode = depthMode;
        if (patchhunt::DepthFixOff(hunt)) depthMode = 0;   // [debug] patch_hunt 5 / 9: the game's own depth range
        {   // the game's lighting left on its own range without ReShade: told once a change
            const bool idle = depthMode == 1 && (g_noReShade || IniB(L"render", L"no_reshade", false));
            static int toldIdle = -1;
            if ((idle ? 1 : 0) != toldIdle) {
                toldIdle = idle ? 1 : 0;
                if (idle)
                    Log("vrcam: [render] depth_fix_mode 1 does nothing without ReShade - the game's ambient occlusion, reflections, water, lights "
                        "and height fog keep its own camera's depth range; Settings > \"How that depth is set\" > \"Once a frame where the game "
                        "makes it\" (depth_fix_mode=2) gives them our camera's");
            }
        }
        const bool depthFix = depthMode == 1;
        const float depthRange = IniF(L"render", L"depth_range", 1000.0f);
        uitrace::SetDepthRange(depthFix, depthRange);
        depthsrc::Configure(depthMode == 2);
        static int toldDepth = -1;
        static float toldRange = 0.0f;
        static bool toldFixed = false;
        if (depthMode != toldDepth || depthRange != toldRange) {
            toldDepth = depthMode; toldRange = depthRange; toldFixed = false;
            static const char* const kModes[3] = {"0 - off, the game's own", "1 - each b10 as it is bound", "2 - once a frame at the game's frame builder"};
            LogF("vrcam: [render] depth_fix_mode %s, depth_range %.0f (mode 1 only; %d root signatures bind b10)", kModes[depthMode], depthRange,
                 uitrace::DepthRangeLayouts());
        }
        if (depthFix && !toldFixed && uitrace::DepthRangeFixes()) {
            toldFixed = true;
            LogF("vrcam: [render] depth_fix - the game's depth range rewritten (%u buffers so far)", uitrace::DepthRangeFixes());
        }
    }
    g_set.skyOn.store(IniB(L"sky", L"enabled", false));
    g_set.gameSky.store(IniB(L"sky", L"game_sky", false));
    g_set.gameSkybox.store(IniB(L"sky", L"game_skybox", false));
    g_set.summitBackdropHidden.store(IniB(L"sky", L"summit_backdrop_hidden", true));
    g_set.weatherFirstPerson.store(weatherplace::ClampMode((int)IniF(L"sky", L"weather_first_person", 0.0f)));
    g_set.domeFogHorizon.store(IniB(L"sky", L"dome_fog_horizon", true));
    g_set.domeFogBaked.store(IniB(L"sky", L"dome_fog_baked", true));
    g_set.domeFogBakedStrength.store(std::clamp(IniF(L"sky", L"dome_fog_baked_strength", 1.0f), 0.0f, 1.0f));
    g_set.domeTrueColour.store(IniB(L"sky", L"dome_true_colour", false));
    g_set.domeGainOnce.store(IniB(L"sky", L"dome_gain_once", false));
    g_set.effectWorld.store(IniB(L"render", L"effect_world", true));
    g_set.phantomRects.store(IniB(L"render", L"phantom_rects", false));   // (our effect's wrist-orb pass in rectangles: vr/phantom_rect.h; 0 = full screen - 0.208 ships it off)
    g_set.domeInGameColour.store(IniB(L"sky", L"dome_in_game_colour", true));
    g_set.domePastFar.store(IniB(L"sky", L"dome_past_far", false));
    g_set.gameSkyTest.store(std::clamp((int)IniF(L"debug", L"game_sky_test", 0.0f), 0, 4));   // another model for the dome (draws only)
    {   // the domes' radii in metres (shared/draw_distance.h: 4 world units a metre); an ini without them: the keys they took over from
        const float skyM = IniF(L"sky", L"game_sky_radius_m", NAN), caveM = IniF(L"sky", L"game_dome_caves_radius_m", NAN);
        const float sky = std::isfinite(skyM) ? drawdistance::ToUnits(skyM) : IniF(L"sky", L"game_sky_radius", 200.0f);
        const float caveShare = IniF(L"sky", L"game_dome_caves_size", NAN);   // (neither key: Serg's 200 m, 2026-10-31)
        const float cave = drawdistance::ToUnits(std::isfinite(caveM)       ? caveM
                                                 : std::isfinite(caveShare) ? drawdistance::CaveRadiusM(std::clamp(caveShare, 0.1f, 4.0f), g_set.modelRadius.load())
                                                                            : drawdistance::kCaveRadiusDefaultM);
        g_set.gameSkyRadius.store(std::clamp(sky, 1.0f, 1.0e8f));   // (the slider: 10 - 7500 m; the ini to 10^8 units)
        g_set.gameDomeCavesRadius.store(std::clamp(cave, 1.0f, 1.0e5f));
    }
    g_set.gameDomeCaves.store(IniB(L"sky", L"game_dome_caves", false));
    LoadSkyActs();
    LoadCrosshairFile();
    g_set.skyClouds.store(std::clamp(IniF(L"sky", L"clouds", 1.0f), 0.0f, 3.0f));
    g_set.skyDayNight.store(IniB(L"sky", L"day_night", true));
    g_set.skyDrift.store(IniB(L"sky", L"drift", false));
    g_set.lightDay.store(std::clamp(IniF(L"sky", L"light_day", 128.0f), 1.0f, 255.0f));
    g_set.lightNight.store(std::clamp(IniF(L"sky", L"light_night", 65.0f), 0.0f, 254.0f));
    g_set.nightBright.store(std::clamp(IniF(L"sky", L"night_brightness", 0.25f), 0.0f, 1.0f));
    {   // [sky] bright_<period>: Serg's own, one a period of the game's day (the game's order, env::Now)
        static const wchar_t* const kKeys[6] = {L"bright_dawn", L"bright_day_start", L"bright_day", L"bright_dusk", L"bright_night_start", L"bright_night"};
        for (int i = 0; i < 6; ++i) g_set.periodBright[i].store(std::clamp(IniF(L"sky", kKeys[i], 1.0f), 0.0f, 3.0f));
    }
    {   // the baked skies (vr/sky/three_skies.h: day, night, dawn / dusk; act 4 one), or the parked tinting ([sky] dome_tint)
        g_set.domeTint.store(IniB(L"sky", L"dome_tint", false));
        g_set.skyCrossfade.store(IniB(L"sky", L"sky_crossfade", false));
        static const wchar_t* const kKeys[4] = {L"three_bright_day", L"three_bright_night", L"three_bright_dawn_dusk", L"three_bright_act4"};
        for (int i = 0; i < 4; ++i) g_set.threeBright[i].store(std::clamp(IniF(L"sky", kKeys[i], 1.0f), 0.0f, 3.0f));
    }
    g_set.skyAlways.store(IniB(L"sky", L"always", false));
    g_set.arcaneSky.store(IniB(L"sky", L"arcane_sky", true));
    g_set.arcaneFog.store(IniB(L"sky", L"arcane_fog", false));
    g_set.skyHidePanels.store(IniB(L"sky", L"hide_with_panels", true));
    g_set.skyOverRims.store(IniB(L"sky", L"cover_rims", true));
    LoadOutdoorBiomes();
    g_set.ceilOn.store(IniB(L"ceiling", L"enabled", false));
    g_set.gameCeiling.store(IniB(L"ceiling", L"game_ceiling", false));
    g_set.gameCeilingWall.store(IniB(L"ceiling", L"game_ceiling_wall", false));
    g_set.gameCeilingBeams.store(IniB(L"ceiling", L"game_ceiling_beams", false));
    g_set.ceilFromWalls.store(IniB(L"ceiling", L"height_from_walls", true));   // (a room with no height of its own: on its walls)
    g_set.gameCeilingDome.store(IniB(L"ceiling", L"game_ceiling_dome", false));
    g_set.gameCeilingDomesTall.store(IniB(L"ceiling", L"game_ceiling_worldstone_dome", false));
    g_set.gameCeilingDomeSize.store(IniF(L"ceiling", L"game_ceiling_dome_size", 1.0f));
    g_set.gameCeilingDomeRise.store(IniF(L"ceiling", L"game_ceiling_dome_rise", 1.0f));
    g_set.worldstoneWiderM.store(IniF(L"ceiling", L"game_ceiling_worldstone_dome_wider_m", 10.0f));
    g_set.ceilHeight.store(kCeilHeightAlone);   // (the common [ceiling] height is not read: each room its own - Serg 2026-10-29)
    g_set.ceilScale.store(std::clamp(IniF(L"ceiling", L"scale", 10.0f), 0.5f, 1000.0f));
    g_set.ceilBright.store(std::clamp(IniF(L"ceiling", L"brightness", 1.0f), 0.0f, 4.0f));
    g_set.ceilRelief.store(std::clamp(IniF(L"ceiling", L"relief", 6.0f), 0.0f, 100.0f));
    g_set.ceilSteps.store(std::clamp((int)IniF(L"ceiling", L"steps", 12.0f), 1, 64));
    g_set.ceilLight.store(std::clamp(IniF(L"ceiling", L"light_radius", 25.0f), 1.0f, 1000.0f));
    g_set.ceilPictureFire.store(IniB(L"ceiling", L"fire_from_picture", false));
    g_set.ceilTorchBright.store(std::clamp(IniF(L"ceiling", L"torch_brightness", 0.6f), 0.0f, 5.0f));
    g_set.ceilTorchRadius.store(std::clamp(IniF(L"ceiling", L"torch_radius", 20.0f), 1.0f, 500.0f));
    g_set.ceilFireNearer.store(std::clamp(IniF(L"ceiling", L"fire_nearer", 0.0f), 0.0f, 200.0f));
    g_set.ceilHalo.store(std::clamp(IniF(L"ceiling", L"torch_halo", 1.0f), 0.0f, 1.0f));
    g_set.ceilWall.store(std::clamp(IniF(L"ceiling", L"wall_distance", 200.0f), 0.0f, 5000.0f));
    g_set.ceilFloor.store(std::clamp(IniF(L"ceiling", L"floor_depth", 5.0f), 0.0f, 500.0f));
    g_set.ceilWet.store(std::clamp(IniF(L"ceiling", L"wet", 0.35f), 0.0f, 3.0f));
    g_set.ceilDetail.store(std::clamp(IniF(L"ceiling", L"detail", 0.8f), 0.0f, 10.0f));
    g_set.ceilContrast.store(std::clamp(IniF(L"ceiling", L"contrast", 1.6f), 0.0f, 5.0f));
    LoadCeilingBiomes();
    LoadCeilingTextures();
    cavelight::Load();
    g_set.armsMode.store(std::clamp((int)IniF(L"arms", L"mode", 0.0f), 0, 2));
    g_set.hideHead.store(IniB(L"arms", L"hide_head", false));
    headshadow::Configure(IniB(L"arms", L"head_shadow", true));   // the hidden head still casts its shadow: shrunk in the eyes' skinning only (vr/head_shadow.h), F4 only
    g_set.armScale.store(std::clamp(IniF(L"arms", L"scale", 1.0f), 0.3f, 3.0f));
    g_set.fullReach.store(IniB(L"arms", L"full_reach", true));
    g_set.defaultSpan.store(IniB(L"calibration", L"default_span", true));
    g_set.poseOrderLog.store(IniB(L"debug", L"pose_order", false));
    g_set.flatPickLog.store(IniB(L"debug", L"flat_pick", false));   // docs/notes/flat_targeting.md
    g_set.frameLog.store(IniB(L"debug", L"frame_log", false));
    frametrace::SetOn(IniB(L"debug", L"frame_trace", false));   // a slow pair's time and the plugin's part of it (vr/frame_trace.h); measures only
    {   // [debug] call_census (vr/native_d3d12.h): the game's D3D12 calls through our hooks counted, a line every 10 s; view only, live
        static int was = -1;
        const bool on = IniB(L"debug", L"call_census", false);
        natived3d::SetCensus(on);
        if ((int)on != was) {
            if (was >= 0 || on)
                Log(on ? "vrcam: call census on - the game's draws, pipelines, binds, barriers... through our hooks counted, a line every 10 s ([debug] call_census)"
                       : "vrcam: call census off ([debug] call_census)");
            was = on;
        }
    }
    health::SetOn(IniB(L"debug", L"health", false));   // one line per feature every 10 s, a mark where a chain is broken (vr/health.h); counts only
    ftraceSetOverMs(IniF(L"debug", L"frame_trace_ms", (float)frametrace::kDefaultOverMs));
    {   // [debug] log_async (vr/log_async.h): the log lines written by a thread of their own; off = each thread writes its own, as ever
        static int logAsyncWas = 0;
        const bool want = IniB(L"debug", L"log_async", false);
        if ((int)want != logAsyncWas) {
            logAsyncWas = (int)want;
            const bool ok = logasync::Enable(g_ctx, want);
            if (want) Log(ok ? "vrcam: log_async on - the log lines are written by a thread of their own, in order ([debug] log_async)" : "vrcam: log_async could not start its thread - the lines are written as before");
            else Log(logasync::Enabled() ? "vrcam: log_async off - but the log's file was found slow on this PC: its thread goes on writing the lines ([debug] log_async_auto)"
                                         : "vrcam: log_async off - each thread writes its own lines");
        }
        // [debug] log_async_auto (vr/log_slow.h): the lines a thread writes itself are timed; a slow log's file (half of 16 lines 5 ms or more) hands
        // them to that thread for the run. Off = as before.
        static int autoWas = -1;
        const bool autoWant = IniB(L"debug", L"log_async_auto", true);
        if ((int)autoWant != autoWas) {
            autoWas = (int)autoWant;
            logasync::SetAuto(autoWant);
            if (!autoWant) Log(logasync::Enabled() ? "vrcam: log_async_auto off - the lines stay with log_async's thread ([debug] log_async=1)"
                                                   : "vrcam: log_async_auto off - the log's speed is not looked at");
        }
    }
    g_set.findPrevMatrix.store(IniB(L"debug", L"find_prev_matrix", false));
    g_set.dlssPrevFix.store(IniB(L"render", L"dlss_prev_fix", false));
    g_set.dlssMv.store(std::clamp((int)IniF(L"render", L"dlss_mv", 2.0f), 0, 2));
    dlssmv::SetMode(g_set.dlssMv.load());
    // [render] dlss_mv 2: DLSS gets the same eye's jitter step and, in the far part, the camera's turn.
    // dlss_mv_sign: the camera's part's sign in the game's vectors (-1); dlss_mv_lag: the picture's view,
    // evaluations back (0); dlss_mv_far_turn: how much of the turn the far part gets (1);
    // dlss_mv_near: the near part redone from the eyes' parallax (off: it swam on grass); dlss_mv_jitter (on).
    dlssmv::SetSign(IniF(L"render", L"dlss_mv_sign", -1.0f));
    dlssmv::SetLag((int)IniF(L"render", L"dlss_mv_lag", 0.0f));
    dlssmv::SetFarTurn(IniF(L"render", L"dlss_mv_far_turn", 1.0f));
    dlssmv::SetFixes(IniB(L"render", L"dlss_mv_near", false), IniB(L"render", L"dlss_mv_jitter", true));
    g_set.phantomRay.store(IniB(L"debug", L"phantom_ray", false));
    g_set.boneAxes.store(IniB(L"debug", L"bone_axes", false));
    g_set.weaponDiag.store(IniB(L"debug", L"weapon_diag", false));
    {
        wchar_t sb[48] = {};
        GetPrivateProfileStringW(L"debug", L"shrink_bone", L"", sb, (DWORD)std::size(sb), g_iniPath);
        char nb[48] = {};
        WideCharToMultiByte(CP_UTF8, 0, sb, -1, nb, (int)sizeof nb - 1, nullptr, nullptr);
        memcpy(g_set.shrinkBone, nb, sizeof nb);
    }
    {   // [debug] matrix_writer: switched on = one 3 s watch of who writes the hero's matrix
        static bool was = false;
        const bool now = IniB(L"debug", L"matrix_writer", false);
        if (now && !was) StartMatrixWatch();
        was = now;
    }
    g_set.bodywalkPad.store(IniB(L"input", L"bodywalk_pad", true));
    g_set.aAttackOnly.store(IniB(L"input", L"a_attack_only", false));
    g_set.flatKeyMove.store(IniB(L"input", L"flat_keyboard_move", true));
    g_set.vrKeyWalk.store(IniB(L"input", L"vr_keys_walk", true));
    g_set.directWalk.store(IniB(L"input", L"direct_walk", true));
    g_set.walkByLean.store(IniB(L"direct", L"walk_by_lean", false));
    g_set.walkByLeanLimit.store(leangait::ClampLimit(IniF(L"direct", L"walk_by_lean_pct", leangait::kDefaultLimit * 100.0f) / 100.0f));
    g_set.bwWalkLift.store(std::clamp(IniF(L"input", L"bodywalk_walk_lift", 0.0f), 0.0f, 0.31f));
    g_set.walkStartLog.store(IniB(L"debug", L"walk_start_log", false));
    g_set.flatNoPad.store(IniB(L"input", L"flat_no_pad", true));
    g_set.flatPadOnTouch.store(IniB(L"input", L"flat_pad_on_touch", true));
    g_set.flatClickShoot.store(IniB(L"input", L"flat_click_shoot", true));
    g_set.flatAimGround.store(IniB(L"input", L"flat_aim_ground", true));
    g_set.flatBodyPick.store(IniB(L"input", L"flat_body_pick", true));
    g_set.flatBodyTarget.store(IniB(L"input", L"flat_body_target", true));
    g_set.vrBodyPick.store(IniB(L"input", L"vr_body_pick", true));
    g_set.vrBodyPickHand.store(IniB(L"input", L"vr_body_pick_hand", false));
    meleetouch::SetPickWanted(g_set.vrBodyPickHand.load());   // vr_body_pick_hand: the monsters near the hero and the server's verdicts (F4)
    g_set.pointPickup.store(IniB(L"input", L"point_pickup", true));
    g_set.pointPickupLabelPct.store(std::clamp(IniF(L"input", L"point_pickup_label_pct", 250.0f), 100.0f, 400.0f));
    g_set.pointPickupRay.store(IniB(L"debug", L"point_pickup_ray", false));
    g_set.pointPickupArmCm.store(std::clamp(IniF(L"input", L"point_pickup_arm_cm", 12.0f), 0.0f, 40.0f));
    g_set.pointPickupReach.store(std::clamp(IniF(L"input", L"point_pickup_reach_cells", 0.0f), 0.0f, unleashed::lootaim::ReachCells));
    g_set.flatPortalNoOval.store(IniB(L"input", L"flat_portal_no_oval", true));
    g_set.portalNoOval.store(IniB(L"render", L"portal_no_oval", false));
    g_set.portalLookHorizon.store(IniB(L"input", L"portal_look_horizon", false));
    g_set.portalAuto.store(IniB(L"input", L"portal_auto", false));
    g_set.portalAutoRadius.store(std::clamp(IniF(L"input", L"portal_auto_radius", 4.0f), 1.0f, 10.0f));
    g_set.portalAutoDelay.store(std::clamp((int)IniF(L"input", L"portal_auto_delay_ms", 150.0f), 0, 2000));
    // (was keep_room_heights, on, until 0.188.5: renamed so an upgrade's ini gets the new key off - the installer keeps old values)
    g_set.keepRoomHeights.store(IniB(L"render", L"floors_stay", false));
    g_set.flatCrosshair.store(IniB(L"input", L"flat_crosshair", true));
    g_set.inventoryPad.store(IniF(L"input", L"inventory_button", 0.0f) != 0.0f ? 1 : 0);
    if (const bool one = IniF(L"input", L"interact_one_press", 0.0f) != 0.0f; one != g_set.interactOnePress.exchange(one))
        Log(one ? "vrcam: pick up / interact - one target per press (VR first person; [input] interact_one_press)"
                : "vrcam: pick up / interact - held, it takes whatever is under it ([input] interact_one_press=0)");
    g_set.facingSign.store(IniF(L"arms", L"facing_sign", 1.0f) < 0 ? -1.0f : 1.0f);
    g_set.facingSource.store(IniF(L"hands", L"facing_source", 1.0f) != 0.0f ? 1 : 0);
    g_set.facingHoldMs.store(std::clamp(IniF(L"hands", L"facing_hold_ms", 4000.0f), 0.0f, 60000.0f));
    g_set.dollTrace.store(IniB(L"debug", L"doll_trace", false));
    g_set.dollRebuild.store(IniB(L"hands", L"doll_rebuild", true));
    g_set.bodyTurn.store(std::clamp((int)IniF(L"body", L"turn", 2.0f), 0, 2));
    g_set.thirdTurn.store(IniB(L"body", L"turn_third", true));
    {
        skel::SetGaitCone(IniF(L"body", L"gait_cone_deg", 45.0f));   // the game's own walk within +- this of the facing, the gait's outside it
        const bool third = IniB(L"body", L"gait_legs_third", false), f4 = IniB(L"body", L"gait_legs_f4", false);
        if (g_set.gaitThird.exchange(third) != third || g_set.gaitF4.exchange(f4) != f4)
            LogF("vrcam: our hero's legs by the gait - from behind %s, F4 %s ([body] gait_legs_third / gait_legs_f4; needs d2r_vr_gait.dll)", third ? "on" : "off", f4 ? "on" : "off");
    }
    g_set.lockUpper.store(IniB(L"body", L"lock", true));
    g_set.legsUnder.store(IniB(L"body", L"legs_under_body", true));
    g_set.flatHoldTrunk.store(IniB(L"body", L"flat_hold_trunk", false));
    g_set.flatHoldTrunkTwist.store(IniB(L"body", L"flat_hold_trunk_twist", false));
    g_set.attackLean.store(IniB(L"body", L"attack_lean", false));
    g_set.attackLeanMaxDeg.store(std::clamp(IniF(L"body", L"attack_lean_max_deg", 40.0f), 0.0f, 90.0f));
    g_set.flatAimTilt.store(IniB(L"body", L"flat_aim_tilt", false));
    g_set.flatAimTiltPct.store(aimtilt::Pct(IniF(L"body", L"flat_aim_tilt_pct", aimtilt::kPctDefault)));
    g_set.flatAimTiltMaxDeg.store(aimtilt::MaxDeg(IniF(L"body", L"flat_aim_tilt_max_deg", aimtilt::kMaxDegDefault)));
    g_set.flatAimTiltTarget.store(IniB(L"body", L"flat_aim_tilt_target", true));
    g_set.flatWeaponAdjust.store(IniB(L"body", L"flat_weapon_adjust", true));
    g_set.walkBack.store(IniB(L"body", L"walk_back", true));
    g_set.walkBackReverse.store(IniB(L"body", L"walk_back_reverse", true));
    g_set.dollFeet.store(IniB(L"body", L"doll_feet", false));
    g_set.legsRear.store(IniB(L"body", L"legs_rear", false));
    g_set.legsRearCycle.store(IniB(L"body", L"legs_rear_cycle", false));
    g_set.legsFrontConeDeg.store(std::clamp(IniF(L"body", L"legs_front_cone_deg", 90.0f), 0.0f, 360.0f));
    g_set.legsRearConeDeg.store(std::clamp(IniF(L"body", L"legs_rear_cone_deg", 90.0f), 0.0f, 360.0f));
    g_set.legsSideGame.store(IniB(L"body", L"legs_side_game", true));
    g_set.legsTurn.store(IniB(L"body", L"legs_turn", true));
    g_set.legsSideRagdoll.store(IniB(L"body", L"legs_side_ragdoll", false));
    g_set.legsSideGait.store(IniB(L"body", L"legs_side_gait", false));
    g_set.legsSideCross.store(IniB(L"body", L"legs_side_cross", true));
    // ... the hero drawn from a copy of the pose as we finished it (the skinning met the game's own legs); [body] attack_lean's bend and
    // flat_weapon_adjust's arms go the same way, and flat_aim_tilt's
    headshadow::WantForPose(g_set.legsRear.load() || g_set.attackLean.load() ||
                            ((g_set.flatWeaponAdjust.load() || g_set.flatAimTilt.load()) && g_set.platform.load() == 0));
    g_set.underCamera.store(IniB(L"body", L"under_camera", true));
    g_set.leapCameraUp.store(IniB(L"body", L"leap_camera_up", false));
    {   // [body] leap_camera_up_pct: an ini with no _pct key keeps what leap_camera_up said (on = the whole arc)
        const float pct = IniF(L"body", L"leap_camera_up_pct", -1.0f);
        g_set.leapCameraPct.store(undercam::RisePct(pct >= 0.0f, pct, g_set.leapCameraUp.load()));
    }
    g_set.crouch.store(IniB(L"body", L"crouch", false));
    g_set.crouchCm.store(std::clamp(IniF(L"body", L"crouch_cm", 20.0f), 5.0f, 60.0f));
    if (const bool forms = IniB(L"shapeshift", L"forms_first_person", false); forms != g_set.formsFirstPerson.exchange(forms))
        Log(forms ? "vrcam: a druid's werewolf / werebear drawn as our body in F4, grafted under the camera ([shapeshift] forms_first_person)"
                  : "vrcam: a druid's werewolf / werebear left to the game ([shapeshift] forms_first_person=0)");
    if (const bool claws = IniB(L"shapeshift", L"claw_hits", false); claws != g_set.clawHits.exchange(claws))
        Log(claws ? "vrcam: a werewolf / werebear's paws strike by touch in F4 - the game's own attack, one recovery for both paws ([shapeshift] claw_hits; needs forms_first_person and melee_touch)"
                  : "vrcam: a werewolf / werebear's paws do not strike by touch ([shapeshift] claw_hits=0)");
    g_set.formHeight[0].store(std::clamp(IniF(L"shapeshift", L"wolf_height", 100.0f), 50.0f, 150.0f) * 0.01f);
    g_set.formHeight[1].store(std::clamp(IniF(L"shapeshift", L"bear_height", 100.0f), 50.0f, 150.0f) * 0.01f);
    g_set.formStrengthPct[0].store(formstrength::ClampPct(IniF(L"shapeshift", L"wolf_strength_pct", formstrength::kWolfDefaultPct)));   // the paws in the grab (vr/form_strength.h)
    g_set.formStrengthPct[1].store(formstrength::ClampPct(IniF(L"shapeshift", L"bear_strength_pct", formstrength::kBearDefaultPct)));
    g_set.handUp.store(std::clamp(IniF(L"arms", L"up", 0.0f), -50.0f, 50.0f));
    g_set.handFwd.store(std::clamp(IniF(L"arms", L"forward", 0.0f), -50.0f, 50.0f));
    g_set.handSide.store(std::clamp(IniF(L"arms", L"side", 0.0f), -50.0f, 50.0f));
    g_set.handsFollowCam.store(std::clamp(IniF(L"arms", L"follow_camera", 100.0f), 0.0f, 100.0f) / 100.0f);
    g_set.bowMode.store(std::clamp((int)IniF(L"bow", L"mode", 4.0f), 0, 4));
    g_set.turnMs.store(std::clamp(IniF(L"bow", L"turn_ms", 80.0f), 20.0f, 400.0f));
    g_set.aimRange.store(std::clamp(IniF(L"bow", L"range", 20.0f), 3.0f, aimpoint::kMaxCells));
    g_set.aimYawDeg.store(std::clamp(IniF(L"bow", L"aim_yaw", 0.0f), -30.0f, 30.0f));
    g_set.staffAimYawDeg.store(std::clamp(IniF(L"bow", L"staff_aim_yaw", 0.0f), -45.0f, 45.0f));
    {
        // its own 0 when unset: a crossbow shoots along the controller's -Z, not round a bow's grip
        g_set.xbowAimYawDeg.store(std::clamp(IniF(L"bow", L"xbow_aim_yaw", 0.0f), -45.0f, 45.0f));
    }
    g_set.xbowTwoHands.store(IniB(L"bow", L"xbow_two_hands", true));
    g_set.xbowHand.store(IniF(L"hands", L"xbow_hand", 1.0f) != 0.0f ? 1 : 0);   // the game hangs it on the left wrist (seen 2026-10-05)
    shoulderwire::Configure(IniB(L"hands", L"shoulder_swap", false), IniF(L"hands", L"shoulder_swap_hand", 0.0f) != 0.0f);   // 0 right, 1 left
    g_set.shoulderDraw.store(IniB(L"hands", L"shoulder_draw", false));
    g_set.shieldDraw.store(IniB(L"hands", L"shoulder_draw_shield", true));
    g_set.shieldStow.store(IniB(L"hands", L"shoulder_draw_shield_stow", false));
    stowedblock::Configure(IniB(L"hands", L"shield_stow_no_block", false));
    spatialaudio::Configure(IniB(L"audio", L"head_follow", true), IniB(L"audio", L"listener_turn", true),
                            IniF(L"audio", L"forward_sign", -1.0f), IniF(L"audio", L"depth_scale", 2.0f));
    g_set.rangedHolster.store(IniB(L"hands", L"shoulder_draw_ranged", false));
    g_set.rangedSwap.store(IniB(L"hands", L"shoulder_draw_ranged_swap", false));
    g_set.bowSwapByGrab.store(IniB(L"bow", L"swap_by_grab", false));
    {   // [bow] bow_hand: "1" / "0" as written (Settings' choice), "left" / "right" by hand
        wchar_t side[16] = {};
        GetPrivateProfileStringW(L"bow", L"bow_hand", L"1", side, (DWORD)std::size(side), g_iniPath);
        g_set.bowHandSide.store(bowswap::ParseSide(side, 1));
    }
    bowdrawwire::SetFreshOnHandChange(g_set.bowSwapByGrab.load());   // [bow] swap_by_grab: a control held through the bow's change of hands is not a fresh press at the string
    g_set.shoulderDrawStowMs.store(std::clamp(IniF(L"hands", L"shoulder_draw_stow_ms", 1000.0f), 0.0f, 5000.0f));
    // 0 the attack, 1..4 B X Y RB, 5..8 with LT; what takes the string: 0 the trigger, 1 the grip, 2 either; the touch point from the bow's own string; the debug view
    bowdrawwire::Configure(IniB(L"hands", L"bow_draw", false), (int)IniF(L"hands", L"bow_draw_button", 0.0f), (int)IniF(L"hands", L"bow_draw_start", 0.0f),
                           IniB(L"hands", L"bow_draw_nock", true), IniB(L"debug", L"bow_vectors", false), IniF(L"hands", L"bow_draw_reach_cm", 28.0f), IniF(L"hands", L"bow_draw_nock_shift_cm", 0.0f), IniF(L"hands", L"bow_draw_min_pull_cm", 12.0f),
                           IniB(L"hands", L"bow_draw_only", true));
    bowdrawwire::SetAimRay(IniB(L"debug", L"bow_aim_ray", false));   // [debug] bow_aim_ray: where the arrow goes, a long ray in the headset
    bowpitchwire::Configure(IniB(L"bow", L"pitch_aim", false), IniB(L"debug", L"bow_pitch_trace", false));   // [bow] pitch_aim: the shot to what its line meets; its log line a shot
    // [bow] arrow_flight: the shot arrow drawn by us from the bow along the shot's line (visual only); its trace, and its test switch for the arrow's effects
    arrowflightwire::Configure(IniB(L"bow", L"arrow_flight", false), IniB(L"debug", L"arrow_flight_trace", false), IniB(L"debug", L"arrow_flight_fx_off", false),
                               IniB(L"bow", L"arrow_flight_straight", false));
    {   // [bow] quiver: a button of the string hand, while the string is drawn, loads the next / previous bow skill (the controls: [direct] quiver_next / quiver_prev)
        auto name = [](const wchar_t* key, const wchar_t* def, char* out, size_t n) {
            wchar_t w[48];
            GetPrivateProfileStringW(L"direct", key, def, w, (DWORD)std::size(w), g_iniPath);
            size_t i = 0;
            for (; w[i] && i + 1 < n; ++i) out[i] = w[i] < 128 ? (char)w[i] : '?';
            out[i] = 0;
        };
        char next[48], prev[48];
        name(L"quiver_next", L"string_secondary", next, sizeof next);
        name(L"quiver_prev", L"string_primary", prev, sizeof prev);
        bowquiverwire::Configure(IniB(L"bow", L"quiver", false), IniB(L"bow", L"quiver_orbs", false), IniB(L"bow", L"quiver_orb_trigger", true), next, prev);   // (quiver_reach, the quiver behind the shoulder, is gone: the key is no longer read)
        // [bow] quiver_orb_*: the orbs' sliders (cm), live: size, grab radius, where the row starts on the stave, the spacing, the row's shift along the bow's axes (across, along, toward the archer),
        // and its turn about them (degrees, on its first orb)
        bowquiverwire::SetOrbTuning(IniF(L"bow", L"quiver_orb_size_cm", 2.2f), IniF(L"bow", L"quiver_orb_reach_cm", 7.5f), IniF(L"bow", L"quiver_orb_start_cm", 18.0f),
                                    IniF(L"bow", L"quiver_orb_spacing_cm", 6.5f), IniF(L"bow", L"quiver_orb_x_cm", 0.0f), IniF(L"bow", L"quiver_orb_y_cm", 0.0f), IniF(L"bow", L"quiver_orb_z_cm", 0.0f),
                                    IniF(L"bow", L"quiver_orb_rot_x_deg", 0.0f), IniF(L"bow", L"quiver_orb_rot_y_deg", 0.0f), IniF(L"bow", L"quiver_orb_rot_z_deg", 0.0f));
        // [bow] arrow_fx: the nocked arrow's fire, frost or arcane light by the loaded skill (the effect's ARROWFX block), live; its size and brightness in percent
        bowquiverwire::SetArrowFx(IniB(L"bow", L"arrow_fx", true), IniF(L"bow", L"arrow_fx_size_pct", 100.0f), IniF(L"bow", L"arrow_fx_bright_pct", 100.0f),
                                  IniF(L"bow", L"arrow_fx_along_cm", 0.0f));
        // [bow] quiver_plain_no_mana: an armed arrow skill the hero cannot pay (too little mana by the game's own numbers) shoots the plain arrow at the release instead of the game's refusal; live
        bowquiverwire::SetPlainNoMana(IniB(L"bow", L"quiver_plain_no_mana", true));
        // [bow] arrow_fire_cut: the arrow's effect goes out at the release that shoots, at once or arrow_fire_cut_ms after it (vr/arrow_fire_cut.h); live
        bowquiverwire::SetArrowFireCut(IniB(L"bow", L"arrow_fire_cut", true), IniF(L"bow", L"arrow_fire_cut_ms", 0.0f));
        bowquiverwire::SetArrowFxNeedsMana(IniB(L"bow", L"arrow_fx_needs_mana", true));   // [bow] arrow_fx_needs_mana: the arrow's effect goes out while the hero cannot pay the armed skill
        bowquiverwire::SetIcon(IniB(L"bow", L"quiver_icon", true));   // [bow] quiver_icon: the armed arrow skill's icon in the mana medallion (vr/armed_icon.h)
    }
    g_set.bowAimAlong.store(IniB(L"bow", L"aim_along_hands", false));
    g_set.bowAimBlendMs.store(std::clamp(IniF(L"bow", L"aim_blend_ms", 120.0f), 50.0f, 400.0f));
    g_set.bowAimRoll.store(IniF(L"bow", L"aim_roll", 0.0f) >= 0.5f ? 1 : 0);
    {   // [bow] arrow_rest_*: the arrow's rest point in the bow hand's frame. The z of it began as arrow_lift_cm (the arrow lifted off the line,
        // 0..8, 2.5): when the new key is not there that key is read if it was set to something else than its own default, so nothing set is
        // lost; else z starts at 5 cm - about the top of the fist - and the arrow is kept clear of the arm besides (arrow_rest_clear)
        const float legacy = IniF(L"bow", L"arrow_lift_cm", -1.0f);
        const float zStart = legacy >= 0.0f && std::fabs(legacy - 2.5f) > 1e-3f ? std::clamp(legacy, 0.0f, 8.0f) : 5.0f;
        g_set.bowRestCm[0].store(std::clamp(IniF(L"bow", L"arrow_rest_x_cm", 0.0f), -10.0f, 10.0f));
        g_set.bowRestCm[1].store(std::clamp(IniF(L"bow", L"arrow_rest_y_cm", 0.0f), -10.0f, 10.0f));
        g_set.bowRestCm[2].store(std::clamp(IniF(L"bow", L"arrow_rest_z_cm", zStart), -10.0f, 10.0f));
        g_set.bowRestClear.store(IniB(L"bow", L"arrow_rest_clear", true));
        g_set.bowRestModel.store(IniB(L"bow", L"arrow_rest_model", false));
        g_set.bowRestAims.store(IniB(L"bow", L"arrow_rest_aims", true));
        bowdrawwire::SetRestAims(g_set.bowRestAims.load());
        bowdrawwire::SetShotLineHold(IniB(L"bow", L"shot_line_hold", false));   // [bow] shot_line_hold: the shot's line is its aim until the game takes it
    }
    g_set.bowHandPose.store(IniB(L"bow", L"hand_pose", false));
    {   // with the string hand's pose off the hand is the game's own animation of it - the hand at the string, whatever the player does: said once
        static bool toldOff = false;
        const bool off = !g_set.bowHandPose.load();
        if (off != toldOff) {
            toldOff = off;
            if (off) Log("vrcam: [bow] hand_pose is OFF: the hand that takes a bow's string shows the game's own animation of it (at the string) - tick \"The hand that takes the string: relaxed, fingers hooked on it\" in Weapon Adjust > Bow for a relaxed hand until the string is taken");
        }
    }
    g_set.bowHook.store(std::clamp(IniF(L"bow", L"hook_close", 85.0f), 0.0f, 100.0f) * 0.01f);
    g_set.bowHookGame.store(IniB(L"bow", L"hook_game_pose", true));
    g_set.bowHookCurl.store(std::clamp(IniF(L"bow", L"hook_curl_pct", 100.0f), 0.0f, 150.0f) * 0.01f);
    g_set.bowHookThumbDeg.store(std::clamp(IniF(L"bow", L"hook_thumb_deg", 0.0f), -60.0f, 60.0f));
    g_set.bowStringAnchor.store(IniB(L"bow", L"string_anchor", true));
    g_set.bowAnchorCm[0].store(std::clamp(IniF(L"bow", L"string_anchor_along_cm", 0.0f), -15.0f, 15.0f));
    g_set.bowAnchorCm[1].store(std::clamp(IniF(L"bow", L"string_anchor_across_cm", 0.0f), -15.0f, 15.0f));
    g_set.bowAnchorCm[2].store(std::clamp(IniF(L"bow", L"string_anchor_palm_cm", 0.8f), -15.0f, 15.0f));
    // +y: what the grip gave when the crossbow lay right (log 2026-10-05); taken again in
    // another pose it gave another axis and the model jumped a quarter turn
    g_set.xbowStockAxis.store(std::clamp((int)IniF(L"hands", L"xbow_stock_axis", 2.0f), -3, 3));
    g_set.xbowGunFrame.store(IniB(L"hands", L"xbow_gun_frame", true));
    g_set.xbowLine[0].store(std::clamp(IniF(L"hands", L"xbow_line_pitch", 34.0f), -180.0f, 180.0f));
    g_set.xbowLine[1].store(std::clamp(IniF(L"hands", L"xbow_line_yaw", 15.0f), -180.0f, 180.0f));
    g_set.xbowLine[2].store(std::clamp(IniF(L"hands", L"xbow_line_roll", 0.0f), -180.0f, 180.0f));
    g_set.xbowRangeOne.store(std::clamp(IniF(L"bow", L"xbow_range_one", 5.0f), 2.0f, aimpoint::kMaxCells));
    g_set.xbowRangeTwo.store(std::clamp(IniF(L"bow", L"xbow_range_two", 30.0f), 2.0f, aimpoint::kMaxCells));
    g_set.staffRangeOne.store(std::clamp(IniF(L"bow", L"staff_range_one", 5.0f), 2.0f, aimpoint::kMaxCells));
    g_set.casterRange.store(std::clamp(IniF(L"bow", L"caster_range", 20.0f), 0.0f, aimpoint::kMaxCells));
    g_set.handRange.store(std::clamp(IniF(L"bow", L"hand_range", 5.0f), 0.0f, aimpoint::kMaxCells));
    g_set.aimByArm.store(IniB(L"bow", L"aim_distance_by_arm", false));
    g_set.aimNearCells.store(std::clamp(IniF(L"bow", L"aim_near_cells", 3.0f), armreach::kNearMinCells, 20.0f));
    g_set.staffRangeTwo.store(std::clamp(IniF(L"bow", L"staff_range_two", 30.0f), 2.0f, aimpoint::kMaxCells));
    g_set.bowHand.store(IniF(L"bow", L"set1_left", 1.0f) != 0.0f ? 1 : 0);
    g_set.bowHand2.store(IniF(L"bow", L"set2_left", 0.0f) != 0.0f ? 1 : 0);
    g_set.rangedLeft.store(IniB(L"bow", L"ranged_left", true));
    g_set.staffTwoHands.store(IniB(L"bow", L"staff_two_hands", true));
    g_set.staffHands.store(IniB(L"hands", L"follow_staff", true));
    g_set.staffFreeLeft.store(IniB(L"hands", L"staff_free_left", true));
    g_set.spellAim.store(IniB(L"hands", L"spell_aim", true));
    g_set.spellAimModel.store(IniB(L"hands", L"spell_aim_model", false));
    g_set.staffUprightArm.store(IniB(L"hands", L"staff_upright_arm", false));
    g_set.staffUprightFullDeg.store(std::clamp(IniF(L"hands", L"staff_upright_full_deg", staffupright::kFullDeg), 0.0f, 60.0f));
    g_set.staffUprightNoneDeg.store(std::clamp(IniF(L"hands", L"staff_upright_none_deg", staffupright::kNoneDeg), 0.0f, 80.0f));
    g_set.spellAimRay.store(IniB(L"debug", L"spell_aim_ray", false));
    g_set.aimNoChase.store(IniB(L"gameplay", L"aim_no_chase", true));
    g_set.leapByGaze.store(IniB(L"gameplay", L"leap_by_gaze", true));
    meleetouch::SetLeapWanted(g_set.leapByGaze.load());   // Leap Attack's monster under the gaze: the monsters near the hero are read (a Barbarian in F4)
    g_set.teleportCircle.store(IniB(L"gameplay", L"teleport_circle", true));
    g_set.leapArc.store(IniB(L"gameplay", L"leap_arc", false));
    g_set.leapArcHead.store(IniB(L"gameplay", L"leap_arc_head", false));
    g_set.leapAirAim.store(IniB(L"gameplay", L"leap_air_aim", false));
    g_set.leapHeadGain.store(pointcast::HeadGain(IniF(L"gameplay", L"leap_head_gain", pointcast::kDefaultHeadGain)));
    g_set.pointMaxCells.store(pointcast::CapCells(IniF(L"gameplay", L"point_max_cells", pointcast::kDefaultCapCells)));
    g_set.pointArcHeightPct.store(pointcast::HeightScale(IniF(L"gameplay", L"point_arc_height", pointcast::kDefaultHeightPct)) * 100.0f);
    g_set.pointArcOpacity.store(pointcast::Opacity(IniF(L"gameplay", L"point_arc_opacity", pointcast::kDefaultOpacityPct)));
    {   // [gameplay] point_arc_color: #RRGGBB; empty or unreadable = the red of pointcast::kDefaultRgb
        wchar_t c[32];
        GetPrivateProfileStringW(L"gameplay", L"point_arc_color", L"", c, (DWORD)std::size(c), g_iniPath);
        float rgb[3];
        const bool given = c[0] && ParseHexColor(c, rgb);
        if (!given) for (int i = 0; i < 3; ++i) rgb[i] = pointcast::kDefaultRgb[i];
        for (int i = 0; i < 3; ++i) g_set.pointArcRgb[i].store(rgb[i]);
        g_set.pointArcAllSet.store(given);
    }
    g_set.pointArcBySkill.store(IniB(L"gameplay", L"point_arc_by_skill", true));
    {   // [gameplay] point_arc_color_<id>: a skill's own colour - the section read once, its point_arc_color_<n>= lines picked out (not 1024 reads of the file)
        uint32_t own[1024] = {};
        static wchar_t sec[32768];
        const DWORD got = GetPrivateProfileSectionW(L"gameplay", sec, (DWORD)std::size(sec), g_iniPath);
        for (const wchar_t* line = sec; got && *line; line += wcslen(line) + 1) {
            constexpr wchar_t kKey[] = L"point_arc_color_";
            constexpr size_t kLen = std::size(kKey) - 1;
            if (_wcsnicmp(line, kKey, kLen) != 0) continue;
            wchar_t* end = nullptr;
            const long id = wcstol(line + kLen, &end, 10);
            if (!end || end == line + kLen || id < 0 || id >= 1024) continue;
            while (*end == L' ') ++end;
            if (*end != L'=') continue;
            float rgb[3];
            if (end[1] && ParseHexColor(end + 1, rgb))
                own[id] = 0x01000000u | (uint32_t)(rgb[0] * 255.0f + 0.5f) << 16 | (uint32_t)(rgb[1] * 255.0f + 0.5f) << 8 | (uint32_t)(rgb[2] * 255.0f + 0.5f);
        }
        for (int id = 0; id < 1024; ++id) g_set.pointArcOwn[id].store(own[id]);
    }
    {   // [gameplay] point_skills: the skill ids cast onto a point (empty: the default list, vr/point_cast.h kDefaultSkills)
        wchar_t w[1024];
        GetPrivateProfileStringW(L"gameplay", L"point_skills", L"", w, (DWORD)std::size(w), g_iniPath);
        char a[1024];
        size_t n = 0;
        for (; w[n] && n + 1 < sizeof a; ++n) a[n] = w[n] < 128 ? (char)w[n] : ' ';
        a[n] = 0;
        pointwire::Configure(g_set.teleportCircle.load(), a, g_set.leapArc.load());
    }
    g_set.bowAim.store(IniB(L"hands", L"bow_aim", false));
    g_set.throwAim.store(IniB(L"hands", L"throw_aim", false));
    {   // [hands] throw_gesture (the right trigger) and throw_by_grip (the right grip, direct input): either one is "a throw by gesture"
        // The grip throw is ON by default - but only where it can work: native OpenXR with [openxr] direct_input (the grip is taken from the layout).
        // Elsewhere (BodyWalk's controls, a flat game) nothing of it is installed, whatever the key says: no hook, no read of the monsters.
        // (xr::LoadSettings above has the [openxr] keys.)
        const bool byTrigger = IniB(L"hands", L"throw_gesture", false);
        const bool byGrip = IniB(L"hands", L"throw_by_grip", true) && xr::On() && directinput::On();
        g_set.throwGesture.store(byTrigger || byGrip);
        throwwire::SetModes(byTrigger, byGrip);
        throwwire::SetFollowHand(IniB(L"hands", L"throw_left_hand", false));   // a thrown weapon in the left hand is thrown with the left controller (vr/throw_hand.h)
        g_set.throwAutoSkill.store(IniB(L"hands", L"throw_auto_skill", true));
        g_set.bowDrawArrow.store(IniB(L"bow", L"draw_shoots_arrow", false));   // [bow] draw_shoots_arrow: the draw's release reads the A slot as the plain shot when it holds no bow skill
        // [hands] javelin_quiver: ON by default - but only where it can work: native OpenXR with [openxr] direct_input (the trigger of a hand behind the shoulder is
        // taken from the layout). It offers nothing unless a javelin or a throwing weapon is in the hand and the hero knows a skill it allows.
        quiverwire::Configure(IniB(L"hands", L"javelin_quiver", true) && xr::On() && directinput::On(), IniB(L"hands", L"javelin_quiver_reach", true));
        g_set.throwTrace.store(IniB(L"debug", L"throw_trace", true));
        standstill::SetThrowStill(IniB(L"hands", L"throw_stand_still", true));
        g_set.throwAutoaimDeg.store(std::clamp(IniF(L"hands", L"throw_autoaim_deg", 15.0f), 0.0f, 45.0f));
        g_set.throwPotionReachPct.store(std::clamp(IniF(L"hands", L"throw_potion_reach_pct", throwaim::kDefaultReachPct), throwaim::kMinReachPct, throwaim::kMaxReachPct));
        meleetouch::SetThrowWanted((byTrigger || byGrip) && g_set.throwAutoaimDeg.load() > 0.0f);   // the monsters near the hero are read for the cone
        throwwire::SetPotionMode((int)IniF(L"hands", L"potion_throw_mode", 0.0f));   // 0 the game's throw at the release, 1 when the potion's model lands (vr/throw_land.h)
        // [hands] potion_throw_ours: ONE switch for the three that make a thrown potion OURS - the weapon-weight model's potion flies the whole trajectory (potion_model_flies), the
        // game's own missile is not drawn (potion_hide_game), the game's explosion is timed to the landing (throw_direct). On: all three, whatever their own keys say. Off: each key alone.
        const bool ours = IniB(L"hands", L"potion_throw_ours", false);
        const bool flies = ours || IniB(L"weight", L"potion_model_flies", true);
        g_set.weightPotionFlies.store(flies);
        throwborn::Configure(ours || IniB(L"hands", L"throw_direct", false));   // the potion timed to explode as the model lands (vr/throw_born.h), experimental
        potionhide::Configure(flies && (ours || IniB(L"hands", L"potion_hide_game", false)));   // the game's own thrown potion not drawn while ours flies (vr/potion_hide.h): only then, or nothing would be seen
    }
    g_set.bowLatency.store(IniB(L"debug", L"bow_latency", false));
    g_pressTraceKey.store(IniB(L"debug", L"melee_press_trace", false));   // [debug] melee_press_trace (the first 12 presses are told whatever it says)
    moveattack::SetProbe(IniB(L"debug", L"quick_blow_probe", false));
    weight::SetGripTrace(IniB(L"debug", L"grip_trace", false));   // [debug] grip_trace: a line per grip press and per change of who holds the weapon (vr/weight.cpp, vr/skeletons.cpp)
    weight::SetGripDraw(IniB(L"debug", L"grip_draw", false));     // [debug] grip_draw: the attach bones, the hands, the shaft and the take zone drawn in the headset (F4)   // [debug] quick_blow_probe: one line per attack - what became of its blow (vr/quick_blow_probe.h)
    animcut::Configure(animcut::Kind::Bow, IniB(L"bow", L"quick_release", false), (int)std::lround(IniF(L"bow", L"quick_release_frame", 2.0f)));
    g_set.quickThrowPotions.store(IniB(L"hands", L"quick_throw_potions", false));
    g_set.grabNearest.store(IniB(L"belt", L"grab_nearest", true));
    g_set.grabTrigger.store(IniB(L"belt", L"grab_trigger", false));
    g_set.holdTakesTrigger.store(IniB(L"belt", L"hold_takes_trigger", true));
    animcut::Configure(animcut::Kind::Melee, IniB(L"hands", L"quick_strike", false), std::max(2, (int)std::lround(IniF(L"hands", L"quick_strike_frame", 2.0f))));   // (2 at the earliest: the author, 2026-10-20 - at 1 the Amazon's one-hand swing AMA11HS attacked and never hit; at 2 it hits)   // the melee blow made earlier inside the attack animation
    animcut::Configure(animcut::Kind::Throw, IniB(L"hands", L"quick_throw", false), (int)std::lround(IniF(L"hands", L"quick_throw_frame", 1.0f)));   // the thrown missile made earlier inside the throw animation   // the arrow made earlier inside the same attack (a change of the game's timing)
    shieldsmite::Configure(IniB(L"gameplay", L"shield_smite", false));
    // the hero's skills to BodyWalk and a button or a recorded gesture per skill cast back (F4); the spare slot a skill on none is put on first
    gengine::Configure(IniB(L"gestures", L"engine_in_game", false), (int)std::lround(IniF(L"gestures", L"engine_pen", 3.0f)));   // (before the skill link: SetEngine)
    {   // [gestures] trigger_left / trigger_right (vr/trigger_roles.h): each trigger's role - as before, glyphs, Shift, an action; F4
        wchar_t a[2][64] = {};
        GetPrivateProfileStringW(L"gestures", L"trigger_left_action", L"", a[0], 64, g_iniPath);
        GetPrivateProfileStringW(L"gestures", L"trigger_right_action", L"", a[1], 64, g_iniPath);
        char u[2][32] = {};
        for (int h = 0; h < 2; ++h) WideCharToMultiByte(CP_UTF8, 0, a[h], -1, u[h], (int)sizeof u[h], nullptr, nullptr);
        trigroleswire::Configure((int)std::lround(IniF(L"gestures", L"trigger_left", 0.0f)), (int)std::lround(IniF(L"gestures", L"trigger_right", 0.0f)), u[0], u[1],
                                 IniB(L"gestures", L"glyph_cast_hold", false));
        skilllinkwire::SetSelectIcon(IniB(L"gestures", L"select_icon", true));   // the armed skills' icons in the medallions
    }
    skilllinkwire::Configure(IniB(L"gestures", L"skill_link", false),
                             (int)std::lround(IniF(L"gestures", L"skill_link_slot", (float)skilllink::kDefaultSpareSlot)),
                             IniB(L"gestures", L"skill_link_settle", true));
    shieldsmite::SetTuning(IniF(L"gameplay", L"shield_smite_min_speed", 0.8f), IniF(L"gameplay", L"shield_smite_stroke_cm", 8.0f));   // the shield bash's own speed and start distance (vr/shield_smite.h BashParams), live
    skilltriggers::SetWanted(IniB(L"debug", L"skill_triggers", false));   // what casts a skill drawn (vr/skill_triggers.h)
    skilltriggers::SetMarkMs((uint32_t)std::clamp(IniF(L"debug", L"skill_mark_ms", 800.0f), 0.0f, (float)skilltriggers::kMaxMarkMs));   // how long its speed marks and trail stay
    g_set.bowRigPose.store(IniB(L"bow", L"rig_pose", false));
    g_set.bowRigGrip.store(IniB(L"debug", L"bow_rig_grip", false));
    g_set.flailChain.store(IniB(L"flail", L"chain", false));
    g_set.flailDampingPct.store(std::clamp(IniF(L"flail", L"damping_pct", 100.0f), 0.0f, 500.0f));
    g_set.flailSwingPct.store(std::clamp(IniF(L"flail", L"swing_pct", 100.0f), 0.0f, 200.0f));
    g_set.flailHitPct.store(std::clamp(IniF(L"flail", L"hit_pct", 100.0f), 0.0f, 200.0f));
    g_set.flailWeightPct.store(std::clamp(IniF(L"flail", L"ball_weight_pct", 100.0f), 25.0f, 400.0f));
    g_set.meleeTouch.store(IniB(L"gameplay", L"melee_touch", false));
    meleetrigger::Configure(IniB(L"gameplay", L"melee_touch_only", true));   // F4, hit by touch swinging a weapon (anything but a bow or a crossbow): the trigger's attack is not passed to the game - part of hit by touch, ON by default; 0 = the old way (the plugin's own controls only; vr/melee_trigger.h)
    doubleswing::Configure(IniB(L"hands", L"double_swing_by_hands", false), IniF(L"hands", L"double_swing_window_ms", 700.0f), (int)IniF(L"hands", L"double_swing_skill", 0.0f),
                           IniB(L"hands", L"double_swing_combo", false));   // F4, hit by touch with a weapon in each hand: the second hand's touch casts the game's own Double Swing / Frenzy (vr/double_swing.h)
    meleearm::Configure(IniB(L"gameplay", L"melee_skill_armed", false));   // F4, hit by touch: a melee attack skill's pad button arms it instead of casting it; every touch is then that skill, the plain attack when the mana is short (vr/melee_arm.h)
    meleearm::ConfigureOnce(IniB(L"gameplay", L"melee_skill_arm_once", false), IniF(L"gameplay", L"melee_skill_arm_once_s", meleearm::kOnceTimeoutDefS));   // ... for ONE blow: the next touch is the skill, the ones after it the plain attack; unspent, disarmed after the seconds
    g_iconLiftMm.store(std::clamp(IniF(L"gameplay", L"melee_skill_armed_icon_lift_mm", 1.0f), 0.0f, 10.0f));   // the icon over the floor, mm: off the medallion's texture (the author: they overlapped)
    meleearm::ConfigureIcon(IniB(L"gameplay", L"melee_skill_armed_icon", true), IniF(L"gameplay", L"melee_skill_armed_icon_size_pct", armedicon::kSizeDefPct),
                            IniF(L"gameplay", L"melee_skill_armed_icon_depth_pct", armedicon::kDepthDefPct), IniF(L"gameplay", L"melee_skill_armed_icon_opacity_pct", armedicon::kOpacityDefPct),
                            IniF(L"gameplay", L"melee_skill_armed_icon_faint_pct", armedicon::kFaintDefPct));   // F4: the armed skill's icon in the mana medallion, faint when the mana is short; its size, depth, opacity with and without the mana (vr/armed_icon.h)
    {   // [melee_armed]: every s<skill> key the section has (the [autotarget] way); unset, the list's default (vr/melee_arm.h kRows: the 34 melee attacks ticked, the movement attacks not)
        meleearm::Allowed allowed = meleearm::Allowed::Defaults();
        static wchar_t sec[4096];
        const DWORD n = GetPrivateProfileSectionW(L"melee_armed", sec, (DWORD)std::size(sec), g_iniPath);
        for (const wchar_t* e = sec; n && *e; e += wcslen(e) + 1) {
            int id = -1, v = 0;
            if (swscanf_s(e, L"s%d = %d", &id, &v) == 2) allowed.Set(id, v);
        }
        meleearm::ConfigureList(allowed.Bits());
    }
    g_set.meleeTouchUnchecked.store(IniB(L"debug", L"melee_touch_unchecked", false));
    g_set.meleeTouchClientVerdict.store(IniB(L"debug", L"melee_touch_client_verdict", false));
    meleetouch::SetWanted(g_set.meleeTouch.load());
    meleetouch::SetThrownTouch(IniB(L"gameplay", L"melee_touch_thrown", true));   // javelins, throwing knives and axes hit by touch too (a point-blank throw; vr/thrown_touch.h)
    meleetouch::SetThrownHeld(IniB(L"gameplay", L"melee_touch_thrown_held", false));   // ... from the equip, like a sword - not only while the throw's grip is squeezed
    meleetouch::SetClawHits(g_set.clawHits.load());   // [shapeshift] claw_hits: the form's paws strike by touch
    meleetouch::SetClawMinSpeed(IniF(L"shapeshift", L"wolf_min_speed", 0.0f), IniF(L"shapeshift", L"bear_min_speed", 0.0f));   // ... their least speed per form (0 / no key: as before)
    meleetouch::SetBarrelsWanted(IniB(L"gameplay", L"melee_touch_barrels", false),   // a swing through a barrel / pot presses the game's own interact on it (vr/barrel_touch.h)
                                 IniF(L"gameplay", L"melee_touch_barrels_min_speed", 2.0f));   // ... from this swing speed, its own gate
    standstill::SetWanted(IniB(L"gameplay", L"attack_in_place", true));   // F4: the plugin's attack presses never walk the hero (vr/stand_still.h)
    moveattack::SetWanted(IniB(L"gameplay", L"move_while_attacking", true));   // F4, EXPERIMENTAL: a spear / bow hero walks on once the blow is out (vr/move_attack.h)
    const int attackPath = (int)IniF(L"gameplay", L"flat_attack_path", 0.0f);   // the prototype: the swing does not stop the hero; it replaces the cut below
    attackpath::SetWanted(attackPath, IniF(L"gameplay", L"flat_attack_path_cone", 45.0f));
    moveattack::SetFlatWanted(attackPath > 0 ? 0 : (int)IniF(L"gameplay", L"flat_move_while_attacking", 0.0f));   // the flat game's F2 / F3: W A S D walk on once the blow is out (0 off, 1 spear / bow, 2 every weapon)
    // [gameplay] quick_blow, EXPERIMENTAL, on by default since 0.204 (vr/quick_blow.h): VR F4 - the blow on the first frame, the hero walks through the swing, the touch is the game's
    // attack, its target in reach; quick_blow_faster: the next attack cuts the animation once the blow is out and quick_blow_min_ms passed
    moveattack::SetQuick(IniB(L"gameplay", L"quick_blow", true), IniB(L"gameplay", L"quick_blow_faster", false), (int)IniF(L"gameplay", L"quick_blow_min_ms", (float)quickblow::kMinMsDefault));
    straywalk::SetWalkWanted(IniB(L"gameplay", L"block_stray_walk", true));     // F4: a walk or run to a point out of a click on bare ground is dropped (vr/stray_walk.h)
    straywalk::SetAttackRunWanted(IniB(L"gameplay", L"block_attack_run", false));   // F4: the run the game starts out of the pad cast of an attack of ours is dropped
    shottrace::SetWanted(IniB(L"debug", L"shot_trace", false));   // the log tells, shot by shot, what the game does with an attack of ours (vr/shot_trace.h)
    hitoutcome::SetWanted(IniB(L"debug", L"hit_outcome", false));   // the log tells every roll of the hero's blow on a monster and the life it took (vr/hit_outcome.h)
    straywalk::SetClickWanted(IniB(L"gameplay", L"block_stray_clicks", true));  // F4: no mouse button reaches the game in the world with no panel open
    // Settings' "Direct hit" selector writes both: Off 0 / 0, By the rules 2 / 1, Fully on 1 / 2 (the old tick boxes' 1 kept their meaning)
    const int directMode = std::clamp((int)std::lround(IniF(L"gameplay", L"melee_direct", 0.0f)), 0, 2);
    const int clientMode = std::clamp((int)std::lround(IniF(L"gameplay", L"melee_direct_client", 0.0f)), 0, 2);
    meleedirect::SetWanted(directMode != 0);   // the user's direct hit where the server runs (vr/melee_direct.h)
    meleedirect::SetByRules(directMode == 2);
    meleedirect::SetScope((int)std::lround(IniF(L"gameplay", L"melee_direct_scope", -1.0f)));   // 0 two-handed extras, 1 every touch by the rules, missing: 0.204's way
    meleedirect::SetByHand(IniB(L"gameplay", L"melee_direct_by_hand", true));   // a blow of the left-hand weapon is the game's "Left Hand Swing" (the author: on)
    meleedirect::SetMinIntervalMs((uint32_t)std::clamp(IniF(L"gameplay", L"melee_direct_min_ms", 0.0f), 0.0f, 3000.0f));
    meleedirectclient::SetMode(clientMode);   // a joined TCP/IP player's touch: the game's own command (vr/melee_direct_client.h)
    {   // ... and its rules for a touch: the least speed where it touched (m/s), force (the strike's score, N s) and swing length (m)
        meleedirectclient::Mins m;
        m.speed = std::clamp(IniF(L"gameplay", L"melee_direct_client_min_speed", 2.5f), 0.0f, 10.0f);
        m.force = std::clamp(IniF(L"gameplay", L"melee_direct_client_min_force", 1.5f), 0.0f, 20.0f);
        m.length = std::clamp(IniF(L"gameplay", L"melee_direct_client_min_length", 0.2f), 0.0f, 3.0f);
        meleedirectclient::SetMins(m);
    }
    {   // [gameplay] melee_direct_client_cleave: a swing's extra hits through the server's attack - who hosts decides: the box is the rule of a game
        // of ours (single player, and every joined player when we host), whatever our own Direct hit (its rules apply where a touch is "by the
        // rules" only: HostRules, a joined player's mode 1); joined, the host's box decides (vr/melee_direct_client.h CleaveRule)
        const bool cleave = IniB(L"gameplay", L"melee_direct_client_cleave", false);
        meleedirectclient::SetCleave(cleave);
        meleedirect::SetRemoteWanted(cleave);   // as the host: the joined players' extra hits are struck here
    }
    {   // [hud] game_menu_settings (vr/flat_menu.h): the flat game's settings from the pause menu, the host's rules' list ([hud] host_rules_key) -
        // and our own values of the host's rules (vr/host_rules.h: what we answer a joined player's mod as the host, what our own game shows)
        flatmenu::Configure(IniB(L"hud", L"game_menu_settings", false), (int)IniF(L"hud", L"game_menu_key", 1.0f), (int)IniF(L"hud", L"host_rules_key", 1.0f),
                            g_set.platform.load() == 0);
        hostrules::Values own;
        for (int i = 0; i < hostrules::kCount; ++i) {
            wchar_t t[32] = L"";
            GetPrivateProfileStringW(hostrules::kRules[i].section, hostrules::kRules[i].key, L"", t, (DWORD)std::size(t), g_iniPath);
            own.v[i] = hostrules::Parse(i, t);
            own.known[i] = true;
        }
        flatmenu::SetOwnRules(own);
    }
    meleetouch::SetUnchecked(g_set.meleeTouchUnchecked.load());
    meleetouch::SetNoAttack(IniB(L"debug", L"melee_touch_no_attack", false));   // a touch presses nothing (the stab and the push tried alone)
    meleetouch::SetClientVerdict(g_set.meleeTouchClientVerdict.load());
    // [hud] swing_timer (vr/swing_timer.h): the attack's ring (0) or bar (1) after an attack the game started, the outcome in it - only shown
    swingtimer::SetPace(IniB(L"gameplay", L"swing_pace_joined", false));   // joined: a touch sent when the host's gate takes it (vr/swing_timer.h PaceAt)
    swingtimer::SetWanted(IniB(L"hud", L"swing_timer", false), (int)IniF(L"hud", L"swing_timer_style", 0.0f), IniB(L"hud", L"swing_timer_numbers", true),
                          IniB(L"hud", L"swing_timer_reasons", true));   // ... and why a touch was no attack (vr/swing_reason.h)
    thrustdebugwire::SetWanted(IniB(L"debug", L"thrust_debug", false));   // the thrust's readout over the weapon hand, the blade tinted, a log line per thrust (vr/thrust_debug.h)
    thrustdebugwire::SetOnly(IniB(L"debug", L"thrust_debug_only", false));   // ... only the thrusts: the swing's colours, marks and log lines left out of the debug
    g_set.stabDebug.store(IniB(L"debug", L"stab_debug", false));   // [debug] stab_debug: the stab's readout and marks (vr/stab_debug.h)
    thrustdebugwire::SetStabWanted(g_set.stabDebug.load());
    {   // [weight] npc_bodies: town NPCs and other non-hostile units get bodies too (never attacked). It was [debug] npc_bodies until 0.183 - a [debug] key only draws
        // or logs (the author, 2026-10-26: «дебаг - это только отображение, но не функция»): an ini without the [weight] key yet keeps what the old place says
        const float weightKey = IniF(L"weight", L"npc_bodies", -1.0f);
        meleetouch::SetNpcBodies(weightKey >= 0.0f ? weightKey != 0.0f : IniB(L"debug", L"npc_bodies", false));
    }
    g_set.strikeMeasure.store(IniB(L"debug", L"strike_measure", false));   // [debug] strike_measure: each strike on a body measured into the log
    soundtrace::SetWanted(IniB(L"debug", L"sound_trace", false));   // [debug] sound_trace: the game's sounds by name (to find its shield-hit sound)
    clashsound::SetWanted(IniB(L"weight", L"clash_sound", true));   // [weight] clash_sound: the two weapons of a pair, a parry - the game's block sound by how hard
    PoseShareConfigure();   // [multiplayer] pose_share
    meleetouch::SetSwayView(IniB(L"debug", L"monster_ragdoll_view", false));   // [weight] monster_ragdoll's view (drawn as melee_boxes, which it switches on)
    meleeboxes::SetWanted(IniB(L"debug", L"melee_boxes", false) || thrustdebugwire::Wanted() || IniB(L"debug", L"monster_ragdoll_view", false) ||
                          g_set.stabDebug.load());   // (thrust_debug, monster_ragdoll_view and stab_debug draw the blade and the monsters too)
    skel::SetRigDump(IniB(L"debug", L"monster_rig_dump", false));   // [weight] monster_bodies: one model's mesh reads written to the log once (vr/monster_rig.h)
    meleeboxes::SetDescribeWanted(!thrustdebugwire::Only());   // (the 5 Hz line of the swing's numbers: not with thrust_debug_only)   // the capsules of hit by touch drawn (vr/melee_boxes.h)
    meleeboxes::SetMarkMs((uint32_t)std::clamp(IniF(L"debug", L"melee_mark_ms", 800.0f), 0.0f, (float)meleeboxes::kMaxMarkMs));   // how long the speed marks and the trail stay (vr/melee_marks.h)
    meleetouch::SetStrokeTuning(IniF(L"gameplay", L"melee_min_speed", 0.8f), IniF(L"gameplay", L"melee_stroke_start_cm", 8.0f));   // the swing engine's speed and start distance, live (vr/stroke_tuning.h)
    {   // the strike rules (vr/strike_direction.h): toward the monster, the head, the amplitude - each with a switch for every kind of weapon - and the engine's two numbers for a kind (0 = the general ones)
        strikedir::Rule rule;
        rule.toward = IniB(L"gameplay", L"melee_toward", true);
        rule.towardDeg = IniF(L"gameplay", L"melee_toward_deg", strikedir::kDefaultTowardDeg);
        rule.head = IniB(L"gameplay", L"melee_head_only", true);
        rule.headPct = IniF(L"gameplay", L"melee_head_pct", strikedir::kDefaultHeadPct);
        rule.amplitude = IniB(L"gameplay", L"melee_amplitude", true);
        rule.pierceCm = IniF(L"gameplay", L"melee_amp_pierce_cm", strikedir::kDefaultPierceCm);
        rule.edgedX = IniF(L"gameplay", L"melee_amp_edged_x", strikedir::kDefaultEdgedX);
        rule.bluntX = IniF(L"gameplay", L"melee_amp_blunt_x", strikedir::kDefaultBluntX);
        rule.staffX = IniF(L"gameplay", L"melee_amp_staff_x", strikedir::kDefaultStaffX);
        rule.backward = IniB(L"gameplay", L"melee_not_backward", false);   // (a weapon that thrusts: no strike from its point toward its handle - melee_touch applies it per blade)
        rule.notButt = IniB(L"gameplay", L"melee_not_butt", true);          // (the butt and the shaft behind the hands never strike - melee_touch sets the front hand per blade)
        meleetouch::SetHeadVolume(IniB(L"gameplay", L"melee_head_volume", true));   // (the head as a volume of its own where the model has it, the shaft as thin as the model's)
        rule.metalHead = IniB(L"gameplay", L"melee_metal_head", true);      // (a metal head or an edge strikes, the shaft below it pushes - melee_touch sets the head's start per blade, vr/metal_head.h)
        rule.towardKinds = rule.headKinds = 0;
        for (int k = 0; k < strikedir::kKinds; ++k) {
            const std::wstring name = strikedir::KindNameW(k);
            if (IniB(L"gameplay", (L"melee_toward_" + name).c_str(), true)) rule.towardKinds |= strikedir::Bit(k);
            if (IniB(L"gameplay", (L"melee_head_only_" + name).c_str(), ((strikedir::kDefaultHeadKinds >> k) & 1u) != 0)) rule.headKinds |= strikedir::Bit(k);
            rule.kindX[static_cast<size_t>(k)] = IniF(L"gameplay", (L"melee_amp_x_" + name).c_str(), 0.0f);
            meleetouch::SetKindTuning(k, IniF(L"gameplay", (L"melee_min_speed_" + name).c_str(), 0.0f), IniF(L"gameplay", (L"melee_stroke_start_cm_" + name).c_str(), 0.0f));
        }
        meleetouch::SetStrikeRule(rule);
        meleeboxes::SetDirectionRed(IniB(L"debug", L"melee_direction_red", true));   // the debug draws "not toward the monster" in red (0: blue)
    }
    {   // the thrust (vr/thrust_stroke.h): a poke along the blade with its own least speed and distance - for the weapons the game thrusts with, each kind settable (0 = by the game)
        thrust::Rule tr;
        tr.on = IniB(L"gameplay", L"melee_thrust", true);
        tr.minSpeed = IniF(L"gameplay", L"melee_thrust_min_speed", thrust::kDefaultMinSpeed);
        tr.cm = IniF(L"gameplay", L"melee_thrust_cm", thrust::kDefaultCm);
        tr.alongPct = IniF(L"gameplay", L"melee_thrust_along_pct", thrust::kDefaultAlongPct);
        tr.minAccel = IniF(L"gameplay", L"melee_thrust_min_accel", thrust::kDefaultMinAccel);   // 0 = not asked
        tr.withRun = IniB(L"gameplay", L"melee_thrust_with_run", false);   // the hero's run is the thrust's movement too (measured in the world)
        g_set.stabWithRun.store(tr.withRun);   // ... and the stab's: its gate and its speed against the body ([weight] stab_in)
        for (int k = 0; k < strikedir::kKinds; ++k) {
            const std::wstring name = strikedir::KindNameW(k);
            // the kind's tick (melee_thrust_on_<kind>); an ini without it keeps the older -1 / 0 / 1 of melee_thrust_<kind>; with neither, by the game's class of the item
            tr.mode[static_cast<size_t>(k)] = thrust::ModeFromIni(IniF(L"gameplay", (L"melee_thrust_on_" + name).c_str(), NAN), IniF(L"gameplay", (L"melee_thrust_" + name).c_str(), NAN));
            tr.kindSpeed[static_cast<size_t>(k)] = IniF(L"gameplay", (L"melee_thrust_min_speed_" + name).c_str(), 0.0f);
            tr.kindCm[static_cast<size_t>(k)] = IniF(L"gameplay", (L"melee_thrust_cm_" + name).c_str(), 0.0f);
            tr.kindAccel[static_cast<size_t>(k)] = IniF(L"gameplay", (L"melee_thrust_min_accel_" + name).c_str(), 0.0f);
        }
        meleetouch::SetThrustRule(tr);
    }
    meleetouch::SetBladeTuning(IniF(L"debug", L"melee_blade_length_pct", 100.0f), IniF(L"debug", L"melee_blade_radius_cm", 0.0f), IniB(L"debug", L"melee_blade_flip", false), IniF(L"debug", L"melee_blade_butt_pct", 100.0f));   // the touch's blade tuned live
    meleetouch::SetThrownTuning(IniF(L"debug", L"melee_thrown_length_pct", 100.0f), IniF(L"debug", L"melee_thrown_butt_pct", 100.0f));   // ... and the thrown kinds' own, on top
    {   // [autotarget]: every s<skill> key the section has; unset, the seekers keep it (the settings program's defaults)
        uint64_t keep[16] = {};
        for (const int id : {22, 53, 93}) keep[id >> 6] |= 1ull << (id & 63);   // Guided Arrow, Chain Lightning, Bone Spirit
        static wchar_t sec[8192];
        const DWORD n = GetPrivateProfileSectionW(L"autotarget", sec, (DWORD)std::size(sec), g_iniPath);
        for (const wchar_t* e = sec; n && *e; e += wcslen(e) + 1) {
            int id = -1, v = 0;
            if (swscanf_s(e, L"s%d = %d", &id, &v) != 2 || id < 0 || id >= 1024) continue;
            if (v) keep[id >> 6] |= 1ull << (id & 63); else keep[id >> 6] &= ~(1ull << (id & 63));
        }
        for (int w = 0; w < 16; ++w) g_set.autoKeep[w].store(keep[w]);
    }
    g_set.fist.store(IniB(L"hands", L"fist", true));
    g_set.shieldFist.store(IniB(L"hands", L"shield_fist", true));
    g_set.leftSlotDrawn.store(IniB(L"hands", L"left_slot_drawn_left", true));
    g_set.forearmTwist.store(IniB(L"hands", L"forearm_twist", true));
    g_set.forearmShare.store(std::clamp(IniF(L"hands", L"forearm_twist_share", 0.5f), 0.0f, 1.0f));
    g_set.shieldForearm.store(IniB(L"shield", L"forearm", true));
    g_set.headsHang.store(IniB(L"shield", L"heads_hang", true));
    {
        static const wchar_t* const kKeys[6] = {L"x", L"y", L"z", L"pitch", L"yaw", L"roll"};
        for (int k = 0; k < 6; ++k) g_set.shieldAdj[k].store(std::clamp(IniF(L"shield", kKeys[k], 0.0f), k < 3 ? -40.0f : -180.0f, k < 3 ? 40.0f : 180.0f));
        g_set.shieldBlink.store(IniB(L"debug", L"shield_blink", false));
        g_set.shoulderZoneBody.store(IniB(L"hands", L"shoulder_zone_body", false));
        {   // what was read from the ini, told when it changes: a slider that "does nothing" - is the ini written, is it read? (the next line, "at the hook", says what the skeleton got)
            static float told[6] = {1e9f, 1e9f, 1e9f, 1e9f, 1e9f, 1e9f};
            static int toldForearm = -1;
            const int forearm = g_set.shieldForearm.load() ? 1 : 0;
            bool same = forearm == toldForearm;
            for (int k = 0; k < 6; ++k) same = same && std::fabs(g_set.shieldAdj[k].load() - told[k]) < 0.05f;
            if (!same) {
                toldForearm = forearm;
                for (int k = 0; k < 6; ++k) told[k] = g_set.shieldAdj[k].load();
                LogF("vrcam settings: [shield] forearm %d, x %.1f y %.1f z %.1f cm, pitch %.0f yaw %.0f roll %.0f deg - read from d2r_vr.ini", forearm,
                     told[0], told[1], told[2], told[3], told[4], told[5]);
            }
        }
    }
#if D2RVR_FIRST_PERSON
    for (int c = 0; c < 8; ++c) {
        wchar_t close[32], open[32];
        swprintf_s(close, L"%hs_close", skel::kHeroClasses[c]);
        swprintf_s(open, L"%hs_open", skel::kHeroClasses[c]);
        g_set.fistClose[c].store(std::clamp(IniF(L"fist", close, c == 3 ? 65.0f : 100.0f), 0.0f, 150.0f));
        g_set.fistOpen[c].store(std::clamp(IniF(L"fist", open, 20.0f), 0.0f, 150.0f));
    }
#endif
    for (int t = 1; t < D2RVR_TYPE_COUNT; ++t) {
        wchar_t sec[40] = L"weapon_";
        for (const char* c = kD2RVRWeaponTypeNames[t]; *c && wcslen(sec) < 38; ++c) { const size_t l = wcslen(sec); sec[l] = (wchar_t)tolower((unsigned char)*c); sec[l + 1] = 0; }
        static const wchar_t* const kKeys[6] = {L"x", L"y", L"z", L"pitch", L"yaw", L"roll"};
#if D2RVR_FIRST_PERSON
        // All 0. The crossbow's turn and shift it about the right controller's own axes
        // from its stock laid where the controller points (skeletons.cpp GunFrame).
#endif
        const float def[6] = {};
        for (int k = 0; k < 6; ++k) g_set.weaponAdj[t][k] = std::clamp(IniF(sec, kKeys[k], def[k]), k < 3 ? -40.0f : -180.0f, k < 3 ? 40.0f : 180.0f);
        static const wchar_t* const kLeft[3] = {L"left_x", L"left_y", L"left_z"};
        for (int k = 0; k < 3; ++k) g_set.weaponLeft[t][k] = std::clamp(IniF(sec, kLeft[k], 0.0f), -20.0f, 20.0f);
        // [body] flat_weapon_adjust: flat F3, cm out to the hand's side, up, ahead - each hand, this kind, in its own [flat_weapon_<kind>] (never VR's
        // [weapon_<kind>]; vr/flat_weapon.h), 0 by default
        {
            wchar_t flatSec[48];
            swprintf_s(flatSec, L"%s%s", flatweapon::kSectionPrefix, sec + 7);   // (sec: "weapon_<kind>")
            for (int k = 0; k < 6; ++k) g_set.weaponFlat[t][k] = flatweapon::ClampCm(IniF(flatSec, flatweapon::kKeys[k], flatweapon::kDefaultCm[k % 3]));
        }
        if (t == 1) {
            static const wchar_t* const kHold[3] = {L"left_hold_x", L"left_hold_y", L"left_hold_z"};
            for (int k = 0; k < 3; ++k) g_set.leftHold[k].store(std::clamp(IniF(L"hands", kHold[k], 0.0f), -20.0f, 20.0f));
            static const wchar_t* const kTurn[3] = {L"left_hold_pitch", L"left_hold_yaw", L"left_hold_roll"};
            for (int k = 0; k < 3; ++k) g_set.leftHoldTurn[k].store(std::clamp(IniF(L"hands", kTurn[k], 0.0f), -180.0f, 180.0f));
        }
    }
    g_set.staffOffset[0].store(std::clamp(IniF(L"hands", L"staff_x", 0.0f), -40.0f, 40.0f));
    g_set.staffOffset[1].store(std::clamp(IniF(L"hands", L"staff_y", 0.0f), -40.0f, 40.0f));
    g_set.staffOffset[2].store(std::clamp(IniF(L"hands", L"staff_z", 0.0f), -40.0f, 40.0f));
    g_set.staffGrabCm.store(std::clamp(IniF(L"hands", L"staff_grab_cm", 15.0f), 3.0f, 100.0f));
    g_set.leftStaffOffset[0].store(std::clamp(IniF(L"hands", L"left_staff_x", 0.0f), -20.0f, 20.0f));
    g_set.leftStaffOffset[1].store(std::clamp(IniF(L"hands", L"left_staff_y", 0.0f), -20.0f, 20.0f));
    g_set.leftStaffOffset[2].store(std::clamp(IniF(L"hands", L"left_staff_z", 0.0f), -20.0f, 20.0f));
    g_set.xbowLeftOffset[0].store(std::clamp(IniF(L"hands", L"xbow_left_x", 0.0f), -20.0f, 20.0f));
    g_set.xbowLeftOffset[1].store(std::clamp(IniF(L"hands", L"xbow_left_y", 0.0f), -20.0f, 20.0f));
    g_set.xbowLeftOffset[2].store(std::clamp(IniF(L"hands", L"xbow_left_z", 0.0f), -20.0f, 20.0f));
    g_set.xbowLeftRoll.store(std::clamp(IniF(L"hands", L"xbow_left_roll", 0.0f), -180.0f, 180.0f));
    g_set.xbowLeftAtCm.store(std::clamp(IniF(L"hands", L"xbow_left_at_cm", 30.0f), 5.0f, 80.0f));
    g_set.xbowGripLatch.store(IniB(L"hands", L"xbow_grip_latch", false));
    g_set.xbowGripWide.store(IniB(L"hands", L"xbow_grip_wide", false));
    g_set.xbowWristTracked.store(IniB(L"hands", L"xbow_wrist_tracked", false));
    g_set.xbowSupportModel.store(IniB(L"hands", L"xbow_support_model", false));
    g_set.xbowSupportDropCm.store(std::clamp(IniF(L"hands", L"xbow_support_drop_adj_cm", 0.0f), -10.0f, 10.0f));
    g_set.xbowSupportBackCm.store(std::clamp(IniF(L"hands", L"xbow_support_back_adj_cm", 0.0f), -10.0f, 20.0f));
    g_set.xbowAimBarrel.store(IniB(L"hands", L"xbow_aim_barrel", false));
    g_set.xbowHoldGrip.store(IniB(L"hands", L"xbow_hold_grip", false));
    g_set.xbowSupportGame.store(IniB(L"hands", L"xbow_support_game", false));
    g_set.xbowSupportClose.store(IniB(L"hands", L"xbow_support_close", false));
    g_set.xbowHandover.store(IniB(L"hands", L"xbow_handover", false));
    g_set.xbowBarrelForward.store(IniB(L"hands", L"xbow_barrel_forward", false));
    g_set.xbowBarrelLine.store(IniB(L"debug", L"xbow_barrel_line", false));
    g_set.polearmTwoHands.store(IniB(L"hands", L"polearm_two_hands", true));
    g_set.swordTwoHands.store(IniB(L"hands", L"sword_two_hands", true));
    g_set.swordLeftAtCm.store(std::clamp(IniF(L"hands", L"sword_left_at_cm", 10.0f), -30.0f, 30.0f));
    g_set.swordSlideCm.store(std::clamp(IniF(L"hands", L"sword_slide_cm", 4.0f), 0.0f, 20.0f));
    g_set.leftSwordOffset[0].store(std::clamp(IniF(L"hands", L"left_sword_x", 0.0f), -20.0f, 20.0f));
    g_set.leftSwordOffset[1].store(std::clamp(IniF(L"hands", L"left_sword_y", 0.0f), -20.0f, 20.0f));
    g_set.leftSwordOffset[2].store(std::clamp(IniF(L"hands", L"left_sword_z", 0.0f), -20.0f, 20.0f));
    g_set.spearHand.store(IniF(L"hands", L"spear_hand", 0.0f) != 0.0f ? 1 : 0);   // right: a halberd hung off the hand with 1 (2026-10-06)
    g_set.swordHand.store(IniF(L"hands", L"sword_hand", 0.0f) != 0.0f ? 1 : 0);
    g_set.axeHand.store(IniF(L"hands", L"axe_hand", 0.0f) != 0.0f ? 1 : 0);
    g_set.swordHiltAxis.store(std::clamp((int)IniF(L"hands", L"sword_hilt_axis", -2.0f), -3, 3));
    g_set.axeShaftAxis.store(std::clamp((int)IniF(L"hands", L"axe_shaft_axis", -2.0f), -3, 3));
    g_set.carryAxis.store(std::clamp((int)IniF(L"hands", L"carry_axis", 2.0f), -3, 3));
    if (g_set.carryAxis.load() == 0) g_set.carryAxis.store(2);
    g_set.carryCm.store(std::clamp(IniF(L"hands", L"carry_cm", 40.0f), 0.0f, 150.0f));
    g_set.bothAttach.store(IniB(L"hands", L"both_attach", true));
    g_set.weightOn.store(IniB(L"weight", L"on", false));
    g_set.weightSecondLetsGo.store(IniB(L"weight", L"second_lets_go", true));
    g_set.weightDualCollide.store(IniB(L"weight", L"dual_collide", true));
    weightwire::ConfigureGripNoChord(IniB(L"weight", L"weapon_grip_no_chord", false));   // a grip on the weapon is no modifier of the panels' chords and the A / RT slot controls (vr/weight_wire.h)
    shoulderwire::SetDraw(g_set.shoulderDraw.load() && (g_set.weightOn.load() || g_set.shieldStow.load() || g_set.rangedHolster.load()));   // [hands] shoulder_draw: a weapon to draw (the weight model) or the shield to put away
    g_set.weightFeel.store(std::clamp(IniF(L"weight", L"feel", 50.0f), 0.0f, 300.0f));
    g_set.weightStrength.store(std::clamp(IniF(L"weight", L"strength", 100.0f), 10.0f, 500.0f));
    g_set.weightGravity.store(std::clamp(IniF(L"weight", L"gravity_pct", 100.0f), 0.0f, 300.0f));
    g_set.weightDamping.store(std::clamp(IniF(L"weight", L"damping_pct", 100.0f), 0.0f, 200.0f));
    g_set.weightGravityFeel.store(IniB(L"weight", L"gravity_feel", true));
    g_set.weightHandsModel.store(IniF(L"weight", L"hands_model", 0.0f) != 0.0f ? 1 : 0);
    g_set.weightHandsPalm.store(IniB(L"weight", L"hands_model_palm", false));
    g_set.weightGripModel.store(IniF(L"weight", L"grip_model", 0.0f) != 0.0f ? 1 : 0);
    g_set.weightHeroStrength.store(IniB(L"weight", L"hero_strength", false));
    gamestate::SetWantHeroStrength(g_set.weightHeroStrength.load());
    g_set.weightStrengthEffect.store(std::clamp(IniF(L"weight", L"strength_effect", 100.0f), 0.0f, 200.0f));
    g_set.weightStrengthHonest.store(IniB(L"weight", L"strength_honest", false));
    g_set.weightTwoHandsDouble.store(IniB(L"weight", L"two_hands_double", false));
    for (int c = 0; c < 8; ++c) {
        wchar_t key[40];
        swprintf_s(key, L"str_%hs", skel::kHeroClasses[c]);
        g_set.weightStrengthClass[c].store(std::clamp(IniF(L"weight", key, (float)herostr::DefaultClassPct(c)), 10.0f, 300.0f));
    }
    g_set.weightGrabCm.store(std::clamp(IniF(L"weight", L"grab_cm", 12.0f), 3.0f, 40.0f));
    g_set.weightLetGoCm.store(std::clamp(IniF(L"weight", L"let_go_cm", 25.0f), 5.0f, 80.0f));
    {
        static const wchar_t* const kFist[3] = {L"fist_x", L"fist_y", L"fist_z"};
        for (int k = 0; k < 3; ++k) g_set.weightFist[k].store(std::clamp(IniF(L"weight", kFist[k], 0.0f), -15.0f, 15.0f));
    }
    g_set.weightGripFit.store(IniB(L"weight", L"grip_fit", true));
    g_set.weightGripLikeBase.store(IniB(L"weight", L"grip_fit_like_base", true));
    g_set.weightGripFingers.store(IniB(L"weight", L"grip_fit_fingers", true));
    g_set.weightAttachApart.store(IniB(L"weight", L"attach_apart", true));
    {
        static const wchar_t* const kRight[6] = {L"right_grip_x", L"right_grip_y", L"right_grip_z", L"right_grip_rx", L"right_grip_ry", L"right_grip_rz"};
        static const wchar_t* const kLeft[6] = {L"left_grip_x", L"left_grip_y", L"left_grip_z", L"left_grip_rx", L"left_grip_ry", L"left_grip_rz"};
        for (int k = 0; k < 6; ++k) {
            const float lim = k < 3 ? 30.0f : 180.0f;   // cm / deg
            g_set.weightGripHand[0][k].store(std::clamp(IniF(L"weight", kRight[k], 0.0f), -lim, lim));
            g_set.weightGripHand[1][k].store(std::clamp(IniF(L"weight", kLeft[k], 0.0f), -lim, lim));
        }
    }
    g_set.weightAfterDrop.store(std::clamp((int)IniF(L"weight", L"after_drop", 1.0f), 0, 2));
    g_set.weightToBackMs.store(std::clamp(IniF(L"weight", L"to_back_delay_ms", 600.0f), 0.0f, 3000.0f));
    g_set.weightTakeHaptic.store(IniB(L"weight", L"take_haptic", true));
    g_set.weightTakeHapticPct.store(std::clamp(IniF(L"weight", L"take_haptic_pct", 40.0f), 0.0f, 100.0f));
    g_set.weightRecallAny.store(IniB(L"weight", L"recall_any_hand", true));
    g_set.weightMonsterBodies.store(IniB(L"weight", L"monster_bodies", false));
    {   // [weight] strike_by_force (vr/strike_force.h): hit by touch by the strike's force - only with the weight model and its monster bodies (the strikes are measured on them)
        strikeforce::Settings fs;
        fs.on = IniB(L"weight", L"strike_by_force", false) && g_set.weightOn.load() && g_set.weightMonsterBodies.load();
        fs.general = IniF(L"weight", L"strike_force_general_pct", 100.0f) * 0.01f;
        fs.blunt = IniF(L"weight", L"strike_force_blunt", 1.5f);
        fs.edged = IniF(L"weight", L"strike_force_edged", 1.1f);
        fs.piercing = IniF(L"weight", L"strike_force_piercing", 1.5f);
        fs.massExp = IniF(L"weight", L"strike_force_mass_exp", 0.5f);
        fs.cutWeight = IniF(L"weight", L"strike_force_cut_weight", 0.1f);
        fs.floorNs = IniF(L"weight", L"strike_force_floor_ns", 1.0f);
        fs.shared = IniB(L"weight", L"strike_force_shared", true);
        fs.stuck = (int)GetPrivateProfileIntW(L"weight", L"strike_force_stuck", 0, g_iniPath);
        g_set.strikeByForce.store(fs.on);
        meleetouch::SetStrikeForce(fs);
        weight::CleaveSettings cs;   // [weight] cleave: a strong strike goes on through the body into the next (its numbers times the same threshold)
        cs.on = IniB(L"weight", L"cleave", true) && g_set.weightOn.load() && g_set.weightMonsterBodies.load();
        cs.over = IniF(L"weight", L"cleave_over", 2.0f);
        cs.cost = IniF(L"weight", L"cleave_cost", 2.0f);
        cs.minSpeed = IniF(L"weight", L"cleave_min_speed", 4.0f);
        cs.general = fs.general;
        cs.blunt = fs.blunt;
        cs.edged = fs.edged;
        cs.piercing = fs.piercing;
        cs.massExp = fs.massExp;
        cs.floorNs = fs.floorNs;
        weight::SetCleave(cs);
        meleetouch::SetStrikeForceDebug(IniB(L"debug", L"strike_force_debug", false));
    }
    g_set.weightStabIn.store(IniB(L"weight", L"stab_in", false));
    g_set.stabMinSpeed.store(IniF(L"weight", L"stab_min_speed", 0.5f));      // m/s
    g_set.stabMaxAngleDeg.store(IniF(L"weight", L"stab_max_angle", 60.0f));  // deg against straight in
    g_set.stabResistPct.store(IniF(L"weight", L"stab_resist_pct", 30.0f));   // % of the toolkit's dampers (5000 / 1000 N s/m)
    g_set.stabDepthCm.store(IniF(L"weight", L"stab_depth_cm", 0.0f));        // 0 = the blade's length
    g_set.stabTearCm.store(IniF(L"weight", L"stab_tear_cm", 45.0f));         // the hand across the line
    g_set.stabUnlockN.store(IniF(L"weight", L"stab_unlock_n", 150.0f));      // the pull that unlocks a resting stab
    g_set.stickByStrength.store(IniB(L"weight", L"stick_by_strength", false));   // how hard a weapon sticks: by the strength table (vr/stick_strength.h)
    g_set.stickPct.store(std::clamp(IniF(L"weight", L"stick_pct", 100.0f), 0.0f, 300.0f));
    meleetouch::SetBodiesWanted(g_set.weightMonsterBodies.load());   // (the touch's scan hands the bodies over; off: it does not)
    {   // [weight] knock_props (vr/knock_props.h): the level's small props knocked about by the weapon and the fists - drawn only, F4, its own tick (off)
        static bool inited = false;
        if (!inited) {
            inited = true;
            std::wstring dir(g_iniPath);
            const size_t slash = dir.find_last_of(L"\\/");
            dir = slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
            knockprops::Init(&Log, [](int side, float amplitude, int ms) { haptics::Pulse(haptics::FromSkel(side), amplitude, ms); }, dir.c_str());
            potionhide::SetXformPass(&knockprops::Transformed);
        }
        const bool knock = IniB(L"weight", L"knock_props", false);
        knockprops::Configure(knock, IniF(L"weight", L"knock_strength_pct", 100.0f), std::clamp(IniF(L"weight", L"knock_haptic_pct", 35.0f), 0.0f, 100.0f) / 100.0f);
        potionhide::WantHooksForProps(knock);
        knockprops::SetTrace(IniB(L"debug", L"knock_props_trace", false));   // [debug] knock_props_trace: a log line per knock and per scan
        knockprops::SetGlow(IniB(L"debug", L"knock_props_glow", false), 0.6f);   // [debug] knock_props_glow: the props found, lit (h 0.6: x1.6 bright)
        g_set.weightGrabProps.store(IniB(L"weight", L"grab_props", false));   // [weight] grab_props: the props in the hand (its own box; with knock_props)
        knockprops::ConfigureGrab(g_set.weightGrabProps.load());
    }
    {   // [weight] world_contact / world_contact_hands (vr/world_contact.h): the held weapon and the bare hands meet the level's walls - drawn only, F4, off
        static bool inited = false;
        if (!inited) {
            inited = true;
            std::wstring dir(g_iniPath);   // (the plugin's folder: d2r_vr_props\solids.bin, made by D2R VR Settings)
            const size_t slash = dir.find_last_of(L"\\/");
            dir = slash == std::wstring::npos ? std::wstring() : dir.substr(0, slash + 1);
            worldcontact::Init(&Log, [](int side, float amplitude, int ms) { haptics::Pulse(haptics::FromSkel(side), amplitude, ms); }, dir.c_str());
        }
        worldcontact::Configure(IniB(L"weight", L"world_contact", false), IniB(L"weight", L"world_contact_hands", false), IniB(L"debug", L"world_contact_draw", false));
    }
    // [weight] monster_shields: the monsters' shields and weapons stop the held weapon, a blow they stop is no hit (F4 with the bodies: they are F4 only)
    meleetouch::SetShieldsWanted(g_set.weightMonsterBodies.load() && IniB(L"weight", L"monster_shields", false));
    {   // [weight] parry_*: the monster's blow met by the weapon or the shield (vr/parry.h) - the feel, the game's block shown, the claws' Weapon Block, the shield into Smite, a blow on the weapon
        parrywire::Settings ps;
        ps.feel = IniB(L"weight", L"parry_feel", true);
        ps.blockShow = IniB(L"weight", L"parry_block_show", true);
        ps.clawBlock = IniB(L"weight", L"parry_claw_block", true);
        ps.shieldSmite = IniB(L"weight", L"parry_shield_smite", true);
        ps.countsHit = IniB(L"weight", L"parry_counts_hit", false);
        ps.shields = g_set.weightMonsterBodies.load() && IniB(L"weight", L"monster_shields", false);   // (the monsters' weapons are there to meet)
        parrywire::Configure(ps);
    }
    g_set.weightMonsterRagdoll.store(IniB(L"weight", L"monster_ragdoll", false));
    meleetouch::SetSwayWanted(g_set.weightMonsterBodies.load() && g_set.weightMonsterRagdoll.load());
    meleetouch::SetPoseWanted(g_set.weightMonsterBodies.load() && g_set.weightMonsterRagdoll.load() && IniB(L"weight", L"monster_ragdoll_pose", false));   // [weight] monster_ragdoll_pose
    meleetouch::SetOwnBodyWanted(IniB(L"weight", L"own_body", false));   // [weight] own_body: the weapon stops on the hero's own body (World contact; monster_bodies not needed)
    {   // [weight] knock_down: a blow, a running hit, a sweep at the legs or a shove knocks a humanoid off its feet - as drawn (its own box; the bodies of
        // monster_bodies, the town's people with npc_bodies; the knock-down library, the weight library's D2rf* exports)
        knockdown::Settings ks;
        ks.on = g_set.weightMonsterBodies.load() && IniB(L"weight", L"knock_down", false);
        ks.throwOn = IniB(L"weight", L"grab_bodies", false) && IniB(L"weight", L"grab_throw", false);   // [weight] grab_throw: the hands take, lift and throw a humanoid
        ks.impaleOn = g_set.weightMonsterBodies.load() && g_set.weightStabIn.load() && IniB(L"weight", L"impale_drag", false);   // [weight] impale_drag: a charge with a spear carries it
        ks.needCms = std::clamp(IniF(L"weight", L"knock_down_cms", 80.0f), 10.0f, 500.0f);
        ks.minSwingMs = std::clamp(IniF(L"weight", L"knock_down_min_speed", 5.0f), 0.0f, 30.0f);
        ks.staggerPct = std::clamp(IniF(L"weight", L"knock_down_stagger_pct", 45.0f), 0.0f, 99.0f);
        ks.lieMs = std::clamp(IniF(L"weight", L"knock_down_lie_ms", 1500.0f), 0.0f, 10000.0f);
        ks.massScale = std::clamp(IniF(L"weight", L"monster_weight_pct", 100.0f), 10.0f, 400.0f) * 0.01f;
        ks.strength = g_set.weightStrength.load() * 0.01f;
        ks.deathOn = g_set.weightMonsterBodies.load() && IniB(L"weight", L"death_ragdoll", false);
        ks.deathFlatOn = IniB(L"weight", L"death_ragdoll_flat", false);   // [weight] death_ragdoll_flat: the same in VR F1-F3 and the flat game, by the game's own swing (no bodies key needed)   // [weight] death_ragdoll: a kill falls as our ragdoll (vr/death_ragdoll.h)
        ks.leapOn = g_set.weightMonsterBodies.load() && IniB(L"weight", L"leap_landing_scatter", false);   // [weight] leap_landing_scatter: a Leap's landing throws them apart (vr/leap_scatter.h)
        ks.balance = IniB(L"weight", L"grab_balance", true);   // [weight] grab_balance: a pulled humanoid steps to keep its feet (on for the test; off: it falls, as 0.192.34)   // ([weight] strength: the hands holding a body, as on the weapon and the props)
        knockdown::Configure(ks);
        grabmove::SetWanted(ks.throwOn && IniB(L"weight", L"grab_moves_unit", false));   // [weight] grab_moves_unit: what grab_throw carries and throws, moved by the game (host / single player)
        grabmove::SetKnockWanted(ks.on && IniB(L"weight", L"knock_down_moves_unit", false));   // [weight] knock_down_moves_unit: what a blow knocks down, moved by the game too
        grabmove::SetLeapWanted(ks.leapOn);   // [weight] leap_landing_scatter: what a leap's landing throws, its unit after it by the game's knockback (host / single player)
        if (ks.throwOn) skel::SetHeldWanted(true);   // (what the monsters hold made capsules: a hand takes a monster by its spear - Serg: «я не могу схватить монстра за оружие»)
    }
    headshadow::WantForDoll((g_set.weightMonsterBodies.load() && g_set.weightMonsterRagdoll.load() && IniB(L"weight", L"monster_ragdoll_pose", false)) ||
                            knockdown::Wanted());   // ... through the skinning composite ([weight] knock_down draws its bodies the same way)
    headshadow::SetDollAnyView(knockdown::DeathFlatWanted());   // [weight] death_ragdoll_flat: the dead's poses in VR F1 / F3 too (Serg's exception to F4 only)
    headshadow::SetAttachWriter(knockdown::Wanted() ? &knockdown::OnXform : nullptr);   // [weight] knock_down: a knocked-down body's weapon stays in its hand
    headshadow::SetBeforeDraw(knockdown::Wanted() ? &knockdown::BeforeDraw : nullptr);  // ... its bounds grown again just before each draw collect of it
    skel::SetRagdollAll(g_set.weightMonsterBodies.load() && g_set.weightMonsterRagdoll.load() && IniB(L"weight", L"monster_ragdoll_all", false));   // [weight] monster_ragdoll_all
    bonecap::Set(std::lround(IniF(L"weight", L"skeleton_bones_max", static_cast<float>(bonecap::kDefault))));   // [weight] skeleton_bones_max (vr/bone_cap.h; live: the rigs read again)
    meleetouch::SetSwayTuning(std::clamp(IniF(L"weight", L"monster_weight_pct", 100.0f), 10.0f, 400.0f), std::clamp(IniF(L"weight", L"monster_push_pct", 100.0f), 0.0f, 400.0f));
    g_set.weightGrabBodies.store(IniB(L"weight", L"grab_bodies", false));   // [weight] grab_bodies (its own box; the bodies come from hit by touch's scan)
    g_set.weightGrabReachCm.store(std::clamp(IniF(L"weight", L"grab_reach_cm", 8.0f), 0.0f, 30.0f));
    g_set.weightGrabBreakCm.store(std::clamp(IniF(L"weight", L"grab_break_cm", 80.0f), 20.0f, 150.0f));
    g_set.weightGrabArmReach.store(IniB(L"weight", L"grab_arm_reach", true));
    g_set.orbDrawnHand.store(IniB(L"hud", L"hand_vitals_orb_drawn_hand", true));
    g_set.weightGrabWrap.store(IniB(L"weight", L"grab_wrap", true));
    g_set.weightGrabMusclePct.store(std::clamp(IniF(L"weight", L"grab_muscle_pct", 100.0f), 10.0f, 600.0f));
    meleetouch::SetGrabWanted(g_set.weightGrabBodies.load());
    g_set.weightGrabItems.store(IniB(L"weight", L"grab_items", false));   // [weight] grab_items (its own box)
    g_set.weightGrabItemsReachCm.store(std::clamp(IniF(L"weight", L"grab_items_reach_cm", 25.0f), 5.0f, 60.0f));
    g_set.weightGrabItemsEmptyGrip.store(IniB(L"weight", L"grab_items_empty_grip", true));   // [weight] grab_items_empty_grip (its own box)
    meleetouch::SetItemsWanted(g_set.weightGrabItems.load());
    g_set.weightThrownFlyMs.store(std::clamp(IniF(L"weight", L"thrown_fly_ms", 3000.0f), 100.0f, 5000.0f));
    g_set.weightThrowReachPct.store(std::clamp(IniF(L"weight", L"throw_reach_pct", 800.0f), 50.0f, 1500.0f));
    g_set.weightThrowMaxCells.store(std::clamp(IniF(L"weight", L"throw_max_cells", 48.0f), 10.0f, 48.0f));
    g_set.weightBackTakeCm.store(std::clamp(IniF(L"weight", L"back_take_cm", 25.0f), 0.0f, 60.0f));
    g_set.weightThrownModel.store(std::clamp((int)IniF(L"weight", L"thrown_model", 0.0f), 0, 1));
    g_set.weightThrownHandoffMs.store(std::clamp(IniF(L"weight", L"thrown_handoff_ms", 0.0f), 0.0f, 1500.0f));
    g_set.weightThrownHandEmptyMs.store(std::clamp(IniF(L"weight", L"thrown_hand_empty_ms", 900.0f), 0.0f, 3000.0f));
    g_set.weightSlide.store(IniB(L"weight", L"slide_second_hand", true));
    {   // [weight] slide_second_hand_<kind>: the kinds of weapon whose second hand slides along the shaft (vr/slide_kinds.h); the others hold where the hand closed
        uint32_t mask = 0;
        for (int i = 0; i < slidekinds::kCount; ++i) {
            wchar_t key[64];
            swprintf_s(key, L"slide_second_hand_%hs", slidekinds::kKinds[i].name);
            if (IniB(L"weight", key, slidekinds::kKinds[i].slides)) mask |= 1u << i;
        }
        g_set.weightSlideKinds.store(mask);
    }
    g_set.weightHiltOnly.store(IniB(L"weight", L"grip_hilt_only", false));
    for (int i = 0; i < gripzone::kCount; ++i) {
        wchar_t key[64];
        swprintf_s(key, L"grip_hilt_cm_%hs", gripzone::kKinds[i].name);
        g_set.weightHiltCm[i].store(std::clamp(IniF(L"weight", key, 0.0f), 0.0f, gripzone::kMaxCm));
    }
    {
        static const wchar_t* const kLong[6] = {L"back_long_x", L"back_long_y", L"back_long_z", L"back_long_pitch", L"back_long_yaw", L"back_long_roll"};
        static const wchar_t* const kShort[6] = {L"back_short_x", L"back_short_y", L"back_short_z", L"back_short_pitch", L"back_short_yaw", L"back_short_roll"};
        for (int k = 0; k < 6; ++k) {
            const float lim = k < 3 ? 40.0f : 180.0f;   // cm / deg
            g_set.weightBackLong[k].store(std::clamp(IniF(L"weight", kLong[k], 0.0f), -lim, lim));
            g_set.weightBackShort[k].store(std::clamp(IniF(L"weight", kShort[k], 0.0f), -lim, lim));
        }
    }
    g_set.staffHand.store(IniF(L"hands", L"staff_hand", 0.0f) != 0.0f ? 1 : 0);
    g_set.staffShaftAxis.store(std::clamp((int)IniF(L"hands", L"staff_shaft_axis", -2.0f), -3, 3));
    g_set.spearShaftAxis.store(std::clamp((int)IniF(L"hands", L"spear_shaft_axis", -2.0f), -3, 3));
    g_set.bodyYawDeg.store(std::clamp(IniF(L"body", L"yaw", 0.0f), -180.0f, 180.0f));
    g_set.wrist.store(IniB(L"hands", L"wrist", true));
    g_set.wristPitch.store(std::clamp(IniF(L"hands", L"pitch", 0.0f), -180.0f, 180.0f));
    g_set.wristYaw.store(std::clamp(IniF(L"hands", L"yaw", 0.0f), -180.0f, 180.0f));
    g_set.wristRoll.store(std::clamp(IniF(L"hands", L"roll", 0.0f), -180.0f, 180.0f));
    char b[256];
    snprintf(b, sizeof b, "vrcam settings: fov %.0f distance %.2f height %.1f pitch %.0f | head yaw %s sign %+.0f | stick rotate %s %s sign %+.0f squash %.2f angle x%.2f",
             g_set.fov.load(), g_set.distance.load(), g_set.height.load(), g_set.pitch.load(), g_set.headYaw.load() ? "on" : "off",
             g_set.yawSign.load(), g_set.stickRotate.load() ? "on" : "off", g_set.stickExact.load() ? "exact" : "approx",
             g_set.stickSign.load(), g_set.stickSquash.load(), g_set.stickAngleScale.load());
    Log(b);
    snprintf(b, sizeof b, "vrcam settings: [flatvr] frame_sync %d (late %.2f of a frame) head_level %d head_position %d room_centre %d recenter_on_enter %d%s",
             g_set.flatSync.load(), g_set.flatSyncLate.load(), g_set.flatLevel.load() ? 1 : 0, g_set.flatPosition.load() ? 1 : 0,
             g_set.flatRoomCentre.load() ? 1 : 0, g_set.flatEnterCentre.load() ? 1 : 0,
             g_set.platform.load() == 1 ? "" : " (VR only: flat mode leaves them)");
    Log(b);
    {   // [debug] patch_hunt 7 / 9: our own sky, fog, cave ceiling and distance blur, and the game's dome and ceiling we lay, off (the ini's values told)
        suspects.fog = g_set.fogOn.load();
        suspects.sky = g_set.skyOn.load();
        suspects.gameSky = g_set.gameSky.load();
        suspects.ceiling = g_set.ceilOn.load();
        suspects.gameCeiling = g_set.gameCeiling.load();
        suspects.blur = g_set.distanceBlur.load();
        if (patchhunt::OwnEffectsOff(hunt)) {
            g_set.fogOn.store(false);
            g_set.skyOn.store(false);
            g_set.gameSky.store(false);
            g_set.ceilOn.store(false);
            g_set.gameCeiling.store(false);
            g_set.distanceBlur.store(false);
        }
        suspects.platform = g_set.platform.load();
        suspects.flatShiftX = g_set.flatShiftX.load();
        suspects.flatShiftY = g_set.flatShiftY.load();
        patchhunt::Configure(hunt, std::max(0, (int)IniF(L"debug", L"patch_hunt_pipe", 0.0f)), suspects);
    }
    {   // [debug] capture_key (not in Settings: told by hand): the capture's key, empty = Ctrl+Alt+F10, none = off (vr/capture.h)
        wchar_t k[64];
        GetPrivateProfileStringW(L"debug", L"capture_key", L"", k, (DWORD)std::size(k), g_iniPath);
        capture::Configure(k, IniB(L"debug", L"capture_targets", false));   // (the key with the intermediate targets too: ~0.1 - 0.4 GB read back at the press)
    }
    const std::string language = uilang::Load(g_iniPath);   // ([hud] headset_language; read again only when it, the language or its file changed)
    if (!language.empty()) Log(language.c_str());
}

// Re-read when the file's write time moves. Returns true if it did. A language picked in D2R VR Settings (its own
// d2r_vr_settings.ini, beside d2r_vr.ini) is taken up too: the headset's words follow it ([hud] headset_language).
FILETIME g_uiIniTime{};
bool ReloadIfChanged() {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(g_iniPath, GetFileExInfoStandard, &fa)) return false;
    if (autotunerun::TakeReload()) {   // [autotune]: a run changed the values it hands the reads - read again, the file untouched
        g_iniTime = fa.ftLastWriteTime;
        LoadSettings();
        return true;
    }
    if (CompareFileTime(&fa.ftLastWriteTime, &g_iniTime) == 0) {
        std::wstring ui = g_iniPath;
        ui.resize(ui.find_last_of(L"\\/") + 1);
        WIN32_FILE_ATTRIBUTE_DATA ua{};
        if (GetFileAttributesExW((ui + L"d2r_vr_settings.ini").c_str(), GetFileExInfoStandard, &ua) && CompareFileTime(&ua.ftLastWriteTime, &g_uiIniTime) != 0) {
            g_uiIniTime = ua.ftLastWriteTime;
            const std::string language = uilang::Load(g_iniPath);
            if (!language.empty()) Log(language.c_str());
        }
        return false;
    }
    g_iniTime = fa.ftLastWriteTime;
    LoadSettings();
    return true;
}

}  // namespace vrc
