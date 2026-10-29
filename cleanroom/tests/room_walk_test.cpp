// Offline checks of vr/room_walk.h: room-scale walking in VR first person ([openxr] roomscale_walk), in a synthetic room and game - a head
// walking, turning, nodding and leaning, a game that walks the hero by the stick it is given (a game tick of 40 ms, the stick read once a
// tick, a top speed per full stick, walls), the camera as the native head view builds it - checking that the hero walks under the head, that
// the view stays exactly with the head while he does, that turning or nodding in place walks nothing, the cap at a wall, the controller's
// stick winning, a teleport or a recentre, and the pad merge.
// Run: build/Release/room_walk_test.exe (exit code 0 = passed)

#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <initializer_list>

#include "../../vr/room_walk.h"

using namespace roomwalk;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool Near(float a, float b, float eps = 0.002f) { return std::fabs(a - b) <= eps; }
constexpr float kPi = 3.14159265f;

// A head turn: yaw (+ left) then pitch (+ up), as a quaternion x y z w.
static void HeadQuat(float yaw, float pitch, float q[4]) {
    const float cy = std::cos(yaw * 0.5f), sy = std::sin(yaw * 0.5f), cp = std::cos(pitch * 0.5f), sp = std::sin(pitch * 0.5f);
    // qYaw (0, sy, 0, cy) * qPitch (sp, 0, 0, cp)
    q[0] = cy * sp; q[1] = sy * cp; q[2] = -sy * sp; q[3] = cy * cp;
}

// -- the synthetic room and game ---------------------------------------------------------------------------------------------------
struct Sim {
    Walker w;
    Config cfg;
    double t = 0.0;
    // the player: the top of the neck on the floor, the head's turn
    V2 neck;
    float yaw = 0.0f, pitch = 0.0f;
    float eyeH = 1.6f;
    // the game: the hero, the stick it read at its last tick, walls
    V2 hero;
    float vmax = 2.5f;          // metres a second at a full stick (the game's speed follows the strength: its server move times it)
    bool hook = false;          // the walk read through the walking-stick hook: no pad dead zone (the strength still in 31 steps)
    float floorMps = -1.0f;     // the game's least speed (its velocity floor), metres a second; < 0: the game's own, a quarter of vmax
    double lagS = 0.0;          // the stick read at a tick moves the hero this much later (the server's answer)
    double nextTick = 0.0;
    float gsx = 0.0f, gsy = 0.0f;   // the stick in the room frame (the game's own turn of it, done right) ...
    std::deque<std::array<double, 3>> pending;   // ... as read at the ticks, each moving the hero from its time on
    float wallZ = -1e9f;            // the hero cannot go to z < wallZ
    bool stickHeld = false;
    V2 ownStick;                    // the controller's stick in the room frame while held
    bool canWalk = true, engaged = true;
    uint32_t epoch = 1;
    // the last step
    Output o;
    float stickX = 0.0f, stickY = 0.0f;   // what the pad has now (view frame)
    float headYawAtStick = 0.0f;
    V2 heroPrev;
    bool tickShown = false;   // the client shows the hero where he is at each tick only (no smoothing between)
    V2 heroShown;
    bool started = false;
    // the camera on the floor (world = room frame here, a metre a world unit)
    V2 cam, cam0, eyes0;
    float maxTilt = 0.0f;
    int walkingSteps = 0, steps = 0, stops = 0, reversals = 0;
    bool wasWalking = false;
    float prevSX = 0.0f, prevSY = 0.0f;
    bool everBlocked = false, everCapped = false;

    void Eyes(float out[3], float q[4]) const {
        HeadQuat(yaw, pitch, q);
        const float back[3] = {0.0f, 0.0f, kNeckM};
        float b[3];
        Rotate(q, back, b);
        out[0] = neck.x - b[0]; out[1] = eyeH - b[1]; out[2] = neck.z - b[2];
    }

    void GameTick(double at) {
        // the stick read once a tick, turned into the room the way the native camera's yaw turns it (the head's yaw at the read)
        float sx = 0.0f, sy = 0.0f, yawNow = headYawAtStick, x = 0.0f, z = 0.0f;
        // the pad: the controller's stick where it is pushed - unless the room walk keeps the pad (roomFirst: its push not taken over yet)
        if (stickHeld && !o.roomFirst) { x = ownStick.x; z = ownStick.z; }
        else {
            sx = stickX; sy = stickY;
            const float l = std::sqrt(sx * sx + sy * sy);
            if (hook ? l > 0.5f / 31.0f : l > kGameDead) {
                const float c = std::cos(yawNow), s = std::sin(yawNow);
                // view (x right, y ahead) -> room: right (c, -s), forward (-s, -c)
                x = sx * c + sy * -s;
                z = sx * -s + sy * -c;
            }
        }
        pending.push_back({at + lagS, x, z});
    }

