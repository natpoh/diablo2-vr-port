// granny_remesh_test - shared/granny_remesh.h: [sky] game_sky's dome made from the game's own Summit sky card.
//   1. BitKnit2 raw quanta (how sections are written back) read back by pybg3's decoder as they were: empty, odd, several quanta;
//   2. half floats; the dome: 1105 vertices, 1984 triangles both ways round (3968), every index in range, the game's VertexScale code;
//   granny_remesh_test [<the game's skybox01_lod1.model> [<out .model>]]: the card made our dome, written, read again - our vertices,
//   indices, one group, VertexScale 800, the bones identity, our texture's name, the types' section the game's byte for byte.
#include "granny_remesh.h"

#include <cstdio>
#include <cstdlib>
#include <string>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using namespace grannyremesh;

static bool ReadAll(const char* path, std::string* out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    out->clear();
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->append(buf, n);
    std::fclose(f);
    return true;
}

int main(int argc, char** argv) {
    // 1. raw quanta
    for (size_t n : {size_t(0), size_t(1), size_t(3), size_t(4096), size_t(65536), size_t(70001), size_t(200000)}) {
        std::vector<uint8_t> in(n);
        uint32_t x = 12345u;
        for (uint8_t& b : in) { x = x * 1103515245u + 12345u; b = (uint8_t)(x >> 16); }
        const std::vector<uint8_t> k = KnitRaw(in.data(), in.size());
        std::vector<uint8_t> back(n, 0xCD);
        CHECK(Unknit(back.data(), n, k.data(), k.size()));
        CHECK(back == in);
    }
    // 2. halves, the dome
    CHECK(Half(0.0f) == 0 && Half(1.0f) == 0x3C00 && Half(0.5f) == 0x3800 && Half(0.015625f) == 0x2400 && Half(-2.0f) == 0xC000);
    const Mesh m = DomeMesh(800.0f);
    CHECK(m.v.size() == 1105 && m.idx.size() == 3968 * 3);
    bool inRange = true;
    for (uint16_t i : m.idx) inRange = inRange && i < m.v.size();
    CHECK(inRange);
    CHECK(m.v[0].pos[3] == 24308 && m.scale == 800.0f);              // log2(800) / 13 * 32767, as the game's 326.123 -> 21045
    CHECK(std::lround(std::log2(326.123) / 13.0 * 32767.0) == 21045);
    CHECK(m.mx[1] > 799.0f && m.mn[1] < -160.0f && m.mn[1] > -170.0f);  // up to the zenith, down to 12 degrees under the horizon
    if (argc > 1) {
        std::string card;
        CHECK(ReadAll(argv[1], &card));
        File f;
        std::string why;
        CHECK(Read(card, &f, &why));
        if (!why.empty()) std::printf("  read: %s\n", why.c_str());
        const std::vector<uint8_t> types = f.sec.size() > 6 ? f.sec[6] : std::vector<uint8_t>();
        Done d;
        const char* oldTex = "data/hd/env/texture/expansion/mountaintop/mountaintop_skybox/expansion_mountaintop_skybox01_HDR.dds";
        const char* newTex = "data/hd/env/texture/d2r_vr/sky/sky_act1_day_HDR.dds";
        CHECK(Remesh(f, m, oldTex, newTex, 2, &d, &why));
        if (!why.empty()) std::printf("  remesh: %s\n", why.c_str());
        std::printf("  the card made our dome: %u vertices, %u triangles, %u texture name(s), %u bone(s)\n", d.vertices, d.triangles, d.textureNames, d.bones);
        const std::string out = Write(f);   // (uncompressed: compression 0 in every section)
        {
            const std::string knit = Write(f, true);   // the raw-quanta form still reads back the same
            File k;
            CHECK(Read(knit, &k, &why) && k.sec == f.sec && k.sh[0].compression == kBitKnit2);
        }
        if (argc > 2) {
            if (FILE* o = std::fopen(argv[2], "wb")) { std::fwrite(out.data(), 1, out.size(), o); std::fclose(o); }
        }
        File g;
        CHECK(Read(out, &g, &why));
        CHECK(g.sec.size() == f.sec.size() && g.sec.size() == 8);
        CHECK(g.sec.size() > 6 && g.sec[6] == types);                // the types: the game's, byte for byte
        for (size_t i = 0; i < g.sh.size(); ++i) CHECK(g.sh[i].compression == 0);
        Walk w(g);
        const Addr rt{g.h.rootType.section, g.h.rootType.offset}, ro{g.h.rootObj.section, g.h.rootObj.offset};
        Addr t;
        const Addr meshes = w.Member(rt, ro, "Meshes", &t);
        const Addr meshType = w.Ref(t), mesh = w.Ptr(w.Ptr(meshes.at(4)));
        const Addr pvd = w.Ptr(w.Member(meshType, mesh, "PrimaryVertexData", &t));
        const Addr verts = w.Member(w.Ref(t), pvd, "Vertices", &t);
        CHECK(w.Get<int32_t>(verts.at(8)) == 1105);
        const Addr vd = w.Ptr(verts.at(12));
        CHECK(w.In(vd, 1105 * 20) && std::memcmp(&g.sec[vd.s][vd.o], m.v.data(), 1105 * 20) == 0);
        const Addr topo = w.Ptr(w.Member(meshType, mesh, "PrimaryTopology", &t));
        const Addr topoType = w.Ref(t);
        Addr e;
        const Addr i16 = w.Member(topoType, topo, "Indices16", &e);
        CHECK(w.Get<int32_t>(i16) == 3968 * 3);
        const Addr id = w.Ptr(i16.at(4));
        CHECK(w.In(id, 3968 * 6) && std::memcmp(&g.sec[id.s][id.o], m.idx.data(), 3968 * 6) == 0);
        const Addr groups = w.Member(topoType, topo, "Groups", &e);
        CHECK(w.Get<int32_t>(groups) == 1);
        const Addr ext = w.Member(meshType, mesh, "ExtendedData", &e);
        const Addr vs = w.Member(w.Ptr(ext), w.Ptr(ext.at(8)), "VertexScale", &e);
        CHECK(w.Get<float>(vs) == 800.0f);
        // the texture: ours named, the card's no more
        int ours = 0, theirs = 0;
        for (size_t s = 0; s < g.fix.size(); ++s)
            for (const Fixup& x : g.fix[s]) {
                const std::string str = w.Str(Addr{x.ptr.section, x.ptr.offset});
                ours += str == newTex;
                theirs += str == oldTex;
            }
        CHECK(ours == (int)d.textureNames && ours > 0 && theirs == 0);
        CHECK(d.bones >= 1);
        // the CRC: the game's rule (from +104 to the end)
        Header h{};
        std::memcpy(&h, out.data() + kHeaderAt, sizeof h);
        CHECK(h.crc == Crc32((const uint8_t*)out.data() + kSectionsAt, out.size() - kSectionsAt) && h.fileSize == out.size());
        CHECK(h.typeTag == 0x80000039u);
        std::printf("  written: %zu bytes, 8 sections (uncompressed), read back the same\n", out.size());
    }
    if (g_fail) { std::printf("granny_remesh_test: %d FAILED\n", g_fail); return 1; }
    std::printf("granny_remesh_test: raw quanta read back, the dome's mesh, %s: all passed\n", argc > 1 ? "the game's card made our dome" : "(no card given)");
    return 0;
}
