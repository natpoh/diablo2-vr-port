// [render] aura_centre: the Paladin's aura rings centred under the hero in VR - the pure part (vr/aura_view.h).
//
//   aura_view_test
//
// Checks: which particle files are the auras' (the Paladin's overlay folder only, any case or slash, nothing climbing out); the direction they get (the vertical part of the
// game camera's own direction - no sideways part - and nothing for a direction that is not one); the game camera's direction out of its view matrix; how fresh it must be; the
// push it leaves (-direction * k: straight up, k * |forward.y|) against the native one (sideways in VR); reading a batch's instances, their definitions and the definitions'
// paths out of a fake game memory (and refusing every wrong vtable, count or pointer); the per-definition cache; the whole batch: only the aura lanes written, a batch
// with one instance or one per lane, a mixed batch, a stream that is not float3.

#include "../../vr/aura_view.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_fails = 0;
int g_checks = 0;

#define EXPECT(c)                                              \
    do {                                                       \
        ++g_checks;                                            \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
        }                                                      \
    } while (0)

using namespace auraview;

// A fake game memory: one flat block at kMem; the "image base" is a number the vtables are counted from.
constexpr uint64_t kMem = 0x200000, kGameBase = 0x140000000ull;
struct Mem {
    std::vector<uint8_t> b = std::vector<uint8_t>(0x20000, 0);
    unsigned writes = 0;
    bool rd(uint64_t at, void* out, size_t n) const {
        if (at < kMem || at + n > kMem + b.size()) return false;
        std::memcpy(out, b.data() + (at - kMem), n);
        return true;
    }
    bool wr(uint64_t at, const void* in, size_t n) {
        if (at < kMem || at + n > kMem + b.size()) return false;
        std::memcpy(b.data() + (at - kMem), in, n);
        ++writes;
        return true;
    }
    template <class T> void put(uint64_t at, T v) { std::memcpy(b.data() + (at - kMem), &v, sizeof v); }
    template <class T> T get(uint64_t at) const { T v{}; std::memcpy(&v, b.data() + (at - kMem), sizeof v); return v; }
    void str(uint64_t at, const char* s) { std::memcpy(b.data() + (at - kMem), s, std::strlen(s) + 1); }
};

// The game's objects for one effect definition with the given file; returns the definition.
uint64_t MakeDefinition(Mem& m, uint64_t at, const char* path) {
    const uint64_t def = at, compiled = at + 0x100, string = at + 0x200;
    m.put(def, kGameBase + kDefinitionVtbl);
    m.put(def + 9 * 8, compiled);
    m.put(compiled, kGameBase + kCompiledVtbl);
    m.put(compiled + 2 * 8, string);
    m.put(string, kGameBase + kStringVtbl);
    m.str(string + kStringChars, path);
    return def;
}
// An effect instance of a definition, put in the scene's pool as id.
void MakeInstance(Mem& m, uint64_t scene, uint64_t pages, uint64_t page0, int32_t id, uint64_t inst, uint64_t def) {
    m.put(scene + kScenePool, pages);       // pages
    m.put(scene + kScenePool + 16, (uint32_t)1);   // count: one page of 512
    m.put(pages, page0);
    m.put(page0 + 8ull * (uint32_t)id, inst);
    m.put(inst, kGameBase + kInstanceVtbl);
    m.put(inst + 8 * 8, def);
    m.put(inst + 9 * 8, inst + 0x80);       // its transform
}