    void Advance(double dt) {
        // the game: ticks at 25 Hz, the hero moves smoothly between them by the last read stick (lagS after the read)
        while (nextTick <= t) { GameTick(nextTick); nextTick += 0.04; heroShown = hero; }
        while (!pending.empty() && pending.front()[0] <= t) { gsx = (float)pending.front()[1]; gsy = (float)pending.front()[2]; pending.pop_front(); }
        const float l = std::sqrt(gsx * gsx + gsy * gsy);
        if (l > 1e-6f) {
            const float q = std::round(std::min(l, 1.0f) * 31.0f) / 31.0f;   // the packet's 5-bit strength
            const float sp = std::max(vmax * q, floorMps >= 0.0f ? floorMps : kGameFloor * vmax);   // its server move: at least 25 %
            hero.x += gsx / l * sp * (float)dt;
            hero.z += gsy / l * sp * (float)dt;
            if (hero.z < wallZ) hero.z = wallZ;
        }
    }

    void Step() {
        Input in;
        in.t = t;
        in.engaged = engaged;
        in.canWalk = canWalk;
        in.stickHeld = stickHeld;
        in.ownTilt = stickHeld ? std::min(1.0f, Len(ownStick)) : 0.0f;
        in.minTilt = hook ? kMinTiltHook : kMinTilt;
        in.epoch = epoch;
        Eyes(in.eyes, in.quat);
        const V2 h = tickShown ? heroShown : hero;
        if (started) in.heroMove = {h.x - heroPrev.x, h.z - heroPrev.z};
        heroPrev = h;
        started = true;
        o = w.Step(cfg, in);
        stickX = o.stickX; stickY = o.stickY;
        headYawAtStick = HeadYaw(in.quat);
        if (o.engaged) {
            cam = {h.x + in.eyes[0] - o.camX, h.z + in.eyes[2] - o.camZ};
            if (o.fresh) { cam0 = cam; eyes0 = {in.eyes[0], in.eyes[2]}; }
        }
        maxTilt = std::max(maxTilt, o.tilt);
        if (o.walking) ++walkingSteps;
        if (wasWalking && !o.walking) ++stops;
        if (wasWalking && o.walking && (o.stickX * prevSX + o.stickY * prevSY) < 0.0f) ++reversals;
        if (o.walking) { prevSX = o.stickX; prevSY = o.stickY; }
        wasWalking = o.walking;
        if (o.blocked) everBlocked = true;
        if (o.capped) everCapped = true;
        ++steps;
    }

    // Run for `secs` at 90 frames a second, the neck moving at (vx, vz) metres a second. Returns the largest gap between the camera and
    // where the head alone would have put it (its start plus the eyes' own travel).
    float Run(double secs, float vx = 0.0f, float vz = 0.0f) {
        const double dt = 1.0 / 90.0;
        float worst = 0.0f;
        for (double e = 0.0; e < secs - 1e-9; e += dt) {
            t += dt;
            neck.x += vx * (float)dt; neck.z += vz * (float)dt;
            Advance(dt);
            Step();
            float ey[3], q[4];
            Eyes(ey, q);
            const float ix = cam0.x + (ey[0] - eyes0.x), iz = cam0.z + (ey[2] - eyes0.z);
            worst = std::max(worst, std::hypot(cam.x - ix, cam.z - iz));
        }
        return worst;
    }
};

static Sim NewSim() {
    Sim s;
    s.cfg.on = true;
    s.Step();   // the first step: fresh, the hero under the head
    return s;
}

// -- the maths --------------------------------------------------------------------------------------------------------------------
static void TestMaths() {
    // HeadYaw is the native camera's atan2(-hf.x, -hf.z) of the head's forward
    for (float yaw : {0.0f, 0.5f, -1.2f, 2.9f, -3.0f}) {
        for (float pitch : {0.0f, -0.6f, 0.7f}) {
            float q[4];
            HeadQuat(yaw, pitch, q);
            CHECK(Near(HeadYaw(q), yaw, 1e-4f));
        }
    }
    // ToView: the head straight ahead - room -z is the stick's ahead, +x its right; turned 90 deg left, room -z is the head's right
    float sx, sy;
    ToView({0.0f, -1.0f}, 0.0f, &sx, &sy); CHECK(Near(sx, 0.0f) && Near(sy, 1.0f));
    ToView({1.0f, 0.0f}, 0.0f, &sx, &sy); CHECK(Near(sx, 1.0f) && Near(sy, 0.0f));
    ToView({0.0f, -1.0f}, kPi / 2, &sx, &sy); CHECK(Near(sx, 1.0f) && Near(sy, 0.0f));
    ToView({-1.0f, 0.0f}, kPi / 2, &sx, &sy); CHECK(Near(sx, 0.0f) && Near(sy, 1.0f));
    ToView({0.0f, 1.0f}, 0.0f, &sx, &sy); CHECK(Near(sx, 0.0f) && Near(sy, -1.0f));
    // WorldToRoom undoes the native camera's toWorld (a turn by the base, upm world units a metre)
    for (float base : {0.0f, 0.7f, -2.2f, 3.1f}) {
        const float cb = std::cos(base), sb = std::sin(base), upm = 37.5f;
        const V2 v{0.3f, -0.8f};
        const float wx = (v.x * cb + v.z * sb) * upm, wz = (-v.x * sb + v.z * cb) * upm;
        const V2 r = WorldToRoom(wx, wz, cb, sb, upm);
        CHECK(Near(r.x, v.x, 1e-4f) && Near(r.z, v.z, 1e-4f));
    }
    CHECK(Len(WorldToRoom(5.0f, 5.0f, 1.0f, 0.0f, 0.0f)) == 0.0f);   // no scale: nothing
    // the neck point does not move when the head turns or nods about it
    for (float yaw : {0.0f, 1.0f, 3.0f}) {
        for (float pitch : {0.0f, -1.2f, 0.8f}) {
            float q[4];
            HeadQuat(yaw, pitch, q);
            const float back[3] = {0.0f, 0.0f, kNeckM};
            float b[3];
            Rotate(q, back, b);
            const float eyes[3] = {0.4f - b[0], 1.6f - b[1], -0.2f - b[2]};
            const V2 n = NeckPoint(eyes, q);
            CHECK(Near(n.x, 0.4f, 1e-5f) && Near(n.z, -0.2f, 1e-5f));
        }
    }
}

