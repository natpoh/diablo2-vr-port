// The allies' portraits in the headset ([hud] allies_place) - the OpenXR side of vr/ally_portraits_wire.*.
//
// NOT a header of its own: xr.cpp includes this file once, inside namespace xr { namespace {, after xr_wrist_status.h, because it uses what is there and in
// xr.cpp's unnamed namespace (QuadAtlas, the session, g_allyAnchor). What is shown is vr/ally_portraits.h (pure, tested) and vr/ally_portraits_wire.cpp
// (the reads, the painter thread, the wrist's glance); this only uploads the finished picture and hangs it as ONE quad layer:
//   - under the map (place 1): under the map this frame's HangHud / HangBigMap hung (g_allyAnchor), in the map's own space and turn, as wide as
//     GridWidthM says, its top a small gap under the map's visible bottom (DropM);
//   - on the right wrist (place 2): where the wrist status panel would stand over the right wrist (the room's space), its bottom row there, more rows
//     above, over that panel when it is up too (WristLiftM); it grows in as the wrist turns, as the wrist status does.

QuadAtlas g_apAtlas;
std::vector<uint32_t> g_apPx;
uint32_t g_apPxRev = 0, g_apUploadedRev = 0, g_apPxW = 0, g_apPxH = 0;
bool g_apPxPending = false;
XrCompositionLayerQuad g_apLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};

uint32_t HangAllyPortraits(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max) {
    const AllyAnchor a = g_allyAnchor;
    g_allyAnchor = AllyAnchor{};   // (set again by the next frame's map)
    allyportraits::WristLayer wl;
    const bool grid = allyportraits::GridWanted() && a.ok;
    const bool wrist = !grid && allyportraits::WristWanted() && allyportraits::WristPanel(&wl);
    uint32_t need = 0;
    if ((!grid && !wrist) || !allyportraits::Want(&need)) return nl;   // the game's row, not F4, the place not in sight or nobody to show: no cost beyond this
    uint32_t rev = 0, pw = 0, ph = 0;
    if (allyportraits::Picture(&g_apPx, &rev, &pw, &ph)) {
        g_apPxRev = rev;
        g_apPxW = pw;
        g_apPxH = ph;
        g_apPxPending = true;
    }
    if (g_apPxPending && g_apPxW && g_apPxH && QuadAtlasUpload(g_apAtlas, g_apPxW, g_apPxH, g_apPx, "ally portraits")) {   // (another size: a new swapchain)
        g_apPxPending = false;
        g_apUploadedRev = g_apPxRev;
    }
    // Not before a picture at least as new as the moment it came up has been uploaded (the swapchain holds the last time's).
    if (!g_apAtlas.shown || g_apUploadedRev < need || nl >= max || !g_apAtlas.w || !g_apAtlas.h) return nl;
    const float aspect = (float)g_apAtlas.h / (float)g_apAtlas.w;   // the picture's rows over its columns
    hset::Pose pose;
    float w = 0.0f;
    XrSpace space = g_roomSpace ? g_roomSpace : g_space;
    if (grid) {
        w = allyportraits::GridWidthM(a.widthM, a.big, allyportraits::Settings());
        if (!(w > 0.0f)) return nl;
        const float drop = std::max(0.0f, a.halfHM) + 0.05f * w + 0.5f * w * aspect;   // (allyportraits::DropM, for the picture as uploaded)
        const XrQuaternionf& q = a.map.pose.orientation;
        const XrVector3f at = Plus(a.map.pose.position, Rot(q, {0.0f, -drop, 0.002f}));   // under the map, in its plane (2 mm nearer)
        pose.p = {at.x, at.y, at.z};
        pose.q = {q.x, q.y, q.z, q.w};
        space = a.map.space;   // the map's space: the room's for the orb in the hand, g_space for the big map
    } else {
        w = wl.tileWidthM * wl.grow;
        if (!(w > 0.0f)) return nl;
        const XrQuaternionf q{wl.pose.q.x, wl.pose.q.y, wl.pose.q.z, wl.pose.q.w};
        const float lift = allyportraits::WristLiftM(wl.panelHM, w * aspect, wl.panelUp);
        const XrVector3f at = Plus({wl.pose.p.x, wl.pose.p.y, wl.pose.p.z}, Rot(q, {0.0f, lift, 0.0f}));
        pose.p = {at.x, at.y, at.z};
        pose.q = wl.pose.q;
        if (wl.alpha < 0.5f) return nl;   // (no fade of its own: shown once more than half up)
    }
    pose.valid = true;
    QuadLayerOf(g_apLayer, g_apAtlas, pose, w, w * aspect);
    g_apLayer.space = space;
    g_apLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // (premultiplied)
    layers[nl++] = (const XrCompositionLayerBaseHeader*)&g_apLayer;
    static int told = -1;
    const int where = grid ? (a.big ? 1 : 0) : 2;
    if (told != where) {
        told = where;
        static const char* const kWhere[] = {"under the map in the hand", "under the big map", "on the right wrist"};
        LogF("vrcam: openxr - ally portraits: hung %s, %.1f x %.1f cm", kWhere[where], w * 100.0f, w * aspect * 100.0f);
    }
    return nl;
}

void AllyPortraitsTeardown() {   // under g_lock or before the session exists
    QuadAtlasDestroy(g_apAtlas);
    g_apPx.clear();
    g_apPx.shrink_to_fit();
    g_apPxPending = false;
    g_apPxRev = g_apUploadedRev = g_apPxW = g_apPxH = 0;
    g_allyAnchor = AllyAnchor{};
    allyportraits::Teardown();
}
