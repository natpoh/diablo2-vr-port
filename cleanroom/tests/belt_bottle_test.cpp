// Offline checks of what becomes of a belt potion past the drink (vr/belt_bottle.h): its size and weight, the matrix <-> falling body round trip, back on the belt or onto
// the ground, the game's word by the item's id, the empties lying about. No game, no headset.
// Run: build/Release/belt_bottle_test.exe   (exit code 0 = all passed)

#include "../../vr/belt_bottle.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace beltbottle;

static int g_failures = 0, g_checks = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

int main() {
    {   // sizes: the big rejuvenation flask is wider than a vial; an unknown code is a common flask
        const Size big = SizeOf("rvl"), vial = SizeOf("vps"), anti = SizeOf("yps"), other = SizeOf("xyz");
        Expect(big.radius > 2.5f * vial.radius, "the full rejuvenation flask is much wider than the stamina vial", big.radius);
        Expect(anti.height < 0.9f && vial.height < 1.0f, "the vials are shorter than the flasks' 1.0", anti.height);
        Expect(other.radius == 0.25f && other.height == 1.0f, "an unknown code: a common flask");
        Expect(SizeOf(nullptr).radius == 0.25f, "no code: a common flask");
    }
    {   // its body in metres: a flask 0.62 of a model unit high in a world of 6.2 units a metre is 10 cm; full is heavier than empty
        const Size s = SizeOf("hp1");
        const bottlefall::Shape full = ShapeOf(s, 0.62f, 6.2f, false), empty = ShapeOf(s, 0.62f, 6.2f, true);
        Expect(std::fabs(full.height - 0.1) < 1e-6, "the height in metres", full.height);
        Expect(std::fabs(full.radius - 0.0205) < 1e-6, "the radius in metres", full.radius);
        Expect(full.mass > 4.0 * empty.mass, "full is far heavier than empty", full.mass / empty.mass);
        Expect(full.mass > 0.05 && full.mass < 1.0, "a small flask weighs between 50 g and a kilo", full.mass);
        const bottlefall::Shape none = ShapeOf(s, 0.0f, 6.2f, false);
        Expect(none.radius == bottlefall::Shape().radius, "no scale: the default body");
    }
    {   // a matrix -> the body -> the matrix again: the same place and axes
        // a bottle tipped 40 degrees about z, scaled 0.6, at (100, 5, -40) world units, 7 units a metre
        const double a = 40.0 * 3.14159265358979 / 180.0, c = std::cos(a), s = std::sin(a);
        const float k = 0.6f;
        float m[16] = {(float)(c * k), (float)(s * k), 0, 0, (float)(-s * k), (float)(c * k), 0, 0, 0, 0, k, 0, 100, 5, -40, 1};
        const double origin[3] = {98, 0, -41};
        double base[3], q[4];
        Expect(FromMatrix(m, origin, 7.0f, base, q), "a matrix with a size reads");
        Expect(std::fabs(base[0] - 2.0 / 7.0) < 1e-6 && std::fabs(base[1] - 5.0 / 7.0) < 1e-6 && std::fabs(base[2] - 1.0 / 7.0) < 1e-6, "the base in metres from the origin", base[0]);
        const bottlefall::Shape shape{0.03, 0.1, 0.3};
        const double zero[3] = {0, 0, 0};
        const bottlefall::Sim sim = bottlefall::Release(shape, base, q, zero, zero);
        float back[16];
        ToMatrix(sim, origin, 7.0f, k, back);
        double worst = 0;
        for (int i = 0; i < 16; ++i) worst = std::fmax(worst, std::fabs(back[i] - m[i]));
        Expect(worst < 1e-4, "the round trip gives the same matrix", worst);
        const float flat[16] = {};
        Expect(!FromMatrix(flat, origin, 7.0f, base, q), "a matrix with no size does not read");
        // every axis-aligned turn and its mirror image survive the quaternion (the branches of FromAxes)
        const double turns[4][9] = {{1, 0, 0, 0, 1, 0, 0, 0, 1}, {-1, 0, 0, 0, -1, 0, 0, 0, 1}, {-1, 0, 0, 0, 1, 0, 0, 0, -1}, {1, 0, 0, 0, -1, 0, 0, 0, -1}};
        for (const auto& t : turns) {
            double qq[4];
            FromAxes(t, t + 3, t + 6, qq);
            double e = 0;
            for (int r = 0; r < 3; ++r) {
                const double u[3] = {r == 0 ? 1.0 : 0.0, r == 1 ? 1.0 : 0.0, r == 2 ? 1.0 : 0.0};
                double o[3];
                bottlefall::detail::Rot(qq, u, o);
                for (int i = 0; i < 3; ++i) e = std::fmax(e, std::fabs(o[i] - t[r * 3 + i]));
            }
            Expect(e < 1e-9, "a half turn about an axis survives the quaternion", e);
        }
    }
    {   // let go: near its place it goes back; away from it, it falls - only with the drop on
        Expect(OnLetGo(true, 0.40f, 0.25f) == LetGo::Falls, "away from the belt: it falls");
        Expect(OnLetGo(true, 0.10f, 0.25f) == LetGo::Back, "at its place: back on the belt");
        Expect(OnLetGo(false, 0.90f, 0.25f) == LetGo::Back, "the drop off: always back (as before)");
        Expect(OnLetGo(true, NAN, 0.25f) == LetGo::Back, "the distance not known: back");
    }
    {   // the throw to the mercenary
        Expect(ForMerc("hp3") && ForMerc("rvl") && ForMerc("yps") && ForMerc("wms") && !ForMerc("mp2") && !ForMerc("vps") && !ForMerc(nullptr),
               "healing, rejuvenation, antidote, thawing are his; mana and stamina are not");
        Expect(OnLetGo(false, true, true, 0.6f, 0.25f) == LetGo::Falls, "the merc throw on, his potion: it flies even with the drop off");
        Expect(OnLetGo(false, true, false, 0.6f, 0.25f) == LetGo::Back, "a mana potion with the drop off: back");
        Expect(OnLetGo(false, true, true, 0.1f, 0.25f) == LetGo::Back, "at its place: back");
        Merc m;
        m.seen = true;
        m.bottom[0] = 3; m.bottom[1] = 0.3; m.bottom[2] = 1;
        m.top[0] = 3; m.top[1] = 1.4; m.top[2] = 1;
        m.radius = 0.3;
        const double inBody[3] = {3.2, 1.0, 1.1}, overHead[3] = {3, 2.0, 1}, nearFeet[3] = {4.2, 0.0, 1.5}, far[3] = {5.5, 0, 1};
        Expect(HitsMerc(m, inBody, 0.03), "a bottle in his body reached him");
        Expect(!HitsMerc(m, overHead, 0.03), "over his head: not");
        Merc none;
        Expect(!HitsMerc(none, inBody, 0.03) && !LandsByMerc(none, inBody, 1.5), "no merc seen: never");
        Expect(LandsByMerc(m, nearFeet, 1.5) && !LandsByMerc(m, far, 1.5), "landed within 1.5 m of his feet / 2.5 m off");
        Expect(OnFlight(true, true, false, true, false, false) == Target::Merc, "it reached him in the air: his");
        Expect(OnFlight(true, true, false, false, true, true) == Target::Merc, "landed by his feet: his");
        Expect(OnFlight(true, true, false, false, false, true) == Target::Flying, "in the air, not at him: flying");
        Expect(OnFlight(true, true, false, false, true, false) == Target::Back, "a miss, the drop off: back on the belt");
        Expect(OnFlight(true, true, true, false, true, false) == Target::Ground, "a miss, the drop on: the game drops it");
        Expect(OnFlight(true, false, true, true, true, true) == Target::Ground, "a mana potion at him: the drop, not his");
        Expect(OnFlight(false, true, true, true, true, true) == Target::Ground, "the merc throw off: the drop as before");
    }
    {   // the drop: asked at the first touch, done when the item leaves the belt, refused when it stays
        Drop d = Drop::Falling;
        d = DropNext(d, false, true, 0);
        Expect(d == Drop::Falling, "in the air: falling");
        d = DropNext(d, true, true, 0);
        Expect(d == Drop::Asked, "the first touch: the game is asked");
        Expect(DropNext(d, false, true, 1.0) == Drop::Asked, "the game thinking: still asked");
        Expect(DropNext(d, false, false, 0.2) == Drop::Done, "the item left the belt: done");
        Expect(DropNext(d, false, true, 2.0) == Drop::Refused, "still in the belt after 1.5 s: refused");
        Expect(DropNext(Drop::Falling, true, false, 0) == Drop::Gone, "gone from the belt before landing: nothing asked");
        Expect(DropEnded(Drop::Done) && DropEnded(Drop::Refused) && DropEnded(Drop::Gone) && !DropEnded(Drop::Asked) && !DropEnded(Drop::Falling), "which are the ends");
        Expect(DropNext(Drop::Done, true, true, 9) == Drop::Done, "an end stays an end");
    }
    {   // the drink: the item left - drunk; still there after 1.5 s - refused
        Expect(DrinkNext(Drink::Waiting, true, 0.5) == Drink::Waiting, "waiting for the game");
        Expect(DrinkNext(Drink::Waiting, false, 0.1) == Drink::Drunk, "the item left: drunk");
        Expect(DrinkNext(Drink::Waiting, true, 1.6) == Drink::Refused, "still there: refused");
        Expect(DrinkNext(Drink::Drunk, true, 9) == Drink::Drunk, "drunk stays drunk");
    }
    {   // the empties lying about
        Expect(!EmptyGoes(true, 5, 6, 20) && EmptyGoes(true, 20, 21, 20), "an empty at rest goes after keep seconds");
        Expect(!EmptyGoes(false, 0, 25, 20) && EmptyGoes(false, 0, 30, 20), "one never at rest goes 10 s later");
        const double at[6] = {5, 3, 9, 1.5, 7, 8};
        Expect(MakeRoom(at, 5) == -1, "room for a sixth");
        Expect(MakeRoom(at, 6) == 3, "no room: the oldest release goes");
    }

    {   // which meshes the emptied bottle loses: the cork and what is round the neck, the liquid, the bubbles - never the glass
        Expect(EmptyHides("solidShape", 1115) && EmptyHides("outerShape", 2020) && EmptyHides("metal_ropeShape", 1684) && EmptyHides("liquidShape", 286),
               "solid, outer, metal/rope, liquid go");
        Expect(!EmptyHides("glassShape", 1000) && !EmptyHides("polySurfaceShape3", 992) && !EmptyHides("polySurface1Shape", 958), "the glass stays");
        Expect(EmptyHides("polySurfaceShape47", 10) && !EmptyHides("polySurfaceShape4", 160), "a bubble goes, the base plate stays");
        Expect(!EmptyHides("healingpotion2Shape", 3208) && !EmptyHides(nullptr, 5), "one mesh for all: nothing goes; no name: nothing");
        Expect(!EmptyHides("GlassSolid", 400), "a name with glass in it stays");
    }
    {   // the hide on synthetic memory: a model instance with three meshes, the visible list [0 1 2]
        static uint8_t mem[0x4000];
        const uintptr_t B = 0x100000;
        auto at = [&](uintptr_t a) { return mem + (a - B); };
        auto read = [&](uintptr_t a, void* out, size_t n) { if (a < B || a + n > B + sizeof mem) return false; std::memcpy(out, at(a), n); return true; };
        int writes = 0;
        auto write = [&](uintptr_t a, const void* in, size_t n) { if (a < B || a + n > B + sizeof mem) return false; std::memcpy(at(a), in, n); ++writes; return true; };
        auto put = [&](uintptr_t a, auto v) { std::memcpy(at(a), &v, sizeof v); };
        const uintptr_t inst = B + 0x100, res = B + 0x1000, meshVec = B + 0x1200, visVec = B + 0x1300, names = B + 0x2000;
        put(inst + kInstResource, (uint64_t)res);
        put(res + kResourceState, (uint8_t)kLoaded);
        put(inst + kInstMeshes, (uint64_t)meshVec);
        put(inst + kInstMeshes + 8, (uint64_t)3);
        const char* nm[3] = {"solidShape", "glassShape", "liquidShape"};
        const uint32_t tri[3] = {1115, 1000, 286};
        for (int i = 0; i < 3; ++i) {
            const uintptr_t mi = B + 0x1400 + i * 0x20, mesh = B + 0x1800 + i * 0x200, np = names + i * 0x40;
            put(meshVec + i * 8, (uint64_t)mi);
            put(mi, (uint64_t)mesh);
            put(mesh + kMeshName, (uint64_t)np);
            put(mesh + kMeshTriangles, tri[i]);
            std::memcpy(at(np), nm[i], std::strlen(nm[i]) + 1);
        }
        auto visible = [&](std::initializer_list<uint64_t> v) {
            put(inst + kInstVisible, (uint64_t)visVec);
            put(inst + kInstVisible + 8, (uint64_t)v.size());
            int k = 0;
            for (uint64_t x : v) put(visVec + 8 * k++, x);
        };
        visible({0, 1, 2});
        Parts p;
        Expect(ReadParts(read, inst, &p), "the parts read", 0);
        Expect(p.meshes == 3 && p.hidden == 0b101 && p.visibleHidden == 2, "solid and liquid to go, both visible", (double)p.hidden);
        Expect(std::strstr(p.names, "solidShape(1115)-") && std::strstr(p.names, "glassShape(1000)") && !std::strstr(p.names, "glassShape(1000)-"), "the log names them", 0);
        Expect(HideParts(read, write, inst, p) == 1, "the list changed: the game's rebuild follows");
        uint64_t n = 0, first = 9;
        read(inst + kInstVisible + 8, &n, 8);
        read(visVec, &first, 8);
        Expect(n == 1 && first == 1, "only the glass is left visible", (double)n);
        ReadParts(read, inst, &p);
        Expect(p.visibleHidden == 0, "the hide holds");
        writes = 0;
        Expect(HideParts(read, write, inst, p) == 0 && writes == 0, "held: nothing written again");
        visible({0, 1, 2});   // the game filled the list again (a LOD change)
        ReadParts(read, inst, &p);
        Expect(p.visibleHidden == 2 && HideParts(read, write, inst, p) == 1, "the game undid it: done again");
        visible({0, 2});      // only hidden ones visible: never the whole bottle
        ReadParts(read, inst, &p);
        Expect(HideParts(read, write, inst, p) == 0, "all of it would go: nothing done");
        put(res + kResourceState, (uint8_t)2);
        Expect(!ReadParts(read, inst, &p) && !p.ready && std::strstr(p.why, "not loaded"), "a model still loading: not yet", 0);
        Expect(HideParts(read, write, inst, p) == 0, "not ready: nothing");
        Expect(!ReadParts(read, (uintptr_t)0x10, &p), "no instance: no parts");
    }

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
