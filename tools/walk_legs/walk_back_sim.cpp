// walk_back_sim - runs vr/walk_back.h's Reverser (the plugin's own code, as skeletons.cpp WalkBackLegs drives it) on a stream of the game's lower-body
// poses written by walk_legs.py --stream, and writes what it draws, for walk_legs.py --played to picture.
//   walk_back_sim <stream.txt> <played.txt> [turnout] [share0188|share]
// stream.txt: "bones footA footB" then per pose "t walking active dt animYaw mode m[16] x bones" (t seconds; walking 0/1 = what Record is told; active
// 0/1 = the walk back on; dt = the time since the last call, as WalkBackLegs passes it; animYaw = the game's own turn on the anim bone, radians, already
// in the matrices; mode = the game's unit mode as read). With "turnout" each pose has that turn taken out first (walk_back.h TurnAbout, as WalkBackLegs
// does since the 2026-10-28 17:03 log); without, as before it.
// share0188 / share: the walk back's share worked out as WalkBackLegs does - the cone (remotepose::ConeBlend, 100 deg; the travel ahead while "active" is
// 0, behind while 1, he always goes) and the game's mode - as 0.188.5 had it (a standing mode drops the cone at once) or as now (walkback::ModeGate, and on
// wholly at once on the game's own pose); the pose drawn is then the game's blended with the play-back by it (walkback::Blend), and Record is told walking
// by the mode. Without either: the play-back alone while active.
// played.txt: per call "t jumps share m[16] x bones" (the pose drawn; jumps -2 = the walk back off: the game's own pose).
#define _CRT_SECURE_NO_WARNINGS   // (fopen / fscanf: a tool's own files)
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "remote_pose.h"
#include "walk_back.h"

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "walk_back_sim <stream.txt> <played.txt> [turnout] [share0188|share]\n"); return 2; }
    bool turnOut = false, share = false, share0188 = false;
    for (int a = 3; a < argc; ++a) {
        const std::string s = argv[a];
        turnOut |= s == "turnout";
        share0188 |= s == "share0188";
        share |= s == "share";
    }
    FILE* in = std::fopen(argv[1], "r");
    FILE* out = std::fopen(argv[2], "w");
    if (!in || !out) { std::fprintf(stderr, "cannot open\n"); return 2; }
    int bones = 0, fa = 0, fb = 0;
    if (std::fscanf(in, "%d %d %d", &bones, &fa, &fb) != 3 || bones <= 0) return 2;
    walkback::Reverser rev;
    remotepose::ConeBlend cone;
    walkback::ModeGate gate;
    std::vector<walkback::M4> pose(bones), drawn(bones);
    int calls = 0, fromStore = 0, jumps = 0, whole = 0, mixed = 0;
    for (;;) {
        double t = 0.0, dt = 0.0;
        float animYaw = 0.0f;
        int walking = 0, active = 0, mode = -1;
        if (std::fscanf(in, "%lf %d %d %lf %f %d", &t, &walking, &active, &dt, &animYaw, &mode) != 6) break;
        for (int b = 0; b < bones; ++b) {
            for (int k = 0; k < 16; ++k) std::fscanf(in, "%f", &pose[b].m[k]);
            if (turnOut && std::fabs(animYaw) > 0.035f) walkback::TurnAbout(pose[b], -animYaw, 0.0f, 0.0f);
        }
        float feet[6];
        for (int j = 0; j < 3; ++j) { feet[j] = pose[fa].m[12 + j]; feet[3 + j] = pose[fb].m[12 + j]; }
        float w = active ? 1.0f : 0.0f;
        bool record = walking != 0;
        const float wasW = cone.share;
        if (share || share0188) {   // WalkBackLegs's lines, the travel and speed given
            const bool stands = share ? gate.Stands(mode, (float)dt) : walkback::GameStands(mode);
            const bool gameWalk = share ? !stands : walkback::GameWalkMode(mode);
            if (stands) cone.still = remotepose::ConeBlend::kHoldStandS + 1.0f;
            w = cone.Step(active ? 178.0f : 0.0f, gameWalk, 100.0f, (float)dt);
            record = gameWalk;
        }
        rev.Record(t, record, pose.data(), bones, feet);
        if (share && wasW <= 0.001f && w > 0.001f && rev.Kept() > 0 && rev.At(rev.Kept() - 1).t == t) { cone.share = 1.0f; w = 1.0f; ++whole; }
        const walkback::Sample* s = rev.Step(dt, w > 0.001f);
        const int j = rev.TakeJumps();
        jumps += j;
        ++calls;
        for (int b = 0; b < bones; ++b)
            drawn[b] = s && (int)s->pose.size() == bones && w > 0.001f ? walkback::Blend(pose[b], s->pose[b], w) : pose[b];
        if (w > 0.001f && w < 0.999f) ++mixed;
        std::fprintf(out, "%.6f %d %.3f", t, w <= 0.001f ? -2 : s ? j : -1, w);   // -2: the walk back off - the game's pose is drawn (after the turn-out, if on)
        for (int b = 0; b < bones; ++b)
            for (int k = 0; k < 16; ++k) std::fprintf(out, " %.5f", drawn[b].m[k]);
        std::fprintf(out, "\n");
        if (s) ++fromStore;
    }
    std::printf("%d calls, %d drawn from the store, %d jumps, %d crossfaded (share between 0 and 1), %d came on wholly at once, %.2f s kept at the end\n",
                calls, fromStore, jumps, mixed, whole, rev.KeptS());
    std::fclose(in);
    std::fclose(out);
    return 0;
}
