// The allies' portraits in the headset ([hud] allies_place) and the armed skills under the map ([hud] skill_under_map) - the OpenXR side of
// vr/ally_portraits_wire.*.
//
// NOT a header of its own: xr.cpp includes this file once, inside namespace xr { namespace {, after xr_wrist_status.h, because it uses what is there and in
// xr.cpp's unnamed namespace (QuadAtlas, the session, g_allyAnchor). What is shown is vr/ally_portraits.h (pure, tested) and vr/ally_portraits_wire.cpp
// (the reads, the painters, the wrist's glance); this only uploads the finished pictures and hangs them, up to TWO quad layers:
//   - under the map (kUnderMap: the armed skills' row, then the allies when they are there): under the map this frame's HangHud / HangBigMap hung
//     (g_allyAnchor), in the map's own space and turn, as wide as GridWidthM says for the picture's tiles across, its top a small gap under the map's
//     visible bottom (DropM);
//   - on the right wrist (kOnWrist, place 2): where the wrist status panel would stand over the right wrist (the room's space), its bottom row there,
//     more rows above, over that panel when it is up too (WristLiftM); it grows in as the wrist turns, as the wrist status does.

struct ApOut {
    QuadAtlas atlas;
    std::vector<uint32_t> px;
    uint32_t pxRev = 0, uploadedRev = 0, pxW = 0, pxH = 0;
    bool pending = false;
    XrCompositionLayerQuad layer{XR_TYPE_COMPOSITION_LAYER_QUAD};
};
ApOut g_ap[2];   // kUnderMap, kOnWrist

// The newest picture for `where` uploaded; true when its quad may be hung (an image at least as new as `need` is in the swapchain).
bool ApReady(int where, uint32_t need) {
    ApOut& o = g_ap[where];
    uint32_t rev = 0, pw = 0, ph = 0;
    if (allyportraits::Picture(where, &o.px, &rev, &pw, &ph)) {
        o.pxRev = rev;
        o.pxW = pw;
        o.pxH = ph;
        o.pending = true;
    }
    if (o.pending && o.pxW && o.pxH && QuadAtlasUpload(o.atlas, o.pxW, o.pxH, o.px, where == allyportraits::kUnderMap ? "ally portraits (under the map)" : "ally portraits (wrist)")) {
        o.pending = false;   // (another size: a new swapchain)
        o.uploadedRev = o.pxRev;
    }
    return o.atlas.shown && o.uploadedRev >= need && o.atlas.w && o.atlas.h;
}

uint32_t HangAllyPortraits(const XrCompositionLayerBaseHeader** layers, uint32_t nl, uint32_t max) {
    const AllyAnchor a = g_allyAnchor;
    g_allyAnchor = AllyAnchor{};   // (set again by the next frame's map)
    static int told[2] = {-1, -1};
    // under the map: the skills' row and / or the allies
    uint32_t need = 0;
    if (a.ok && allyportraits::UnderMapWanted() && allyportraits::Want(allyportraits::kUnderMap, &need) && nl < max && ApReady(allyportraits::kUnderMap, need)) {
        ApOut& o = g_ap[allyportraits::kUnderMap];
        const float aspect = (float)o.atlas.h / (float)o.atlas.w;   // the picture's rows over its columns
        const int across = (int)(o.atlas.w / (uint32_t)allyportraits::kTilePx);
        const float w = allyportraits::GridWidthM(a.widthM, a.big, allyportraits::Settings(), across);
        if (w > 0.0f) {
            const float drop = std::max(0.0f, a.halfHM) + 0.05f * w + 0.5f * w * aspect;   // (allyportraits::DropM, for the picture as uploaded)
            const XrQuaternionf& q = a.map.pose.orientation;
            const XrVector3f at = Plus(a.map.pose.position, Rot(q, {0.0f, -drop, 0.002f}));   // under the map, in its plane (2 mm nearer)
            hset::Pose pose;
            pose.p = {at.x, at.y, at.z};
            pose.q = {q.x, q.y, q.z, q.w};
            pose.valid = true;
            QuadLayerOf(o.layer, o.atlas, pose, w, w * aspect);
            o.layer.space = a.map.space;   // the map's space: the room's for the orb in the hand, g_space for the big map
            o.layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;   // (premultiplied)
            layers[nl++] = (const XrCompositionLayerBaseHeader*)&o.layer;
            if (told[0] != (int)a.big) {
                told[0] = (int)a.big;
                LogF("vrcam: openxr - ally portraits: hung under the %s, %.1f x %.1f cm", a.big ? "big map" : "map in the hand", w * 100.0f, w * aspect * 100.0f);
            }
        }
    }
    // on the right wrist: the allies
    allyportraits::WristLayer wl;
    if (allyportraits::WristWanted() && allyportraits::WristPanel(&wl) && allyportraits::Want(allyportraits::kOnWrist, &need) && nl < max &&
        ApReady(allyportraits::kOnWrist, need) && wl.alpha >= 0.5f) {   // (no fade of its own: shown once more than half up)
        ApOut& o = g_ap[allyportraits::kOnWrist];
        const float aspect = (float)o.atlas.h / (float)o.atlas.w;
        const float w = wl.tileWidthM * wl.grow;
        if (w > 0.0f) {
            const XrQuaternionf q{wl.pose.q.x, wl.pose.q.y, wl.pose.q.z, wl.pose.q.w};
            const float lift = allyportraits::WristLiftM(wl.panelHM, w * aspect, wl.panelUp);
            const XrVector3f at = Plus({wl.pose.p.x, wl.pose.p.y, wl.pose.p.z}, Rot(q, {0.0f, lift, 0.0f}));
            hset::Pose pose;
            pose.p = {at.x, at.y, at.z};
            pose.q = wl.pose.q;
            pose.valid = true;
            QuadLayerOf(o.layer, o.atlas, pose, w, w * aspect);
            o.layer.space = g_roomSpace ? g_roomSpace : g_space;   // the frames' poses are the room's
            o.layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            layers[nl++] = (const XrCompositionLayerBaseHeader*)&o.layer;
            if (told[1] != 1) {
                told[1] = 1;
                LogF("vrcam: openxr - ally portraits: hung on the right wrist, %.1f x %.1f cm", w * 100.0f, w * aspect * 100.0f);
            }
        }
    }
    return nl;
}

void AllyPortraitsTeardown() {   // under g_lock or before the session exists
    for (ApOut& o : g_ap) {
        QuadAtlasDestroy(o.atlas);
        o.px.clear();
        o.px.shrink_to_fit();
        o.pending = false;
        o.pxRev = o.uploadedRev = o.pxW = o.pxH = 0;
    }
    g_allyAnchor = AllyAnchor{};
    allyportraits::Teardown();
}
