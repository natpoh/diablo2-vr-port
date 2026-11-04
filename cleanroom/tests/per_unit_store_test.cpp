// per_unit_store_test.cpp - vr/per_unit_store.h: each rig its own memory, kept while it comes, let go when it does not; and the 0.201.0 crash's shape - two
// rigs taking turns on two threads, each growing and clearing vectors in its memory - with no memory torn (each thread finds only its own marks).
#include "../../vr/per_unit_store.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
int g_fail = 0;
#define CHECK(c)                                                                         \
    do {                                                                                 \
        if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } \
    } while (0)

struct Mem {   // as WalkBackMem: vectors that grow and are let go
    uintptr_t self = 0;
    std::vector<int> pose;
    std::vector<std::vector<float>> samples;
};
}  // namespace

int main() {
    {   // its own memory per rig, kept between calls
        PerUnitStore<Mem> st;
        bool fresh = false;
        st.With(1, 0, [&](Mem& m, bool f) { fresh = f; m.self = 1; m.pose.assign(5, 1); });
        CHECK(fresh);
        st.With(2, 10, [&](Mem& m, bool f) { fresh = f; m.self = 2; m.pose.assign(3, 2); });
        CHECK(fresh);
        st.With(1, 20, [&](Mem& m, bool f) { fresh = f; CHECK(m.self == 1 && m.pose.size() == 5); });
        CHECK(!fresh);   // (rig 2 between did not reset rig 1's - the old global did)
        CHECK(st.Size() == 2);
        // a rig not seen for idleMs goes; the one calling stays
        st.With(1, 5000, [&](Mem&, bool) {});
        CHECK(st.Size() == 1);
        st.With(2, 5010, [&](Mem& m, bool f) { fresh = f; CHECK(m.pose.empty()); });
        CHECK(fresh);
    }
    {   // past cap the longest unseen goes
        PerUnitStore<Mem> st(3, 100000);
        for (uintptr_t k = 1; k <= 3; ++k) st.With(k, k, [](Mem&, bool) {});
        st.With(4, 10, [](Mem&, bool) {});
        CHECK(st.Size() == 3);
        bool fresh = false;
        st.With(1, 11, [&](Mem&, bool f) { fresh = f; });
        CHECK(fresh);   // (1 was the oldest: let go for 4)
    }
    {   // two rigs on two threads taking turns, as the game's job threads pose them: each grows and lets go of vectors in its memory
        PerUnitStore<Mem> st(8, 100);   // (the clock read before the lock: a later stamp from another thread is not taken for ages idle)
        std::atomic<int> torn{0};
        std::atomic<uint64_t> clock{0};
        auto work = [&](uintptr_t self) {
            for (int i = 0; i < 20000; ++i)
                st.With(self, clock.fetch_add(1), [&](Mem& m, bool fresh) {
                    if (!fresh && m.self != self) torn.fetch_add(1);
                    m.self = self;
                    m.pose.assign(64 + i % 64, (int)self);
                    m.samples.emplace_back(32, (float)self);
                    if (m.samples.size() > 40) m.samples.clear();
                    for (int v : m.pose)
                        if (v != (int)self) { torn.fetch_add(1); break; }
                });
        };
        std::thread a(work, (uintptr_t)0x1000), b(work, (uintptr_t)0x2000), c(work, (uintptr_t)0x1000);
        a.join();
        b.join();
        c.join();
        CHECK(torn.load() == 0);
        CHECK(st.Size() >= 1 && st.Size() <= 2);   // (0x2000 finishes first: let go once idle, rightly)
    }
    {   // a time read before the lock, older than a stamp another thread made since: not taken for ages idle (unsigned, it read as ~2^64 ms)
        PerUnitStore<Mem> st(8, 100);
        st.With(1, 1000, [](Mem& m, bool) { m.self = 1; });
        bool fresh = true;
        st.With(2, 990, [](Mem&, bool) {});
        st.With(1, 995, [&](Mem& m, bool f) { fresh = f; CHECK(m.self == 1); });
        CHECK(!fresh && st.Size() == 2);
    }
    std::printf(g_fail ? "per_unit_store_test: %d failed\n" : "per_unit_store_test: per rig, kept, let go, two threads by turns: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