// -- the walker ------------------------------------------------------------------------------------------------------------------
static void TestOffAndFresh() {
    Walker w;
    Config cfg;   // off
    Input in;
    in.engaged = true; in.canWalk = true;
    in.eyes[2] = -1.0f;
    Output o = w.Step(cfg, in);
    CHECK(!o.engaged && o.stickX == 0.0f && o.stickY == 0.0f);
    cfg.on = true;
    in.engaged = false;
    o = w.Step(cfg, in);
    CHECK(!o.engaged);
    // the first engaged step: the hero under the head, no stick, the camera as without the walk at the recentre's gaze
    in.engaged = true;
    in.eyes[0] = 0.0f; in.eyes[2] = 0.0f;
    o = w.Step(cfg, in);
    CHECK(o.engaged && o.fresh && !o.walking);
    CHECK(o.stickX == 0.0f && o.stickY == 0.0f);
    CHECK(Near(in.eyes[0] - o.camX, 0.0f, 1e-6f) && Near(in.eyes[2] - o.camZ, 0.0f, 1e-6f));
}

static void TestTurnNodLeanWalkNothing() {
    Sim s = NewSim();
    // turning round in place, twice, and nodding: the neck stays, the hero stays, the view is the head's alone
    const double dt = 1.0 / 90.0;
    for (int i = 0; i < 180; ++i) {
        s.t += dt;
        s.yaw = 2.0f * kPi * i / 90.0f;
        s.pitch = 0.9f * std::sin(i * 0.2f);
        s.Advance(dt);
        s.Step();
    }
    CHECK(s.walkingSteps == 0);
    CHECK(Near(s.hero.x, 0.0f, 1e-6f) && Near(s.hero.z, 0.0f, 1e-6f));
    // a lean of 8 cm (dead zone 12): nothing
    s.yaw = 0.0f; s.pitch = 0.0f;
    s.Run(0.5, 0.0f, -0.16f);
    CHECK(s.walkingSteps == 0);
    CHECK(Near(s.hero.z, 0.0f, 1e-6f));
    CHECK(s.o.offM < s.cfg.deadM);
}

static void TestWalkForward() {
    Sim s = NewSim();
    const float worst = s.Run(1.0, 0.0f, -1.0f);   // a metre forward in a second
    const float settle = s.Run(1.0);               // and stand
    CHECK(s.walkingSteps > 0);
    // the hero walked the metre forward (room -z: ahead) and stands under the head
    CHECK(s.hero.z < -0.85f && s.hero.z > -1.12f);
    CHECK(std::fabs(s.hero.x) < 0.01f);
    CHECK(s.o.offM < s.cfg.deadM);
    CHECK(!s.o.walking);
    // the view was the head's exactly the whole way (the hero's motion taken off): no jump, no pull
    CHECK(worst < 1e-4f);
    CHECK(settle < 1e-4f);
    // a slow walk is not a full stick
    CHECK(s.maxTilt >= kMinTilt && s.maxTilt < 0.95f);
    std::printf("  walk 1 m forward: hero at z %.3f, offset left %.3f m, most tilt %.2f, camera off the head's path %.6f m\n", s.hero.z, s.o.offM, s.maxTilt, worst);
}

static void TestSidewaysWithHeadTurned() {
    Sim s = NewSim();
    s.yaw = kPi / 2;   // looking left (room -x)
    s.Run(0.2);
    s.Run(1.0, 0.0f, -0.8f);   // step to the room's -z: the head's right
    s.Run(1.0);
    CHECK(s.hero.z < -0.65f && std::fabs(s.hero.x) < 0.02f);
    CHECK(s.o.offM < s.cfg.deadM);
    // and a step while walking: the stick pointed to the head's right (x > 0, y about 0)
    Sim r = NewSim();
    r.yaw = kPi / 2;
    r.Run(0.2);
    float sxMax = 0.0f, syAbs = 0.0f;
    for (int i = 0; i < 40; ++i) {
        r.Run(1.0 / 90.0, 0.0f, -0.8f);
        if (r.o.walking) { sxMax = std::max(sxMax, r.o.stickX); syAbs = std::max(syAbs, std::fabs(r.o.stickY)); }
    }
    CHECK(sxMax > 0.3f && syAbs < 0.1f);
}

