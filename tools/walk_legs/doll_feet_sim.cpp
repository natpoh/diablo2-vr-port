// doll_feet_sim - runs vr/doll_feet.h (the plugin's own code, as skeletons.cpp DollFeetLegs drives it) through a case and writes the feet for
// walk_legs.py --doll to picture.
//   doll_feet_sim <case> <stanceRx> <stanceRz> <stanceLx> <stanceLz> <upm> <out.txt>
// cases: forward (walking ahead 1.5 m/s), back (facing ahead, going back 1.5 m/s), runback (5 m/s back), turn (standing, the body turned round 90 deg
// a second - the camera round him), reverse (1.5 s ahead, then back). out.txt per pose: "t bodyX bodyZ yaw velX velZ" then per foot (right, left)
// "x z lift yaw flying".
#define _CRT_SECURE_NO_WARNINGS   // (fopen: a tool's own file)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "doll_feet.h"

int main(int argc, char** argv) {
    if (argc < 8) { std::fprintf(stderr, "doll_feet_sim <case> <stanceRx> <stanceRz> <stanceLx> <stanceLz> <upm> <out.txt>\n"); return 2; }
    const std::string kase = argv[1];
    dollfeet::P2 stance[2] = {{(float)std::atof(argv[2]), (float)std::atof(argv[3])}, {(float)std::atof(argv[4]), (float)std::atof(argv[5])}};
    const float upm = (float)std::atof(argv[6]);
    FILE* out = std::fopen(argv[7], "w");
    if (!out) return 2;
    dollfeet::Doll doll;
    dollfeet::P2 body{};
    const float dt = 1.0f / 90.0f;
    for (int i = 0; i < 360; ++i) {
        const float t = i * dt;
        float yaw = 0.0f, speed = 0.0f, dir = 0.0f;   // dir: the travel's way (radians, as the yaw)
        if (kase == "forward") { speed = 1.5f; dir = 0.0f; }
        else if (kase == "back") { speed = 1.5f; dir = 3.14159265f; }
        else if (kase == "runback") { speed = 5.0f; dir = 3.14159265f; }
        else if (kase == "turn") { yaw = 1.5707963f * t; }
        else if (kase == "reverse") { speed = 1.5f; dir = t < 1.5f ? 0.0f : 3.14159265f; }
        const dollfeet::P2 vel = dollfeet::Mul(dollfeet::Ahead(dir), speed * upm);
        body = dollfeet::Add(body, dollfeet::Mul(vel, dt));
        doll.Step(dt, body, yaw, vel, stance, upm);
        std::fprintf(out, "%.4f %.4f %.4f %.4f %.4f %.4f", t, body.x, body.z, yaw, vel.x, vel.z);
        for (int side = 0; side < 2; ++side) {
            const dollfeet::Foot& f = doll.Get(side);
            std::fprintf(out, " %.4f %.4f %.4f %.4f %d", f.at.x, f.at.z, f.lift, f.yaw, f.flying ? 1 : 0);
        }
        std::fprintf(out, "\n");
    }
    std::printf("%s: %d steps, %d with both feet in the air\n", kase.c_str(), doll.Steps(), doll.Flights());
    std::fclose(out);
    return 0;
}
