// [weight] knock_props: the pure parts (shared/props_scene.h, shared/props_presets.h).
//
// Checks: a model's name to its kind (folders, numbers, case; candles are no prop); a preset's knockable props (plain entities only, a static body allowed, a light or an
// emitter not; the id and position); the catalogue made and read back (sorted, duplicates once, a wrong file refused, a prop found only at its own position); the renderer's
// matrix taken apart and put together again (turned, scaled; a mirrored one refused); the world bounds; what a prop stands on (the floor, a table top, a shelf inside a
// cabinet, a mantle in a building guessed, a cup on a plate); guessed ledges joining along a row and not across a gap.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "props_presets.h"
#include "props_scene.h"

namespace {

int g_fails = 0;
#define EXPECT(c, ...)                                         \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

using namespace propscene;

int KindNamed(const char* n) {
    const auto& k = Kinds();
    for (size_t i = 0; i < k.size(); ++i) if (std::string(k[i].name) == n) return (int)i;
    return -1;
}

void TestKinds() {
    EXPECT(Kinds().size() == 197, "197 kinds: bearstonem's 101 and the 96 of his catalogue version 4 (%zu)", Kinds().size());
    for (size_t i = 0; i < Kinds().size(); ++i)
        for (size_t j = i + 1; j < Kinds().size(); ++j) EXPECT(std::string(Kinds()[i].name) != Kinds()[j].name, "a name once (%s)", Kinds()[i].name);
    // version 4's, each kind its shape and material as in his 508c5d0
    auto is = [](const char* model, const char* name, Shape s, Material m) {
        const int k = KindOfModel(model);
        return k >= 0 && k == KindNamed(name) && Kinds()[k].shape == s && Kinds()[k].material == m;
    };
    EXPECT(is("data/hd/env/model/act1/props/sword_rusty_02.model", "sword_rusty", Shape::Box, Material::Metal), "a rusty sword: a metal box");
    EXPECT(is("data/hd/env/model/act1/props/armor_helm01.model", "armor_helm", Shape::Round, Material::Metal), "a helm: round metal");
    EXPECT(is("data/hd/env/model/act1/props/global_barrel02_open.model", "global_barrel02_open", Shape::Round, Material::Wood),
           "an open barrel: its own name, the 02 inside it kept");
    EXPECT(is("data/hd/env/model/act3/props/act3_basket01_open_03.model", "act3_basket01_open", Shape::Round, Material::Wicker), "an open basket");
    EXPECT(is("data/hd/env/model/act1/props/onion_01.model", "onion", Shape::Ball, Material::Food), "an onion: a ball");
    EXPECT(is("data/hd/env/model/act2/props/vase_red_broken.model", "vase_red_broken", Shape::Round, Material::Ceramic), "a broken vase");
    EXPECT(is("data/hd/env/model/act1/props/bench_02.model", "bench", Shape::Box, Material::Furniture), "a bench");
    EXPECT(is("data/hd/env/model/act1/props/figurine.model", "figurine", Shape::Box, Material::Stone), "a figurine");
    EXPECT(KindOfModel("data/hd/env/model/act1/props/cup_02.model") == KindNamed("cup"), "cup_02");
    EXPECT(KindOfModel("DATA\\HD\\ENV\\MODEL\\Act2\\Props\\Vase_Blue.model") == KindNamed("vase_blue"), "case and backslashes");
    EXPECT(KindOfModel("data/hd/env/model/props/barrel03.model") == KindNamed("barrel"), "a number without an underscore");
    EXPECT(KindOfModel("data/hd/env/model/props/candle_01.model") < 0, "a candle is no prop");
    EXPECT(KindOfModel("data/hd/env/model/props/cup_02.json") < 0, "not a model");
    EXPECT(KindOfModel("cup.model") == KindNamed("cup"), "no folder");
}

const char* kPreset = R"({
  "name": "tavern",
  "entities": [
    { "type": "Entity", "name": "cup", "id": 1234,
      "components": [
        { "type": "TransformDefinitionComponent", "name": "t", "position": { "x": 1.5, "y": 0.8, "z": -2.0 }, "orientation": { "x": 0, "y": 0, "z": 0, "w": 1 } },
        { "type": "ModelDefinitionComponent", "name": "m", "filename": "data/hd/env/model/act1/props/cup_02.model" } ] },
    { "type": "Entity", "name": "vase", "id": 99,
      "components": [
        { "type": "TransformDefinitionComponent", "position": { "x": 0, "y": 0, "z": 0 } },
        { "type": "ModelDefinitionComponent", "filename": "data/hd/env/model/act1/props/vase_01.model" },
        { "type": "PhysicsBodyDefinitionComponent", "bodytype": "static" },
        { "type": "WallTransparencyComponent" } ] },
    { "type": "Entity", "name": "lit jug", "id": 7,
      "components": [
        { "type": "TransformDefinitionComponent", "position": { "x": 0, "y": 0, "z": 0 } },
        { "type": "ModelDefinitionComponent", "filename": "data/hd/env/model/act1/props/jug_01.model" },
        { "type": "PointLightDefinitionComponent" } ] },
    { "type": "Entity", "name": "moving", "id": 8,
      "components": [
        { "type": "TransformDefinitionComponent", "position": { "x": 0, "y": 0, "z": 0 } },
        { "type": "ModelDefinitionComponent", "filename": "data/hd/env/model/act1/props/stool_01.model" },
        { "type": "PhysicsBodyDefinitionComponent", "bodytype": "dynamic" } ] },
    { "type": "Entity", "name": "wall", "id": 9,
      "components": [
        { "type": "TransformDefinitionComponent", "position": { "x": 0, "y": 0, "z": 0 } },
        { "type": "ModelDefinitionComponent", "filename": "data/hd/env/model/act1/walls/wall_01.model" } ] },
    { "type": "Entity", "name": "escaped \"name\"", "id": 10,
      "components": [
        { "type": "TransformDefinitionComponent", "position": { "x": -1e2, "y": 2.5E-1, "z": 3 } },
        { "type": "ModelDefinitionComponent", "filename": "data\/hd\/env\/model\/props\/book_red.model" } ] }
  ]
})";

