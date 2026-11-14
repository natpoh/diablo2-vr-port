// wrist_orb_models_test - [hud] wrist_game_models: the pure parts (the liquid lens, the medallion's three matrices, the pulse) always; with a game folder,
// D2R_VR_Settings' maker (tools/settings/orb_assets.cpp) on the player's own game through CascLib into a scratch mod folder (never the game's): the six
// files made, each liquid read back by its own types (one mesh - our lens, salted -, the material renamed ours with the glow asked, every uv the liquid's
// texel), the units naming the right models; made again: nothing written; off: ours taken out.
//   wrist_orb_models_test [<game folder> <empty scratch folder> [keep]]   (keep: the files left made, to look at)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "hud_globes.h"
#include "orb_assets.h"
#include "wrist_models.h"
#include "wrist_orb_models.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
static float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float Len(const float* a) { return std::sqrt(Dot(a, a)); }
static std::wstring Wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }
static std::wstring In(const std::wstring& mpq, const std::string& rel) {
    std::wstring p = mpq + Wide(rel);
    for (wchar_t& c : p) if (c == L'/') c = L'\\';
    return p;
}
static bool ReadAll(const std::wstring& p, std::string* out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"rb") || !f) return false;
    out->clear();
    char b[65536];
    size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) out->append(b, n);
    std::fclose(f);
    return true;
}