void Assets() {
    EXPECT(Asset("data/hd/vfx/particles/overlays/paladin/aura_thorns_front/FX_thorns_overlay.particles"));
    EXPECT(Asset("DATA\\HD\\VFX\\Particles\\Overlays\\Paladin\\holyfreeze\\FX_holyfreeze_overlay.PARTICLES"));
    EXPECT(Asset("data/hd/vfx/particles/overlays/paladin/convictionfront/FX_Conviction_Overlay.particles"));
    EXPECT(!Asset("data/hd/vfx/particles/overlays/necro/bonearmor_front/vfx_BoneArmor_Front.particles"));   // another class's overlay
    EXPECT(!Asset("data/hd/vfx/particles/overlays/common/shrine_mana_regen/vfx_shrine_mana_regen.particles"));
    EXPECT(!Asset("data/hd/vfx/particles/objects/vfx_only/town_portal/x.particles"));
    EXPECT(!Asset("data/hd/vfx/particles/overlays/paladin/../necro/x.particles"));   // nothing that climbs out
    EXPECT(!Asset("data/hd/vfx/particles/overlays/paladin/thorns.json"));            // not a particle file
    EXPECT(!Asset("data/hd/vfx/particles/overlays/paladin/.particles"));             // no name
    EXPECT(!Asset("") && !Asset(nullptr));
    char longp[200];
    std::snprintf(longp, sizeof longp, "data/hd/vfx/particles/overlays/paladin/%0120d.particles", 7);
    EXPECT(!Asset(longp));   // longer than the path the game's string holds for us
}

void Directions() {
    // the game camera looks 60 degrees down along -z: at 100 % the aura effects get its whole vertical part, nothing across the floor
    const Vec f{0.0f, -std::sin(1.0471976f), -std::cos(1.0471976f)};
    Vec v;
    EXPECT(Upright(f, 1.0f, &v) && v.x == 0.0f && v.z == 0.0f && std::fabs(v.y - f.y) < 1e-6f);
    // the push the effects make with it (-direction * k, k = 3 as Holy Freeze): straight up, the game's own lift; with the native VR direction it is sideways
    const float k = 3.0f;
    const Vec push{-v.x * k, -v.y * k, -v.z * k};
    EXPECT(push.x == 0.0f && push.z == 0.0f && std::fabs(push.y - k * std::sin(1.0471976f)) < 1e-5f);
    // THE DEFAULT, 0 %: the ring at its root (the feet), as the flat game shows it - a thousandth of the direction kept (never a zero vector), the push under a millimetre
    EXPECT(Upright(f, 0.0f, &v) && v.x == 0.0f && v.z == 0.0f && v.y < 0.0f && std::fabs(v.y - f.y * kMinLift) < 1e-7f);
    EXPECT(-v.y * k < 0.005f);   // 3 units * 0.001 * 0.87 = 0.0026 units, ~0.5 mm at ~5 units a metre
    // a share between: linear in it (d2r-pcvr's headset value 0.475 = that much of the lift)
    EXPECT(Upright(f, 0.475f, &v) && std::fabs(v.y - f.y * 0.475f) < 1e-6f);
    // out of range: held to [kMinLift, 1]; not finite: nothing replaced
    EXPECT(Upright(f, 3.0f, &v) && std::fabs(v.y - f.y) < 1e-6f);
    EXPECT(Upright(f, -1.0f, &v) && std::fabs(v.y - f.y * kMinLift) < 1e-7f);
    EXPECT(!Upright(f, NAN, &v));
    EXPECT(ClampLiftPct(40.0f) == 40.0f && ClampLiftPct(-5.0f) == 0.0f && ClampLiftPct(150.0f) == 100.0f && ClampLiftPct(NAN) == 0.0f);
    const Vec headLevel{0.0f, 0.0f, -1.0f};   // the head in F4 looking ahead: the native push is 3 units BEHIND the ring's place, on the floor
    EXPECT(-headLevel.z * k == 3.0f && -headLevel.y * k == 0.0f);
    // the game's own camera looks diagonally (its iso view: down and to one side) - still nothing across the floor survives
    const Vec diag{0.5f, -0.70710678f, -0.5f};
    EXPECT(Upright(diag, 1.0f, &v) && v.x == 0.0f && v.z == 0.0f && std::fabs(v.y + 0.70710678f) < 1e-6f);
    // not a direction: nothing replaced
    EXPECT(!Upright(Vec{0.0f, 0.5f, -0.866f}, 1.0f, &v));            // looking up
    EXPECT(!Upright(Vec{0.0f, 0.0f, -1.0f}, 1.0f, &v));              // level: no vertical part to keep
    EXPECT(!Upright(Vec{0.0f, -2.0f, 0.0f}, 1.0f, &v));              // not of unit length
    EXPECT(!Upright(Vec{NAN, -1.0f, 0.0f}, 1.0f, &v));
    EXPECT(Upright(Vec{0.0f, -1.0f, 0.0f}, 1.0f, &v) && v.y == -1.0f);   // straight down (the view from above)
    // the game camera's direction from its view matrix: column 2 negated (row-major, v * M)
    float g[16] = {1, 0, 0, 0,  0, 0.5f, 0.866f, 0,  0, -0.866f, 0.5f, 0,  10, 20, 30, 1};
    const Vec fw = ForwardOfView(g);
    EXPECT(fw.x == 0.0f && std::fabs(fw.y + 0.866f) < 1e-6f && std::fabs(fw.z + 0.5f) < 1e-6f);
    EXPECT(Upright(fw, 1.0f, &v) && std::fabs(v.y + 0.866f) < 1e-6f);
    // fresh: set within the last 250 ms
    EXPECT(Fresh(1000, 1000) && Fresh(1250, 1000) && !Fresh(1251, 1000) && !Fresh(1000, 0) && !Fresh(999, 1000));
}

