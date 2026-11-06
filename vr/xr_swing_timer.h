// The swing timer in the headset ([hud] swing_timer) - the OpenXR side of vr/swing_timer_wire.*.
//
// NOT a header of its own: xr.cpp includes this file once, inside namespace xr { namespace {, after xr_thrust_debug.h (QuadAtlas, the room's space). It
// uploads the finished picture when a newer one is ready and hangs it as ONE quad layer in the room's space, held before the eyes (the ring in the middle
// of the view, or the bar over it). Nothing is hung while the timer shows nothing: no ring is the "go".

QuadAtlas g_swingAtlas;
std::vector<uint32_t> g_swingPx;
uint32_t g_swingPxRev = 0, g_swingUploadedRev = 0;
bool g_swingPxPending = false;
XrCompositionLayerQuad g_swingLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};

uint32_t HangSwingTimer(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max) {
    swingtimer::Layer L;
    uint32_t need = 0;
    if (!swingtimer::Draw(&L, &need)) return nl;   // off, not F4, or nothing to show: no cost beyond this call
    uint32_t rev = 0;
    if (swingtimer::Picture(&g_swingPx, &rev, g_swingPxRev)) {
        g_swingPxRev = rev;
        g_swingPxPending = true;
    }
    if (g_swingPxPending && QuadAtlasUpload(g_swingAtlas, (uint32_t)swingtimer::kPicW, (uint32_t)swingtimer::kPicH, g_swingPx, "swing timer")) {
        g_swingPxPending = false;
        g_swingUploadedRev = g_swingPxRev;
    }
    if (!g_swingAtlas.shown || g_swingUploadedRev < need || nl >= max) return nl;
    QuadLayerOf(g_swingLayer, g_swingAtlas, L.pose, L.widthM, L.heightM);
    g_swingLayer.space = g_roomSpace ? g_roomSpace : g_space;   // the head's pose is the room's (stage)
    g_swingLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // (premultiplied)
    layers[nl++] = (const XrCompositionLayerBaseHeader*)&g_swingLayer;
    static bool told = false;
    if (!told) {
        told = true;
        LogF("vrcam: openxr - swing timer: hung, %.2f x %.2f m, %.1f m before the eyes", L.widthM, L.heightM, swingtimer::kDistM);
    }
    return nl;
}

void SwingTimerTeardown() {   // under g_lock or before the session exists
    QuadAtlasDestroy(g_swingAtlas);
    g_swingPx.clear();
    g_swingPx.shrink_to_fit();
    g_swingPxPending = false;
    g_swingPxRev = g_swingUploadedRev = 0;
    swingtimer::Teardown();
}
