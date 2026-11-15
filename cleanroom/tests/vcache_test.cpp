// vcache_test - vr/vcache.h Pick(): the one L3 larger than every other, nothing for alike caches; Ranges() for the log.
// Then this machine's own caches, read and judged (printed only: any machine passes).
#include "vcache.h"

#include <cstdio>
#include <initializer_list>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main() {
    using vcache::L3;
    const uint64_t MB = 1ull << 20;
    // 7950X3D: CCD0 96 MB (0-15), CCD1 32 MB (16-31)
    CHECK(vcache::Pick({{96 * MB, 0, 0x0000FFFFull}, {32 * MB, 0, 0xFFFF0000ull}}) == 0);
    // the V-Cache CCD listed second
    CHECK(vcache::Pick({{32 * MB, 0, 0x0000FFFFull}, {96 * MB, 0, 0xFFFF0000ull}}) == 1);
    // 7950X: two alike CCDs; one CCD (7800X3D, Intel): one L3
    CHECK(vcache::Pick({{32 * MB, 0, 0x0000FFFFull}, {32 * MB, 0, 0xFFFF0000ull}}) == -1);
    CHECK(vcache::Pick({{96 * MB, 0, 0xFFFFull}}) == -1);
    CHECK(vcache::Pick({}) == -1);
    // What Apply does (2026-11-15: the process's default sets crowded the game's spinning job workers onto the CCD - 2-9 s stalls):
    // by default (vcache_how 0, or anything not 1 - 3) only the draw thread's own sets, never the process's, never a priority - on,
    // off, with or without a big L3
    for (int how : {0, -1, 4, 99})
        for (bool want : {false, true})
            for (int big : {-1, 0, 1}) {
                const vcache::Plan p = vcache::PlanFor(want, big, how);
                CHECK(!p.processDefault);
                CHECK(!p.raisePriority);
                CHECK(p.drawThreadSets == (want && big >= 0));
            }
    CHECK(!vcache::PlanFor(true, 0).processDefault && vcache::PlanFor(true, 0).drawThreadSets);   // (the default argument is 0)
    // the live test's parts, each alone: 1 the process's sets, 2 the priority, 3 both (as 0.217) - never the draw thread's own sets
    {
        const vcache::Plan s = vcache::PlanFor(true, 0, 1), r = vcache::PlanFor(true, 0, 2), b = vcache::PlanFor(true, 0, 3);
        CHECK(s.processDefault && !s.raisePriority && !s.drawThreadSets);
        CHECK(!r.processDefault && r.raisePriority && !r.drawThreadSets);
        CHECK(b.processDefault && b.raisePriority && !b.drawThreadSets);
    }
    // off, or no big L3: nothing at all, whatever the how
    for (int how : {1, 2, 3}) {
        const vcache::Plan off = vcache::PlanFor(false, 0, how), none = vcache::PlanFor(true, -1, how);
        CHECK(!off.processDefault && !off.raisePriority && !off.drawThreadSets);
        CHECK(!none.processDefault && !none.raisePriority && !none.drawThreadSets);
    }
    // three caches, two big alike: no single big one
    CHECK(vcache::Pick({{96 * MB, 0, 0xFFull}, {96 * MB, 0, 0xFF00ull}, {32 * MB, 0, 0xFF0000ull}}) == -1);
    // an empty mask is no answer
    CHECK(vcache::Pick({{96 * MB, 0, 0}, {32 * MB, 0, 0xFF00ull}}) == -1);
    CHECK(vcache::Ranges(0x0000FFFFull) == "0-15");
    CHECK(vcache::Ranges(0xFFFF0000ull) == "16-31");
    CHECK(vcache::Ranges(0x5ull) == "0,2");
    CHECK(vcache::Ranges(0) == "");

    const std::vector<L3> here = vcache::Detect();
    const int big = vcache::Pick(here);
    std::printf("this machine: %zu L3 cache(s)", here.size());
    for (const L3& c : here) std::printf(" [%llu MB on %s]", (unsigned long long)(c.bytes / MB), vcache::Ranges(c.mask).c_str());
    std::printf(" -> %s\n", big < 0 ? "nothing to prefer" : ("prefer " + vcache::Ranges(here[big].mask)).c_str());

    if (g_fail) { std::printf("vcache_test: %d FAILED\n", g_fail); return 1; }
    std::printf("vcache_test: the big L3 picked only when it is the one largest; ranges for the log: all passed\n");
    return 0;
}
