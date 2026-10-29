// [weight] knock_props: the props world's loader (vr/props_lib.h) against the weight library itself (a d2r_vr_weight.dll built from the private repository) - or, without
// one, only that a missing file does nothing. Run: props_lib_test.exe [<path to d2r_vr_weight.dll>]
// Checks: no file - not loaded, every call does nothing; the library - loaded, a world made; a cup on a table, a blade swept through it at 3 m/s: knocked once, it falls
// off the table, rests and freezes; a world freed.

#include "props_lib.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

int g_fails = 0;
std::string g_log;
#define EXPECT(c, ...)                                         \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

void Log(const char* s) { g_log += s; g_log += "\n"; }

}  // namespace

int wmain(int argc, wchar_t** argv) {
    propslib::SetLog(&Log);
    propslib::SetPath(L"C:\\no\\such\\d2r_vr_weight.dll");
    EXPECT(!propslib::Ensure() && !propslib::Loaded(), "no file: not loaded");
    EXPECT(propslib::Create(nullptr) == nullptr && propslib::Step(nullptr, 0, 0.01f) < 0, "no library: every call does nothing");
    EXPECT(g_log.find("there is no d2r_vr_weight.dll") != std::string::npos, "the log says so");
    if (argc < 2) {
        std::printf(g_fails ? "props_lib_test: %d check(s) failed\n" : "props_lib_test: all checks passed (no library given: only the missing file)\n", g_fails);
        return g_fails ? 1 : 0;
    }
    propslib::SetPath(argv[1]);
    Sleep(2100);   // (it looks again at most every 2 s)
    EXPECT(propslib::Ensure() && propslib::Loaded(), "the library is loaded (%s)", g_log.c_str());
    void* w = propslib::Create(nullptr);
    EXPECT(w != nullptr, "a world");
    if (w) {
        propslib::Clear(w, 0.0f);
        D2rpBox table{};
        table.lo[0] = 0; table.lo[1] = 0.78f; table.lo[2] = -0.5f;
        table.hi[0] = 1; table.hi[1] = 0.8f; table.hi[2] = 0.5f;
        table.friction = 0.6f;
        table.bounce = 0.1f;
        propslib::SetStatics(w, &table, 1);
        D2rpProp cup{};
        cup.id = 42;
        cup.shape = D2RP_SHAPE_ROUND;
        cup.material = D2RP_MAT_CERAMIC;
        cup.p[0] = 0.85f; cup.p[1] = 0.8f;
        cup.q[3] = 1;
        cup.centre[1] = 0.05f;
        cup.half[0] = 0.04f; cup.half[1] = 0.05f; cup.half[2] = 0.04f;
        EXPECT(propslib::SetProps(w, &cup, 1) == 1, "the cup in the world");
        const float dt = 1.0f / 120.0f;
        double t = 0;
        int knocks = 0;
        bool rested = false;
        float last[3] = {0.85f, 0.8f, 0};
        for (int i = 0; i < 1400 && !rested; ++i) {
            D2rpMover m{};
            if (i < 30) {
                m.on = 1;
                m.hands = 1;
                m.a[0] = 0.55f + 3.0f * (float)t; m.a[1] = 0.86f; m.a[2] = -0.45f;
                m.b[0] = m.a[0]; m.b[1] = 0.86f; m.b[2] = 0.45f;
                m.radius = 0.03f;
                m.mass = 3.5f;
                m.vel[0] = 3.0f;
                m.time = t;
            }
            propslib::SetMovers(w, &m, 1);
            propslib::Step(w, t, dt);
            t += dt;
            D2rpKnock k[4];
            knocks += propslib::Knocks(w, k, 4);
            D2rpPose p[4];
            const int n = propslib::Poses(w, p, 4);
            for (int j = 0; j < n; ++j)
                if (p[j].id == 42) { std::memcpy(last, p[j].p, sizeof last); rested = p[j].state == 2; }
        }
        EXPECT(knocks == 1, "knocked once (%d)", knocks);
        EXPECT(rested, "it came to rest");
        EXPECT(last[0] > 1.0f && last[1] < 0.2f, "off the table onto the floor (%.2f, %.2f)", last[0], last[1]);
        propslib::Destroy(w);
    }
    // [weight] grab_props: the hands (the optional D2rpSetHands / D2rpHandAt): a palm over a cup, the grip, the cup in the hand
    EXPECT(propslib::HasHands(), "the library has the hands");
    if (void* g = propslib::HasHands() ? propslib::Create(nullptr) : nullptr) {
        propslib::Clear(g, 0.0f);
        D2rpProp cup{};
        cup.id = 7;
        cup.shape = D2RP_SHAPE_ROUND;
        cup.material = D2RP_MAT_CERAMIC;
        cup.q[3] = 1;
        cup.centre[1] = 0.05f;
        cup.half[0] = 0.04f; cup.half[1] = 0.05f; cup.half[2] = 0.04f;
        propslib::SetProps(g, &cup, 1);
        D2rpHand h{};
        h.tracked = 1;
        h.mayTake = 1;
        h.pos[1] = 0.15f;
        h.rot[3] = 1;
        D2rpHandOut o{};
        int events = 0;
        double t = 0;
        for (int i = 0; i < 40; ++i) {
            h.grip = i < 4 ? 0.0f : 1.0f;
            h.time = t;
            propslib::SetHands(g, &h, 1);
            propslib::Step(g, t, 1.0f / 120.0f);
            t += 1.0 / 120.0;
            propslib::HandAt(g, 0, &o);
            events |= o.events;
        }
        EXPECT(o.state == D2RP_HAND_HOLDING && o.id == 7 && (events & D2RP_HEV_GRAB), "the cup in the hand (state %d, id %u, events %d)", o.state, o.id, events);
        EXPECT(propslib::SetHands(g, nullptr, 0) == 0 && propslib::HandAt(g, 0, &o) == 1 && (o.events & D2RP_HEV_RELEASE), "no hands: let go (events %d)", o.events);
        propslib::Destroy(g);
    }
    std::printf(g_fails ? "props_lib_test: %d check(s) failed\n" : "props_lib_test: all checks passed\n", g_fails);
    return g_fails ? 1 : 0;
}