static void TestFastStride() {
    Sim s = NewSim();
    s.Run(1.0, 0.0f, -2.0f);   // a brisk stride: 2 m/s
    CHECK(s.maxTilt > 0.75f);   // a stride near the hero's own full speed asks for (nearly) a full stick: the game runs if its run is on
    s.Run(1.5);
    CHECK(s.hero.z < -1.75f && s.hero.z > -2.2f);
    CHECK(s.o.offM < s.cfg.deadM);
}

static void TestWall() {
    Sim s = NewSim();
    s.wallZ = -0.4f;   // a wall 40 cm ahead of the hero
    float worstOff = 0.0f;
    const double dt = 1.0 / 90.0;
    for (int i = 0; i < 135; ++i) {   // 1.5 m forward at 1 m/s into it
        s.Run(dt, 0.0f, -1.0f);
        // the camera never farther from the hero than the cap (and the eyes' own lever)
        worstOff = std::max(worstOff, std::hypot(s.cam.x - s.hero.x, s.cam.z - s.hero.z));
    }
    CHECK(Near(s.hero.z, -0.4f, 1e-4f));
    CHECK(s.everBlocked);
    CHECK(s.everCapped);
    CHECK(s.o.offM <= s.cfg.capM + 1e-4f);
    CHECK(worstOff <= s.cfg.capM + 1e-3f);
    // back off: the hero comes back under the head, nothing jumps
    s.wallZ = -1e9f;
    s.Run(1.0, 0.0f, 0.6f);
    s.Run(1.0);
    CHECK(s.o.offM < s.cfg.deadM);
    CHECK(!s.o.blocked);
    std::printf("  into a wall: hero stopped at z %.2f, camera at most %.3f m off him (cap %.2f)\n", s.hero.z, worstOff, s.cfg.capM);
}

static void TestControllerStickWins() {
    Sim s = NewSim();
    // the controller walks the hero 1 m to the right; the head stands: no stick of ours, nothing credited - the camera goes with the hero
    s.stickHeld = true;
    s.ownStick = {1.0f, 0.0f};
    s.Run(0.4);
    CHECK(s.walkingSteps == 0);
    CHECK(s.hero.x > 0.8f);
    CHECK(Near(s.cam.x - s.cam0.x, s.hero.x, 0.01f));   // the view went with the hero
    // let go: the room walk does not pull the hero back (the head did not move: the offset is still where it was)
    s.stickHeld = false;
    s.ownStick = {};
    const float x = s.hero.x;
    s.Run(1.0);
    CHECK(std::fabs(s.hero.x - x) < 0.1f);
    CHECK(s.o.offM < s.cfg.deadM);
    // the head walked 0.5 m while the stick was held: once let go, the room walk closes it
    Sim r = NewSim();
    r.stickHeld = true;
    r.ownStick = {};
    r.Run(0.5, 0.0f, -1.0f);   // (held against a wall, say: the hero does not move)
    CHECK(r.walkingSteps == 0);
    r.stickHeld = false;
    r.Run(1.5);
    CHECK(r.hero.z < -0.38f);
    CHECK(r.o.offM < r.cfg.deadM);
}

static void TestTeleportRecentreGapMenu() {
    // a teleport (a skill, a waypoint) while the room walk drives: not credited, the camera goes with the hero
    Sim s = NewSim();
    s.Run(0.3, 0.0f, -1.0f);
    s.hero.x += 5.0f;
    s.Run(1.0 / 90.0, 0.0f, -1.0f);
    CHECK(s.cam.x - s.cam0.x > 4.9f);
    // a recentre: start fresh
    Sim r = NewSim();
    r.Run(0.3, 0.0f, -0.6f);
    r.epoch = 2;
    r.Run(1.0 / 90.0);
    CHECK(r.o.fresh && r.o.offM < 1e-4f && !r.o.walking);
    // a gap in the frames (a loading screen): start fresh
    Sim g = NewSim();
    g.Run(0.2, 0.0f, -1.0f);
    g.t += 2.5;
    g.Run(1.0 / 90.0);
    CHECK(g.o.fresh);
    // a hitch (no view built for 0.4 s while the head walked on): the place is kept, the view stays on the head's path, no re-anchor
    Sim h = NewSim();
    h.Run(1.0, 0.0f, -1.0f);
    h.neck.z -= 0.4f;
    h.t += 0.4;
    h.Advance(0.4);
    const float worstH = h.Run(1.0, 0.0f, -1.0f);
    CHECK(!h.o.fresh);
    CHECK(worstH < 1e-4f);
    h.Run(1.5);
    CHECK(!h.o.walking && h.o.offM < h.cfg.deadM);
    // a panel open: no stick, the place kept (engaged), the walk goes on when it closes
    Sim m = NewSim();
    m.canWalk = false;
    m.Run(0.5, 0.0f, -1.0f);
    CHECK(m.walkingSteps == 0 && m.o.engaged && !m.o.fresh);
    CHECK(m.o.stickX == 0.0f && m.o.stickY == 0.0f);
    CHECK(Near(m.hero.z, 0.0f, 1e-6f));
    m.canWalk = true;
    m.Run(1.5);
    CHECK(m.hero.z < -0.38f && m.o.offM < m.cfg.deadM);
    // not engaged any more (another view, the key off): nothing, then fresh again
    Sim e = NewSim();
    e.engaged = false;
    e.Run(0.1);
    CHECK(!e.o.engaged);
    e.engaged = true;
    e.Run(1.0 / 90.0);
    CHECK(e.o.fresh);
}

