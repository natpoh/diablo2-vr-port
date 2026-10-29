// Offline checks of the bone guard (vr/bone_guard.h): a weapon's bone matrix that came out shrunk - the game hides a bone by shrinking it, and a grip taken then flattened the staff -
// is brought back to the scale it should have, along its own axes, with the handedness it had; a row with no direction left is made from the other two; a healthy matrix is not
// touched; what cannot be mended is left alone. Pure maths.
// Run: build/Release/bone_guard_test.exe   (exit code 0 = all passed)

#include "../../vr/bone_guard.h"

#include <cmath>
#include <cstdio>
#include <array>
#include <cstring>
#include <random>
#include <vector>

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}

// A rotation about the unit axis (ax, ay, az) by `a`, as three rows, scaled by s per row, mirrored (the first row turned over) when `mirror`, placed at (px, py, pz).
static void Make(float m[16], float ax, float ay, float az, float a, float s, bool mirror, float px = 1.0f, float py = 2.0f, float pz = 3.0f) {
    const float l = std::sqrt(ax * ax + ay * ay + az * az);
    ax /= l; ay /= l; az /= l;
    const float c = std::cos(a), sn = std::sin(a), t = 1.0f - c;
    const float r[3][3] = {{c + ax * ax * t, ay * ax * t + az * sn, az * ax * t - ay * sn},
                           {ax * ay * t - az * sn, c + ay * ay * t, az * ay * t + ax * sn},
                           {ax * az * t + ay * sn, ay * az * t - ax * sn, c + az * az * t}};
    std::memset(m, 0, 16 * sizeof(float));
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m[i * 4 + j] = r[i][j] * s * (mirror && i == 0 ? -1.0f : 1.0f);
    m[12] = px; m[13] = py; m[14] = pz; m[15] = 1.0f;
}
static void ScaleRow(float m[16], int row, float k) {
    for (int c = 0; c < 3; ++c) m[row * 4 + c] *= k;
}

