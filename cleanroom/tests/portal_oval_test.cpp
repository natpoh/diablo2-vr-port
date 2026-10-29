// [render] flat_portal_no_oval (vr/portal_oval.h): which portal meshes are the black oval, and when a remembered render mesh may be written.
#include "portal_oval.h"

#include <cstdio>

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

using namespace portaloval;

int main() {
    // the three solid centres (as the game names them, any case and slash), and nothing else
    Check(IsOvalPath("data/hd/vfx/meshes/fx_mesh_portalSphere_01_lod0.model"), "portalSphere_01 is the oval");
    Check(IsOvalPath("data/hd/vfx/meshes/fx_mesh_portalSphere_lod0.model"), "portalSphere is the oval");
    Check(IsOvalPath("DATA\\HD\\VFX\\MESHES\\FX_MESH_PORTALSPHERE_CONVEX_LOD0.MODEL"), "portalSphere_convex, upper case and backslashes");
    Check(!IsOvalPath("data/hd/vfx/meshes/fx_mesh_portalSphere_convex_long_hole_vertexalpha_smaller_seamfix.model"), "the ring is not");
    Check(!IsOvalPath("data/hd/vfx/meshes/fx_mesh_portalSphere_convex_long_hole_vertexalpha.model"), "the ring is not (2)");
    Check(!IsOvalPath("data/hd/vfx/meshes/fx_mesh_portalsphere_innerflow_3.model"), "the closing swirl is not");
    Check(!IsOvalPath("data/hd/vfx/meshes/fx_mesh_townportal_lip_crawl_v4.model"), "the closing lip is not");
    Check(!IsOvalPath("data/hd/env/model/act1/town/waypoint.model") && !IsOvalPath("") && !IsOvalPath(nullptr), "other models, empty, null");

    Check(Plausible({380, 230, 380}), "the oval's counts are a mesh's");
    Check(!Plausible({380, 230, 379}) && !Plausible({0, 0, 0}) && !Plausible({380, 5000, 380}), "mismatched, zero or impossible counts are not");

    Entry e;
    e.rm = 0x1000;
    e.own = {380, 230, 380};
    for (size_t i = 0; i < kHeadBytes; ++i) e.head[i] = (uint8_t)(i * 7);
    uint8_t same[kHeadBytes];
    memcpy(same, e.head, kHeadBytes);
    Counts out;
    Check(WriteNow(e, {380, 230, 380}, same, true, &out) && Zero(out), "hide: its own counts become zeros");
    Check(!WriteNow(e, {0, 0, 0}, same, true, &out), "hide again: already hidden, nothing written");
    Check(WriteNow(e, {0, 0, 0}, same, false, &out) && out.tri == 380 && out.vert == 230 && out.sub == 380, "show: the zeros become its counts again");
    Check(!WriteNow(e, {380, 230, 380}, same, false, &out), "show again: nothing written");
    uint8_t other[kHeadBytes];
    memcpy(other, same, kHeadBytes);
    other[3] ^= 0xFF;
    Check(!WriteNow(e, {0, 0, 0}, other, false, &out), "the record's head changed (freed, reused): never written");
    Check(!WriteNow(e, {12, 30, 12}, same, true, &out), "counts that are neither its own nor zeros: never written");
    Entry none;
    Check(!WriteNow(none, {380, 230, 380}, same, true, &out), "no render mesh: nothing");

    if (g_fail) { std::printf("portal_oval_test: %d FAILED\n", g_fail); return 1; }
    std::printf("portal_oval_test: all passed\n");
    return 0;
}