static void Pure() {
    // the lens: a half ball of radius 1 on y 0, closed below, one uv
    const grannyremesh::Mesh m = wristorbs::LensMesh(0.25f, 0.75f);
    CHECK(m.v.size() == (size_t)(wristorbs::kLensRings * (wristorbs::kLensSegments + 1) + 1 + 1 + wristorbs::kLensSegments + 1));
    CHECK(m.idx.size() % 6 == 0);   // both windings
    CHECK(Near(m.mn[1], 0.0f) && Near(m.mx[1], 1.0f, 1e-3f));
    CHECK(Near(m.mn[0], -1.0f, 1e-3f) && Near(m.mx[0], 1.0f, 1e-3f) && Near(m.mn[2], -1.0f, 1e-3f) && Near(m.mx[2], 1.0f, 1e-3f));
    bool sameUv = true, inside = true, outward = true;
    for (const grannyremesh::Vertex& x : m.v) {
        sameUv = sameUv && x.uv[0] == m.v[0].uv[0] && x.uv[1] == m.v[0].uv[1];
        const float p[3] = {x.pos[0] / 32767.0f, x.pos[1] / 32767.0f, x.pos[2] / 32767.0f};
        inside = inside && Len(p) <= 1.0005f && p[1] >= -1e-4f;
        const float n[3] = {(float)x.normal[0], (float)x.normal[1], (float)x.normal[2]};
        outward = outward && (p[1] < 1e-4f ? n[1] < 0.0f || Len(p) > 0.999f : Dot(n, p) > 0.0f);
    }
    CHECK(sameUv && inside && outward);
    for (uint16_t i : m.idx) CHECK(i < m.v.size());
    {   // the glass: the open dome - no bottom
        const grannyremesh::Mesh open = wristorbs::LensMesh(0.25f, 0.75f, false);
        CHECK(open.v.size() == (size_t)(wristorbs::kLensRings * (wristorbs::kLensSegments + 1) + 1));
        bool down = false;
        for (const grannyremesh::Vertex& x : open.v) down = down || x.normal[1] < -100;
        CHECK(!down);
        for (uint16_t i : open.idx) CHECK(i < open.v.size());
    }
    // the uv rule: the first vertex in the band, else the nearest
    {
        grannyremesh::Mesh g;
        auto add = [&](int16_t y, float u, float v) { grannyremesh::Vertex x{}; x.pos[1] = y; x.uv[0] = grannyremesh::Half(u); x.uv[1] = grannyremesh::Half(v); g.v.push_back(x); };
        add(0, 0.1f, 0.1f); add(1000, 0.9f, 0.9f); add(560, 0.375f, 0.7f); add(580, 0.5f, 0.5f);
        float u = 0, v = 0, h = 0;
        CHECK(wristorbs::LiquidUv(g, &u, &v, &h));
        CHECK(Near(u, 0.375f, 1e-3f) && Near(v, 0.7f, 1e-3f) && Near(h, 0.56f, 1e-3f));
        grannyremesh::Mesh none;
        CHECK(!wristorbs::LiquidUv(none, &u, &v));
    }
    CHECK(wristorbs::GlowClamp(-3.0f) == 0.0f && wristorbs::GlowClamp(99.0f) == wristorbs::kGlowMax && wristorbs::GlowClamp(NAN) == wristorbs::kGlowDefault);
    CHECK(wristorbs::kGlowDefault == 13.0f);   // the game's light healing potion's glow
    {   // [hud] wrist_globe_dx / _dy / _scale: none at 0 0 1; dx / dy move both middles by that much of the disc's radius; scale the radius
        const hudglobes::Boxes a = hudglobes::Compute(2048, 2048, 0.6f), z = hudglobes::Compute(2048, 2048, 0.6f, hudglobes::Nudge{});
        CHECK(a.ok && z.ok && a.cx[0] == z.cx[0] && a.cy == z.cy && a.radius == z.radius);
        const hudglobes::Boxes n = hudglobes::Compute(2048, 2048, 0.6f, hudglobes::Nudge{0.5f, -0.25f, 1.2f});
        const float k = 2048.0f / hudglobes::kRefHeight * 0.6f, r = hudglobes::kCutR * k;
        // `in` mirrored: the life globe (left) toward the middle (+x), the mana globe (right) toward it (-x); dy both down; scale both
        CHECK(n.ok && Near(n.cx[0] - a.cx[0], 0.5f * r, 1e-3f) && Near(n.cx[1] - a.cx[1], -0.5f * r, 1e-3f) && Near(n.cy - a.cy, -0.25f * r, 1e-3f) && Near(n.radius, 1.2f * a.radius, 1e-3f));
        // the controller layout on Serg's 2048 square at 80 %: not shrunk - the globes at x 146 / 1899, radius 121 px (where his nudges went), the squares inside
        const hudglobes::Boxes kb = hudglobes::Compute(2048, 2048, 0.8f), ct = hudglobes::Compute(2048, 2048, 0.8f, hudglobes::Nudge{}, hudglobes::kControllerLayout);
        CHECK(kb.ok && ct.ok && Near(kb.cx[0], 276.0f, 2.0f) && Near(kb.cx[1], 1773.0f, 2.0f));
        CHECK(Near(ct.cx[0], 146.0f, 2.0f) && Near(ct.cx[1], 1899.0f, 2.0f) && Near(ct.radius, 121.0f, 1.5f) && Near(ct.cy, 1706.0f, 2.0f));
        // the wider nudge (Serg: -5 .. 5): 5 radii apart reached, past it held there
        const hudglobes::Boxes apart = hudglobes::Compute(3840, 2160, 0.8f, hudglobes::Nudge{-5.0f, 0.0f, 1.0f}, hudglobes::kControllerLayout);
        CHECK(apart.ok && Near(apart.cx[0], 1920.0f - 1158.0f * 0.8f - 5.0f * 160.0f * 0.8f, 0.5f));
        const hudglobes::Boxes held = hudglobes::Compute(3840, 2160, 0.8f, hudglobes::Nudge{-7.0f, 0.0f, 1.0f}, hudglobes::kControllerLayout);
        CHECK(held.ok && Near(held.cx[0], apart.cx[0], 0.01f));
        // a cut past the picture's side (his scale 1.46 on the controller layout): not whole, but on the layer - cut, the medallion drawn
        const hudglobes::Boxes big = hudglobes::Compute(2048, 2048, 0.8f, hudglobes::Nudge{0.0f, 0.0f, 1.46f}, hudglobes::kControllerLayout);
        CHECK(!big.ok && big.onLayer && big.left[0] >= 0 && big.left[1] + 2 * big.half <= 2048 && big.fit < 1.0f && big.radius < 1.46f * ct.radius);
        CHECK(Near(big.left[0] + big.half, big.cx[0], 1.0f) && Near(big.left[1] + big.half, big.cx[1], 1.0f));   // the same middles
        CHECK(ct.ok && ct.onLayer);
        const hudglobes::Boxes off = hudglobes::Compute(2048, 2048, 0.8f, hudglobes::Nudge{-5.0f, 0.0f, 1.0f}, hudglobes::kControllerLayout);
        CHECK(!off.onLayer);   // a middle off the picture: nothing
        // a wide picture: the controller panel fits, no shrink (the same scale as the keyboard layout's)
        const hudglobes::Boxes wide = hudglobes::Compute(3840, 2160, 0.8f, hudglobes::Nudge{}, hudglobes::kControllerLayout);
        CHECK(wide.ok && Near(wide.radius, 160.0f * 0.8f, 1e-2f) && Near(wide.cx[0], 1920.0f - 1158.0f * 0.8f, 0.5f));
        // a smaller interface (the game's own scale on a large layer) brings the globes in toward the middle and down
        const hudglobes::Boxes lesser = hudglobes::Compute(2048, 2048, 0.45f);
        CHECK(lesser.ok && lesser.cx[0] > a.cx[0] && lesser.cx[1] < a.cx[1] && lesser.radius < a.radius);
    }
    // the matrices: a medallion on a tilted wrist
    wristmodels::Orb o;
    o.on = true;
    o.c[0] = 100.0f; o.c[1] = 20.0f; o.c[2] = -50.0f;
    const float s = std::sqrt(0.5f);
    o.back[0] = 0.0f; o.back[1] = s; o.back[2] = s;          // out of the back of the hand
    o.along[0] = 1.0f; o.along[1] = 0.1f; o.along[2] = -0.1f; // nearly square to it
    o.radius = 0.08f;
    o.fill = 0.5f;
    float mm[wristmodels::kParts][16];
    bool show[wristmodels::kParts];
    CHECK(wristmodels::Matrices(o, mm, show));
    CHECK(show[wristmodels::kGlass] && show[wristmodels::kPlate] && show[wristmodels::kLiquid]);
    const float dome = handorbs::DomeRadius(o.radius);
    // the shape numbers are the effect's own medallion's (vr/hand_orbs.h)
    CHECK(wristorbs::kMedDomeR == handorbs::kDomeR && wristorbs::kMedPlateT == handorbs::kPlateT && wristorbs::kMedBezelOuter == handorbs::kBezelOuter &&
          wristorbs::kMedBezelH == handorbs::kBezelH);
    // the glass: the dome's radius every way, its middle (its rim's plane) on the base's top plane - the medallion's middle
    CHECK(Near(mm[0][12], 100.0f) && Near(mm[0][13], 20.0f) && Near(mm[0][14], -50.0f));
    CHECK(Near(Len(mm[0] + 0), dome) && Near(Len(mm[0] + 4), dome) && Near(Len(mm[0] + 8), dome));
    CHECK(Near(Dot(mm[0] + 0, mm[0] + 4), 0.0f) && Near(Dot(mm[0] + 4, mm[0] + 8), 0.0f) && Near(Dot(mm[0] + 0, mm[0] + 8), 0.0f));
    {   // its y is the back of the hand; the frame right-handed (x cross y = z)
        const float* x = mm[0] + 0; const float* y = mm[0] + 4; const float* z = mm[0] + 8;
        CHECK(Near(y[1] / dome, s) && Near(y[2] / dome, s));
        const float c[3] = {x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0]};
        CHECK(Near(Dot(c, z), dome * dome * dome, 1e-5f));
    }
    // the plate: the base's radius every way (its file is the plate in those units), its top plane on the middle too: its underside a plate's thickness
    // under it, where the effect's is
    CHECK(Near(Len(mm[1] + 0), o.radius) && Near(Len(mm[1] + 4), o.radius) && Near(Len(mm[1] + 8), o.radius));
    CHECK(Near(mm[1][12], 100.0f) && Near(mm[1][13], 20.0f) && Near(mm[1][14], -50.0f));
    // sealed: the liquid's foot and the glass's rim inside the bezel's inner wall, on the plate's floor (the same plane)
    CHECK(wristmodels::kLiquidInside * handorbs::kDomeR < wristorbs::kMedDomeR && Near(mm[2][13], mm[1][13]) && Near(mm[0][13], mm[1][13]));
    {   // the base mesh: radius 1, its floor and lip on y 0, the bezel round the dome's foot, the plate's thickness under it
        const grannyremesh::Mesh b = wristorbs::BaseMesh(0.3f, 0.6f);
        float lo = 1.0f, hi = -1.0f, rmax = 0.0f;
        bool floorFlat = true, bezelAt = false;
        for (const grannyremesh::Vertex& x : b.v) {
            const float p[3] = {x.pos[0] / 32767.0f, x.pos[1] / 32767.0f, x.pos[2] / 32767.0f};
            const float r = std::sqrt(p[0] * p[0] + p[2] * p[2]);
            lo = std::min(lo, p[1]); hi = std::max(hi, p[1]); rmax = std::max(rmax, r);
            const float rin = std::sqrt(wristorbs::kMedDomeR * wristorbs::kMedDomeR - wristorbs::kMedBezelH * wristorbs::kMedBezelH);   // the effect's Rin
            if (x.normal[1] == 127 && r < rin - 1e-3f) floorFlat = floorFlat && Near(p[1], 0.0f, 1e-4f);   // the floor inside the dome
            if (x.normal[1] == 0 && Near(r, rin, 1e-3f) && Near(p[1], wristorbs::kMedBezelH, 1e-3f)) bezelAt = true;   // the bezel's top from Rin
        }
        CHECK(Near(lo, -wristorbs::kMedPlateT, 1e-3f) && Near(hi, wristorbs::kMedBezelH, 1e-3f) && Near(rmax, 1.0f, 1e-3f) && floorFlat && bezelAt);
        CHECK(Near(b.mn[1], -wristorbs::kMedPlateT) && Near(b.mx[1], wristorbs::kMedBezelH));
        for (uint16_t i : b.idx) CHECK(i < b.v.size());
    }
    // no glass at opacity 0; the plate and the liquid stay
    {
        wristmodels::Orb g = o;
        g.glass = 0.0f;
        float gm[wristmodels::kParts][16];
        bool gs[wristmodels::kParts];
        CHECK(wristmodels::Matrices(g, gm, gs) && !gs[wristmodels::kGlass] && gs[wristmodels::kPlate] && gs[wristmodels::kLiquid]);
    }
    // the liquid: on the plane at the dome's size (not squashed: its level is a mesh cut flat), inside the glass
    CHECK(Near(Len(mm[2] + 4), dome * wristmodels::kLiquidInside) && Near(Len(mm[2] + 0), dome * wristmodels::kLiquidInside));
    CHECK(Near(mm[2][12], 100.0f) && Near(mm[2][13], 20.0f));
    // the level: the nearest of kLevels, never 0 while shown; a little more life, the next mesh; none under the minimum; full the last
    wristmodels::Orb o2 = o;
    float m2[wristmodels::kParts][16];
    bool s2[wristmodels::kParts];
    int level = -1;
    wristmodels::Matrices(o, m2, s2, &level);
    CHECK(level == wristorbs::kLevels / 2);
    o2.fill = 0.5f + 1.0f / wristorbs::kLevels;
    wristmodels::Matrices(o2, m2, s2, &level);
    CHECK(level == wristorbs::kLevels / 2 + 1 && Near(Len(m2[2] + 4), Len(mm[2] + 4)));
    o2.fill = 0.012f;
    wristmodels::Matrices(o2, m2, s2, &level);
    CHECK(s2[wristmodels::kLiquid] && level == 1);
    o2.fill = 0.005f;
    wristmodels::Matrices(o2, m2, s2, &level);
    CHECK(s2[wristmodels::kGlass] && !s2[wristmodels::kLiquid] && level == 0);
    o2.fill = 7.0f;
    wristmodels::Matrices(o2, m2, s2, &level);
    CHECK(level == wristorbs::kLevels);
    // the level meshes: the dome's wall up to the cut, a flat top there, the bottom; every level the whole half ball's box
    for (int k : {1, 17, 32, 63, 64}) {
        const float h = (float)k / wristorbs::kLevels;
        const grannyremesh::Mesh z = wristorbs::ZoneMesh(h, 0.3f, 0.6f);
        float top = -1.0f, rimTop = 0.0f;
        bool inside = true, flatTop = true;
        for (const grannyremesh::Vertex& x : z.v) {
            const float p[3] = {x.pos[0] / 32767.0f, x.pos[1] / 32767.0f, x.pos[2] / 32767.0f};
            top = std::max(top, p[1]);
            inside = inside && Len(p) <= 1.0005f && p[1] >= -1e-4f && p[1] <= h + 1e-3f;
            if (x.normal[1] == 127 && p[1] > 1e-3f) { flatTop = flatTop && Near(p[1], top < p[1] ? p[1] : p[1], 1e-6f); rimTop = std::max(rimTop, std::sqrt(p[0] * p[0] + p[2] * p[2])); }
        }
        CHECK(inside && flatTop);
        CHECK(Near(top, h, 1e-3f));
        CHECK(Near(rimTop, std::sqrt(std::max(0.0f, 1.0f - h * h)), 2e-3f));   // the flat top reaches the wall
        CHECK(Near(z.mn[1], 0.0f) && Near(z.mx[1], 1.0f) && Near(z.mx[0], 1.0f));
        for (uint16_t i : z.idx) CHECK(i < z.v.size());
    }
    CHECK(wristorbs::LevelOf(0.0f) == 0 && wristorbs::LevelOf(1.0f) == wristorbs::kLevels && wristorbs::LevelOf(0.5f) == wristorbs::kLevels / 2 && wristorbs::LevelOf(NAN) == 0);
    // the wrist's frame: a place in it and back is the same place; a frame turned and moved carries the medallion with it
    {
        const float wr[3] = {1.0f, 2.0f, 3.0f}, el[3] = {1.0f, 2.0f, 3.3f}, pt[3] = {0.0f, 0.0f, -1.0f}, ac[3] = {0.2f, -1.0f, 0.1f};
        wristmodels::ArmFrame fr;
        CHECK(wristmodels::MakeFrame(wr, el, pt, ac, &fr));
        CHECK(Near(Dot(fr.x, fr.y), 0.0f) && Near(Len(fr.z), 1.0f) && Near(fr.x[2], -1.0f));
        const float p[3] = {1.05f, 2.04f, 2.97f};
        float l[3], back[3];
        wristmodels::ToFrame(fr, p, true, l);
        wristmodels::FromFrame(fr, l, true, 1.0f, back);
        CHECK(Near(back[0], p[0]) && Near(back[1], p[1]) && Near(back[2], p[2]));
        const float wr2[3] = {5.0f, 2.0f, 3.0f}, el2[3] = {5.3f, 2.0f, 3.0f}, pt2[3] = {-1.0f, 0.0f, 0.0f}, ac2[3] = {0.1f, -1.0f, -0.2f};   // turned 90 degrees, moved
        wristmodels::ArmFrame f2;
        CHECK(wristmodels::MakeFrame(wr2, el2, pt2, ac2, &f2));
        float q[3];
        wristmodels::FromFrame(f2, l, true, 1.0f, q);
        const float d1[3] = {p[0] - wr[0], p[1] - wr[1], p[2] - wr[2]}, d2[3] = {q[0] - wr2[0], q[1] - wr2[1], q[2] - wr2[2]};
        CHECK(Near(Len(d1), Len(d2)));   // the same distance from the wrist
        const float e1[3] = {wr[0] - el[0], wr[1] - el[1], wr[2] - el[2]};
        CHECK(!wristmodels::MakeFrame(wr, wr, nullptr, ac, &fr) && wristmodels::MakeFrame(wr, el, nullptr, ac, &fr) && Near(Dot(fr.x, e1) / Len(e1), 1.0f));
    }
    // not shown: off, no size, no frame, not finite
    o2 = o; o2.on = false;
    CHECK(!wristmodels::Matrices(o2, m2, s2) && !s2[0] && !s2[1] && !s2[2]);
    o2 = o; o2.radius = 0.0f;
    CHECK(!wristmodels::Matrices(o2, m2, s2));
    o2 = o; o2.along[0] = 0.0f; o2.along[1] = s; o2.along[2] = s;   // along the back itself: no frame
    CHECK(!wristmodels::Matrices(o2, m2, s2));
    o2 = o; o2.c[1] = NAN;
    CHECK(!wristmodels::Matrices(o2, m2, s2));
    // the pulse: the heartbeat times the setting, none at rest or at 0
    CHECK(Near(wristmodels::Highlight(0.5f, 1.0f), 0.5f) && Near(wristmodels::Highlight(2.0f, 1.5f), 1.5f));
    CHECK(wristmodels::Highlight(0.0f, 1.0f) == 0.0f && wristmodels::Highlight(0.7f, 0.0f) == 0.0f && wristmodels::Highlight(NAN, 1.0f) == 0.0f);
    // the glow, live: the highlight at rest - 13 is x 1.65, 40 x 3, 0 none, past 40 held
    CHECK(Near(wristmodels::GlowHighlight(13.0f), 0.65f) && Near(wristmodels::GlowHighlight(40.0f), 2.0f) && wristmodels::GlowHighlight(0.0f) == 0.0f &&
          Near(wristmodels::GlowHighlight(99.0f), 2.0f) && wristmodels::GlowHighlight(NAN) == 0.0f);
    // the units name the models
    const std::string u = wristorbs::UnitJson("x", wristorbs::kGlassModel, {"data/hd/a.texture"});
    CHECK(u.find(wristorbs::kGlassModel) != std::string::npos && u.find("data/hd/a.texture") != std::string::npos && u.find("\"shadowMask\":0") != std::string::npos);
    // each aged base is laid from one even worn-metal area of its picture (measured on the pictures, 0.215.12: gold's white patch, bronze's bands)
    {
        struct Even { float u, v, r; };   // a disc of even metal: no padding streaks, no grey, white, blue or black pixel
        const Even even[3] = {{0.225f, 0.400f, 0.036f}, {0.470f, 0.190f, 0.036f}, {0.790f, 0.117f, 0.124f}};
        for (int m = 0; m < 3; ++m) {
            const wristorbs::PlateMetal& pm = wristorbs::kPlateMetal[m];
            CHECK(pm.r > 0.02f && std::hypot(pm.cu - even[m].u, pm.cv - even[m].v) + pm.r <= even[m].r);
            CHECK(pm.cu - pm.r >= 0.0f && pm.cv - pm.r >= 0.0f && pm.cu + pm.r <= 1.0f && pm.cv + pm.r <= 1.0f);
        }
        CHECK(!(wristorbs::kPlateMetal[1].cu == 0.566f && wristorbs::kPlateMetal[1].r == 0.19f));   // (bronze's banded hub and bezel)
    }
    // our model files are only at the game's test model paths no piece of ours uses (the domes and ceilings are under Tests/)
    for (const char* f : wristorbs::kLiquidFile) CHECK(std::string(f).rfind("data/hd/bvt/models/", 0) == 0);
}