void TestPreset() {
    std::vector<Row> rows;
    EXPECT(propspresets::PropsOf(kPreset, rows), "a preset parses");
    EXPECT(rows.size() == 3, "the cup, the vase and the book (%zu)", rows.size());
    if (rows.size() == 3) {
        EXPECT(rows[0].id == 1234 && rows[0].kind == KindNamed("cup") && rows[0].x == 1.5f && rows[0].y == 0.8f && rows[0].z == -2.0f, "the cup's id, kind, place");
        EXPECT(rows[1].id == 99 && rows[1].kind == KindNamed("vase"), "a static body and wall fading allowed");
        EXPECT(rows[2].id == 10 && rows[2].kind == KindNamed("book_red") && rows[2].x == -100.0f && std::fabs(rows[2].y - 0.25f) < 1e-6f, "escapes and exponents");
    }
    std::vector<Row> none;
    EXPECT(!propspresets::PropsOf("{ \"entities\": [ ", none), "a broken file is refused");
    EXPECT(propspresets::PropsOf("{}", none) && none.empty(), "a preset without entities");
    EXPECT(propspresets::PropsOf("\xEF\xBB\xBF{\"entities\":[]}", none), "a byte-order mark first");
}

void TestCatalog() {
    std::vector<Row> rows = {
        {500, KindNamed("cup"), 1, 2, 3}, {20, KindNamed("plate"), 0, 0, 0}, {500, KindNamed("cup"), 1, 2, 3}, {500, KindNamed("bowl"), 4, 5, 6}, {7, -1, 0, 0, 0}};
    const std::vector<uint8_t> bin = Encode(rows);
    Catalog c;
    EXPECT(!bin.empty() && c.Parse(bin.data(), bin.size()), "made and read back");
    EXPECT(c.Size() == 3, "duplicates once, an unknown kind left out (%zu)", c.Size());
    EXPECT(c.KindCount() == 3, "three kinds (%u)", c.KindCount());
    const float at[3] = {1, 2, 3}, near[3] = {1.0005f, 2, 3}, other[3] = {4, 5, 6}, wrong[3] = {1, 2, 3.01f};
    EXPECT(c.Find(500, at) >= 0 && std::string(c.KindAt((unsigned)c.Find(500, at)).name) == "cup", "the cup at its place");
    EXPECT(c.Find(500, near) >= 0, "float noise taken in");
    EXPECT(c.Find(500, other) >= 0 && std::string(c.KindAt((unsigned)c.Find(500, other)).name) == "bowl", "the same id elsewhere is another preset's bowl");
    EXPECT(c.Find(500, wrong) < 0, "an id at another place is not it");
    EXPECT(c.Find(21, at) < 0, "an unknown id");
    int how = -1;
    EXPECT(c.Find(500, wrong, &how) < 0 && how == 3, "a preset's id at another place: known elsewhere, not it (how %d)", how);
    // a prefab's entity (the Rogue Encampment's tents and wagons): found at its place, and by its id alone where the game places it otherwise
    Row tent{0x9001, KindNamed("barrel"), 7, 0, -2};
    tent.prefab = true;
    std::vector<Row> withPrefab = rows;
    withPrefab.push_back(tent);
    const std::vector<uint8_t> bin2 = Encode(withPrefab);
    Catalog p;
    EXPECT(p.Parse(bin2.data(), bin2.size()) && p.Size() == 4, "a prefab's row read back (%zu)", p.Size());
    const float tentAt[3] = {7, 0, -2}, tentMoved[3] = {107, 0, 48};
    EXPECT(p.Find(0x9001, tentAt, &how) >= 0 && how == 1 && std::string(p.KindAt((unsigned)p.Find(0x9001, tentAt)).name) == "barrel", "a prefab's barrel at its place");
    EXPECT(p.Find(0x9001, tentMoved, &how) >= 0 && how == 2, "a prefab's barrel by its id alone (how %d)", how);
    EXPECT(p.Find(500, wrong, &how) < 0 && how == 3, "a preset's id still needs its place");
    EXPECT(p.Find(21, at, &how) < 0 && how == 0, "an unknown id (how %d)", how);
    // the flags after the kind: the first entry (id 20, a preset's) 0 as in bearstonem's file, the last (the prefab's, the highest id) 1
    EXPECT(bin2.size() == 16 + 4 * 32 + 4 * 20 && bin2[16 + 4 * 32 + 18] == 0 && bin2[bin2.size() - 2] == 1, "the flags after the kind");
    std::vector<uint8_t> bad = bin;
    bad[0] ^= 1;
    Catalog d;
    EXPECT(!d.Parse(bad.data(), bad.size()) && d.Size() == 0, "a wrong magic is refused");
    EXPECT(!d.Parse(bin.data(), bin.size() - 1), "a short file is refused");
}