void Reading() {
    Mem m;
    const uint64_t scene = kMem + 0x1000, pages = kMem + 0x1100, page0 = kMem + 0x2000;
    const uint64_t thorns = MakeDefinition(m, kMem + 0x4000, "data/hd/vfx/particles/overlays/paladin/aura_thorns_front/FX_thorns_overlay.particles");
    const uint64_t bone = MakeDefinition(m, kMem + 0x4400, "data/hd/vfx/particles/overlays/necro/bonearmor_front/vfx_BoneArmor_Front.particles");
    MakeInstance(m, scene, pages, page0, 5, kMem + 0x5000, thorns);
    MakeInstance(m, scene, pages, page0, 6, kMem + 0x5200, bone);
    auto rd = [&](uint64_t a, void* o, size_t n) { return m.rd(a, o, n); };
    Context c;
    c.scene = scene;
    c.instance = 5;
    EXPECT(LaneInstance(rd, c, 0) == 5 && LaneInstance(rd, c, 99) == 5);   // one instance for the batch
    EXPECT(DefinitionOf(rd, c, 5, kGameBase) == thorns && DefinitionOf(rd, c, 6, kGameBase) == bone);
    char path[kPathCap];
    EXPECT(DefinitionPath(rd, thorns, kGameBase, path) && std::strstr(path, "FX_thorns_overlay") && Asset(path));
    EXPECT(DefinitionPath(rd, bone, kGameBase, path) && !Asset(path));
    // per lane: the index list
    const uint64_t idx = kMem + 0x1200;
    m.put(idx, (int32_t)6);
    m.put(idx + 4, (int32_t)-1);
    m.put(idx + 8, (int32_t)5);
    m.put(idx + 12, (int32_t)200000);
    Context per = c;
    per.instance = -1;
    per.indices = idx;
    per.count = 4;
    EXPECT(LaneInstance(rd, per, 0) == 6 && LaneInstance(rd, per, 1) == -1 && LaneInstance(rd, per, 2) == 5);
    EXPECT(LaneInstance(rd, per, 3) == -1);   // past the ids the game can have
    EXPECT(LaneInstance(rd, per, 4) == -1);   // past the list
    Context noList = per;
    noList.indices = 0;
    EXPECT(LaneInstance(rd, noList, 0) == -1);
    // the wrong objects are refused
    EXPECT(DefinitionOf(rd, c, 7, kGameBase) == 0);            // an empty pool slot
    EXPECT(DefinitionOf(rd, c, 600, kGameBase) == 0);          // a page the pool has not
    EXPECT(DefinitionOf(rd, c, 5, kGameBase + 0x1000) == 0);   // another build: the vtable is not the one we know
    Context noScene = c;
    noScene.scene = 0;
    EXPECT(DefinitionOf(rd, noScene, 5, kGameBase) == 0);
    m.put(scene + kScenePool + 16, (uint32_t)300);             // a pool count no scene has
    EXPECT(DefinitionOf(rd, c, 5, kGameBase) == 0);
    m.put(scene + kScenePool + 16, (uint32_t)1);
    EXPECT(!DefinitionPath(rd, thorns, kGameBase + 8, path) && path[0] == 0);
    const uint64_t broken = MakeDefinition(m, kMem + 0x4800, "x");
    m.put(broken + 0x100, (uint64_t)1234);                     // the compiled asset's vtable wrong
    EXPECT(!DefinitionPath(rd, broken, kGameBase, path));
    const uint64_t broken2 = MakeDefinition(m, kMem + 0x4C00, "x");
    m.put(broken2 + 0x200, (uint64_t)1234);                    // the string's vtable wrong
    EXPECT(!DefinitionPath(rd, broken2, kGameBase, path) && path[0] == 0);
    const uint64_t unterminated = MakeDefinition(m, kMem + 0x6000, "y");
    std::memset(m.b.data() + (unterminated + 0x200 + kStringChars - kMem), 'a', kPathCap);   // no end within the cap
    EXPECT(!DefinitionPath(rd, unterminated, kGameBase, path) && path[0] == 0);
    EXPECT(!DefinitionPath(rd, 0, kGameBase, path));
}

