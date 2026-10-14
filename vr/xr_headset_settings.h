// The settings panel in the headset ([openxr] headset_settings) - the OpenXR side of vr/hset_*.
//
// NOT a header of its own: xr.cpp includes this file once, inside namespace xr { namespace {, after vr/xr_quad_atlas.h,
// because it uses what is in xr.cpp's unnamed namespace (the session, the beam). Everything it decides is in the pure
// headers beside it, tested without a headset (tools/hset_test.cpp):
//   hset_panel.h  the chord, the lasers against the panel, clicks, the release gate - a frame in, a frame out
//   hset_model.h  the keys' draft, the buttons' places, Apply
//   hset_paint    the picture; hset_store  the ini on a thread of its own (shared/ini_txn: backup, conflict)
// This file only feeds the session's hands in and carries the answer out: the controllers' values the rest of
// the mod reads (Publish: BodyWalk through Game Link, the plugin's own input) with the panel's share taken
// out, the panel as a quad layer with its beam and dot, its picture in a swapchain of its own.
//
// It never sends a click or a key to the game: nothing here calls SendInput or SetCursorPos, and while the panel
// is up the game's own laser stands down (LaserStep, why 8).
//
// Idea and layout after bearstonem/d2r-pcvr's settings panel (steamvr/transport.cpp OpenSettings / SettingsPointer,
// vr_settings_ui.cpp; MIT); written again for our layer machinery.

static_assert(BW_LINK_XRB_PRIMARY == hset::kPrimary && BW_LINK_XRB_SECONDARY == hset::kSecondary && BW_LINK_XRB_STICK == hset::kStick &&
              BW_LINK_XRB_MENU == hset::kMenu && BW_LINK_XRB_TRIGGER == hset::kTrigger && BW_LINK_XRB_SQUEEZE == hset::kSqueeze,
              "hset::Btn is Game Link's button bits");

hset::Controller g_hs;
bool g_hsStoreOn = false;   // the ini's thread runs: from both grips squeezed (the chord arming) until the panel is closed
QuadAtlas g_hsAtlas;
uint64_t g_hsPaintedRev = ~0ull;
hset::Hit g_hsPaintedHover;
ULONGLONG g_hsPaintAt = 0;
BeamView g_hsBeam;

XrVector3f HsXr(hset::V3 v) { return {v.x, v.y, v.z}; }

// The Video page's lines: the size of the game's output now, what the headset's runtime recommends and the most it takes (all 0
// until known; the same numbers D2R VR Settings reads from d2r_vr_headset.ini).
hset::video::Env HsEnv() {
    const auto size = [](uint64_t v) {
        const vrres::Size s = vrres::Unpack(v);
        return hset::video::Size{(int)std::min<uint32_t>(s.w, 1u << 20), (int)std::min<uint32_t>(s.h, 1u << 20)};
    };
    hset::video::Env e;
    e.now = size(g_outputSize.load());
    e.recommended = size(g_vrRecommended.load());
    e.maximum = size(g_vrMaximum.load());
    return e;
}

