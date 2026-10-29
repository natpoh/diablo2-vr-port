// Offline checks of vr/walk_back.h ([body] walk_back_reverse): the game's own forward walk played backwards while the hero walks back - the planted foot
// stands on the ground, the swing lifts the heel BEHIND him (where the 0.185 mirror lifted the foot high in front: Serg's 2026-10-28 "the legs still cross"),
// the left foot stays left, and the cursor's jumps through the kept walk leave no seam - also when the stride's pace changes and when he starts from
// standing. Run: walk_back_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>
#include <vector>

#include "remote_pose.h"   // (the cone the walk back is crossfaded by)
#include "walk_back.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using walkback::M4;

static constexpr float kPi = 3.14159265f;
static bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// The game's forward walk, a foot against the hips (x across, y up, z along his forward), phi its place in the cycle: the stance from half a stride ahead
// to half behind on the ground, then the swing - the heel kicked up early, BEHIND the hips (lift at its most a quarter into the swing), set down ahead.
struct Foot { float along, lift; };
static Foot GameFoot(float phi) {
    phi = std::fmod(phi, 2.0f * kPi);
    if (phi < 0.0f) phi += 2.0f * kPi;
    if (phi < kPi) return {0.5f - phi / kPi, 0.0f};
    const float t = (phi - kPi) / kPi;
    return {-0.5f + t * t * (3.0f - 2.0f * t), std::sin(kPi * std::sqrt(t))};
}
// A foot bone: at (side, 0.9 + lift, along * stride), pitched by its lift (the toes down as the heel comes up) - a turn to check the play-back keeps.
static M4 FootBone(float side, Foot f, float stride) {
    const float a = -0.8f * f.lift, c = std::cos(a), s = std::sin(a);
    M4 m{};
    m.m[0] = 1.0f;
    m.m[5] = c; m.m[6] = s;
    m.m[9] = -s; m.m[10] = c;
    m.m[12] = side; m.m[13] = 0.9f + f.lift; m.m[14] = f.along * stride;
    m.m[15] = 1.0f;
    return m;
}

struct Run {
    int frames = 0, drawn = 0, jumps = 0;
    float worstPlanted = 0.0f;    // a planted foot's speed over the ground, units a second (he goes `speed`)
    float worstStep = 0.0f;       // the most a drawn foot moved in one frame
    float gameStep = 0.0f;        // ... and the game's own feet (the yardstick)
    float worstSide = 1e9f;       // the left foot's lead across over the right one
    float liftAlongSum = 0.0f;    // where the lifted foot is along, weighted by its lift (behind < 0)
    float liftSum = 0.0f;
    float worstTurn = 0.0f;       // the drawn foot's pitch against the one its height says (the bone's turn kept with its place)
};

