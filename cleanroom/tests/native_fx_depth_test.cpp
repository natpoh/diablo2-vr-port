// native_fx_depth_test - vr/native_fx_depth.h: the scene's depth on the no-ReShade path.
//   1. the copy is made in flat too when the effect draws the sky, the fog or the ceiling (PC1 2026-10-29: flat with "Our effect (old)",
//      no copy, a black screen), in VR always, never with ReShade (its own DEPTH), not in flat with nothing placed by the depth;
//   2. the effect draws what needs the depth only on a copy made within kStalePresents presents - none yet, or long stale, and not;
//   3. the count of presents since the copy: back to 0 at a copy, up by one otherwise, held at its ceiling.
#include "native_fx_depth.h"

#include <cstdio>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

int main() {
    using namespace nativefxdepth;
    // 1. when the copy is made
    CHECK(CopyWanted(true, false, true));    // flat, no ReShade, our sky / fog: the PC1 case
    CHECK(CopyWanted(true, true, false));    // VR, no ReShade: the eye made moves with it
    CHECK(CopyWanted(true, true, true));
    CHECK(!CopyWanted(true, false, false));  // flat, the game draws its own sky and fog: nothing of ours needs it
    CHECK(!CopyWanted(false, true, true));   // ReShade: its own DEPTH
    CHECK(!CopyWanted(false, false, true));
    // 2. when the effect may draw by it
    CHECK(!Usable(false, 0));                // no copy ever made: the PC1 case - nothing of ours, not the void over all
    CHECK(Usable(true, 0));
    CHECK(Usable(true, kStalePresents));     // (a present or a few the buffer was not bound in)
    CHECK(!Usable(true, kStalePresents + 1));
    // 3. the count
    unsigned since = 5;
    since = NextSince(since, true);
    CHECK(since == 0);
    for (unsigned i = 0; i < kStalePresents + 1; ++i) since = NextSince(since, false);
    CHECK(since == kStalePresents + 1 && !Usable(true, since));
    since = 1000000u;
    CHECK(NextSince(since, false) == 1000000u);   // held, never round to fresh
    // 4. VR -> flat -> VR, present by present, as vrcam's present runs it (BindNative copies when CopyWanted and a pick is held; the
    // effect draws by the copy when Usable). PC1 2026-10-29 17:08 - 17:10 (0.188.3): VR picked the depth, back in flat the copy stopped
    // ([mode] platform=0) and the effect went on with the last VR frame's copy - the fog's edge never moved with the flat picture, the far
    // ruins black against the sky ("he doesn't redraw the border"). Now: every present in flat with our sky / fog is copied, and a copy
    // left behind is never drawn by.
    {
        struct Step { bool vr, depthEffects; int presents; };
        const Step steps[] = {{true, true, 20}, {false, true, 30}, {false, false, 30}, {false, true, 30}, {true, true, 20}, {false, true, 5}};
        unsigned s = 1000000u;   // (no copy yet)
        bool have = false;
        int stale = 0, staleOld = 0, drawn = 0;
        unsigned sOld = 1000000u;
        bool haveOld = false;
        for (const Step& st : steps)
            for (int p = 0; p < st.presents; ++p) {
                const bool copied = CopyWanted(true, st.vr, st.depthEffects);
                have = have || copied;
                s = NextSince(s, copied);
                // drawn by: the depth is this present's whenever the effect draws by it (never a frame left behind)
                if (st.depthEffects && Usable(have, s)) {
                    ++drawn;
                    if (s != 0) ++stale;
                }
                // the build before: copied in VR only
                const bool copiedOld = st.vr;
                haveOld = haveOld || copiedOld;
                sOld = NextSince(sOld, copiedOld);
                if (st.depthEffects && !st.vr && haveOld) ++staleOld;   // (it drew by whatever copy there was)
            }
        std::printf("  VR -> flat -> VR: %d presents drawn by the depth, %d of them by a copy not of that present (the build before: %d in flat "
                    "by the last VR frame's)\n", drawn, stale, staleOld);
        CHECK(drawn == 20 + 30 + 30 + 20 + 5 && stale == 0);
        CHECK(staleOld == 30 + 30 + 5);   // (what PC1 saw: every flat present with our sky / fog by the VR frame left behind)
        // after a stretch with nothing of ours placed by the depth (no copies), the first present that wants it again has its own copy
        CHECK(CopyWanted(true, false, true) && NextSince(31, true) == 0);
    }
    if (g_fail) { std::printf("native_fx_depth_test: %d FAILED\n", g_fail); return 1; }
    std::printf("native_fx_depth_test: the depth copied in flat for our sky / fog / ceiling, drawn by only when fresh: all passed\n");
    return 0;
}