void HsStep(D2RVR_XrInput& in, bool focused) {
    hsetview::Tick();   // a view button's press is let go when it is due (before anything can return: the panel is closed by then)
    // With the skill panel in the tiles' look the settings are its Settings tab (vr/skill_wheel.cpp), which asks for the sizes it shows here:
    // the panel of their own stands down. F4 only (vr/view_gate.h): in another view it is as if the key were off - an open panel closes, and
    // what it had stays released until let go
    if (skillwheel::SettingsTabOn()) skillwheel::SetSettingsHost(HsEnv());
    const bool on = g_cfg.headsetSettings && g_iniPath[0] && viewgate::F4() && !skillwheel::SettingsTabOn();
    if (!on && !g_hs.Busy() && !g_hsStoreOn) return;   // off, and nothing of it left: no cost
    hset::FrameIn f;
    f.ms = UsNow() / 1000.0;
    f.enabled = on;
    f.focused = focused;
    f.laserHand = g_cfg.laserHand;
    f.calibrating = calib::Running();   // a T-pose measurement has the hands (the panel's "Calibrate body", or D2R VR Settings' button)
    f.env = HsEnv();
    f.view = on ? 4 : 0;   // the panel is F4's: the view it stands in is F4 (viewgate has no number)
    bool grips = true;
    for (int h = 0; h < 2; ++h) {
        const D2RVR_XrHand& o = in.hand[h];
        hset::Pad& p = f.pad[h];
        p.trigger = o.trigger;
        p.squeeze = o.squeeze;
        p.stickX = o.stickX;
        p.stickY = o.stickY;
        p.buttons = o.buttons & 0x3Fu;
        f.present[h] = o.active != 0;
        grips = grips && o.squeeze > hset::kReleaseAt;
    }
    const bool wake = on && (grips || g_hs.Open() || g_hs.Resuming());   // the chord is arming, the panel is up, or it will come back after a measurement
    if (wake && !g_hsStoreOn) {   // its ini is read now, so the panel has values the moment it opens
        hset::store::SetLogger(g_log);
        hset::store::Start(g_iniPath);
        g_hsStoreOn = true;
    }
    if (g_hsStoreOn) f.snapshot = hset::store::Read();
    if (wake) {
        PoseIn(g_viewSpace, &f.head);
        for (int h = 0; h < 2; ++h) if (g_hs.Open() || f.present[h]) PoseIn(g_aimSpace[h], &f.aim[h]);
    }

    const hset::FrameOut out = g_hs.Step(f);

    for (int h = 0; h < 2; ++h) {   // what the rest of the mod gets of the hands
        D2RVR_XrHand& o = in.hand[h];
        o.trigger = out.pad[h].trigger;
        o.squeeze = out.pad[h].squeeze;
        o.stickX = out.pad[h].stickX;
        o.stickY = out.pad[h].stickY;
        o.buttons = (o.buttons & ~0x3Fu) | (out.pad[h].buttons & 0x3Fu);
    }
    if (out.justOpened) {
        g_hsPaintedRev = ~0ull;   // painted at once
        g_hsPaintAt = 0;
        Log("vrcam: openxr - headset settings: opened (both hands are the panel's; nothing of them goes to the game)");
    }
    if (out.justClosed) Log("vrcam: openxr - headset settings: closed (what was held stays released until let go)");
    g_hsOpen.store(out.open);

    switch (out.action.what) {
        case hset::Do::Save:
            if (!hset::store::Submit(out.action.request)) {
                g_hs.SubmitFailed(out.action.request.sequence);
                Log("vrcam: openxr - headset settings: Apply could not be handed to the ini's thread");
            }
            break;
        case hset::Do::Recentre:
            Recenter();
            Log("vrcam: openxr - headset settings: Recentre view");
            break;
        case hset::Do::View:   // F1 - F4: the controller's own view path (vr/hset_view.h); the view leaves F4, so the panel closes with it
            if (hsetview::Press(out.action.view))
                LogF("vrcam: openxr - headset settings: game view F%d (the panel closes with it; F4 is the way back: the F4 key, D2R VR Settings > Home, or a view action bound in BodyWalk / [direct])", out.action.view);
            break;
        case hset::Do::Calibrate:   // the panel hides; the measurement's own prompt leads the player, and the panel comes back when it ends
            calib::RequestMeasure();
            Log("vrcam: openxr - headset settings: Calibrate body (the panel hides until the measurement is over)");
            break;
        default: break;
    }

    g_hsBeam = BeamView{};
    if (out.open && out.beam) {
        g_hsBeam.drawn = true;
        g_hsBeam.hit = out.hit;
        g_hsBeam.from = HsXr(out.from);
        g_hsBeam.dir = HsXr(out.dir);
        g_hsBeam.at = HsXr(out.at);
        g_hsBeam.len = out.len;
        g_hsBeam.surface = {out.panel.q.x, out.panel.q.y, out.panel.q.z, out.panel.q.w};
    }
    if (g_hsStoreOn && !out.open && !grips && !g_hs.Resuming()) {   // closed and no chord arming (and no measurement to come back from): the ini's thread ends (after an Apply in flight)
        hset::store::Stop();
        g_hsStoreOn = false;
    }
}

// Painted again when the draft, the answer or the button under the beam changed - at most ten times a second.
void HsPaint() {
    const ULONGLONG now = GetTickCount64();
    if (now < g_hsPaintAt) return;
    const hset::Hit hover = g_hs.hover;
    const uint64_t rev = g_hs.model.revision;
    if (g_hsAtlas.shown && g_hsPaintedRev == rev && g_hsPaintedHover == hover) return;
    g_hsPaintAt = now + 100;
    std::vector<uint32_t> px;
    if (!hset::Paint(g_hs.model, hover, px)) return;
    if (QuadAtlasUpload(g_hsAtlas, hset::kW, hset::kH, px, "headset settings")) { g_hsPaintedRev = rev; g_hsPaintedHover = hover; }
}

uint32_t HangSettings(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max,
                      XrCompositionLayerQuad& panel, XrCompositionLayerQuad& beam, XrCompositionLayerQuad& dot) {
    if (!g_hsOpen.load() || nl >= max) return nl;
    HsPaint();
    if (!g_hsAtlas.shown || !g_hsAtlas.sc) return nl;
    QuadLayerOf(panel, g_hsAtlas, g_hs.PanelPose(), hset::kWidthM, hset::kHeightM);
    layers[nl++] = (const XrCompositionLayerBaseHeader*)&panel;
    static bool told = false;
    if (!told) {
        told = true;
        LogF("vrcam: openxr - headset settings: the panel %.2f m wide, %.2f m ahead of the eyes, painted at most 10 times a second", hset::kWidthM, hset::kAheadM);
    }
    return HangBeam(g_hsBeam, layers, nl, max, beam, dot);
}

void HsTeardown() {   // under g_lock or before the session exists
    hsetview::LetGo();
    g_hsOpen.store(false);
    g_hs = hset::Controller{};
    g_hsBeam = BeamView{};
    if (g_hsStoreOn) { hset::store::Stop(); g_hsStoreOn = false; }
    hset::store::Join(1500);   // an Apply in flight is finished first
    QuadAtlasDestroy(g_hsAtlas);   // (the game's queue is waited for before a teardown)
    g_hsPaintedRev = ~0ull;
    g_hsPaintedHover = hset::Hit{};
    g_hsPaintAt = 0;
}