int main() {
    using namespace boneguard;
    // ---- Proper ----
    {
        float m[16];
        Make(m, 1, 2, 3, 0.7f, 1.0f, false);
        Expect(Proper(m, 1.0, 1), "a rotation is proper");
        Expect(!Proper(m, 1.0, -1), "... and not a mirror");
        Make(m, 1, 2, 3, 0.7f, 1.3f, true);
        Expect(Proper(m, 1.3, -1) && !Proper(m, 1.0, -1), "a scaled mirror is proper at its scale");
        Make(m, 1, 2, 3, 0.7f, 1.0f, false);
        ScaleRow(m, 1, 0.001f);
        std::string why;
        Expect(!Proper(m, 1.0, 1, &why) && !why.empty(), "a squashed axis is not proper, and says so");
        Make(m, 1, 2, 3, 0.7f, 1.0f, false);
        m[5] = NAN;
        Expect(!Proper(m, 1.0, 1), "a NaN is not proper");
    }
    // ---- a healthy matrix is left exactly as it is ----
    {
        float m[16], was[16];
        Make(m, 0.3f, 0.5f, 0.8f, 2.1f, 0.9f, true);
        std::memcpy(was, m, sizeof m);
        Expect(Restore(m, 0.9) == Fault::None && std::memcmp(m, was, sizeof m) == 0, "a healthy matrix is untouched");
        Expect(Restore(m, 0.0) == Fault::None && std::memcmp(m, was, sizeof m) == 0, "no scale wanted: untouched");
    }
    // ---- shrunk: the game's hidden bone ----
    for (const float shrink : {1e-1f, 1e-3f, 1e-5f, 1e-7f})
        for (const bool mirror : {false, true}) {
            float m[16], ref[16];
            Make(ref, 0.2f, 0.9f, -0.4f, 1.3f, 1.0f, mirror, 4.0f, 5.0f, 6.0f);
            Make(m, 0.2f, 0.9f, -0.4f, 1.3f, shrink, mirror, 4.0f, 5.0f, 6.0f);
            const Fault f = Restore(m, 1.0);
            Expect(f == Fault::Shrunk, "a shrunk bone is told shrunk", (double)shrink);
            Expect(Proper(m, 1.0, mirror ? -1 : 1), "... and brought back to a proper matrix of the scale wanted", (double)shrink);
            for (int i = 0; i < 12; ++i)
                if (i % 4 != 3) Expect(std::fabs(m[i] - ref[i]) < 1e-3f, "... along its own axes", (double)shrink);
            Expect(m[12] == 4.0f && m[13] == 5.0f && m[14] == 6.0f, "... and in its place");
        }
    // ---- a hand's scale in it: wanted 1.3 ----
    {
        float m[16];
        Make(m, 1, 1, 1, 0.5f, 0.0004f, false);
        Expect(Restore(m, 1.3) == Fault::Shrunk && Proper(m, 1.3, 1), "brought back to the scale it should have, not to 1");
    }
    // ---- one axis shrunk, the others not (a flattened mesh: scale 0 along one) ----
    for (int row = 0; row < 3; ++row)
        for (const bool mirror : {false, true}) {
            float m[16];
            Make(m, 0.4f, -0.2f, 0.7f, 2.4f, 1.0f, mirror);
            ScaleRow(m, row, 0.0005f);
            Expect(Restore(m, 1.0) == Fault::Shrunk && Proper(m, 1.0, mirror ? -1 : 1), "one flattened axis is brought back", row);
        }
    // ---- a row with no direction left: made from the other two (either handedness) ----
    for (int row = 0; row < 3; ++row)
        for (const bool mirror : {false, true}) {
            float m[16], ref[16];
            Make(ref, 0.4f, -0.2f, 0.7f, 2.4f, 1.0f, mirror);
            Make(m, 0.4f, -0.2f, 0.7f, 2.4f, 1.0f, mirror);
            for (int c = 0; c < 3; ++c) m[row * 4 + c] = 0.0f;
            const Fault f = Restore(m, 1.0, mirror ? -1 : 1);   // (a matrix with a row gone has no handedness of its own: the caller says)
            Expect(f == Fault::Degenerate && Proper(m, 1.0, mirror ? -1 : 1), "a row with no direction is rebuilt", row);
            for (int i = 0; i < 12; ++i)
                if (i % 4 != 3) Expect(std::fabs(m[i] - ref[i]) < 1e-3f, "... as it was", row);
        }
    // ---- what cannot be mended is left alone ----
    {
        float m[16], was[16];
        Make(m, 0.4f, -0.2f, 0.7f, 2.4f, 1.0f, false);
        for (int c = 0; c < 3; ++c) { m[4 + c] = 0.0f; m[8 + c] = 0.0f; }
        std::memcpy(was, m, sizeof m);
        Expect(Restore(m, 1.0) == Fault::Degenerate && std::memcmp(m, was, sizeof m) == 0, "two rows gone: nothing to rebuild from, untouched");
        Make(m, 0.4f, -0.2f, 0.7f, 2.4f, 1.0f, false);
        for (int c = 0; c < 3; ++c) { m[4 + c] = m[c] * 0.001f; m[8 + c] = 0.0f; }   // two rows parallel, the third gone
        std::memcpy(was, m, sizeof m);
        Expect(Restore(m, 1.0) == Fault::Degenerate && std::memcmp(m, was, sizeof m) == 0, "two parallel rows and none else: untouched");
        Make(m, 1, 0, 0, 0.5f, 1.0f, false);
        m[1] = NAN;
        std::memcpy(was, m, sizeof m);
        Expect(Restore(m, 1.0) == Fault::NotFinite && std::memcmp(m, was, sizeof m) == 0, "a NaN: untouched");
    }
    // ---- a sweep: every rotation of the bone (the whole sphere of the Y axis, its roll), every shrink pattern ----
    {
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> u(-1.0f, 1.0f);
        int fixed = 0, total = 0;
        // axes and the edges between them first (straight up and down, every horizontal), then random
        std::vector<std::array<float, 3>> axes = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {1, 0, 1}, {0, 1, 1}, {1, -1, 0}, {-1, 0, 1}, {0, 1, -1}};
        for (int i = 0; i < 400; ++i) axes.push_back({u(rng), u(rng), u(rng) + 0.01f});
        for (const auto& ax : axes)
            for (const float a : {0.0f, 1.0f, 3.14159265f, -2.2f})
                for (const bool mirror : {false, true})
                    for (int pattern = 0; pattern < 4; ++pattern) {
                        float m[16];
                        Make(m, ax[0], ax[1], ax[2], a, 1.0f, mirror);
                        if (pattern == 0) for (int r = 0; r < 3; ++r) ScaleRow(m, r, 0.001f);   // all shrunk
                        if (pattern == 1) ScaleRow(m, 1, 0.0f);                                   // one row gone
                        if (pattern == 2) ScaleRow(m, 2, 1e-4f);                                  // one row nearly
                        if (pattern == 3) { ScaleRow(m, 0, 1e-3f); ScaleRow(m, 1, 1e-5f); }       // two nearly
                        ++total;
                        const Fault f = Restore(m, 1.0, mirror ? -1 : 1);
                        if (f != Fault::Degenerate && f != Fault::Shrunk) continue;
                        ++fixed;
                        if (!Proper(m, 1.0, mirror ? -1 : 1)) Expect(false, "a mended matrix is proper (sweep)", pattern);
                    }
        Expect(fixed == total, "every matrix of the sweep was mended", (double)(total - fixed));
    }
    if (g_failures == 0) std::printf("bone_guard_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
