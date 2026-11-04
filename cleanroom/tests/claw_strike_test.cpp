// claw_strike_test.cpp - [shapeshift] claw_hits (vr/claw_strike.h): a form's paw made from its bones, and the two paws as one weapon -
// each paw its own strokes, one shared recovery. Run: claw_strike_test.exe
#include "claw_strike.h"
#include "strike_direction.h"   // strikedir::Effective: how the paws' least speed meets melee_min_speed
#include <cmath>
#include <cstdio>
#include <string_view>
using namespace clawstrike;
static int g_fail = 0, g_pass = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++g_fail; } else ++g_pass; } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

int main() {
    {   // A paw from its bones: the farthest bone under the wrist is the tip, the spread about that line the radius.
        const float wrist[3] = {0.0f, 0.0f, 0.0f};
        const float ends[] = {0.0f, 0.0f, -0.10f,    // a knuckle on the line
                              0.06f, 0.0f, -0.18f,   // a claw 6 cm to the side
                              0.0f, 0.0f, -0.25f};   // the middle claw's tip: the farthest
        const Paw p = PawFrom(wrist, ends, 3);
        CHECK(p.ok);
        CHECK(Near(p.length, 0.25f));
        CHECK(Near(p.dir[2], -1.0f) && Near(p.dir[0], 0.0f));
        CHECK(Near(p.radius, 0.06f));
    }
    {   // Bounds: a slim paw is never thinner than a fist, a broad one never thicker than kMaxRadius, a long one capped.
        const float wrist[3] = {1.0f, 2.0f, 3.0f};
        const float slim[] = {1.0f, 2.2f, 3.0f};
        const Paw a = PawFrom(wrist, slim, 1);
        CHECK(a.ok && Near(a.radius, kMinRadius) && Near(a.length, 0.2f) && Near(a.dir[1], 1.0f));
        const float broad[] = {1.0f, 2.3f, 3.0f, 1.4f, 2.1f, 3.0f};
        CHECK(Near(PawFrom(wrist, broad, 2).radius, kMaxRadius));
        const float longOne[] = {1.0f, 3.0f, 3.0f};
        CHECK(Near(PawFrom(wrist, longOne, 1).length, kMaxLength));
    }
    {   // No paw: no bones under the hand, all of them on the wrist, or not numbers.
        const float wrist[3] = {0.0f, 0.0f, 0.0f};
        CHECK(!PawFrom(wrist, nullptr, 0).ok);
        const float onWrist[] = {0.01f, 0.0f, 0.0f, 0.0f, 0.02f, 0.0f};
        CHECK(!PawFrom(wrist, onWrist, 2).ok);
        const float nan[] = {NAN, 0.0f, 0.0f};
        CHECK(!PawFrom(wrist, nan, 1).ok);
    }
    {   // The paw's grip point kept in the controller's frame: back out the same however it was turned, and turned with the controller after.
        const float h = std::sqrt(0.5f);
        const float q[4] = {0.0f, h, 0.0f, h};   // 90 deg about +y
        const float d[3] = {0.0f, -0.02f, -0.09f};   // 9 cm ahead, 2 cm down
        float l[3], back[3];
        Turn(q, d, true, l);
        Turn(q, l, false, back);
        CHECK(Near(back[0], d[0]) && Near(back[1], d[1]) && Near(back[2], d[2]));
        CHECK(Near(l[0], 0.09f) && Near(l[1], -0.02f) && Near(l[2], 0.0f));   // (ahead of a controller turned 90 deg left lies along its own +x)
        const float id[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float same[3];
        Turn(id, d, false, same);
        CHECK(Near(same[0], d[0]) && Near(same[1], d[1]) && Near(same[2], d[2]));
    }
    {   // [shapeshift] wolf_min_speed / bear_min_speed: each form its own least speed; 0, under the floor, not a number or a person = none of its own.
        CHECK(Near(FormMinSpeed(1, 1.0f, 1.5f), 1.0f) && Near(FormMinSpeed(2, 1.0f, 1.5f), 1.5f));
        CHECK(FormMinSpeed(0, 1.0f, 1.5f) == 0.0f && FormMinSpeed(3, 1.0f, 1.5f) == 0.0f);
        CHECK(FormMinSpeed(1, 0.0f, 1.5f) == 0.0f && FormMinSpeed(2, 1.0f, 0.29f) == 0.0f && FormMinSpeed(1, NAN, 1.0f) == 0.0f && FormMinSpeed(2, 1.0f, -3.0f) == 0.0f);
        CHECK(Near(FormMinSpeed(2, 1.0f, 99.0f), kFormMinSpeedCeil) && Near(FormMinSpeed(1, kFormMinSpeedFloor, 0.0f), kFormMinSpeedFloor));
        // Against the general melee_min_speed (Serg's 6 m/s) through the engine's own rule (strikedir::Effective): the form's 1 m/s wins; none of its own -
        // the unarmed kind's where set, else the general, exactly as before the keys.
        const stroke::StrokeTuning general{6.0f, 15.0f};
        auto paw = [&](int form, float wolf, float bear, float unarmed) {
            strikedir::KindTuning own{unarmed, 0.0f};
            own.minSpeed = PawOwnSpeed(form, wolf, bear, own.minSpeed);
            return strikedir::Effective(general, own);
        };
        CHECK(Near(paw(1, 1.0f, 1.0f, 0.0f).minSpeed, 1.0f) && Near(paw(2, 1.0f, 1.2f, 0.0f).minSpeed, 1.2f));
        CHECK(Near(paw(2, 1.0f, 1.2f, 3.0f).minSpeed, 1.2f));          // (the form's own over the unarmed kind's)
        CHECK(Near(paw(1, 0.0f, 0.0f, 3.0f).minSpeed, 3.0f));          // (no form's own: the unarmed kind's)
        CHECK(Near(paw(2, 0.0f, 0.0f, 0.0f).minSpeed, 6.0f));          // (nothing of its own: the general, as before)
        CHECK(Near(paw(0, 1.0f, 1.0f, 0.0f).minSpeed, 6.0f));          // (a person's hands: never the forms' numbers)
        CHECK(Near(paw(1, 1.0f, 1.0f, 0.0f).startCm, 15.0f));          // (the stroke's start stays melee_stroke_start_cm)
    }
    {   // The recovery is the hero's own attack time, never under the floor.
        CHECK(RecoveryMs(600) == 600 && RecoveryMs(420) == 420 && RecoveryMs(40) == kMinRecoveryMs && RecoveryMs(0) == kMinRecoveryMs);
    }
    const float least = 2.0f;
    const uint64_t rec = 500;
    {   // Each paw its own lane; both share ONE recovery: the left paw right after the right one waits, as the right one again does.
        Lanes l;
        const Contact r1{0, 11, 100, 3.0f, 1000};
        CHECK(l.Judge(r1, least, rec) == Why::Counts);
        l.Struck(r1);
        CHECK(l.LastPaw() == 0 && l.LastAt() == 1000);
        uint64_t since = 0;
        const Contact l1{1, 21, 101, 4.0f, 1200};   // the other paw, another monster, 200 ms later
        CHECK(l.Judge(l1, least, rec, &since) == Why::Recovering && since == 200);
        const Contact r2{0, 12, 102, 4.0f, 1300};   // the same paw's next stroke
        CHECK(l.Judge(r2, least, rec) == Why::Recovering);
        const Contact l2{1, 21, 101, 4.0f, 1500};   // the recovery over: the left paw's same stroke (a refused contact did not use the monster up)
        CHECK(l.Judge(l2, least, rec) == Why::Counts);
        l.Struck(l2);
        CHECK(l.LastPaw() == 1);
        CHECK(l.Judge({0, 12, 102, 4.0f, 1999}, least, rec) == Why::Recovering);
        CHECK(l.Judge({0, 12, 102, 4.0f, 2000}, least, rec) == Why::Counts);
    }
    {   // Two paws alternating as fast as they can: never more attacks than one weapon's - one each recovery.
        Lanes l;
        int attacks = 0;
        for (uint64_t t = 0; t < 10000; t += 50) {   // a paw every 50 ms, alternating, each a new stroke
            const int paw = static_cast<int>((t / 50) & 1);
            const Contact c{paw, t / 50 + 1, 7, 5.0f, t + 1};
            if (l.Judge(c, least, rec) == Why::Counts) { l.Struck(c); ++attacks; }
        }
        CHECK(attacks == 20);   // 10 s / 500 ms
    }
    {   // One stroke strikes a monster once; a slow paw is refused whatever the recovery; the stroke may strike another monster after the recovery.
        Lanes l;
        const Contact c{0, 5, 9, 3.0f, 100};
        l.Struck(c);
        CHECK(l.Judge({0, 5, 9, 9.0f, 5000}, least, rec) == Why::Struck);
        CHECK(l.Judge({1, 5, 9, 9.0f, 5000}, least, rec) == Why::Counts);   // the other paw's lane is its own
        CHECK(l.Judge({0, 5, 10, 9.0f, 5000}, least, rec) == Why::Counts);
        CHECK(l.Judge({0, 6, 9, 1.9f, 5000}, least, rec) == Why::Slow);
        CHECK(l.Judge({0, 6, 9, NAN, 5000}, least, rec) == Why::Slow);
        CHECK(l.Judge({0, 6, 9, 1.9f, 200}, least, rec) == Why::Slow);   // (slow is told before recovering)
    }
    {   // Reset: another game, a shift back - nothing carries over.
        Lanes l;
        l.Struck({0, 1, 1, 3.0f, 100});
        l.Reset();
        CHECK(l.Judge({0, 1, 1, 3.0f, 150}, least, rec) == Why::Counts && l.LastPaw() == -1 && l.LastAt() == 0);
    }
    {   // A clock that went back (another session's numbers): no recovery is read from it.
        Lanes l;
        l.Struck({0, 1, 1, 3.0f, 5000});
        CHECK(l.Judge({1, 2, 2, 3.0f, 4000}, least, rec) == Why::Counts);
        CHECK(std::string_view(WhyText(Why::Recovering)).size() > 0 && std::string_view(WhyText(Why::Counts)) == "struck");
    }
    std::printf("claw_strike_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