static void Game(std::wstring game, std::wstring scratch, bool keep) {
    if (game.back() != L'\\') game += L'\\';
    if (scratch.back() != L'\\') scratch += L'\\';
    const std::wstring modRoot = scratch + L"mods\\D2R-VR\\", mpq = modRoot + L"D2R-VR.mpq\\";
    CreateDirectoryW((scratch + L"mods").c_str(), nullptr);
    CreateDirectoryW(modRoot.c_str(), nullptr);
    CreateDirectoryW(mpq.c_str(), nullptr);
    CHECK(orbassets::Prepare(game, L"", true, 6.0f).state == orbassets::kNotHere);
    const orbassets::Result r = orbassets::Prepare(game, modRoot, true, 9.0f);
    std::printf("made: %ls\n", r.text.c_str());
    CHECK(r.state == orbassets::kMade && r.wrote);
    for (const std::string& rel : wristorbs::OurPaths()) CHECK(GetFileAttributesW(In(mpq, rel).c_str()) != INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW((modRoot + L"wrist_orbs.json").c_str()) != INVALID_FILE_ATTRIBUTES);
    for (int kind = 0; kind < 2; ++kind) {   // each liquid read back
        std::string bytes;
        CHECK(ReadAll(In(mpq, wristorbs::kLiquidFile[kind]), &bytes));
        grannyremesh::File f;
        std::string why;
        CHECK(grannyremesh::Read(bytes, &f, &why));
        grannyremesh::Mesh lens;
        CHECK(grannyremesh::MeshOf(f, &lens, &why));
        const grannyremesh::Mesh want = wristorbs::ZoneMesh(1.0f / wristorbs::kLevels, 0.0f, 0.0f);
        CHECK(lens.v.size() == want.v.size() + (size_t)kind + 1);   // level 1, salted by its kind
        CHECK(lens.idx.size() == want.idx.size());
        CHECK(wristorbs::MeshIndex(f, "orb_level_64") == wristorbs::kLevels - 1 && wristorbs::MeshIndex(f, "orb_level_02") == 1);   // every level, in order
        for (int k : {2, 32, 64}) {   // a level read alone (the file cut to that mesh, as the flipbook shows it): its top at its height
            grannyremesh::File one;
            grannyremesh::Mesh lv;
            CHECK(grannyremesh::Read(bytes, &one, &why) && grannyremesh::KeepOnlyMesh(one, k - 1, &why) && grannyremesh::MeshOf(one, &lv, &why));
            int top = -32768;
            for (const grannyremesh::Vertex& x : lv.v) top = std::max<int>(top, x.pos[1]);
            CHECK(Near(top / 32767.0f, (float)k / wristorbs::kLevels, 1e-3f));
            CHECK(lv.v.size() == wristorbs::ZoneMesh((float)k / wristorbs::kLevels, 0.0f, 0.0f).v.size());
        }
        float u = 0, v = 0;
        CHECK(wristorbs::LiquidUv(lens, &u, &v));
        CHECK(u > 0.0f && u < 1.0f && v > 0.0f && v < 1.0f);   // (the salt's own vertices lie far off; the lens's first: the liquid's texel)
        std::string name;
        float emissive = -1.0f;
        CHECK(wristorbs::EditMaterial(f, "probe", 1.0f, &name, &emissive, &why));
        CHECK(name == wristorbs::kMaterialName[kind]);
        CHECK(Near(emissive, 9.0f));
        CHECK(wristorbs::TexturesOf(f).size() >= 1);
        std::string unit;
        CHECK(ReadAll(In(mpq, wristorbs::kLiquidUnit[kind]), &unit));
        CHECK(unit.find(wristorbs::kLiquidModel[kind]) != std::string::npos);
        std::printf("liquid %d: %zu vertices, %zu indices, material %s, glow %.1f, uv %.3f %.3f\n", kind, lens.v.size(), lens.idx.size(), name.c_str(), emissive, u, v);
    }
    {
        std::string unit;
        CHECK(ReadAll(In(mpq, wristorbs::kGlassUnit), &unit) && unit.find(wristorbs::kGlassModel) != std::string::npos);
        for (const wristorbs::PlateMetal& pm : wristorbs::kPlateMetal) CHECK(ReadAll(In(mpq, pm.unit), &unit) && unit.find(pm.model) != std::string::npos);
        struct { const char* file; const char* material; bool closed; unsigned salt; bool base; } parts[4] = {
            {wristorbs::kGlassFile, wristorbs::kGlassMaterial, false, 3u, false}, {wristorbs::kPlateMetal[0].file, wristorbs::kPlateMetal[0].material, true, 4u, true},
            {wristorbs::kPlateMetal[1].file, wristorbs::kPlateMetal[1].material, true, 5u, true}, {wristorbs::kPlateMetal[2].file, wristorbs::kPlateMetal[2].material, true, 6u, true}};
        for (const auto& p : parts) {   // our shape (salted) in the potion's material, renamed ours, no glow
            std::string bytes, why, name;
            grannyremesh::File f;
            grannyremesh::Mesh mesh;
            CHECK(ReadAll(In(mpq, p.file), &bytes) && grannyremesh::Read(bytes, &f, &why) && grannyremesh::MeshOf(f, &mesh, &why));
            const grannyremesh::Mesh want = p.base ? wristorbs::BaseMesh(0.0f, 0.0f) : wristorbs::LensMesh(0.0f, 0.0f, p.closed);
            {   // the potion model's own placement reset: the identity (no lift, no shift)
                grannyremesh::Walk g(f);
                const grannyremesh::Addr rt{f.h.rootType.section, f.h.rootType.offset}, ro{f.h.rootObj.section, f.h.rootObj.offset};
                grannyremesh::Addr t, e;
                const grannyremesh::Addr models = g.Member(rt, ro, "Models", &t);
                const grannyremesh::Addr ip = g.Member(g.Ref(t), g.Ptr(g.Ptr(models.at(4))), "InitialPlacement", &e);
                CHECK(ip.ok() && g.Get<uint32_t>(ip) == 0 && g.Get<float>(ip.at(4)) == 0.0f && g.Get<float>(ip.at(8)) == 0.0f && g.Get<float>(ip.at(12)) == 0.0f &&
                      g.Get<float>(ip.at(4 + 6 * 4)) == 1.0f);
            }
            CHECK(mesh.v.size() == want.v.size() + p.salt && mesh.idx.size() == want.idx.size());
            float emissive = -1.0f;
            CHECK(wristorbs::EditMaterial(f, "probe", -1.0f, &name, &emissive, &why) && name == p.material && emissive == 0.0f);
            std::printf("%s: %zu vertices, material %s\n", p.file, mesh.v.size(), name.c_str());
            if (p.base) {   // a base: two meshes, [1] the aged one - the rosette laid across it (its uvs spread round the rosette's middle)
                CHECK(wristorbs::MeshIndex(f, "orb_level_02") == 1);
                grannyremesh::File aged;
                grannyremesh::Mesh am;
                CHECK(grannyremesh::Read(bytes, &aged, &why) && grannyremesh::KeepOnlyMesh(aged, 1, &why) && grannyremesh::MeshOf(aged, &am, &why));
                const wristorbs::PlateMetal* pm = nullptr;
                for (const wristorbs::PlateMetal& x : wristorbs::kPlateMetal) if (std::string(x.file) == p.file) pm = &x;
                float u = 0, v = 0;
                CHECK(pm && wristorbs::LiquidUv(am, &u, &v, nullptr, 0.0f, 2.0f) && std::fabs(u - pm->cu) <= pm->r * 1.01f && std::fabs(v - pm->cv) <= pm->r * 1.01f);
            }
            if (!p.base) {   // the glass: the lesser mana potion's empty-glass texel again (0.215.5 - 0.215.7's), not the healing potion's white
                float u = 0, v = 0;
                CHECK(wristorbs::LiquidUv(mesh, &u, &v) && Near(u, 0.2725f, 2e-3f) && Near(v, 0.4751f, 2e-3f));
            }
        }
    }
    // made again: nothing written
    const orbassets::Result again = orbassets::Prepare(game, modRoot, true, 9.0f);
    CHECK(again.state == orbassets::kMade && !again.wrote);
    // another glow: the liquids written again
    const orbassets::Result glow = orbassets::Prepare(game, modRoot, true, 3.0f);
    CHECK(glow.state == orbassets::kMade && glow.wrote);
    // the liquids are in the potions' own StandardMaterial - as the glass
    // dome's; never the healing potion's GlassMaterial (0.216.0's wrist_game_liquid_glass: its shader takes no opacity and draws the picture solid)
    for (int kind = 0; kind < 2; ++kind) {
        std::string bytes, glassBytes;
        CHECK(ReadAll(In(mpq, wristorbs::kLiquidFile[kind]), &bytes) && bytes.find("GlassMaterial") == std::string::npos &&
              bytes.find("StandardMaterial") != std::string::npos);
        CHECK(ReadAll(In(mpq, wristorbs::kGlassFile), &glassBytes) && glassBytes.find("GlassMaterial") == std::string::npos &&
              glassBytes.find("StandardMaterial") != std::string::npos);
    }
    if (keep) { orbassets::Prepare(game, modRoot, true, 6.0f); return; }   // (left made at the default glow)
    // off: ours out
    const orbassets::Result off = orbassets::Prepare(game, modRoot, false, 3.0f);
    CHECK(off.state == orbassets::kOff && off.wrote);
    for (const std::string& rel : wristorbs::OurPaths()) CHECK(GetFileAttributesW(In(mpq, rel).c_str()) == INVALID_FILE_ATTRIBUTES);
}

