// walk_back_sim - runs vr/walk_back.h's Reverser (the plugin's own code, as skeletons.cpp WalkBackLegs drives it) on a stream of the game's lower-body
// poses written by walk_legs.py --stream, and writes what it draws, for walk_legs.py --played to picture.
//   walk_back_sim <stream.txt> <played.txt> [turnout]
// stream.txt: "bones footA footB" then per pose "t walking active dt animYaw m[16] x bones" (t seconds; walking 0/1 = what Record is told; active 0/1 =
// the walk back on; dt = the time since the last call, as WalkBackLegs passes it; animYaw = the game's own turn on the anim bone, radians, already in the
// matrices). With "turnout" each pose has that turn taken out first (walk_back.h TurnAbout, as WalkBackLegs does since the 17:03 log); without, as
// before it. played.txt: per call "t jumps m[16] x bones" (the pose drawn; jumps -2 = the walk back off: the game's own pose).
#define _CRT_SECURE_NO_WARNINGS   // (fopen / fscanf: a tool's own files)
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "walk_back.h"

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "walk_back_sim <stream.txt> <played.txt> [turnout]\n"); return 2; }
    const bool turnOut = argc > 3 && std::string(argv[3]) == "turnout";
    FILE* in = std::fopen(argv[1], "r");
    FILE* out = std::fopen(argv[2], "w");
    if (!in || !out) { std::fprintf(stderr, "cannot open\n"); return 2; }
    int bones = 0, fa = 0, fb = 0;
    if (std::fscanf(in, "%d %d %d", &bones, &fa, &fb) != 3 || bones <= 0) return 2;
    walkback::Reverser rev;
    std::vector<walkback::M4> pose(bones);
    int calls = 0, drawn = 0, jumps = 0;
    for (;;) {
        double t = 0.0, dt = 0.0;
        float animYaw = 0.0f;
        int walking = 0, active = 0;
        if (std::fscanf(in, "%lf %d %d %lf %f", &t, &walking, &active, &dt, &animYaw) != 5) break;
        for (int b = 0; b < bones; ++b) {
            for (int k = 0; k < 16; ++k) std::fscanf(in, "%f", &pose[b].m[k]);
            if (turnOut && std::fabs(animYaw) > 0.035f) walkback::TurnAbout(pose[b], -animYaw, 0.0f, 0.0f);
        }
        float feet[6];
        for (int j = 0; j < 3; ++j) { feet[j] = pose[fa].m[12 + j]; feet[3 + j] = pose[fb].m[12 + j]; }
        rev.Record(t, walking != 0, pose.data(), bones, feet);
        const walkback::Sample* s = rev.Step(dt, active != 0);
        const int j = rev.TakeJumps();
        jumps += j;
        ++calls;
        std::fprintf(out, "%.6f %d", t, !active ? -2 : s ? j : -1);   // -2: the walk back off - the game's pose is drawn (after the turn-out, if on)
        for (int b = 0; b < bones; ++b)
            for (int k = 0; k < 16; ++k) std::fprintf(out, " %.5f", s && (int)s->pose.size() == bones ? s->pose[b].m[k] : pose[b].m[k]);
        std::fprintf(out, "\n");
        if (s) ++drawn;
    }
    std::printf("%d calls, %d drawn from the store, %d jumps, %.2f s kept at the end\n", calls, drawn, jumps, rev.KeptS());
    std::fclose(in);
    std::fclose(out);
    return 0;
}