static void TestOvershootNoWalkBack() {
    // a game slow to stop (a fast hero: 6 m/s) - he overshoots; the walk back the other way waits (the refractory), no to and fro
    int reversals = 0;
    float lastY = 0.0f;
    Sim r = NewSim();
    r.vmax = 6.0f;
    for (int i = 0; i < 27 + 135; ++i) {
        r.Run(1.0 / 90.0, 0.0f, i < 27 ? -1.0f : 0.0f);
        if (r.o.walking) {
            if (lastY != 0.0f && (lastY > 0.0f) != (r.o.stickY > 0.0f)) ++reversals;
            lastY = r.o.stickY;
        }
    }
    CHECK(reversals == 0);
    CHECK(!r.o.walking);
    std::printf("  fast hero (6 m/s): offset left %.3f m after a 0.3 m step\n", r.o.offM);
}

static void TestMerge() {
    int16_t lx = 0, ly = 0;
    // the controller's stick pushed: it wins
    CHECK(!Merge(20000, 0, 0, 30000, &lx, &ly) && lx == 20000 && ly == 0);
    // the controller's stick at rest (or drifting under the game's dead zone): the room walk's
    CHECK(Merge(3000, -2000, 0, 30000, &lx, &ly) && lx == 0 && ly == 30000);
    CHECK(Merge(0, 0, -11000, 0, &lx, &ly) && lx == -11000 && ly == 0);
    // no room stick: the controller's as it is, drift and all
    CHECK(!Merge(3000, -2000, 0, 0, &lx, &ly) && lx == 3000 && ly == -2000);
    CHECK(Axis(1.0f) == 32767 && Axis(-1.0f) == -32767 && Axis(0.0f) == 0 && Axis(2.0f) == 32767);
    // roomFirst (the room walk walks the hero, the controller's push not taken over): the room walk's stick, zero too
    CHECK(Merge(11000, 0, 0, 30000, &lx, &ly, true) && lx == 0 && ly == 30000);
    CHECK(Merge(11000, 0, 0, 0, &lx, &ly, true) && lx == 0 && ly == 0);
    // the least stick is past the game's dead zone (it walks)
    const int16_t m = Axis(kMinTilt);
    CHECK((long long)m * m > 10349ll * 10349ll);
}

// The author's log (2026-10-18): a full stick is ~7 m/s in room metres at his hero's scale, so the pad's least tilt is already ~2.3 m/s.
// Through the walking-stick hook a walk faster than the game's least walk (a quarter of a full one) is one steady tilt: no stop and go
// once the speeds are learned - with the game's lag too.
static void TestSteadyWalkNoStopAndGo() {
    for (double lag : {0.0, 0.1}) {
        for (float vmax : {7.0f, 3.0f, 10.0f}) {
            const float head = std::max(1.0f, 0.36f * vmax);   // above the floor (0.25 vmax)
            Sim s = NewSim();
            s.hook = true;
            s.vmax = vmax;
            s.lagS = lag;
            s.Run(2.0, 0.0f, -head);   // the start and the learning
            const int stops0 = s.stops;
            const float z0 = s.hero.z;
            float offMax = 0.0f;
            for (int i = 0; i < 270; ++i) { s.Run(1.0 / 90.0, 0.0f, -head); offMax = std::max(offMax, s.o.offM); }
            const float speed = (z0 - s.hero.z) / 3.0f;
            CHECK(s.stops == stops0);           // walked the whole 3 s, never stopped
            CHECK(s.reversals == 0);
            CHECK(Near(speed, head, 0.08f * head));   // at the head's own speed
            CHECK(offMax < 0.2f);               // close under the head
            CHECK(std::fabs(s.o.fullMps - vmax) < 0.15f * vmax);   // the full-stick speed learned
            CHECK(s.o.tilt < 0.6f);
            std::printf("  steady walk %.1f m/s (hook, full stick %.0f m/s, lag %.1f s): hero %.2f m/s, stops %d, most offset %.2f m, tilt %.2f, learned %.2f m/s\n",
                        head, vmax, lag, speed, s.stops - stops0, offMax, s.o.tilt, s.o.fullMps);
            s.Run(1.5);
            CHECK(!s.o.walking && s.o.offM < s.cfg.deadM + 0.02f);
        }
    }
}