// Walking forward foreS (kept, nothing drawn), standing standS, then walking back walkS: the game's forward walk at `pace` cycles a second (pace2 from
// half way), the body going back at the speed the stride gives. Each frame the game's pose is kept and the play-back drawn; measured from settleS into
// the walk back.
static Run Simulate(float foreS, float standS, float walkS, float settleS, float pace, float pace2, unsigned seed, bool flicker = false) {
    Run r;
    walkback::Reverser rev;
    const float stride = 3.0f;
    double t = 0.0;
    float phase = 0.0f, bodyZ = 0.0f;
    M4 wasDrawn[2]{}, wasGame[2]{};
    bool have = false, wasPlanted[2] = {false, false};
    float wasWorld[2] = {0.0f, 0.0f};
    unsigned rnd = seed;
    const double backT = foreS + standS;
    const double changeT = backT + walkS * 0.5;   // (the pace changes here: the game's poses kept from before it are of the slower walk - not measured)
    for (int f = 0; t < backT + walkS; ++f) {
        rnd = rnd * 1664525u + 1013904223u;
        const double dt = 1.0 / 90.0 * (0.85 + 0.3 * ((rnd >> 8) & 0xFFFF) / 65535.0);   // frame times that wander
        t += dt;
        const bool back = t > backT, walking = back || t <= foreS;
        const float p = t > changeT ? pace2 : pace;
        const float speed = 2.0f * stride * p;   // two steps a cycle, each a stride
        if (walking) phase += 2.0f * kPi * p * (float)dt;
        if (back) bodyZ -= speed * (float)dt;
        M4 game[2];
        float feet[6];
        for (int i = 0; i < 2; ++i) {
            const float side = i ? 1.0f : -1.0f;   // [0] right (-x), [1] left (+x)
            const Foot ft = walking ? GameFoot(phase + (i ? kPi : 0.0f)) : Foot{i ? 0.15f : -0.15f, 0.0f};
            game[i] = FootBone(side, ft, stride);
            feet[i * 3] = game[i].m[12]; feet[i * 3 + 1] = game[i].m[13]; feet[i * 3 + 2] = game[i].m[14];
        }
        rev.Record(t, walking && !(flicker && f % 3 == 0), game, 2, feet);   // (flicker: every third frame not taken as walking - Serg's first log)
        const walkback::Sample* s = rev.Step(dt, back);
        if (!s) { have = false; continue; }
        ++r.frames;
        if (t < backT + settleS) { have = false; for (int i = 0; i < 2; ++i) wasGame[i] = game[i]; continue; }   // (before it settles: not measured)
        ++r.drawn;
        const M4* d = s->pose.data();
        r.worstSide = std::min(r.worstSide, d[1].m[12] - d[0].m[12]);
        for (int i = 0; i < 2; ++i) {
            const float lift = d[i].m[13] - 0.9f;
            const bool planted = lift < 1e-4f;
            const float world = d[i].m[14] + bodyZ;
            if (have && planted && wasPlanted[i] && !(pace2 != pace && t > changeT && s->t < changeT + 0.1)) r.worstPlanted = std::max(r.worstPlanted, std::fabs(world - wasWorld[i]) / (float)dt);
            if (have) {
                r.worstStep = std::max(r.worstStep, std::hypot(d[i].m[13] - wasDrawn[i].m[13], d[i].m[14] - wasDrawn[i].m[14]));
                r.gameStep = std::max(r.gameStep, std::hypot(game[i].m[13] - wasGame[i].m[13], game[i].m[14] - wasGame[i].m[14]));
            }
            r.liftAlongSum += lift * d[i].m[14] / stride;
            r.liftSum += lift;
            r.worstTurn = std::max(r.worstTurn, std::fabs(std::atan2(d[i].m[6], d[i].m[5]) - (-0.8f * lift)));
            wasDrawn[i] = d[i];
            wasGame[i] = game[i];
            wasPlanted[i] = planted;
            wasWorld[i] = world;
        }
        have = true;
    }
    r.jumps = rev.TakeJumps();
    return r;
}

