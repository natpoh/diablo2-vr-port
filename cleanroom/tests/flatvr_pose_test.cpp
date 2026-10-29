// flatvr_pose_test - vr/flatvr_pose.h (docs/flatvr_sync.md): BodyWalk's roll back to the head's own, the head's move in the room
// into the game's world, and pairs started on FlatVR's frames (one a frame in phase; a late one waits for the next: an even half
// rate; no FlatVR head: the old timer).
#include "flatvr_pose.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using namespace flatvrpose;

static bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// The head's right vector for yaw (about +y), then pitch (about the turned x), then roll (about the turned -z: + = right ear down),
// as OpenXR's axes have it; BodyWalk's roll from it as UniversalOutputService::update makes it.
static float BodyWalkRoll(float yawDeg, float pitchDeg, float rollDeg) {
    const float y = yawDeg * kDeg, p = pitchDeg * kDeg, r = rollDeg * kDeg;
    // right = Ry(y) * Rx(p) * Rz(-r) * (1,0,0): roll toward the right shoulder turns +x down
    float x0 = std::cos(-r), y0 = std::sin(-r), z0 = 0.0f;           // Rz
    float x1 = x0, y1 = y0 * std::cos(p) - z0 * std::sin(p), z1 = y0 * std::sin(p) + z0 * std::cos(p);   // Rx
    (void)x1; (void)z1;
    const float rightY = y1;   // Ry leaves y alone
    (void)y;
    return -std::asin(std::clamp(rightY, -1.0f, 1.0f)) / kDeg;
}

// A FlatVR at `periodTicks` handing a head at each frame; the game waits (Decide) and draws a pair of `pairTicks` after each Take.
// Returns the heads taken, in order.
static std::vector<uint32_t> Run(uint32_t periodTicks, uint32_t pairTicks, float lateFrac, int frames, int mode = kEven, uint32_t* giveUps = nullptr) {
    Sync s(90.0f);
    std::vector<uint32_t> taken;
    uint32_t t = 1000, g = 0;
    const uint32_t end = 1000 + periodTicks * (uint32_t)frames;
    while (t < end) {
        uint32_t waited = 0;
        for (;;) {
            const uint32_t head = 1000 + ((t - 1000) / periodTicks) * periodTicks;   // the newest head FlatVR has handed out by t
            const Step st = s.Decide(head, true, t, waited, lateFrac, mode);
            if (st == Step::Take) { taken.push_back(head); break; }
            if (st == Step::GiveUp) { ++g; break; }
            ++t; ++waited;   // polled again a tick later
        }
        t += pairTicks;
    }
    if (giveUps) *giveUps = g;
    return taken;
}

