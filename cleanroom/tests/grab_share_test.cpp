// Offline checks of vr/grab_share.h ([multiplayer] pose_share_grab): the hold's packing, the grab message and relay, the holder's sends (holds, the release
// with the palm's velocity, its repeats), the host's table, the playback of a holder's hand, the limb frame (the touch point from the model's joints), and the
// viewer's steps against a fake library - a loopback from the holder's hand through the host to a viewer, a viewer without the monster, a lost holder, an older
// build's view of the new messages. Run: grab_share_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "grab_share.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using namespace grabshare;

static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
static bool Near3(const float* a, const float* b, float tol) { return Near(a[0], b[0], tol) && Near(a[1], b[1], tol) && Near(a[2], b[2], tol); }

// A man's joints (render metres, y up, facing -z): what the touch reads of a humanoid.
static void Man(float j[kJoints][3], float dx = 0.0f, float scale = 1.0f) {
    const float p[kJoints][3] = {
        {0.0f, 1.00f, 0.0f},   {0.0f, 1.30f, 0.0f},  {0.0f, 1.55f, 0.0f},  {0.0f, 1.70f, 0.0f},                              // pelvis chest neck head
        {-0.20f, 1.50f, 0.0f}, {-0.45f, 1.50f, 0.0f}, {-0.70f, 1.50f, 0.0f},                                                  // left arm out
        {0.20f, 1.50f, 0.0f},  {0.22f, 1.22f, -0.05f}, {0.25f, 0.98f, -0.10f},                                                // right arm down
        {-0.10f, 0.95f, 0.0f}, {-0.10f, 0.52f, 0.02f}, {-0.10f, 0.08f, 0.0f}, {-0.10f, 0.02f, -0.15f},                       // left leg
        {0.10f, 0.95f, 0.0f},  {0.10f, 0.52f, 0.02f},  {0.10f, 0.08f, 0.0f},  {0.10f, 0.02f, -0.15f}};                       // right leg
    for (int i = 0; i < kJoints; ++i) for (int k = 0; k < 3; ++k) j[i][k] = p[i][k] * scale + (k == 0 ? dx : 0.0f);
}

static Hold SampleHold(uint8_t side, uint8_t state, uint32_t ms, float x) {
    Hold h;
    h.side = side;
    h.state = state;
    h.serial = 7;
    h.joint = kRElbow;
    h.monster = 0x1234;
    h.seq = 9;
    h.ms = ms;
    h.local[0] = 0.55f; h.local[1] = -0.12f; h.local[2] = 0.3f;
    h.palm[0] = x; h.palm[1] = 1.25f; h.palm[2] = -0.6f;
    h.heading = 1.2f;
    h.eyeM = 1.71f;
    h.vel[0] = 2.5f; h.vel[1] = -0.75f; h.vel[2] = 4.0f;
    return h;
}

