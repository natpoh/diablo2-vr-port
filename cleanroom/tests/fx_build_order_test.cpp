// fx_build_order_test - vr/fx_build_order.h: the native runtime builds the size in use first, the sizes built before after it.
//   PC1 2026-10-29 17:50 (0.188.4, a new build's first start): three earlier sizes warmed first (35 s), the back buffer's size after them -
//   a minute of no sky. Run as the runtime's Begin runs it, one present after another, a build taking a few presents.
#include "fx_build_order.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main() {
    using namespace fxbuildorder;
    // the presents: three sizes built before, the size in use not built; each build takes 3 presents; the in-use one may fail
    for (int failInUse = 0; failInUse < 2; ++failInUse) {
        int warmLeft = 3, busyFor = 0;
        bool inUseBuilt = false, inUseTried = false, running = false;
        Job runningJob = Job::None;
        std::vector<std::string> order;
        for (int present = 0; present < 40; ++present) {
            if (running && --busyFor == 0) {   // a build done
                running = false;
                if (runningJob == Job::InUse && !failInUse) inUseBuilt = true;
            }
            const Job j = Next(running, !inUseBuilt, inUseTried, warmLeft > 0);
            if (j == Job::InUse) { inUseTried = true; order.push_back("in use"); }
            if (j == Job::Warm) { --warmLeft; order.push_back("warm"); }
            if (j != Job::None) { running = true; busyFor = 3; runningJob = j; }
        }
        std::string line;
        for (const std::string& s : order) line += (line.empty() ? "" : ", ") + s;
        std::printf("  %s: %s\n", failInUse ? "the in-use build failing" : "the in-use build", line.c_str());
        CHECK(!order.empty() && order[0] == "in use");   // the game's own size first
        CHECK(order.size() == 4 && warmLeft == 0);      // then every size built before, once; the in-use one not again after a failure
        CHECK(inUseBuilt == !failInUse);
    }
    // while a build runs nothing else starts; nothing at all when all is done
    CHECK(Next(true, true, false, true) == Job::None);
    CHECK(Next(false, false, false, false) == Job::None);
    CHECK(Next(false, true, true, false) == Job::None);   // tried (failed), nothing to warm: no retry every present
    if (g_fail) { std::printf("fx_build_order_test: %d FAILED\n", g_fail); return 1; }
    std::printf("fx_build_order_test: the size in use built first, the sizes built before warmed after it: all passed\n");
    return 0;
}