// 2026-10-22, "рывками" again: the game's server move never goes under a quarter of a full walk (0x351047: at least 25 %) - a room
// walk of 0.4 - 1.5 m/s at the author's scale (a full walk 7 m/s) is under it. The cut of 2026-10-18 took the hook's 2/31 for the
// least: the hero ran ahead at the floor, was stopped, waited a dead zone behind - 6 - 12 stops in 4 s, 0.24 - 0.36 m each way, and
// it learned a full stick of 9.5 m/s. Now the floor is known: short pulses at it, the hero within a start of the head each way, never
// walked back, the view the head's exactly, and the speeds learned right - with no lag and with a 0.1 s one.
static void TestUnderTheFloor() {
    for (double lag : {0.0, 0.1}) {
        for (float head : {0.4f, 0.7f, 1.0f, 1.5f}) {
            Sim s = NewSim();
            s.hook = true;
            s.vmax = 7.0f;   // the game's floor: 1.75 m/s
            s.lagS = lag;
            s.Run(2.0, 0.0f, -head);
            const int rev0 = s.reversals, stops0 = s.stops;
            const float z0 = s.hero.z;
            float behind = 0.0f, ahead = 0.0f;
            const float worst = s.Run(0.0);
            float view = worst;
            for (int i = 0; i < 360; ++i) {
                view = std::max(view, s.Run(1.0 / 90.0, 0.0f, -head));
                behind = std::max(behind, s.hero.z - s.neck.z);
                ahead = std::max(ahead, s.neck.z - s.hero.z);
            }
            const float speed = (z0 - s.hero.z) / 4.0f;
            CHECK(s.reversals == rev0);                        // never walked back at the head
            CHECK(behind < s.o.startM + 0.04f);                // he starts a start behind (the head's lag past it) ...
            CHECK(ahead < s.o.startM + 0.02f);                 // ... and stops no more than that past it
            CHECK(Near(speed, head, 0.06f));
            CHECK(view < 1e-4f);                               // the view is the head's, the hero's pulses taken off
            CHECK(s.o.floorLearned && Near(s.o.leastMps, 1.75f, 0.09f));   // his least walk seen
            CHECK(Near(s.o.fullMps, 7.0f, 0.7f));              // ... and the full-stick speed not taught by it
            CHECK(s.o.leadS > (float)lag && s.o.leadS < (float)lag + 0.06f);   // the lag and the tick's read
            std::printf("  under the floor, %.1f m/s (lag %.1f s): %d stops in 4 s, behind <= %.3f m, ahead <= %.3f m, start %.3f, least %.2f m/s, full %.2f m/s, lag %.3f s\n",
                        head, lag, s.stops - stops0, behind, ahead, s.o.startM, s.o.leastMps, s.o.fullMps, s.o.leadS);
            s.Run(2.0);
            CHECK(!s.o.walking && s.o.offM < s.o.startM + 0.02f);
        }
    }
}

// A client that shows the hero only where he is at each tick (no smoothing between): the same bounds, the view still the head's, the
// floor seen no lower than it is (a floor taken high only walks in pulses).
static void TestTickShownClient() {
    for (float dead : {0.12f, 0.0f}) {
        for (float head : {0.5f, 1.2f}) {
            Sim s = NewSim();
            s.hook = true;
            s.vmax = 7.0f;
            s.lagS = 0.1;
            s.tickShown = true;
            s.cfg.deadM = dead;
            s.Run(2.0, 0.0f, -head);
            const int rev0 = s.reversals;
            float behind = 0.0f, ahead = 0.0f, view = 0.0f;
            const float z0 = s.heroShown.z;
            for (int i = 0; i < 360; ++i) {
                view = std::max(view, s.Run(1.0 / 90.0, 0.0f, -head));
                behind = std::max(behind, s.heroShown.z - s.neck.z);
                ahead = std::max(ahead, s.neck.z - s.heroShown.z);
            }
            CHECK(s.reversals == rev0);
            CHECK(behind < s.o.startM + 0.05f && ahead < s.o.startM + 0.05f);
            CHECK(Near((z0 - s.heroShown.z) / 4.0f, head, 0.08f));
            CHECK(view < 1e-4f);
            CHECK(s.o.leastMps > 1.6f && s.o.leastMps < 2.2f);
            std::printf("  hero shown once a tick, %.1f m/s, dead zone %.2f: behind <= %.3f m, ahead <= %.3f m, least %.2f m/s\n", head, dead, behind, ahead, s.o.leastMps);
        }
    }
}