void Cache() {
    Kinds k;
    EXPECT(k.Get(0x1000, 5) == -1);
    k.Put(0x1000, 5, true);
    EXPECT(k.Get(0x1000, 5) == 1 && k.Get(0x1000, 1005) == 1 && k.Get(0x1000, 1006) == -1);   // kept a second, then read again
    k.Put(0x1000 + 32 * 64, 10, false);                                                     // the same slot: the newer one stays
    EXPECT(k.Get(0x1000, 10) == -1 && k.Get(0x1000 + 32 * 64, 10) == 0);
    EXPECT(k.Get(0x2000, 4) == -1);
}

void Batches() {
    Mem m;
    const uint64_t scene = kMem + 0x1000, pages = kMem + 0x1100, page0 = kMem + 0x2000;
    const uint64_t thorns = MakeDefinition(m, kMem + 0x4000, "data/hd/vfx/particles/overlays/paladin/aura_thorns_front/FX_thorns_overlay.particles");
    const uint64_t bone = MakeDefinition(m, kMem + 0x4400, "data/hd/vfx/particles/overlays/necro/bonearmor_front/vfx_BoneArmor_Front.particles");
    MakeInstance(m, scene, pages, page0, 5, kMem + 0x5000, thorns);
    MakeInstance(m, scene, pages, page0, 6, kMem + 0x5200, bone);
    auto rd = [&](uint64_t a, void* o, size_t n) { return m.rd(a, o, n); };
    auto wr = [&](uint64_t a, const void* in, size_t n) { return m.wr(a, in, n); };
    unsigned pathReads = 0;
    auto isAura = [&](uint64_t def) {
        ++pathReads;
        char p[kPathCap];
        return DefinitionPath(rd, def, kGameBase, p) && Asset(p);
    };
    const Vec native{0.7f, -0.1f, -0.7f};   // the VR head's direction, as the game handed it
    const Vec value{0.0f, -0.5f, 0.0f};
    const uint64_t data = kMem + 0x8000;
    auto fill = [&](uint32_t n) { for (uint32_t i = 0; i < n; ++i) m.put(data + 12ull * i, native); };
    Stream out;
    out.data = data;
    out.count = 4;
    out.stride = 12;
    Kinds kinds;
    // one instance for the batch, an aura's: every lane gets the upright direction; the path is read once
    fill(4);
    Context c;
    c.scene = scene;
    c.instance = 5;
    uint64_t first = 0;
    EXPECT(CorrectBatch(rd, wr, out, c, kGameBase, value, &kinds, 100, isAura, &first) == 4 && first == thorns && pathReads == 1);
    for (uint32_t i = 0; i < 4; ++i) {
        const Vec got = m.get<Vec>(data + 12ull * i);
        EXPECT(got.x == 0.0f && got.y == -0.5f && got.z == 0.0f);
    }
    // the cache: the same definition again within a second reads no path
    fill(4);
    EXPECT(CorrectBatch(rd, wr, out, c, kGameBase, value, &kinds, 600, isAura) == 4 && pathReads == 1);
    // not an aura's (Bone Armor): nothing written
    fill(4);
    Kinds k2;
    c.instance = 6;
    const unsigned before = m.writes;
    EXPECT(CorrectBatch(rd, wr, out, c, kGameBase, value, &k2, 100, isAura) == 0 && m.writes == before);
    EXPECT(m.get<Vec>(data).x == native.x);
    // a mixed batch, one id per lane: only the aura's lanes (0 and 2); lane 1 has no id, lane 3 is Bone Armor's
    fill(4);
    const uint64_t idx = kMem + 0x1200;
    m.put(idx, (int32_t)5);
    m.put(idx + 4, (int32_t)-1);
    m.put(idx + 8, (int32_t)5);
    m.put(idx + 12, (int32_t)6);
    Context per;
    per.scene = scene;
    per.instance = -1;
    per.indices = idx;
    per.count = 4;
    Kinds k3;
    EXPECT(CorrectBatch(rd, wr, out, per, kGameBase, value, &k3, 100, isAura) == 2);
    EXPECT(m.get<Vec>(data).y == -0.5f && m.get<Vec>(data + 12).x == native.x && m.get<Vec>(data + 24).y == -0.5f && m.get<Vec>(data + 36).x == native.x);
    // streams it never writes: not float3, no lanes, too many, no data
    Stream wide = out;
    wide.stride = 16;
    Stream none = out;
    none.count = 0;
    Stream many = out;
    many.count = kMaxLanes + 1;
    Stream nodata = out;
    nodata.data = 0;
    c.instance = 5;
    EXPECT(CorrectBatch(rd, wr, wide, c, kGameBase, value, &kinds, 100, isAura) == 0);
    EXPECT(CorrectBatch(rd, wr, none, c, kGameBase, value, &kinds, 100, isAura) == 0);
    EXPECT(CorrectBatch(rd, wr, many, c, kGameBase, value, &kinds, 100, isAura) == 0);
    EXPECT(CorrectBatch(rd, wr, nodata, c, kGameBase, value, &kinds, 100, isAura) == 0);
    EXPECT(StreamOk(out) && !StreamOk(wide) && !StreamOk(none) && !StreamOk(many) && !StreamOk(nodata));
    // another build (the vtables elsewhere): nothing is known as an aura, nothing written
    fill(4);
    Kinds k4;
    EXPECT(CorrectBatch(rd, wr, out, c, kGameBase + 0x10, value, &k4, 100, isAura) == 0);
}

}  // namespace

int main() {
    Assets();
    Directions();
    Reading();
    Cache();
    Batches();
    if (g_fails) {
        std::printf("aura_view_test: %d of %d FAILED\n", g_fails, g_checks);
        return 1;
    }
    std::printf("aura_view_test: the auras' files, the upright direction and the push it leaves, the game camera's direction, reading the game's objects, the cache, whole batches: %d checks, all passed\n",
                g_checks);
    return 0;
}
