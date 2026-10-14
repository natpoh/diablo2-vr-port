// The skill wheel in the headset ([openxr] skill_wheel) - the OpenXR side of vr/skill_wheel.*.
//
// NOT a header of its own: xr.cpp includes this file once, inside namespace xr { namespace {, after xr_quad_atlas.h, because it
// uses what is there and in xr.cpp's unnamed namespace (QuadAtlas, HangBeam, the session). What the wheel decides, draws and
// does with the controllers is in vr/skill_wheel.* and vr/wheel_*; this only hangs the finished picture as a quad layer with the
// laser's beam and dot over it, and lets it go with the session.
//
// The panel's picture is painted on a thread of its own (wheelpaint::Async) and uploaded here when a newer one is ready; the
// panel is not shown until a picture at least as new as the moment the wheel opened has been uploaded (the swapchain still holds
// the last time's).

QuadAtlas g_wheelAtlas;
std::vector<uint32_t> g_wheelPx;
uint32_t g_wheelPxRev = 0, g_wheelUploadedRev = 0;
uint32_t g_wheelPxW = 0, g_wheelPxH = 0;   // the picture's size: the wheel's is square, the tiles' window is wide
bool g_wheelPxPending = false;

uint32_t HangWheel(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max, XrCompositionLayerQuad& panel,
                   XrCompositionLayerQuad& beam, XrCompositionLayerQuad& dot) {
    wheel::Layer L;
    uint32_t need = 0;
    if (!skillwheel::Draw(&L, &need)) return nl;   // not up (or not switched on): no cost beyond this call
    uint32_t rev = 0, pw = 0, ph = 0;
    if (skillwheel::Picture(&g_wheelPx, &rev, &pw, &ph)) {
        g_wheelPxRev = rev;
        g_wheelPxW = pw;
        g_wheelPxH = ph;
        g_wheelPxPending = true;
    }
    if (g_wheelPxPending && g_wheelPxW && g_wheelPxH && QuadAtlasUpload(g_wheelAtlas, g_wheelPxW, g_wheelPxH, g_wheelPx, "skill wheel")) {
        g_wheelPxPending = false;
        g_wheelUploadedRev = g_wheelPxRev;
    }
    if (!g_wheelAtlas.shown || g_wheelUploadedRev < need || nl >= max) return nl;
    // The wheel's poses (the head, the hand, the panel, the ray) are the input layer's: in the ROOM (stage) space when the runtime has
    // one. The layers are hung in g_space (the session's reference space, LOCAL by default): the room's place in it, once a frame.
    hset::Pose room;
    if (g_roomSpace) {
        if (!PoseIn(g_roomSpace, &room)) return nl;
    } else {
        room.q = {0.0f, 0.0f, 0.0f, 1.0f};
        room.valid = true;
    }
    const hset::Pose where = wheel::Compose(room, L.panel);
    QuadLayerOf(panel, g_wheelAtlas, where, L.widthM, L.heightM);
    panel.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // (premultiplied, with a clear corner outside the ring)
    layers[nl++] = (const XrCompositionLayerBaseHeader*)&panel;
    static bool told = false;
    if (!told) {
        told = true;
        LogF("vrcam: openxr - skill panel (%s): hung, %.2f x %.2f m, painted on a thread of its own", L.style == wheel::Style::Tiles ? "tiles" : "wheel", L.widthM, L.heightM);
    }
    BeamView bv;
    if (L.beam) {
        bv.drawn = true;
        bv.hit = L.hit;
        const hset::V3 from = wheel::Apply(room, L.from), at = wheel::Apply(room, L.at), dir = hset::Rot(room.q, L.dir);
        bv.from = {from.x, from.y, from.z};
        bv.dir = {dir.x, dir.y, dir.z};
        bv.at = {at.x, at.y, at.z};
        bv.len = L.len;
        bv.surface = {where.q.x, where.q.y, where.q.z, where.q.w};
    }
    return HangBeam(bv, layers, nl, max, beam, dot);
}

void WheelTeardown() {   // under g_lock or before the session exists
    QuadAtlasDestroy(g_wheelAtlas);
    g_wheelPx.clear();
    g_wheelPx.shrink_to_fit();
    g_wheelPxPending = false;
    g_wheelPxRev = g_wheelUploadedRev = 0;
    g_wheelPxW = g_wheelPxH = 0;
    skillwheel::Teardown();
}