// The dead zone 0 ([openxr] roomscale_dead_zone_m = 0): the hero starts at the least offset he can be placed by (half his least walk's
// coast) - a step under the old dead zone walks him - and he never goes to and fro round a head standing or swaying.
static void TestDeadZoneZero() {
    for (double lag : {0.0, 0.1}) {
        Sim s = NewSim();
        s.hook = true;   // vmax 2.5: the floor 0.625 m/s
        s.lagS = lag;
        s.cfg.deadM = 0.0f;
        s.Run(2.0, 0.0f, -0.3f);   // a slow walk first: his least walk seen (until then a quarter of the guessed 7 m/s: a start of 9 cm)
        s.Run(1.5);
        CHECK(s.o.floorLearned && Near(s.o.leastMps, 0.625f, 0.065f) && s.o.startM > 0.0f && s.o.startM < 0.06f);
        // ten small steps of 5 cm, a second apart: after each he stands within a start of the head - with the old dead zone 0.12 he
        // stays where a step leaves him, up to 12 cm off
        Sim r = NewSim();
        r.hook = true;
        r.lagS = lag;
        r.Run(2.0, 0.0f, -0.3f);
        r.Run(1.5);
        float offZero = 0.0f, offOld = 0.0f, offZeroMax = 0.0f;
        for (int k = 0; k < 10; ++k) {
            s.Run(0.1, 0.0f, -0.5f);
            s.Run(1.0);
            r.Run(0.1, 0.0f, -0.5f);
            r.Run(1.0);
            offZero += s.o.offM / 10.0f;
            offOld += r.o.offM / 10.0f;
            offZeroMax = std::max(offZeroMax, s.o.offM);
            CHECK(!s.o.walking);
        }
        CHECK(offZeroMax < s.o.startM + 0.01f);
        CHECK(offZero < 0.5f * offOld);
        // swaying 1.5 cm each way for 6 s: nothing, or a walk that settles - never a to and fro
        const double dt = 1.0 / 90.0;
        int walks = 0;
        bool was = false;
        const float x0 = s.neck.x;
        for (int i = 0; i < 540; ++i) {
            s.t += dt;
            s.neck.x = x0 + 0.015f * std::sin((float)(i * dt) * kPi);
            s.Advance(dt);
            s.Step();
            if (s.o.walking && !was) ++walks;
            was = s.o.walking;
        }
        CHECK(walks <= 2);
        std::printf("  dead zone 0 (lag %.1f s): starts at %.3f m; ten 5 cm steps: off %.3f m after each on average (dead zone 0.12: %.3f m); "
                    "swaying 1.5 cm: %d walks in 6 s\n", lag, s.o.startM, offZero, offOld, walks);
        // a fast hero and the dead zone 0: the start is his least walk's, still no to and fro after a step
        Sim f = NewSim();
        f.hook = true;
        f.vmax = 7.0f;
        f.lagS = lag;
        f.cfg.deadM = 0.0f;
        f.Run(0.5, 0.0f, -0.6f);
        f.Run(1.0);
        const int fw0 = f.walkingSteps;
        f.Run(2.0);
        CHECK(f.walkingSteps == fw0);
        CHECK(f.o.startM > 0.05f && f.o.startM < 0.11f);
    }
    // a negative dead zone is 0
    Sim n = NewSim();
    n.cfg.deadM = -1.0f;
    n.Run(0.3, 0.0f, -0.6f);
    n.Run(1.0);
    CHECK(n.o.offM < n.o.startM + 0.01f && n.o.startM > 0.0f);
}

// Through the pad (no hook) the least tilt is faster than the walk: stop and go is all the game allows - but bounded, never backwards.
static void TestPadPathBounded() {
    Sim s = NewSim();
    s.vmax = 7.0f;
    float offMax = 0.0f, aheadMax = 0.0f;
    for (int i = 0; i < 360; ++i) {
        s.Run(1.0 / 90.0, 0.0f, -1.0f);
        offMax = std::max(offMax, s.o.offM);
        aheadMax = std::max(aheadMax, (s.neck.z - s.hero.z) > 0.0f ? (s.neck.z - s.hero.z) : 0.0f);   // the hero past the head (z ahead = -z)
    }
    CHECK(s.reversals == 0);
    CHECK(offMax < s.cfg.deadM + 0.12f);
    CHECK(aheadMax < 0.3f);
    std::printf("  pad path, 1 m/s, full stick 7 m/s: %d stops in 4 s (the game's least walk is %.1f m/s), most offset %.2f m, hero ahead at most %.2f m\n", s.stops,
                7.0f * kMinTilt, offMax, aheadMax);
    s.Run(1.5);
    CHECK(!s.o.walking && s.o.offM < s.cfg.deadM);
}

// A velocity floor in the game (its least speed above what is asked): the hero runs ahead - he waits, never walks back at the walking head.
static void TestFloorNeverBack() {
    Sim s = NewSim();
    s.hook = true;
    s.vmax = 7.0f;
    s.floorMps = 1.8f;
    float aheadMax = 0.0f;
    for (int i = 0; i < 360; ++i) {
        s.Run(1.0 / 90.0, 0.0f, -1.0f);
        aheadMax = std::max(aheadMax, (s.neck.z - s.hero.z) > 0.0f ? (s.neck.z - s.hero.z) : 0.0f);
    }
    CHECK(s.reversals == 0);
    CHECK(aheadMax < 0.3f);
    CHECK(s.hero.z < -3.3f);   // he kept up over the 4 m
    s.Run(1.5);
    CHECK(!s.o.walking && s.o.offM < s.cfg.deadM);
}