int main() {
    {   // the roll as BodyWalk sends it, and back
        CHECK(Near(BodyWalkRoll(0, 0, 10), 10.0f, 0.01f));                       // level: the same
        CHECK(Near(BodyWalkRoll(30, -60, 10), 4.98f, 0.05f));                    // looking 60 down: about half (asin(sin 10 * cos 60))
        for (float yaw : {0.0f, 75.0f, -140.0f})
            for (float pitch : {-75.0f, -60.0f, -30.0f, 0.0f, 20.0f, 45.0f, 70.0f})
                for (float roll : {-40.0f, -10.0f, 0.0f, 5.0f, 25.0f}) {
                    const float sent = BodyWalkRoll(yaw, pitch, roll);
                    CHECK(Near(TrueRollDeg(sent, pitch), roll, 0.05f));
                }
        CHECK(Near(TrueRollDeg(12.0f, 89.0f), 12.0f, 1e-4f));                   // straight up: as sent, never a NaN
        CHECK(std::isfinite(TrueRollDeg(80.0f, 85.0f)));
    }
    {   // the room into the world: the room's ahead of the head lands on the camera's ahead (vrcam's Facing)
        for (float roomYaw : {0.0f, 0.7f, -2.4f})
            for (float cam : {0.0f, 1.3f, -0.4f}) {
                const V ahead{-std::sin(roomYaw), 0.0f, -std::cos(roomYaw)};   // where the head looks in the room
                const V w = RoomToWorld(ahead, cam, roomYaw, 1.0f, 10.0f);
                CHECK(Near(w.x, -std::sin(cam), 1e-4f) && Near(w.y, 0.0f, 1e-6f) && Near(w.z, -std::cos(cam), 1e-4f));
            }
        const V up = RoomToWorld({0.0f, -0.3f, 0.0f}, 0.5f, 0.2f, 4.0f, 2.0f);   // crouching 30 cm: the camera down, scaled
        CHECK(Near(up.x, 0.0f, 1e-6f) && Near(up.y, -1.2f, 1e-5f) && Near(up.z, 0.0f, 1e-6f));
        const V far = RoomToWorld({3.0f, 0.0f, 4.0f}, 0.0f, 0.0f, 1.0f, 2.0f);    // 5 m away: held at 2
        CHECK(Near(std::sqrt(far.x * far.x + far.y * far.y + far.z * far.z), 2.0f, 1e-4f));
        const V bad = RoomToWorld({NAN, 0.0f, 0.0f}, 0.0f, 0.0f, 1.0f, 2.0f);
        CHECK(bad.x == 0.0f && bad.y == 0.0f && bad.z == 0.0f);
    }
    {   // a pair that fits a frame: one pair a FlatVR frame, every head taken once, in order
        const std::vector<uint32_t> h = Run(111, 90, 0.5f, 200);
        bool every = h.size() >= 198;
        for (size_t i = 1; i < h.size(); ++i) every = every && h[i] - h[i - 1] == 111;
        CHECK(every);
    }
    {   // a pair a little shorter than a frame (10 ms at 90 Hz): still one a frame
        const std::vector<uint32_t> h = Run(111, 100, 0.5f, 200);
        bool every = h.size() >= 198;
        for (size_t i = 1; i < h.size(); ++i) every = every && h[i] - h[i - 1] == 111;
        CHECK(every);
    }
    {   // a pair a little longer than a frame (12-13 ms at 90 Hz): every other head, never 1,2,1,1,2 - an even 45
        for (uint32_t pair : {120u, 130u, 150u}) {
            const std::vector<uint32_t> h = Run(111, pair, 0.5f, 200);
            bool even = h.size() >= 95 && h.size() <= 102;
            for (size_t i = 3; i < h.size(); ++i) even = even && h[i] - h[i - 1] == 222;
            CHECK(even);
        }
        // a pair longer than two frames: every third head
        const std::vector<uint32_t> t = Run(111, 240, 0.5f, 300);
        bool third = t.size() >= 95;
        for (size_t i = 4; i < t.size(); ++i) third = third && t[i] - t[i - 1] == 333;
        CHECK(third);
        // mode "every head": the same pairs take heads as they come - uneven gaps (the most pictures)
        const std::vector<uint32_t> g = Run(111, 130, 0.5f, 200, kEvery);
        bool uneven = g.size() > 110;
        bool differ = false;
        for (size_t i = 2; i < g.size(); ++i) differ = differ || g[i] - g[i - 1] != g[i - 1] - g[i - 2];
        CHECK(uneven && differ);
    }
    {   // the pairs get faster again (an area with less to draw): back to every head
        Sync s(90.0f);
        uint32_t t = 1000;
        auto pair = [&](uint32_t len) {
            uint32_t waited = 0;
            for (;;) {
                const uint32_t head = 1000 + ((t - 1000) / 111) * 111;
                if (s.Decide(head, true, t, waited, 0.5f) == Step::Take) break;
                ++t; ++waited;
            }
            t += len;
        };
        for (int i = 0; i < 20; ++i) pair(140);
        CHECK(s.interval == 2);
        for (int i = 0; i < 40; ++i) pair(70);
        CHECK(s.interval == 1);
    }
    {   // no FlatVR head, a stale one, or FlatVR stopping mid-wait: give up (the old timer paces)
        Sync s;
        CHECK(s.Decide(1000, false, 1005, 0, 0.5f) == Step::GiveUp);     // another source's head
        CHECK(s.Decide(1000, true, 1000 + 600, 0, 0.5f) == Step::GiveUp); // 60 ms old: FlatVR runs no frames
        CHECK(s.Decide(1000, true, 1010, 0, 0.5f) == Step::Take);
        CHECK(s.Decide(1000, true, 1050, 40, 0.5f) == Step::Wait);        // the same head again: wait for the next
        CHECK(s.Decide(1000, true, 1290, 280, 0.5f) == Step::GiveUp);     // 2.5 frames and no new head
    }
    {   // a head that came while the last pair ran over (older than half a frame at its end) is let go, once; the next one taken
        Sync s(90.0f);
        CHECK(s.Decide(1000, true, 1002, 0, 0.5f, kEvery) == Step::Take);
        CHECK(s.Decide(1111, true, 1111 + 70, 0, 0.5f, kEvery) == Step::Wait && s.skipped == 1);
        CHECK(s.Decide(1111, true, 1111 + 75, 5, 0.5f, kEvery) == Step::Wait && s.skipped == 1);   // counted once
        CHECK(s.Decide(1222, true, 1224, 43, 0.5f, kEvery) == Step::Take && s.taken == 1222);
        CHECK(s.Decide(1333, true, 1333 + 40, 0, 0.5f, kEvery) == Step::Take);                      // within half a frame: taken
    }
    {   // FlatVR's frame learnt from the heads: guessed 90, the headset runs 72 (13.9 ms) or 120 (8.3 ms)
        Sync s(90.0f);
        for (uint32_t k = 0; k < 60; ++k) s.See(1000 + k * 139);
        CHECK(Near(s.periodTicks, 139.0f, 1.0f));
        Sync f(90.0f);
        for (uint32_t k = 0; k < 60; ++k) f.See(1000 + k * 83);
        CHECK(Near(f.periodTicks, 83.0f, 1.0f));
        Sync m(90.0f);
        for (uint32_t k = 0; k < 60; ++k) m.See(1000 + k * 222);   // every other head seen: not taken for the frame
        CHECK(Near(m.periodTicks, 111.1f, 0.5f));
        CHECK(m.DueIn(1000 + 59 * 222 + 50) == 61);
    }

    if (g_fail) { std::printf("flatvr_pose_test: %d FAILED\n", g_fail); return 1; }
    std::printf("flatvr_pose_test: the roll back from BodyWalk's, the room into the world, pairs on FlatVR's frames (in phase; late = "
                "an even half rate; no FlatVR head = the old timer): all passed\n");
    return 0;
}
