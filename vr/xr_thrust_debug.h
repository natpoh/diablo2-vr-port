// The thrust debug's readout in the headset ([debug] thrust_debug) - the OpenXR side of vr/thrust_debug_wire.*.
//
// NOT a header of its own: xr.cpp includes this file once, inside namespace xr { namespace {, after xr_hand_vitals.h (QuadAtlas, the room's space). It uploads the
// finished picture and hangs it as ONE quad layer in the room's space, over the weapon hand.

QuadAtlas g_thrustAtlas;
std::vector<uint32_t> g_thrustPx;
uint32_t g_thrustPxRev = 0, g_thrustUploadedRev = 0;
bool g_thrustPxPending = false;

uint32_t HangThrustDebug(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max, XrCompositionLayerQuad& panel) {
    thrustdebugwire::Layer L;
    uint32_t need = 0;
    if (!thrustdebugwire::Draw(&L, &need)) return nl;   // off, not F4, or nothing to show: no cost beyond this call
    uint32_t rev = 0;
    if (thrustdebugwire::Picture(&g_thrustPx, &rev)) {
        g_thrustPxRev = rev;
        g_thrustPxPending = true;
    }
    if (g_thrustPxPending && QuadAtlasUpload(g_thrustAtlas, (uint32_t)thrustdebugwire::kW, (uint32_t)thrustdebugwire::kH, g_thrustPx, "thrust debug")) {
        g_thrustPxPending = false;
        g_thrustUploadedRev = g_thrustPxRev;
    }
    if (!g_thrustAtlas.shown || g_thrustUploadedRev < need || nl >= max) return nl;
    QuadLayerOf(panel, g_thrustAtlas, L.pose, L.widthM, L.heightM);
    panel.space = g_roomSpace ? g_roomSpace : g_space;   // the frames' poses are the room's (stage)
    panel.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // (premultiplied)
    layers[nl++] = (const XrCompositionLayerBaseHeader*)&panel;
    return nl;
}

void ThrustDebugTeardown() {   // under g_lock or before the session exists
    QuadAtlasDestroy(g_thrustAtlas);
    g_thrustPx.clear();
    g_thrustPx.shrink_to_fit();
    g_thrustPxPending = false;
    g_thrustPxRev = g_thrustUploadedRev = 0;
    thrustdebugwire::Teardown();
}