void TestMatrices() {
    // a prop turned 90 degrees about y, scaled 2 x 1 x 0.5, at (10, 1, -4)
    Placement p;
    p.p = {10, 1, -4};
    const float s = std::sqrt(0.5f);
    p.q = {0, s, 0, s};
    p.scale = {2, 1, 0.5f};
    float m[16];
    Compose(p, m);
    Placement back;
    EXPECT(Decompose(m, back), "taken apart");
    EXPECT(std::fabs(back.p.x - 10) < 1e-5f && std::fabs(back.p.z + 4) < 1e-5f, "its place");
    EXPECT(std::fabs(back.scale.x - 2) < 1e-4f && std::fabs(back.scale.y - 1) < 1e-4f && std::fabs(back.scale.z - 0.5f) < 1e-4f, "its scale");
    const float dq = std::fabs(back.q.x * p.q.x + back.q.y * p.q.y + back.q.z * p.q.z + back.q.w * p.q.w);
    EXPECT(dq > 0.9999f, "its turn (%.5f)", dq);
    float again[16];
    Compose(back, again);
    bool same = true;
    for (int i = 0; i < 16; ++i) same = same && std::fabs(again[i] - m[i]) < 1e-4f;
    EXPECT(same, "and put together again the same");
    float mirror[16];
    std::memcpy(mirror, m, sizeof mirror);
    mirror[0] = -mirror[0]; mirror[1] = -mirror[1]; mirror[2] = -mirror[2];
    EXPECT(!Decompose(mirror, back), "a mirrored model is no rigid body");
    // world bounds: a unit box under that matrix
    const Box local{{-0.5f, 0, -0.5f}, {0.5f, 1, 0.5f}};
    const Box w = WorldBounds(local, m);
    EXPECT(std::fabs((w.hi.x - w.lo.x) - 0.5f) < 1e-4f && std::fabs((w.hi.z - w.lo.z) - 2.0f) < 1e-4f, "the bounds turned with it (x %.2f z %.2f)", w.hi.x - w.lo.x, w.hi.z - w.lo.z);
    EXPECT(std::fabs(w.lo.y - 1) < 1e-4f && std::fabs(w.hi.y - 2) < 1e-4f, "and raised");
}