// wrist_orb_models_test.exe <game> <a copy of an installed mods\D2R-VR folder> upgrade: files an older version made (their receipt beside them) are
// made again by this one - the liquids a 0.216.0 wrist_game_liquid_glass left in the game's GlassMaterial come out in the potions' own material.
static void Upgrade(std::wstring game, std::wstring modRoot) {
    if (!modRoot.empty() && modRoot.back() != L'\\') modRoot += L'\\';
    const std::wstring mpq = modRoot + L"D2R-VR.mpq\\";
    int glassBefore = 0;
    for (int kind = 0; kind < 2; ++kind) {
        std::string bytes;
        if (ReadAll(In(mpq, wristorbs::kLiquidFile[kind]), &bytes) && bytes.find("GlassMaterial") != std::string::npos) ++glassBefore;
    }
    const orbassets::Result r = orbassets::Prepare(game, modRoot, true, 25.0f);
    std::printf("upgrade: %d liquid file(s) in GlassMaterial before; Prepare: %ls\n", glassBefore, r.text.c_str());
    CHECK(r.state == orbassets::kMade);
    CHECK(glassBefore == 0 || r.wrote);
    for (int kind = 0; kind < 2; ++kind) {
        std::string bytes;
        CHECK(ReadAll(In(mpq, wristorbs::kLiquidFile[kind]), &bytes) && bytes.find("GlassMaterial") == std::string::npos &&
              bytes.find("StandardMaterial") != std::string::npos);
    }
}

int wmain(int argc, wchar_t** argv) {
    Pure();
    if (argc >= 4 && std::wstring(argv[3]) == L"upgrade") Upgrade(argv[1], argv[2]);
    else if (argc >= 3) Game(argv[1], argv[2], argc >= 4 && std::wstring(argv[3]) == L"keep");
    else std::printf("(no game folder given: the maker on the game's files not run)\n");
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