int main() {
    // the walk back at an even pace, then with the pace changing half way (walk to run) - his first walk, measured from its second cycle on (the store holds
    // a whole one) - and from standing after a walk (measured from its first frames: the walk kept from before the stop is played): the planted foot stands
    // (under 10 % of his speed - the forward walk turned with him slid it at twice his speed), the lifted foot is behind him at its highest (the heel kick, not
    // a knee raised in front), the left foot stays left, the bone's turn comes with its place, and the play-back has no seam: no drawn foot moves in a frame
    // much more than the game's own do (a jump through the kept walk lands on a pose where the feet are and go the same).
    // Case 3: as 2, but every third frame is not taken as walking (the speed or the mode dipping for a frame, as in Serg's 2026-10-28 log of the first
    // build: 108-114 jumps in 5 s, the legs dithering) - the stretch goes on through the dips and the jumps stay as few as without them.
    const float paces[4][2] = {{0.9f, 0.9f}, {0.8f, 1.3f}, {1.2f, 1.2f}, {1.0f, 1.0f}};
    for (int k = 0; k < 4; ++k) {
        const Run r = k >= 2 ? Simulate(3.0f, 1.0f, 6.0f, 0.1f, paces[k][0], paces[k][1], 12345u + (unsigned)k, k == 3)
                             : Simulate(0.0f, 0.0f, 6.0f, 1.5f, paces[k][0], paces[k][1], 12345u + (unsigned)k);
        const float speed = 2.0f * 3.0f * std::max(paces[k][0], paces[k][1]);
        std::printf("walk back %d: %d frames drawn, %d jumps; planted foot %.3f units/s (he goes %.1f), the most a foot moved in a frame %.3f (the game's %.3f), "
                    "the lifted foot along %.2f of a stride, the left foot's lead %.2f, foot turn off %.4f rad\n",
                    k, r.drawn, r.jumps, r.worstPlanted, speed, r.worstStep, r.gameStep, r.liftSum > 0.0f ? r.liftAlongSum / r.liftSum : 0.0f, r.worstSide,
                    r.worstTurn);
        CHECK(r.drawn > 300);
        CHECK(r.jumps >= 3);                         // it went through the kept walk and jumped back to its newest part, more than once
        CHECK(r.jumps * 45 < r.frames);              // ... but seldom: about once a second, not every few frames
        CHECK(r.worstPlanted < 0.1f * speed);        // a planted foot stands on the ground (a jump's landing at most a hair off it, for a frame)
        CHECK(r.worstStep < 1.6f * r.gameStep);      // no seam at the jumps
        CHECK(r.liftSum > 0.0f && r.liftAlongSum / r.liftSum < -0.1f);   // lifted behind him: a backward step, not a forward one mirrored
        CHECK(r.worstSide > 1.5f);                   // the left foot stays left
        CHECK(r.worstTurn < 2e-3f);                  // each drawn bone from the game's poses (its turn with its place; between two of them, blended)
    }

    // standing (the game's mode 1, town 5) with the rig's place reading a speed now and then, any way (Serg's 2026-10-28 17:00 log, the walk back on under a
    // hero standing still): with the game's mode it stays off; only by the speed (mode not known) it came on. Then walking back (mode 2) it comes on, and
    // the moment the game says he stands it is gone within 0.25 s (no hold).
    {
        unsigned rnd = 777u;
        float worstStanding = 0.0f, worstBySpeed = 0.0f;
        remotepose::ConeBlend byMode, bySpeed;
        const float dt = 1.0f / 90.0f;
        for (int f = 0; f < 900; ++f) {
            rnd = rnd * 1664525u + 1013904223u;
            const bool blip = ((rnd >> 20) & 3) == 0;   // a quarter of the frames read a speed
            const float angle = (float)((rnd >> 8) % 181);   // any way from his facing
            for (int k = 0; k < 2; ++k) {
                const int mode = k ? -1 : 1;
                if (walkback::GameStands(mode)) byMode.still = remotepose::ConeBlend::kHoldStandS + 1.0f;
                remotepose::ConeBlend& c = k ? bySpeed : byMode;
                const float w = c.Step(angle, blip && walkback::GameWalkMode(mode), 100.0f, dt);
                (k ? worstBySpeed : worstStanding) = std::max(k ? worstBySpeed : worstStanding, w);
            }
        }
        CHECK(worstStanding == 0.0f);
        CHECK(worstBySpeed > 0.5f);   // (as the first builds did it)
        remotepose::ConeBlend c;
        float w = 0.0f;
        for (int f = 0; f < 45; ++f) w = c.Step(170.0f, walkback::GameWalkMode(2), 100.0f, dt);
        CHECK(w > 0.99f);
        int frames = 0;
        for (; frames < 90 && w > 0.0f; ++frames) {
            if (walkback::GameStands(1)) c.still = remotepose::ConeBlend::kHoldStandS + 1.0f;
            w = c.Step(-1.0f, walkback::GameWalkMode(1), 100.0f, dt);
        }
        CHECK(w == 0.0f && frames <= 23);
        CHECK(walkback::GameWalkMode(2) && walkback::GameWalkMode(3) && walkback::GameWalkMode(6) && walkback::GameWalkMode(-1) && !walkback::GameWalkMode(1) &&
              !walkback::GameWalkMode(5) && !walkback::GameWalkMode(7) && !walkback::GameStands(-1) && walkback::GameStands(5));
    }

    // the mirror (0.185's legik::MirrorAlong) of the same walk lifts the foot IN FRONT: the cue that read as crossed legs
    {
        float sum = 0.0f, lifts = 0.0f;
        for (int i = 0; i < 360; ++i) {
            const Foot f = GameFoot(2.0f * kPi * i / 360.0f);
            sum += f.lift * -f.along;
            lifts += f.lift;
        }
        CHECK(sum / lifts > 0.1f);
    }

    // not walking back: nothing drawn, and the next start begins at the newest pose (the game's own of that moment - no jump in)
    {
        walkback::Reverser rev;
        M4 p[1]{};
        p[0].m[15] = 1.0f;
        const float feet[6] = {};
        for (int i = 0; i < 50; ++i) {
            p[0].m[12] = (float)i;
            rev.Record(i * 0.01, true, p, 1, feet);
        }
        CHECK(rev.Step(0.01, false) == nullptr);
        const walkback::Sample* s = rev.Step(0.01, true);
        CHECK(s && Near(s->pose[0].m[12], 49.0f, 1e-6f));
        p[0].m[12] = 50.0f;
        rev.Record(0.50, true, p, 1, feet);
        s = rev.Step(0.01, true);
        CHECK(s && Near(s->pose[0].m[12], 48.0f, 1e-6f));   // one back as the time went one on
        // standing (past kGraceS) is not kept, and ends the stretch; the walk after it is a stretch of its own
        rev.Record(0.51, false, p, 1, feet);
        rev.Record(0.80, false, p, 1, feet);
        CHECK(rev.Kept() == 51);
        rev.Record(0.90, true, p, 1, feet);
        CHECK(rev.Kept() == 52 && rev.At(51).seg == rev.At(50).seg + 1 && rev.At(51).dt == 0.0f && Near((float)rev.KeptS(), 0.50f, 1e-4f));
        // another rig's size: the store starts again
        M4 two[2]{};
        rev.Record(0.91, true, two, 2, feet);
        CHECK(rev.Kept() == 1);
        // an old time again: not kept
        rev.Record(0.40, true, two, 2, feet);
        CHECK(rev.Kept() == 1);
        // a dip under kGraceS: the stretch goes on, its poses kept; a stop longer than that: the poses since he last walked out again, the stretch over
        walkback::Reverser dip;
        for (int i = 0; i < 100; ++i) dip.Record(i * 0.01, !(i >= 40 && i < 60), p, 1, feet);   // 0.2 s not walking in the middle
        CHECK(dip.Kept() == 100 && dip.At(99).seg == dip.At(0).seg);
        for (int i = 100; i < 140; ++i) dip.Record(i * 0.01, false, p, 1, feet);                // 0.4 s: a stop
        CHECK(dip.Kept() == 100 && Near((float)dip.At(99).t, 0.99f, 1e-6f));
        dip.Record(1.40, true, p, 1, feet);
        CHECK(dip.Kept() == 101 && dip.At(100).seg == dip.At(99).seg + 1 && dip.At(100).t0 == 1.40);
        // only the newest kKeepS of walking is kept, by its walking time
        walkback::Reverser keep;
        for (int i = 0; i < 400; ++i) keep.Record(i * 0.01, (i / 100) % 2 == 0, p, 1, feet);   // walking, standing, walking, standing: 1 s each
        CHECK(keep.Kept() == 200 && Near((float)keep.KeptS(), 1.98f, 1e-3f));
        for (int i = 400; i < 700; ++i) keep.Record(i * 0.01, true, p, 1, feet);   // 3 s more: the newest 2.5 s of walking stays
        CHECK(Near((float)keep.KeptS(), (float)walkback::Reverser::kKeepS, 0.011f) && keep.At(0).t > 4.4);
    }

    // Blend: the ends, half of a quarter turn about y is an eighth, the place along the line; a mirrored bone (det < 0) comes back as it was
    {
        M4 a{}, b{};
        a.m[0] = a.m[5] = a.m[10] = a.m[15] = 1.0f;
        b.m[2] = -1.0f; b.m[5] = 1.0f; b.m[8] = 1.0f; b.m[15] = 1.0f;   // a quarter turn about y (rows: x -> -z, z -> x)
        b.m[12] = 2.0f; b.m[14] = 4.0f;
        const M4 h = walkback::Blend(a, b, 0.5f);
        const float e = std::cos(kPi / 4.0f);
        CHECK(Near(h.m[0], e, 1e-5f) && Near(h.m[2], -e, 1e-5f) && Near(h.m[8], e, 1e-5f) && Near(h.m[10], e, 1e-5f) && Near(h.m[5], 1.0f, 1e-5f));
        CHECK(Near(h.m[12], 1.0f, 1e-6f) && Near(h.m[14], 2.0f, 1e-6f));
        const M4 z = walkback::Blend(a, b, 0.0f), o = walkback::Blend(a, b, 1.0f);
        bool same = true;
        for (int i = 0; i < 16; ++i) same = same && z.m[i] == a.m[i] && o.m[i] == b.m[i];
        CHECK(same);
        M4 m{};   // mirrored across x, sized 2, turned a little about z
        const float c = std::cos(0.3f), s = std::sin(0.3f);
        m.m[0] = -2.0f * c; m.m[1] = -2.0f * s; m.m[4] = -2.0f * s; m.m[5] = 2.0f * c; m.m[10] = 2.0f; m.m[15] = 1.0f;
        float q[4], sz[3];
        walkback::Split(m, q, sz);
        M4 back{};
        walkback::Join(q, sz, back);
        bool round = true;
        for (int i = 0; i < 12; ++i) round = round && Near(back.m[i], m.m[i], 1e-5f);
        CHECK(round);
    }

    // Walking steadily back while the game's mode reads standing (1) in 56 % of the poses (Serg's 2026-10-29 18:13:07 window: 145 of 257, the walk back on
    // 100 poses and never wholly): dropped at every such pose (0.188.5) the share dithered and the forward walk was blended into the backward one; with
    // walkback::ModeGate it stays whole. A real stop (mode 1 held) still ends it within kStandS + the 0.2 s fade.
    {
        unsigned rnd = 1029u;
        const float dt = 1.0f / 90.0f;
        float worst[2] = {1.0f, 1.0f};
        for (int k = 0; k < 2; ++k) {
            remotepose::ConeBlend c;
            walkback::ModeGate gate;
            for (int f = 0; f < 360; ++f) {
                rnd = rnd * 1664525u + 1013904223u;
                const int mode = ((rnd >> 8) % 100) < 56 ? 1 : 2;
                const bool stands = k ? gate.Stands(mode, dt) : walkback::GameStands(mode);
                if (stands) c.still = remotepose::ConeBlend::kHoldStandS + 1.0f;
                const float w = c.Step(178.0f, !stands, 100.0f, dt);
                if (f >= 45) worst[k] = std::min(worst[k], w);
            }
        }
        std::printf("walking back, the mode reading standing in 56%% of poses: the share at least %.2f as 0.188.5, %.2f with the gate\n", worst[0], worst[1]);
        CHECK(worst[0] < 0.7f);    // (what the 18:13 test saw)
        CHECK(worst[1] > 0.999f);
        remotepose::ConeBlend c;
        walkback::ModeGate gate;
        float w = 0.0f;
        for (int f = 0; f < 45; ++f) w = c.Step(178.0f, !gate.Stands(2, dt), 100.0f, dt);
        int frames = 0;
        for (; frames < 120 && w > 0.0f; ++frames) {
            const bool stands = gate.Stands(1, dt);
            if (stands) c.still = remotepose::ConeBlend::kHoldStandS + 1.0f;
            w = c.Step(-1.0f, !stands, 100.0f, dt);
        }
        CHECK(w == 0.0f && frames <= 42);   // a held stand: off within 0.2 + 0.2 s (+ a frame)
    }

    // Walking 2 s, then running back 3 s (Serg's 2026-10-29 "running backwards he began to sway"): with the gait told, the play-back draws the run only
    // once 0.3 s of it is kept (a change of gait starts another stretch; jumps land on his gait); without (as before) the cursor went back from the run
    // into the walk kept before it while he ran. A third bone carries the gait (its height) to see which was drawn.
    for (int told = 0; told < 2; ++told) {
        walkback::Reverser rev;
        double t = 0.0;
        float phase = 0.0f;
        int walkDrawn = 0, drawn = 0;
        const double dt = 1.0 / 90.0;
        while (t < 5.0) {
            t += dt;
            const bool run = t > 2.0;
            const int gait = run ? 3 : 2;
            phase += 2.0f * kPi * (run ? 1.6f : 1.0f) * (float)dt;
            M4 game[3];
            float feet[6];
            for (int i = 0; i < 2; ++i) {
                game[i] = FootBone(i ? 1.0f : -1.0f, GameFoot(phase + (i ? kPi : 0.0f)), run ? 4.0f : 3.0f);
                for (int j = 0; j < 3; ++j) feet[i * 3 + j] = game[i].m[12 + j];
            }
            game[2] = M4{};
            game[2].m[0] = game[2].m[5] = game[2].m[10] = game[2].m[15] = 1.0f;
            game[2].m[13] = (float)gait;
            rev.Record(t, true, game, 3, feet, told ? gait : 0);
            const walkback::Sample* s = rev.Step(dt, run, told ? gait : -1);
            if (!s || t < 2.3) continue;
            ++drawn;
            if (s->pose[2].m[13] < 2.99f) ++walkDrawn;
        }
        std::printf("running back after a walk, the gait %s: %d of %d frames drawn from the walk\n", told ? "told" : "not told (as before)", walkDrawn, drawn);
        CHECK(drawn > 200);
        if (told) CHECK(walkDrawn == 0);
        else CHECK(walkDrawn > 0);
    }

    // TurnAbout / YawOf: a bone turned about a point keeps its height and its distance from the point, its yaw goes round by the turn, and the turn back
    // gives it again.
    {
        M4 b = FootBone(1.0f, Foot{0.3f, 0.2f}, 3.0f);
        const M4 b0 = b;
        const float y0 = walkback::YawOf(b);
        walkback::TurnAbout(b, 0.7f, 0.5f, -0.2f);
        CHECK(Near(walkback::WrapAngle(walkback::YawOf(b) - y0), 0.7f, 1e-4f));
        CHECK(Near(b.m[13], b0.m[13], 1e-6f));
        CHECK(Near(std::hypot(b.m[12] - 0.5f, b.m[14] + 0.2f), std::hypot(b0.m[12] - 0.5f, b0.m[14] + 0.2f), 1e-5f));
        walkback::TurnAbout(b, -0.7f, 0.5f, -0.2f);
        float worst = 0.0f;
        for (int i = 0; i < 16; ++i) worst = std::max(worst, std::fabs(b.m[i] - b0.m[i]));
        CHECK(worst < 1e-5f);
        CHECK(Near(walkback::WrapAngle(3.5f), 3.5f - 2.0f * kPi, 1e-5f));
        CHECK(Near(walkback::WrapAngle(-3.5f), 2.0f * kPi - 3.5f, 1e-5f));
    }
    // The change of way as the game makes it (Serg's 2026-10-28 17:03 log): his matrix turns half a turn at once and the skeleton's anim bone starts half a
    // turn back and comes round over 0.15 s - for that moment the game's legs face the way he goes. Walking forward 2 s, then back 3 s, the walk back on
    // 0.1 s after the change (the cone reads the smoothed travel); drawn: the game's pose while it is off, the play-back after. With the turn taken out
    // (WalkBackLegs since the 17:03 log) the left foot stays left in every frame; without it (as before) the legs stood crossed at each change of way.
    for (int out = 0; out < 2; ++out) {
        walkback::Reverser rev;
        const float stride = 3.0f, pace = 1.0f;
        double t = 0.0;
        float phase = 0.0f;
        int crossed = 0, frames = 0;
        for (; t < 5.0;) {
            const double dt = 1.0 / 90.0;
            t += dt;
            phase += 2.0f * kPi * pace * (float)dt;   // the game walks him forward all the while (back: turned round, the same walk)
            const double since = t - 2.0;
            const float turn = since >= 0.0 && since < 0.15 ? kPi * (1.0f - (float)(since / 0.15)) : 0.0f;
            M4 game[2];
            float feet[6];
            for (int i = 0; i < 2; ++i) {
                game[i] = FootBone(i ? 1.0f : -1.0f, GameFoot(phase + (i ? kPi : 0.0f)), stride);
                if (turn != 0.0f) walkback::TurnAbout(game[i], turn, 0.0f, 0.0f);       // the game's turn in the skeleton
                if (out && turn != 0.0f) walkback::TurnAbout(game[i], -turn, 0.0f, 0.0f);  // ... taken out
                for (int j = 0; j < 3; ++j) feet[i * 3 + j] = game[i].m[12 + j];
            }
            rev.Record(t, true, game, 2, feet);
            const walkback::Sample* s = rev.Step(dt, since >= 0.1);
            const M4* d = s ? s->pose.data() : game;
            if (since >= 0.0) {
                ++frames;
                if (d[1].m[12] - d[0].m[12] < 0.5f) ++crossed;   // the left foot not to the left of the right one
            }
        }
        std::printf("change of way, the game's turn %s: %d of %d frames with the left foot not left\n", out ? "taken out" : "kept (as before)", crossed, frames);
        CHECK(frames > 200);
        if (out) CHECK(crossed == 0);
        else CHECK(crossed > 0);   // (the picture of the 17:03 test: what the turn-out is for)
    }
    std::printf("walk_back_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
