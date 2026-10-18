// The wrist status panels in the headset ([hud] wrist_status) - the OpenXR side of vr/wrist_status_wire.*.
//
// NOT a header of its own: xr.cpp includes this file once, inside namespace xr { namespace {, after xr_hand_vitals.h, because it uses what is there and in
// xr.cpp's unnamed namespace (QuadAtlas, the session, the room's space). What decides when a panel shows and where it hangs is vr/wrist_status.h (pure,
// tested) and vr/wrist_status_wire.cpp (the frames, what the hero has, the painter thread); this only uploads the finished pictures and hangs them as up to TWO quad
// layers in the room's space, one over each wrist - the same way the hand vitals' palm panel is hung (vr/xr_hand_vitals.h), each with its own fade.
//
// A layer's fade is the layer's colour scale (XR_KHR_composition_layer_color_scale_bias), a struct of its own for each layer: CloseFrame's loop sets the brightness
// link on every layer, and these layers' links are set after it. A runtime without the extension gets the panels without the fade (they show when they are more than half up).

QuadAtlas g_wsAtlas[2];
std::vector<uint32_t> g_wsPx[2];
uint32_t g_wsPxRev[2] = {}, g_wsUploadedRev[2] = {}, g_wsPxW[2] = {}, g_wsPxH[2] = {};
bool g_wsPxPending[2] = {};
float g_wsK[2] = {-1.0f, -1.0f};   // this frame's colour scale for each layer; < 0: the layer is not in the frame

uint32_t HangWristStatus(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max, XrCompositionLayerQuad* panel) {
    g_wsK[0] = g_wsK[1] = -1.0f;
    for (int side = 0; side < 2; ++side) {
        wriststatus::Layer L;
        uint32_t need = 0;
        if (!wriststatus::Draw(side, &L, &need)) continue;   // off, not F4, or this wrist not turned to the face: no cost beyond this call
        uint32_t rev = 0, pw = 0, ph = 0;
        if (wriststatus::Picture(side, &g_wsPx[side], &rev, &pw, &ph)) {
            g_wsPxRev[side] = rev;
            g_wsPxW[side] = pw;
            g_wsPxH[side] = ph;
            g_wsPxPending[side] = true;
        }
        if (g_wsPxPending[side] && g_wsPxW[side] && g_wsPxH[side] && QuadAtlasUpload(g_wsAtlas[side], g_wsPxW[side], g_wsPxH[side], g_wsPx[side], side == 0 ? "wrist status (left)" : "wrist status (right)")) {
            g_wsPxPending[side] = false;
            g_wsUploadedRev[side] = g_wsPxRev[side];
        }
        // Not before a picture at least as new as the moment the wrist turned has been uploaded (the swapchain holds the last time's).
        if (!L.visible || !g_wsAtlas[side].shown || g_wsUploadedRev[side] < need || nl >= max) continue;
        if (!g_colorScaleExt && L.colorScale < 0.5f) continue;
        QuadLayerOf(panel[side], g_wsAtlas[side], L.pose, L.widthM, L.heightM);
        panel[side].space = g_roomSpace ? g_roomSpace : g_space;   // the frames' poses are the room's (stage), where the hands were located
        panel[side].layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // (premultiplied)
        layers[nl++] = (const XrCompositionLayerBaseHeader*)&panel[side];
        g_wsK[side] = g_colorScaleExt ? std::clamp(L.colorScale, 0.0f, 1.0f) : 1.0f;
        static bool told[2] = {};
        if (!told[side]) {
            told[side] = true;
            LogF("vrcam: openxr - wrist status panel (%s): hung, %.1f x %.1f cm at the wrist%s", side == 0 ? "left" : "right", L.widthM * 100.0f, L.heightM * 100.0f,
                 g_colorScaleExt ? "" : " (the runtime has no colour scale: no fade)");
        }
    }
    return nl;
}

// With the layers in the frame: each one's colour scale (the fade) as the runtime applies it, over the brightness setting.
void WristStatusScale(XrCompositionLayerQuad* panel, XrCompositionLayerColorScaleBiasKHR* scale, float bright) {
    for (int side = 0; side < 2; ++side) {
        if (g_wsK[side] < 0.0f) continue;
        scale[side] = {XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};
        scale[side].colorScale = {bright * g_wsK[side], bright * g_wsK[side], bright * g_wsK[side], g_wsK[side]};
        scale[side].colorBias = {0.0f, 0.0f, 0.0f, 0.0f};
        const bool needed = g_colorScaleExt && (std::fabs(g_wsK[side] - 1.0f) > 0.001f || std::fabs(bright - 1.0f) > 0.001f);
        panel[side].next = needed ? &scale[side] : nullptr;
    }
}

void WristStatusTeardown() {   // under g_lock or before the session exists
    for (int side = 0; side < 2; ++side) {
        QuadAtlasDestroy(g_wsAtlas[side]);
        g_wsPx[side].clear();
        g_wsPx[side].shrink_to_fit();
        g_wsPxPending[side] = false;
        g_wsPxRev[side] = g_wsUploadedRev[side] = 0;
        g_wsPxW[side] = g_wsPxH[side] = 0;
        g_wsK[side] = -1.0f;
    }
    wriststatus::Teardown();
}
