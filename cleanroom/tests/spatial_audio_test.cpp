// [audio] head_follow: a sound's screen offset as a direction on FMOD's ground plane (vr/spatial_audio.h GroundPos).
#include <cmath>
#include <cstdio>

#include "spatial_audio.h"

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}
static bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
    using spatialaudio::GroundPos;
    // straight right on the screen: right on the ground, the same distance
    auto p = GroundPos(3.0f, 0.0f, 0.0f, -1.0f, 2.0f);
    Check(Near(p.x, 3.0f) && Near(p.y, 0.0f) && Near(p.z, 0.0f), "right stays right");
    // up the screen (y < 0) is ahead with forward_sign -1
    p = GroundPos(0.0f, -2.0f, 0.0f, -1.0f, 2.0f);
    Check(Near(p.x, 0.0f) && Near(p.z, 2.0f), "up the screen is ahead, at its distance");
    // forward_sign 1 turns that round
    p = GroundPos(0.0f, -2.0f, 0.0f, 1.0f, 2.0f);
    Check(Near(p.z, -2.0f), "forward_sign 1: up the screen is behind");
    // the distance is kept, the direction takes the depth scale: (1, -1) -> right 1, ahead 2, length sqrt(2)
    p = GroundPos(1.0f, -1.0f, 0.0f, -1.0f, 2.0f);
    const float d = std::sqrt(2.0f), n = std::sqrt(5.0f);
    Check(Near(p.x, d / n) && Near(p.z, 2.0f * d / n) && Near(std::sqrt(p.x * p.x + p.z * p.z), d), "distance kept, the squash undone");
    // at the hero, and broken input: as it was
    p = GroundPos(0.0f, 0.0f, 0.0f, -1.0f, 2.0f);
    Check(p.x == 0.0f && p.y == 0.0f && p.z == 0.0f, "at the hero: unchanged");
    p = GroundPos(NAN, 1.0f, 0.0f, -1.0f, 2.0f);
    Check(std::isnan(p.x) && p.y == 1.0f, "NaN: unchanged");
    // only z (no ground part): unchanged
    p = GroundPos(0.0f, 0.0f, 5.0f, -1.0f, 2.0f);
    Check(p.z == 5.0f, "no ground part: unchanged");
    std::printf(g_fail ? "spatial_audio_test: %d FAILED\n" : "spatial_audio_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