void TestSupports() {
    const float u = 1.0f, floor = 0.0f;
    const Box table{{0, 0, 0}, {1.2f, 0.8f, 0.8f}};
    const Box cabinet{{3, 0, 0}, {3.5f, 1.8f, 0.4f}};
    const Box building{{-10, 0, -10}, {10, 6, 10}};
    const std::vector<Box> around = {table, cabinet, building};
    const Box onFloor{{0.2f, 0.0f, 2.0f}, {0.3f, 0.2f, 2.1f}};
    EXPECT(FindSupport(onFloor, around, floor, u).floor, "on the floor");
    const Box cup{{0.5f, 0.8f, 0.3f}, {0.58f, 0.9f, 0.38f}};
    const Support t = FindSupport(cup, around, floor, u);
    EXPECT(!t.floor && !t.guessed && std::fabs(t.y - 0.8f) < 1e-5f, "on the table top (y %.2f)", t.y);
    EXPECT(t.x0 >= 0 && t.x1 <= 1.2f, "over the table only");
    const Box shelf{{3.1f, 1.0f, 0.1f}, {3.2f, 1.15f, 0.2f}};   // inside the cabinet's bounds: its own bottom
    const Support c = FindSupport(shelf, around, floor, u);
    EXPECT(!c.floor && std::fabs(c.y - 1.0f) < 1e-5f, "a shelf inside a cabinet: the prop's own bottom (y %.2f)", c.y);
    const Box mantle{{5, 1.4f, 5}, {5.1f, 1.6f, 5.1f}};   // only the building round it
    const Support g = FindSupport(mantle, around, floor, u);
    EXPECT(g.guessed && std::fabs(g.y - 1.4f) < 1e-5f && g.x1 - g.x0 < 0.2f, "inside a building the plane is guessed, just past the prop");
    const Box plate{{0.5f, 0.8f, 0.3f}, {0.7f, 0.82f, 0.5f}}, onPlate{{0.55f, 0.82f, 0.35f}, {0.62f, 0.9f, 0.42f}};
    EXPECT(RestsOn(onPlate, plate, u), "a cup on a plate");
    EXPECT(!RestsOn(plate, onPlate, u), "not the plate on the cup");
    // ledges: three books in a row join; one past a gap does not
    std::vector<Support> row;
    for (float x : {5.0f, 5.12f, 5.24f, 6.5f}) {
        Support s{false, 1.4f, x, 5, x + 0.1f, 5.1f};
        s.guessed = true;
        row.push_back(s);
    }
    JoinLedges(row, u);
    EXPECT(row[0].x0 == 5.0f && std::fabs(row[0].x1 - 5.34f) < 1e-4f && row[1] == row[0] && row[2] == row[0], "the row joined (%.2f..%.2f)", row[0].x0, row[0].x1);
    EXPECT(row[3].x0 == 6.5f && std::fabs(row[3].x1 - 6.6f) < 1e-4f, "the one past the gap alone");
}

}  // namespace

int main() {
    TestKinds();
    TestPreset();
    TestCatalog();
    TestMatrices();
    TestSupports();
    if (g_fails) {
        std::printf("props_scene_test: %d check(s) failed\n", g_fails);
        return 1;
    }
    std::printf("props_scene_test: all checks passed\n");
    return 0;
}