// The controller's stick teaches the full-stick speed too (a steady tilt), and a stick passing through its middle is still held.
static void TestStickLearnAndHold() {
    Sim s = NewSim();
    s.vmax = 6.0f;
    s.stickHeld = true;
    s.ownStick = {0.0f, -0.5f};
    s.Run(2.5);
    CHECK(std::fabs(s.o.fullMps - 6.0f) < 0.6f);
    // released for 30 ms in the middle of a push and pushed again: held throughout, no room walk in between
    Sim r = NewSim();
    r.Run(0.3, 0.0f, -1.0f);   // the head 0.3 m ahead
    r.stickHeld = true;
    r.ownStick = {};
    r.Run(0.2, 0.0f, -1.0f);
    r.stickHeld = false;
    const int w0 = r.walkingSteps;
    r.Run(0.03, 0.0f, -1.0f);
    CHECK(r.walkingSteps == w0);   // inside the release hold
    CHECK(r.o.stickHeld);
    r.stickHeld = true;
    r.Run(0.2, 0.0f, -1.0f);
    CHECK(r.walkingSteps == w0);
    r.stickHeld = false;
    r.Run(1.5);
    CHECK(!r.o.stickHeld && r.o.offM < r.cfg.deadM);   // let go for good: the room walk closes the rest
}

// 2026-10-22, the headset: "holds - the controller's stick walks" / "live" several times a second while walking the room, the stick
// just past the game's dead zone - a thumb resting on it. Each flip let the thumb walk the hero with the camera on him: a lurch. Now a
// light push while the room walk walks the hero takes over only when held kStickTakeS; until then the pad keeps the room walk's stick.
static void TestThumbOnTheStick() {
    Sim s = NewSim();
    s.hook = true;
    s.vmax = 7.0f;
    s.Run(2.0, 0.0f, -0.8f);
    const int rev0 = s.reversals;
    bool everHeld = false;
    float view = 0.0f;
    const float z0 = s.hero.z;
    for (int k = 0; k < 10; ++k) {   // a brush of 0.33 to the side for 80 ms every 0.4 s, walking on
        s.stickHeld = true;
        s.ownStick = {0.33f, 0.0f};
        for (int i = 0; i < 7; ++i) { view = std::max(view, s.Run(1.0 / 90.0, 0.0f, -0.8f)); everHeld = everHeld || s.o.stickHeld; }
        s.stickHeld = false;
        s.ownStick = {};
        for (int i = 0; i < 29; ++i) { view = std::max(view, s.Run(1.0 / 90.0, 0.0f, -0.8f)); everHeld = everHeld || s.o.stickHeld; }
    }
    CHECK(!everHeld);
    CHECK(view < 1e-4f);                 // the view the head's all along: the thumb never walked the hero
    CHECK(std::fabs(s.hero.x) < 0.02f);  // ... not a step to the side
    CHECK(s.reversals == rev0);
    CHECK(Near((z0 - s.hero.z) / 4.0f, 0.8f, 0.06f));
    // a push held on: taken over after kStickTakeS, the hero walks by it, the view with him
    s.stickHeld = true;
    s.ownStick = {0.33f, 0.0f};
    s.Run(0.08, 0.0f, -0.8f);
    CHECK(!s.o.stickHeld);
    s.Run(0.1, 0.0f, -0.8f);
    CHECK(s.o.stickHeld && !s.o.walking);
    const float x0 = s.hero.x;
    s.Run(0.5);
    CHECK(s.hero.x > x0 + 0.5f);
    // a hard push takes over at once
    Sim h = NewSim();
    h.hook = true;
    h.vmax = 7.0f;
    h.Run(1.0, 0.0f, -0.8f);
    h.stickHeld = true;
    h.ownStick = {0.9f, 0.0f};
    h.Run(1.0 / 90.0, 0.0f, -0.8f);
    CHECK(h.o.stickHeld);
    // the room walk at rest: a light push takes over at once (the stick walks as it always did)
    Sim r = NewSim();
    r.Run(1.0);
    r.stickHeld = true;
    r.ownStick = {0.33f, 0.0f};
    r.Run(1.0 / 90.0);
    CHECK(r.o.stickHeld);
    std::printf("  a thumb brushing the stick while walking the room: held %s, view off the head %.6f m; held on: taken after 0.12 s\n",
                everHeld ? "YES" : "never", view);
}

int main() {
    TestMaths();
    TestOffAndFresh();
    TestTurnNodLeanWalkNothing();
    TestWalkForward();
    TestSidewaysWithHeadTurned();
    TestFastStride();
    TestWall();
    TestControllerStickWins();
    TestTeleportRecentreGapMenu();
    TestOvershootNoWalkBack();
    TestMerge();
    TestSteadyWalkNoStopAndGo();
    TestUnderTheFloor();
    TestTickShownClient();
    TestDeadZoneZero();
    TestPadPathBounded();
    TestFloorNeverBack();
    TestStickLearnAndHold();
    TestThumbOnTheStick();
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("room_walk_test: all %d checks passed\n", g_checks);
    return 0;
}
