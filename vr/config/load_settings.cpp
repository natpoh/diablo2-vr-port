// vr/config/load_settings.cpp - d2r_vr.ini read into g_set and handed to every feature (LoadSettings), again whenever the file changes
// (ReloadIfChanged) - cut from vr/vrcam.cpp (docs/plan_modules.md M2). It reaches into most features: it sees vrcam.cpp's headers
// (vr/vrcam_headers.h) and the names of vrcam.cpp it sets (vr/vrcam_internal.h). Its place in the update thread is the same.
#include "../vrcam_headers.h"

using namespace D2RL;

namespace vrc {

FILETIME g_iniTime{};

void LoadSettings() {
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
        lowhp::Configure(lh);
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
        hv.on = wrists || IniB(L"hud", L"hand_vitals", false);
        hv.hand = (int)IniF(L"hud", L"hand_vitals_hand", 0.0f);
        hv.sizeCm = IniF(L"hud", L"hand_vitals_size_cm", 8.0f);
        hv.heightCm = IniF(L"hud", L"hand_vitals_height_cm", 6.0f);
        hv.opacity = IniF(L"hud", L"hand_vitals_opacity", 1.0f);
        hv.coneDeg = IniF(L"hud", L"hand_vitals_cone", 35.0f);
        hv.pulse = IniB(L"hud", L"hand_vitals_pulse", false);
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
        hv.orbOnBone = IniB(L"hud", L"hand_vitals_orb_on_bone", true);   // on the forearm bone's own mesh box (handorbs::OnBone), no spot numbers
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
        ws.companions = IniB(L"hud", L"wrist_status_companions", true);
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
    g_set.hudMap.store(std::clamp((int)IniF(L"hud", L"map", 0.0f), 0, 1));
    hud::SetMapMode(g_set.hudMap.load());
    hud::SetInterfaceScale(GameInterfaceScale());
    // [hud] hide = 1 takes the toolbar's strip out of the game's interface layer: that strip is worked out from the layer's size and Safe Screen Percent and fits the game's art only as
    // far as the 1920 x 1920 picture it was measured on - at the user's 2048 x 2048 (80 %) the horns, the statues' hands and the globes' tops stayed 10 - 55 px over the cut. So the strip
    // can be made taller above (hide_up_pct) and below (hide_down_pct), percent of the picture's height; [debug] hide_strip_tint paints the strip a see-through magenta to see its edges.
    hud::SetToolbarCut(IniF(L"hud", L"hide_up_pct", 0.0f), IniF(L"hud", L"hide_down_pct", 0.0f), IniB(L"debug", L"hide_strip_tint", false));
    hud::SetMapCorner((int)IniF(L"hud", L"map_corner", 0.0f));
    g_set.mapDirection.store(IniB(L"hud", L"map_direction", true));   // the arrow on the game's map in our camera's views: MapDirectionTick
    g_set.mapTurn.store(IniB(L"hud", L"map_rotate", false) || IniB(L"hud", L"map_compass", false));   // the head's way for the map in the hand (xr.cpp reads the keys itself)
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
    plateworld::SetTrack(g_set.plateFar.load() < 0.995f || g_set.plateDepthBody.load() > 0.005f);   // the hook notes the targeted monster for them
    g_set.plateNearBody.store(std::clamp(IniF(L"hud", L"monster_near_body", g_set.labelsNearBody.load()), -20.0f, 20.0f));   // (no key: where the labels' depth has always put it)
    plateworld::SetFollow(platelook::PlateNeedsBox(true, g_set.plateNearBody.load(), g_set.labelsNearBody.load(), g_set.plateAlpha.load(), g_set.plateSize.load(),
                                                   g_set.plateFar.load(), g_set.plateDepthBody.load()));   // F4 needs the plate's box: the hook says where the plate hangs (PlateBoxUv)
    g_set.labelsDepthBody.store(std::clamp(IniF(L"hud", L"labels_depth_body", 0.0f), 0.0f, 150.0f) * 0.01f);
    g_set.labelsFarBody.store(std::clamp(IniF(L"hud", L"labels_far", 100.0f), 20.0f, 100.0f) * 0.01f);
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
    g_ownDepth.store(IniB(L"render", L"own_depth", true));
    g_squareWin.store(IniB(L"openxr", L"square_window", false));
    {   // the game's light culling at non-16:10 sizes: made whenever the key is on (read at the game's start), bound in VR, and in flat mode with light_bounds_flat
        const bool fix = IniB(L"render", L"light_bounds_fix", true), flat = g_set.platform.load() == 0;
        lightbounds::Configure(fix, !flat || IniB(L"render", L"light_bounds_flat", true), IniB(L"debug", L"log_compute_shaders", false), flat);
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
    g_set.vcache.store(IniB(L"render", L"vcache", false));
    {
        static const int kHz[5] = {72, 75, 80, 90, 120};
        g_set.headsetHz.store(kHz[std::clamp((int)IniF(L"stereo", L"headset_rate", 3.0f), 0, 4)]);
    }
    replay::g_want.store((int)IniF(L"stereo", L"replay_right", 0.0f));
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
    replay::g_dlssBoth.store(IniB(L"stereo", L"replay_dlss", false));
    replay::g_replayCopy.store(IniB(L"stereo", L"replay_copy", true));
    replay::g_fxAtHold.store(IniB(L"stereo", L"replay_fx", true));
    // [render] monitor_eye: the window one eye (0 as before, 1 left, 2 right), cropped to the window's shape; by turns the eye made or only drawn ones
    monitoreye::Configure((int)IniF(L"render", L"monitor_eye", 1.0f), IniB(L"render", L"monitor_eye_fit", true), IniB(L"render", L"monitor_eye_drawn_only", false));
    xr::LoadSettings(g_iniPath);   // [openxr]: the game in the headset itself (vr/xr.cpp)
    calib::LoadSettings(g_iniPath);   // [calibration]: the measured height and arm span (vr/calibration.cpp)
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
    fogframe::SetWanted(IniB(L"render", L"fog_in_frame", false));   // (the same: read at the game's start)
    fx::g_fifPaint.store(std::clamp((int)IniF(L"debug", L"fog_in_frame_paint", 0.0f), 0, 4));   // live
    g_set.fogStart.store(std::clamp(IniF(L"fog", L"start", 150.0f), 0.0f, 100000.0f));
    g_set.fogEnd.store(std::clamp(IniF(L"fog", L"end", 600.0f), g_set.fogStart.load() + 1.0f, 200000.0f));
    // caves: their own range, the open air's when not set (caves are smaller, their fog nearer)
    g_set.fogCaveStart.store(std::clamp(IniF(L"fog", L"caves_start", g_set.fogStart.load()), 0.0f, 100000.0f));
    g_set.fogCaveEnd.store(std::clamp(IniF(L"fog", L"caves_end", g_set.fogEnd.load()), g_set.fogCaveStart.load() + 1.0f, 200000.0f));
    g_set.fogCaveStrength.store(std::clamp(IniF(L"fog", L"caves_strength", 1.0f), 0.0f, 1.0f));
    g_set.fogCurve.store(std::clamp(IniF(L"fog", L"curve", 1.6f), 0.3f, 4.0f));
    g_set.fogBlur.store(std::clamp(IniF(L"fog", L"blur", 3.0f), 0.0f, 12.0f));
    g_set.fogLight.store(IniB(L"fog", L"light", false));
    g_set.gameFog.store(IniB(L"fog", L"game_fog", false));   // [fog] game_fog: the game's own fog with our numbers (VR F4)
    g_set.fogHeight.store(IniB(L"fog", L"game_fog_height", false));
    g_set.fogTop.store(std::clamp(IniF(L"fog", L"game_fog_top", 60.0f), -100.0f, 2000.0f));
    g_set.fogSoft.store(std::clamp(IniF(L"fog", L"game_fog_soft", 50.0f), 1.0f, 1000.0f));
    g_set.fogTopFromFloor.store(IniB(L"fog", L"game_fog_top_from_floor", false));
    g_set.fogMist.store(IniB(L"fog", L"game_fog_mist", true));
    g_set.fogCavesBlack.store(IniB(L"fog", L"game_fog_caves_black", true));
    g_set.fogBeforeFx.store(IniB(L"fog", L"game_fog_before_fx", false));
    g_set.fogOurCurve.store(IniB(L"fog", L"game_fog_our_curve", false));
    g_set.fogMatch.store(IniB(L"fog", L"game_fog_match", false));
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
        static std::wstring toldShaders;
        if (toldShaders != ss) { toldShaders = ss; LogF("vrcam: [render] skip_shaders - %d shader(s) left out", nh); }
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
    g_set.gameSkyTest.store(std::clamp((int)IniF(L"debug", L"game_sky_test", 0.0f), 0, 4));   // another model for the dome (draws only)
    g_set.gameSkyRadius.store(std::clamp(IniF(L"sky", L"game_sky_radius", 200.0f), 1.0f, 1.0e8f));   // (the slider: 50 - 30000; the ini to 10^8)
    g_set.gameDomeCaves.store(IniB(L"sky", L"game_dome_caves", false));
    g_set.gameDomeCavesSize.store(std::clamp(IniF(L"sky", L"game_dome_caves_size", 1.0f), 0.1f, 4.0f));
    LoadSkyActs();
    LoadCrosshairFile();
    g_set.skyClouds.store(std::clamp(IniF(L"sky", L"clouds", 1.0f), 0.0f, 3.0f));
    g_set.skyDayNight.store(IniB(L"sky", L"day_night", true));
    g_set.skyDrift.store(IniB(L"sky", L"drift", false));
    g_set.lightDay.store(std::clamp(IniF(L"sky", L"light_day", 128.0f), 1.0f, 255.0f));
    g_set.lightNight.store(std::clamp(IniF(L"sky", L"light_night", 65.0f), 0.0f, 254.0f));
    g_set.nightBright.store(std::clamp(IniF(L"sky", L"night_brightness", 0.25f), 0.0f, 1.0f));
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
    g_set.ceilTorches.store(IniB(L"ceiling", L"torches", false));
    g_set.ceilPictureFire.store(IniB(L"ceiling", L"fire_from_picture", false));
    g_set.ceilTorchBright.store(std::clamp(IniF(L"ceiling", L"torch_brightness", 0.6f), 0.0f, 5.0f));
    g_set.ceilTorchRadius.store(std::clamp(IniF(L"ceiling", L"torch_radius", 20.0f), 1.0f, 500.0f));
    g_set.ceilFireNearer.store(std::clamp(IniF(L"ceiling", L"fire_nearer", 0.0f), 0.0f, 200.0f));
    g_set.ceilTorchDist.store(std::clamp(IniF(L"ceiling", L"torch_distance", 80.0f), 5.0f, 1000.0f));
    g_set.ceilMonsters.store(IniB(L"ceiling", L"monster_lights", false));
    g_set.ceilMonsterBright.store(std::clamp(IniF(L"ceiling", L"monster_brightness", 0.35f), 0.0f, 3.0f));
    g_set.ceilHalo.store(std::clamp(IniF(L"ceiling", L"torch_halo", 1.0f), 0.0f, 1.0f));
    g_set.ceilTorchWarm.store(std::clamp(IniF(L"ceiling", L"torch_warmth", 0.6f), 0.0f, 1.0f));
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
    g_set.walkBack.store(IniB(L"body", L"walk_back", true));
    g_set.walkBackReverse.store(IniB(L"body", L"walk_back_reverse", true));
    g_set.dollFeet.store(IniB(L"body", L"doll_feet", false));
    g_set.underCamera.store(IniB(L"body", L"under_camera", true));
    g_set.leapCameraUp.store(IniB(L"body", L"leap_camera_up", false));
    g_set.crouch.store(IniB(L"body", L"crouch", false));
    g_set.crouchCm.store(std::clamp(IniF(L"body", L"crouch_cm", 20.0f), 5.0f, 60.0f));
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
    animcut::Configure(animcut::Kind::Bow, IniB(L"bow", L"quick_release", false), (int)std::lround(IniF(L"bow", L"quick_release_frame", 2.0f)));
    g_set.quickThrowPotions.store(IniB(L"hands", L"quick_throw_potions", false));
    g_set.grabNearest.store(IniB(L"belt", L"grab_nearest", true));
    g_set.grabTrigger.store(IniB(L"belt", L"grab_trigger", false));
    g_set.holdTakesTrigger.store(IniB(L"belt", L"hold_takes_trigger", true));
    animcut::Configure(animcut::Kind::Melee, IniB(L"hands", L"quick_strike", false), std::max(2, (int)std::lround(IniF(L"hands", L"quick_strike_frame", 2.0f))));   // (2 at the earliest: the author, 2026-10-20 - at 1 the Amazon's one-hand swing AMA11HS attacked and never hit; at 2 it hits)   // the melee blow made earlier inside the attack animation
    animcut::Configure(animcut::Kind::Throw, IniB(L"hands", L"quick_throw", false), (int)std::lround(IniF(L"hands", L"quick_throw_frame", 1.0f)));   // the thrown missile made earlier inside the throw animation   // the arrow made earlier inside the same attack (a change of the game's timing)
    shieldsmite::Configure(IniB(L"gameplay", L"shield_smite", false));
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
    meleetouch::SetBarrelsWanted(IniB(L"gameplay", L"melee_touch_barrels", false));   // a swing through a barrel / pot presses the game's own interact on it (vr/barrel_touch.h)
    standstill::SetWanted(IniB(L"gameplay", L"attack_in_place", true));   // F4: the plugin's attack presses never walk the hero (vr/stand_still.h)
    moveattack::SetWanted(IniB(L"gameplay", L"move_while_attacking", true));   // F4, EXPERIMENTAL: a spear / bow hero walks on once the blow is out (vr/move_attack.h)
    straywalk::SetWalkWanted(IniB(L"gameplay", L"block_stray_walk", true));     // F4: a walk or run to a point out of a click on bare ground is dropped (vr/stray_walk.h)
    straywalk::SetAttackRunWanted(IniB(L"gameplay", L"block_attack_run", false));   // F4: the run the game starts out of the pad cast of an attack of ours is dropped
    shottrace::SetWanted(IniB(L"debug", L"shot_trace", false));   // the log tells, shot by shot, what the game does with an attack of ours (vr/shot_trace.h)
    straywalk::SetClickWanted(IniB(L"gameplay", L"block_stray_clicks", true));  // F4: no mouse button reaches the game in the world with no panel open
    // Settings' "Direct hit" selector writes both: Off 0 / 0, By the rules 2 / 1, Fully on 1 / 2 (the old tick boxes' 1 kept their meaning)
    const int directMode = std::clamp((int)std::lround(IniF(L"gameplay", L"melee_direct", 0.0f)), 0, 2);
    const int clientMode = std::clamp((int)std::lround(IniF(L"gameplay", L"melee_direct_client", 0.0f)), 0, 2);
    meleedirect::SetWanted(directMode != 0);   // the user's direct hit where the server runs (vr/melee_direct.h)
    meleedirect::SetByRules(directMode == 2);
    meleedirect::SetMinIntervalMs((uint32_t)std::clamp(IniF(L"gameplay", L"melee_direct_min_ms", 0.0f), 0.0f, 3000.0f));
    meleedirectclient::SetMode(clientMode);   // a joined TCP/IP player's touch: the game's own command (vr/melee_direct_client.h)
    {   // ... and its rules for a touch: the least speed where it touched (m/s), force (the strike's score, N s) and swing length (m)
        meleedirectclient::Mins m;
        m.speed = std::clamp(IniF(L"gameplay", L"melee_direct_client_min_speed", 2.5f), 0.0f, 10.0f);
        m.force = std::clamp(IniF(L"gameplay", L"melee_direct_client_min_force", 1.5f), 0.0f, 20.0f);
        m.length = std::clamp(IniF(L"gameplay", L"melee_direct_client_min_length", 0.2f), 0.0f, 3.0f);
        meleedirectclient::SetMins(m);
    }
    {   // [gameplay] melee_direct_client_cleave: a swing's extra hits through the host's server attack (needs melee_direct_client; the host ticks both too)
        const bool cleave = IniB(L"gameplay", L"melee_direct_client_cleave", false) && (clientMode == 1 || directMode == 2);   // (by the rules only)
        meleedirectclient::SetCleave(cleave);
        meleedirect::SetRemoteWanted(cleave);   // as the host: the joined players' extra hits are struck here
    }
    meleetouch::SetUnchecked(g_set.meleeTouchUnchecked.load());
    meleetouch::SetNoAttack(IniB(L"debug", L"melee_touch_no_attack", false));   // a touch presses nothing (the stab and the push tried alone)
    meleetouch::SetClientVerdict(g_set.meleeTouchClientVerdict.load());
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
    meleetouch::SetOwnBodyWanted(g_set.weightMonsterBodies.load() && IniB(L"weight", L"own_body", false));   // [weight] own_body: the weapon stops on the hero's own body
    headshadow::WantForDoll(g_set.weightMonsterBodies.load() && g_set.weightMonsterRagdoll.load() && IniB(L"weight", L"monster_ragdoll_pose", false));   // ... through the skinning composite
    skel::SetRagdollAll(g_set.weightMonsterBodies.load() && g_set.weightMonsterRagdoll.load() && IniB(L"weight", L"monster_ragdoll_all", false));   // [weight] monster_ragdoll_all
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
    const std::string language = uilang::Load(g_iniPath);   // ([hud] headset_language; read again only when it, the language or its file changed)
    if (!language.empty()) Log(language.c_str());
}

// Re-read when the file's write time moves. Returns true if it did. A language picked in D2R VR Settings (its own
// d2r_vr_settings.ini, beside d2r_vr.ini) is taken up too: the headset's words follow it ([hud] headset_language).
FILETIME g_uiIniTime{};
bool ReloadIfChanged() {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(g_iniPath, GetFileExInfoStandard, &fa)) return false;
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
