// The hand vitals panel in the headset ([hud] hand_vitals) - the OpenXR side of vr/hand_vitals_wire.*.
//
// NOT a header of its own: xr.cpp includes this file once, inside namespace xr { namespace {, after xr_skill_wheel.h, because it uses
// what is there and in xr.cpp's unnamed namespace (QuadAtlas, the session, the room's space). What decides when the panel shows and
// where it hangs is vr/hand_vitals.h (pure, tested) and vr/hand_vitals_wire.cpp (the frames, the hero's numbers, the painter thread);
// this only uploads the finished picture and hangs it as ONE quad layer in the room's space, over the hand.
//
// The panel's fade and its soft pulse are the layer's colour scale (XR_KHR_composition_layer_color_scale_bias), a struct of its own for
// this layer: CloseFrame's loop sets the brightness link on every layer, and this layer's link is set after it. A runtime without the
// extension gets the panel without the fade (it shows when it is more than half up).

QuadAtlas g_vitalsAtlas;
std::vector<uint32_t> g_vitalsPx;
uint32_t g_vitalsPxRev = 0, g_vitalsUploadedRev = 0, g_vitalsPxW = 0, g_vitalsPxH = 0;
bool g_vitalsPxPending = false;
float g_vitalsK = -1.0f;   // this frame's colour scale for the layer; < 0: the layer is not in the frame

uint32_t HangVitals(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max, XrCompositionLayerQuad& panel) {
    g_vitalsK = -1.0f;
    handvitals::Layer L;
    uint32_t need = 0;
    if (!handvitals::Draw(&L, &need)) return nl;   // off, not F4, or no palm turned to the face: no cost beyond this call
    uint32_t rev = 0, pw = 0, ph = 0;
    if (handvitals::Picture(&g_vitalsPx, &rev, &pw, &ph)) {
        g_vitalsPxRev = rev;
        g_vitalsPxW = pw;
        g_vitalsPxH = ph;
        g_vitalsPxPending = true;
    }
    if (g_vitalsPxPending && g_vitalsPxW && g_vitalsPxH && QuadAtlasUpload(g_vitalsAtlas, g_vitalsPxW, g_vitalsPxH, g_vitalsPx, "hand vitals")) {
        g_vitalsPxPending = false;
        g_vitalsUploadedRev = g_vitalsPxRev;
    }
    // Not before a picture at least as new as the moment the palm turned has been uploaded (the swapchain holds the last time's).
    if (!L.visible || !g_vitalsAtlas.shown || g_vitalsUploadedRev < need || nl >= max) return nl;
    if (!g_colorScaleExt && L.colorScale < 0.5f) return nl;
    QuadLayerOf(panel, g_vitalsAtlas, L.pose, L.widthM, L.heightM);
    panel.space = g_roomSpace ? g_roomSpace : g_space;   // the frames' poses are the room's (stage), where the hands were located
    panel.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // (premultiplied)
    layers[nl++] = (const XrCompositionLayerBaseHeader*)&panel;
    g_vitalsK = g_colorScaleExt ? std::clamp(L.colorScale, 0.0f, 1.0f) : 1.0f;
    static bool told = false;
    if (!told) {
        told = true;
        LogF("vrcam: openxr - hand vitals panel: hung, %.1f x %.1f cm at the hand%s", L.widthM * 100.0f, L.heightM * 100.0f,
             g_colorScaleExt ? "" : " (the runtime has no colour scale: no fade)");
    }
    return nl;
}

// With the layer in the frame: the colour scale (the fade, the soft pulse) as the runtime applies it, over the brightness setting.
void VitalsScale(XrCompositionLayerQuad& panel, XrCompositionLayerColorScaleBiasKHR& scale, float bright) {
    if (g_vitalsK < 0.0f) return;
    scale = {XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};
    scale.colorScale = {bright * g_vitalsK, bright * g_vitalsK, bright * g_vitalsK, g_vitalsK};
    scale.colorBias = {0.0f, 0.0f, 0.0f, 0.0f};
    const bool needed = g_colorScaleExt && (std::fabs(g_vitalsK - 1.0f) > 0.001f || std::fabs(bright - 1.0f) > 0.001f);
    panel.next = needed ? &scale : nullptr;
}

void VitalsTeardown() {   // under g_lock or before the session exists
    QuadAtlasDestroy(g_vitalsAtlas);
    g_vitalsPx.clear();
    g_vitalsPx.shrink_to_fit();
    g_vitalsPxPending = false;
    g_vitalsPxRev = g_vitalsUploadedRev = 0;
    g_vitalsPxW = g_vitalsPxH = 0;
    g_vitalsK = -1.0f;
    handvitals::Teardown();
}