// The library as the viewer drives it: which monsters the game has near, what was asked.
struct Fake : Bodies {
    std::vector<uint32_t> near;   // monsters our game has (a humanoid read lately)
    struct Call { char what; uint32_t holder; int side; uint32_t monster; int joint; float local[3], palm[3], vel[3]; bool palmNull; };
    std::vector<Call> calls;
    bool Take(uint32_t holder, int side, uint32_t monster, int joint, const float local[3], const float palm[3]) override {
        Call c{'T', holder, side, monster, joint, {local[0], local[1], local[2]}, {palm[0], palm[1], palm[2]}, {}, false};
        calls.push_back(c);
        for (uint32_t m : near) if (m == monster) return true;
        return false;
    }
    void Hand(uint32_t holder, int side, const float palm[3]) override {
        calls.push_back(Call{'H', holder, side, 0, 0, {}, {palm[0], palm[1], palm[2]}, {}, false});
    }
    void Release(uint32_t holder, int side, const float palm[3], const float vel[3]) override {
        Call c{'R', holder, side, 0, 0, {}, {}, {vel[0], vel[1], vel[2]}, palm == nullptr};
        if (palm) for (int k = 0; k < 3; ++k) c.palm[k] = palm[k];
        calls.push_back(c);
    }
    int Count(char w) const { int n = 0; for (const auto& c : calls) n += c.what == w; return n; }
};

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // (a crash keeps the lines before it)
    // ---- the hold on the wire ----
    {
        const Hold a = SampleHold(1, kHold, 123456789u, 0.4321f);
        uint8_t b[kHoldBytes];
        PackHold(a, b);
        Hold r;
        CHECK(UnpackHold(b, &r));
        CHECK(r.side == 1 && r.state == kHold && r.serial == 7 && r.joint == kRElbow && r.monster == 0x1234 && r.seq == 9 && r.ms == 123456789u);
        CHECK(Near3(r.local, a.local, 0.0006f) && Near3(r.palm, a.palm, 0.0006f) && Near3(r.vel, a.vel, 0.0006f));
        CHECK(Near(r.heading, a.heading, 0.0002f) && Near(r.eyeM, 1.71f, 0.0006f));
        Hold bad = a;
        bad.joint = 30;   // no such limb
        PackHold(bad, b);
        CHECK(!UnpackHold(b, &r));
        bad = a;
        bad.monster = 0;
        PackHold(bad, b);
        CHECK(!UnpackHold(b, &r));
        Hold huge = a;   // out of range and not finite: clamped, never wrapped
        huge.vel[0] = 1000.0f; huge.vel[1] = -1000.0f; huge.vel[2] = NAN;
        PackHold(huge, b);
        CHECK(UnpackHold(b, &r) && r.vel[0] > 32.0f && r.vel[1] < -32.0f && r.vel[2] == 0.0f);
    }
    // ---- the grab message (ours to the host) and the relay (the host's) ----
    {
        Hold h[2] = {SampleHold(0, kHold, 1000, 0.1f), SampleHold(1, kRelease, 1000, -0.2f)};
        uint8_t m[kGrabMsgMaxBytes];
        const uint32_t n = BuildGrabMsg(77, h, 2, m, sizeof m);
        CHECK(n == 6 + 2 * kHoldBytes && n <= poseshare::kMaxPayload);
        uint32_t unit = 0;
        Hold back[2];
        CHECK(ParseGrabMsg(m, n, &unit, back, 2) == 2 && unit == 77 && back[0].side == 0 && back[1].state == kRelease);
        CHECK(ParseGrabMsg(m, n - 1, &unit, back, 2) == 0);   // cut short
        CHECK(BuildGrabMsg(77, h, 0, m, sizeof m) == 0 && BuildGrabMsg(77, h, kGrabMsgMax + 1, m, sizeof m) == 0 && BuildGrabMsg(77, h, 2, m, 40) == 0);
        uint8_t other[kGrabMsgMaxBytes];
        std::memcpy(other, m, n);
        other[0] = kFormat + 1;   // a later format: not read as this one
        CHECK(ParseGrabMsg(other, n, &unit, back, 2) == 0);
        // a pose message is not a grab message (an old format byte) and a grab message is no pose (its first byte is not the pose's version)
        uint32_t pu = 0;
        poseshare::Pose pp;
        CHECK(!poseshare::ParsePoseMsg(m, n, &pu, &pp));
        RelayEntry e[5];
        for (int i = 0; i < 5; ++i) e[i] = RelayEntry{100u + (uint32_t)i, SampleHold((uint8_t)(i & 1), kHold, 2000u + i, 0.1f * i)};
        uint8_t r[poseshare::kMaxPayload];
        int taken = 0;
        const uint32_t rn = BuildGrabRelay(e, 5, r, sizeof r, &taken);
        CHECK(taken == kGrabRelayMax && rn == 2 + kGrabRelayMax * (4 + kHoldBytes) && rn <= poseshare::kMaxPayload);
        RelayEntry got[kGrabRelayMax];
        CHECK(ParseGrabRelay(r, rn, got, kGrabRelayMax) == kGrabRelayMax && got[3].unit == 103 && got[3].hold.ms == 2003 && got[1].hold.side == 1);
        CHECK(ParseGrabRelay(r, rn - 5, got, kGrabRelayMax) == 0);
        // an older build's client reading the relay as a pose relay (it only takes kMsgRelay - but even then): the format byte refuses it
        poseshare::RelayEntry pe[poseshare::kRelayMax];
        CHECK(poseshare::ParseRelay(r, rn, pe, poseshare::kRelayMax) == 0);
        CHECK(kMsgGrab != poseshare::kMsgPose && kMsgGrab != poseshare::kMsgRelay && kMsgGrab != poseshare::kMsgPing && kMsgGrab != poseshare::kMsgPong &&
              kMsgGrab != poseshare::kMsgBurst && kMsgGrabRelay != kMsgGrab && kMsgGrabRelay > poseshare::kMsgBurst);
    }
    // ---- the limb frame: the touch point from the model's own joints ----
    {
        float j[kJoints][3];
        Man(j);
        // a point on the right forearm, a little in front of it
        const float p[3] = {0.235f, 1.10f, -0.13f};
        const int limb = NearestLimb(j, ~0u, p);
        CHECK(limb == kRElbow);
        float local[3], back[3];
        CHECK(ToLimb(j, limb, p, local) && FromLimb(j, limb, local, back) && Near3(back, p, 1e-5f));
        CHECK(local[0] > 0.3f && local[0] < 0.7f);   // about half way down the forearm
        // the same body drawn elsewhere and bigger (another PC: its own world scale): the point comes out on the same spot of the forearm
        float j2[kJoints][3];
        Man(j2, 3.0f, 1.25f);
        float p2[3];
        CHECK(FromLimb(j2, limb, local, p2));
        const float want[3] = {p[0] * 1.25f + 3.0f, p[1] * 1.25f, p[2] * 1.25f};
        CHECK(Near3(p2, want, 1e-4f));
        // ... and turned about the vertical (it faces elsewhere there): still the same spot of the limb
        float j3[kJoints][3];
        const float c = std::cos(1.1f), s = std::sin(1.1f);
        for (int i = 0; i < kJoints; ++i) { j3[i][0] = j[i][0] * c + j[i][2] * s; j3[i][1] = j[i][1]; j3[i][2] = -j[i][0] * s + j[i][2] * c; }
        float p3[3];
        CHECK(FromLimb(j3, limb, local, p3));
        const float want3[3] = {p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c};
        CHECK(Near3(p3, want3, 1e-4f));
        // the end joints: the head, a wrist, a toe have a frame (along the bone into them)
        float o[3], x[3], y[3], z[3], l = 0.0f;
        CHECK(LimbFrame(j, kHead, o, x, y, z, &l) && Near(l, 0.15f, 1e-4f) && Near(x[1], 1.0f, 1e-4f));
        CHECK(LimbFrame(j, kLWrist, o, x, y, z, &l) && Near(x[0], -1.0f, 1e-4f));
        CHECK(LimbFrame(j, kRToe, o, x, y, z, &l));
        // the head nearest a point over it; a leg's point
        const float above[3] = {0.0f, 1.85f, 0.0f}, knee[3] = {0.12f, 0.6f, 0.05f};
        CHECK(NearestLimb(j, ~0u, above) == kHead);
        CHECK(NearestLimb(j, ~0u, knee) == kRHip);
        // a limb with a guessed joint loses to a model's own one farther off
        const uint32_t noRightKnee = ~0u & ~(1u << kRKnee);
        CHECK(NearestLimb(j, noRightKnee, knee) != kRHip && NearestLimb(j, noRightKnee, knee) != kRKnee);
        // joints that coincide: no frame, no point
        float flat[kJoints][3] = {};
        CHECK(!LimbFrame(flat, kChest, o, x, y, z, &l) && !ToLimb(flat, kChest, p, local) && NearestLimb(flat, ~0u, p) == -1);
        CHECK(ChildOf(kHead) == -1 && ParentOf(kHead) == kNeck && ChildOf(kRShoulder) == kRElbow && ParentOf(kPelvis) == -1);
    }
    // ---- the holder: a hold each tick, the release with the palm's velocity, repeated ----
    {
        Sender s;
        Own own[2];
        Hold out[2];
        CHECK(s.Feed(own, 0, out) == 0 && !s.Busy());   // nothing held: nothing sent
        own[0].held = true; own[0].monster = 0x55; own[0].joint = kLElbow; own[0].local[0] = 0.4f; own[0].heading = 0.3f; own[0].eyeM = 1.8f;
        uint8_t serial = 0;
        uint32_t ms = 1000;
        for (int i = 0; i < 30; ++i, ms += 11) {   // the palm going +x at 3 m/s
            own[0].palm[0] = 0.033f * i; own[0].palm[1] = 1.2f; own[0].palm[2] = -0.5f;
            const int n = s.Feed(own, ms, out);
            CHECK(n == 1 && out[0].state == kHold && out[0].side == 0 && out[0].monster == 0x55 && out[0].joint == kLElbow && out[0].ms == ms);
            if (i == 0) serial = out[0].serial;
            CHECK(out[0].serial == serial);
        }
        CHECK(Near(out[0].vel[0], 3.0f, 0.1f) && Near(out[0].vel[1], 0.0f, 0.01f));
        own[0].held = false;
        const uint32_t letGoMs = ms;
        int releases = 0;
        for (int i = 0; i < 10; ++i, ms += 11) {
            const int n = s.Feed(own, ms, out);
            if (!n) continue;
            ++releases;
            CHECK(out[0].state == kRelease && out[0].serial == serial && out[0].ms == letGoMs && Near(out[0].vel[0], 3.0f, 0.1f));
            CHECK(Near(out[0].palm[0], 0.033f * 29, 1e-5f));   // where the hand left it
        }
        CHECK(releases == kReleaseRepeats && !s.Busy());
        own[0].held = true;   // a new hold: a new number, the velocity afresh
        CHECK(s.Feed(own, ms, out) == 1 && out[0].serial != serial && out[0].vel[0] == 0.0f);
        const uint8_t s2 = out[0].serial;
        own[0].monster = 0x56;   // another body without a let-go between: another number too
        CHECK(s.Feed(own, ms + 11, out) == 1 && out[0].serial != s2 && out[0].monster == 0x56);
        own[1] = own[0];   // both hands
        own[1].monster = 0x57;
        CHECK(s.Feed(own, ms + 22, out) == 2 && out[1].side == 1 && out[1].monster == 0x57 && out[0].seq != out[1].seq);
        // the velocity is the world's: the head turned (the hero frame with it) with the hand still in the world is no throw; a walk carries on
        Sender t;
        Own w[2];
        w[0].held = true; w[0].monster = 0x60; w[0].joint = kChest; w[0].eyeM = 1.7f;
        const float still[3] = {0.4f, 1.2f, -0.3f};   // the palm in the world's axes, from the feet
        for (int i = 0; i < 30; ++i) {
            w[0].heading = 0.05f * i;   // turning on the spot
            poseshare::DirToHero(still, w[0].heading, w[0].palm);
            t.Feed(w, 5000 + 11 * i, out);
        }
        CHECK(Near(out[0].vel[0], 0.0f, 0.02f) && Near(out[0].vel[1], 0.0f, 0.02f) && Near(out[0].vel[2], 0.0f, 0.02f));
        for (int i = 30; i < 60; ++i) {
            w[0].feet[2] = -0.02f * (i - 30);   // walking -z at 1.8 m/s, the hand still against the body
            t.Feed(w, 5000 + 11 * i, out);
        }
        float vw[3];
        poseshare::DirToWorld(out[0].vel, out[0].heading, vw);
        CHECK(Near(vw[0], 0.0f, 0.05f) && Near(vw[2], -0.02f / 0.011f, 0.1f));
    }
    // ---- the host's table: each peer's hands, to the others; its own back only with echo ----
    {
        HostGrabs t;
        CHECK(t.Take(1, 11, SampleHold(0, kHold, 100, 0.0f)) && t.Take(2, 22, SampleHold(1, kHold, 100, 0.0f)) && t.AnyFresh());
        RelayEntry e[HostGrabs::kMax];
        CHECK(t.Build(1, false, e, HostGrabs::kMax) == 1 && e[0].unit == 22);
        CHECK(t.Build(3, false, e, HostGrabs::kMax) == 2);
        CHECK(t.Build(1, true, e, HostGrabs::kMax) == 2);
        t.Flush();
        CHECK(!t.AnyFresh() && t.Build(3, false, e, HostGrabs::kMax) == 0);
        CHECK(t.Take(1, 11, SampleHold(0, kHold, 111, 0.1f)));   // the same hand again: one entry, the newer
        CHECK(t.Build(2, false, e, HostGrabs::kMax) == 1 && e[0].hold.ms == 111);
        t.Flush();
        CHECK(t.Take(1, 11, SampleHold(0, kRelease, 122, 0.1f)));   // a release relayed frees the hand's entry
        t.Flush();
        int used = 0;
        for (const auto& x : t.e) used += x.peer ? 1 : 0;
        CHECK(used == 1);   // (peer 2's hold stays)
        t.Remove(2);
        used = 0;
        for (const auto& x : t.e) used += x.peer ? 1 : 0;
        CHECK(used == 0);
    }
    // ---- the playback of a holder's hand: behind as its poses, blended, a release in its place, a silent holder lost ----
    {
        GrabTrack tr;
        Hold h;
        CHECK(!tr.Sample(5000, 100, &h));
        const uint64_t lag = 40;   // the network's (our clock - theirs on arrival, here 5000 - 1000 + 40)
        for (int i = 0; i < 10; ++i) CHECK(tr.Add(4040 + 33 * i, SampleHold(0, kHold, 1000 + 33 * i, 0.1f * i)));
        CHECK(!tr.Add(4040 + 33 * 9, SampleHold(0, kHold, 1000 + 33 * 9, 0.9f)));   // a copy
        CHECK(!tr.Add(4400, SampleHold(0, kHold, 1000 + 33 * 3, 0.3f)));            // older than the newest
        bool lost = true;
        CHECK(!tr.Sample(4040 - 50, 100, &h));   // (before the first hold is due)
        // our 4040 + 33*5 + 16 - offset(3040) - delay 100: the sender's 1000 + 33*5 + 16 - 100
        CHECK(tr.Sample(4040 + 33 * 5 + 16 + 100, 100, &h, &lost) && !lost && h.state == kHold);
        CHECK(Near(h.palm[0], 0.5f + 0.1f * 16.0f / 33.0f, 0.002f));
        (void)lag;
        Hold rel = SampleHold(0, kRelease, 1000 + 33 * 10, 1.0f);
        CHECK(tr.Add(4040 + 33 * 10, rel));
        CHECK(!tr.Add(4040 + 33 * 10 + 11, rel));   // its repeat
        CHECK(tr.Sample(4040 + 33 * 9 + 100 + 20, 100, &h) && h.state == kHold && h.palm[0] > 0.9f && h.palm[0] < 1.0f);   // blended toward where it let go
        CHECK(tr.Sample(4040 + 33 * 10 + 100, 100, &h) && h.state == kRelease && Near(h.vel[2], 4.0f, 1e-5f));
        // a holder that falls silent while holding: lost - a release with no throw
        GrabTrack t2;
        t2.Add(100, SampleHold(1, kHold, 50, 0.2f));
        CHECK(t2.Sample(100 + kGrabStaleMs + 1, 100, &h, &lost) && lost && h.state == kRelease && h.vel[0] == 0.0f && h.palm[0] == 0.2f);
    }
    // ---- the viewer against the fake library ----
    {
        // a loopback: the holder's sends -> the host's message -> the relay -> the viewer's track -> the viewer's steps
        Sender holder;
        HostGrabs host;
        GrabTrack track;
        Viewer viewer;
        Fake lib;
        lib.near = {0x99};
        Own own[2];
        own[1].held = true; own[1].monster = 0x99; own[1].joint = kRElbow; own[1].local[0] = 0.5f; own[1].heading = 0.0f; own[1].eyeM = 1.7f;
        uint32_t ms = 10000;
        uint64_t now = 50000;
        auto pump = [&](int ticks) {
            for (int i = 0; i < ticks; ++i, ms += 11, now += 11) {
                Hold out[2];
                const int n = holder.Feed(own, ms, out);
                if (n) {
                    uint8_t m[kGrabMsgMaxBytes];
                    const uint32_t sz = BuildGrabMsg(42, out, n, m, sizeof m);
                    uint32_t unit = 0;
                    Hold in[2];
                    const int k = ParseGrabMsg(m, sz, &unit, in, 2);
                    for (int j = 0; j < k; ++j) host.Take(7, unit, in[j]);
                }
                RelayEntry e[HostGrabs::kMax];
                const int ne = host.Build(8, false, e, HostGrabs::kMax);   // to the viewer's peer
                host.Flush();
                if (ne) {
                    uint8_t r[poseshare::kMaxPayload];
                    int taken = 0;
                    const uint32_t rs = BuildGrabRelay(e, ne, r, sizeof r, &taken);
                    RelayEntry got[kGrabRelayMax];
                    const int ng = ParseGrabRelay(r, rs, got, kGrabRelayMax);
                    for (int j = 0; j < ng; ++j) track.Add(now + 30, got[j].hold);
                }
                Seen seen[1];
                int ns = 0;
                Hold h;
                bool lost = false;
                if (track.Sample(now, 100, &h, &lost)) {
                    seen[0].holder = 42;
                    seen[0].hold = h;
                    seen[0].lost = lost;
                    for (int k = 0; k < 3; ++k) { seen[0].palm[k] = h.palm[k] * 2.0f; seen[0].vel[k] = h.vel[k] * 2.0f; }   // (our world: the caller's)
                    ns = 1;
                }
                viewer.Step(seen, ns, now, lib);
            }
        };
        for (int i = 0; i < 20; ++i) { own[1].palm[0] = 0.02f * i; own[1].palm[1] = 1.2f; pump(1); }
        CHECK(lib.Count('T') == 1 && lib.calls[0].monster == 0x99 && lib.calls[0].side == 1 && lib.calls[0].joint == kRElbow && lib.calls[0].holder == 42);
        CHECK(Near(lib.calls[0].local[0], 0.5f, 0.001f));
        CHECK(lib.Count('H') >= 5 && viewer.Taken() == 1);
        float lastX = -1.0f;
        bool steady = true;
        for (const auto& c : lib.calls) if (c.what == 'H') { steady = steady && c.palm[0] >= lastX - 1e-5f; lastX = c.palm[0]; }
        CHECK(steady);   // the palm only goes on (played back in order)
        for (int i = 20; i < 40; ++i) { own[1].palm[0] = 0.02f * i; pump(1); }
        own[1].held = false;
        pump(30);
        CHECK(lib.Count('R') == 1 && viewer.Taken() == 0);
        const Fake::Call* rel = nullptr;
        for (const auto& c : lib.calls) if (c.what == 'R') rel = &c;
        CHECK(rel && !rel->palmNull && Near(rel->vel[0], 2.0f * 0.02f / 0.011f, 0.25f));   // thrown with the palm's velocity (in our world)
        CHECK(Near(rel->palm[0], 2.0f * 0.02f * 39, 0.002f));
        pump(30);
        CHECK(lib.Count('R') == 1 && lib.Count('T') == 1);   // its repeats and the silence after: nothing more

        // a viewer whose game has not got that monster: it tries a few times a second, never holds, never lets go
        Viewer v2;
        Fake none;
        Seen s;
        s.holder = 5;
        s.hold = SampleHold(0, kHold, 0, 0.0f);
        for (uint64_t t = 0; t < 1000; t += 11) v2.Step(&s, 1, t, none);
        CHECK(none.Count('T') >= 3 && none.Count('T') <= 5 && none.Count('H') == 0 && v2.Taken() == 0);
        s.hold.state = kRelease;
        const Viewer::Counts c = v2.Step(&s, 1, 1000, none);
        CHECK(c.ignored == 1 && none.Count('R') == 0);
        // ... and the monster comes near while he holds it: taken then
        Viewer v3;
        Fake later;
        s.hold.state = kHold;
        v3.Step(&s, 1, 0, later);
        later.near = {s.hold.monster};
        v3.Step(&s, 1, 100, later);   // (before the retry is due)
        CHECK(later.Count('T') == 1);
        v3.Step(&s, 1, 300, later);
        CHECK(later.Count('T') == 2 && v3.Taken() == 1 && later.Count('H') == 1);

        // a holder gone from the list while holding (left our area): let go, no throw
        Viewer v4;
        Fake f4;
        f4.near = {s.hold.monster};
        v4.Step(&s, 1, 0, f4);
        CHECK(v4.Taken() == 1);
        const Viewer::Counts c4 = v4.Step(nullptr, 0, 11, f4);
        CHECK(c4.lost == 1 && f4.Count('R') == 1 && f4.calls.back().palmNull && f4.calls.back().vel[0] == 0.0f && v4.Taken() == 0);
        // a lost holder (silent): a release with no throw
        v4.Step(&s, 1, 20, f4);
        Seen ls = s;
        ls.hold.state = kRelease;
        ls.lost = true;
        ls.vel[0] = 9.0f;
        const Viewer::Counts c5 = v4.Step(&ls, 1, 40, f4);
        CHECK(c5.lost == 1 && f4.calls.back().what == 'R' && f4.calls.back().vel[0] == 0.0f);
        // a new hold of the same hand while one is held (a re-grip): the old let go, the new taken
        Viewer v5;
        Fake f5;
        f5.near = {s.hold.monster, 0x777};
        v5.Step(&s, 1, 0, f5);
        Seen n2 = s;
        n2.hold.serial = (uint8_t)(s.hold.serial + 1);
        n2.hold.monster = 0x777;
        v5.Step(&n2, 1, 11, f5);
        CHECK(f5.Count('T') == 2 && f5.Count('R') == 1 && v5.Taken() == 1 && f5.calls.back().what == 'H');
        CHECK(v5.ReleaseAll(f5) == 1 && f5.Count('R') == 2 && v5.Taken() == 0);
    }

    // ---- knocks: one event, sent a few ticks, relayed, played once on time (the author: "if one player pushed or threw a monster, the others must see it") ----
    {
        Hold k;
        k.monster = 0x4242;
        k.joint = kChest;
        k.kind = 2;   // a shove
        k.local[0] = 0.5f; k.local[2] = 0.4f;
        k.vel[0] = 1.5f; k.vel[1] = 0.25f; k.vel[2] = -2.0f;
        k.heading = 0.7f;
        k.eyeM = 1.75f;
        k.ms = 20000;
        k.state = kKnock;
        uint8_t b[kHoldBytes];
        PackHold(k, b);
        Hold r;
        CHECK(UnpackHold(b, &r) && r.state == kKnock && r.kind == 2 && r.monster == 0x4242 && r.joint == kChest && Near3(r.vel, k.vel, 0.0006f));
        // a message with both hands and two knocks
        Hold four[kGrabMsgMax + 1] = {SampleHold(0, kHold, 1, 0.0f), SampleHold(1, kHold, 1, 0.0f), k, k, k};
        four[3].kind = 3;
        uint8_t m[kGrabMsgMaxBytes];
        const uint32_t mn = BuildGrabMsg(9, four, kGrabMsgMax, m, sizeof m);
        uint32_t unit = 0;
        Hold back[kGrabMsgMax];
        CHECK(mn == kGrabMsgMaxBytes && ParseGrabMsg(m, mn, &unit, back, kGrabMsgMax) == 4 && back[2].state == kKnock && back[3].kind == 3 && back[1].state == kHold);
        CHECK(BuildGrabMsg(9, four, kGrabMsgMax + 1, m, sizeof m) == 0);
        // the striker: each knock numbered, sent kKnockRepeats ticks, then nothing
        KnockSender ks;
        uint16_t seq = 0;
        Hold out[4];
        CHECK(ks.Take(out, 4, &seq) == 0 && !ks.Busy());
        ks.Add(k);
        Hold k2 = k;
        k2.monster = 0x4343;
        ks.Add(k2);
        int sent = 0;
        uint8_t serial1 = 0, serial2 = 0;
        for (int t = 0; t < 6; ++t) {
            const int n = ks.Take(out, 4, &seq);
            sent += n;
            for (int i = 0; i < n; ++i) {
                CHECK(out[i].state == kKnock);
                if (out[i].monster == 0x4242) serial1 = out[i].serial; else serial2 = out[i].serial;
            }
        }
        CHECK(sent == 2 * kKnockRepeats && serial1 != serial2 && !ks.Busy() && seq == 2 * kKnockRepeats);
        CHECK(ks.Take(out, 1, &seq) == 0);
        // the host: each knock its own entry beside the hands' holds, freed once relayed
        HostGrabs host;
        Hold ka = k; ka.serial = 1;
        Hold kb = k; kb.serial = 2;
        CHECK(host.Take(5, 55, SampleHold(0, kHold, 1, 0.0f)) && host.Take(5, 55, ka) && host.Take(5, 55, kb) && host.Take(5, 55, ka));
        RelayEntry e[HostGrabs::kMax];
        CHECK(host.Build(6, false, e, HostGrabs::kMax) == 3);
        host.Flush();
        int used = 0;
        for (const auto& x : host.e) used += x.peer ? 1 : 0;
        CHECK(used == 1);   // (the hand's hold stays)
        // the viewer: played once, as far behind as the striker's poses; repeats and copies dropped; stale ones too
        KnockQueue q;
        Hold kq = k;
        kq.serial = 9;
        CHECK(q.Add(50030, 77, kq));      // came 30 ms after the striker's 20000 on our 50000
        CHECK(!q.Add(50041, 77, kq));     // its repeat
        CHECK(!q.Add(50000, 77, SampleHold(0, kHold, 1, 0.0f)));   // no hold in the knocks' queue
        KnockQueue::Knock got[4];
        auto delay100 = [](uint32_t) { return (uint32_t)100; };
        CHECK(q.Due(50100, delay100, got, 4) == 0);   // (30 + 100 ms behind its moment: not yet)
        CHECK(q.Due(50130, delay100, got, 4) == 1 && got[0].unit == 77 && got[0].h.monster == 0x4242 && got[0].h.kind == 2);
        CHECK(q.Due(50300, delay100, got, 4) == 0);
        CHECK(!q.Add(50140, 77, kq));     // a late repeat of one played: not again
        Hold old = k;
        old.serial = 10;
        old.ms = 20500;
        CHECK(q.Add(50600, 77, old));
        CHECK(q.Due(50600 + kKnockStaleMs + 1, delay100, got, 4) == 0);   // never played in time: dropped
        // a faster arrival later puts the striker's clock nearer: the offset is the least seen
        Hold f1 = k; f1.serial = 11; f1.ms = 30000;
        CHECK(q.Add(60010, 77, f1));   // 30000 -> 60010: 20 ms less than before
        CHECK(q.Due(60100, delay100, got, 4) == 0 && q.Due(60110, delay100, got, 4) == 1);
    }

    std::printf("grab_share_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
