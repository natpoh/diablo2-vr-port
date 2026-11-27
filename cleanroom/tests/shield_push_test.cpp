// [weight] shield_collide_unheld (vr/shield_push.h): an orb or a claw - never the weight model's, so shield_collide alone let it through the hero's own shield
// (Serg's sorceress, 0.225.0: 'ob2 ' beside a 'sml ') - stops on the shield's face by its drawn hand. Here: a blade driven into a face stops on it (its hand
// held back, the controller gone on), a knock's speed is the approach; moved across the face while pressed on it, it slides along; pressed past 60 cm it goes
// through, and meets the face again once clear; a face swung onto a resting blade pushes it away; fast swings never tunnel (short moves); the frames' round
// trip; the log's teller (a line at most every 3 s while there is contact, one more after it ends); off (no face): the hand is the controller.
//
//   shield_push_test

#include "../../vr/shield_push.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

int g_fails = 0;
#define EXPECT(c, ...)                                         \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

using shieldcollide::Seg;
using shieldpush::Local;

// A flat face in the plane z = zFace (the hands' frame: z back, so -z is ahead), 50 x 40 cm, of horizontal capsules 1.9 cm thick 3 cm apart.
int Face(double zFace, double xShift, Seg* out) {
    int n = 0;
    for (double y = -0.18; y <= 0.181 && n < 24; y += 0.03) out[n++] = Seg{{-0.25 + xShift, y, zFace}, {0.25 + xShift, y, zFace}, 0.019};
    return n;
}
// An orb's blade on a hand at the origin with no turn: from the hand 30 cm straight ahead, 4.5 cm thick.
Local Orb() {
    const float at[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1};
    return shieldpush::ToLocal(Seg{{0, 0, 0}, {0, 0, -0.30}, 0.045}, at, q);
}

void RoundTrip() {
    const float q[4] = {0.2f, -0.4f, 0.1f, 0.0f};
    const double l = std::sqrt(0.2 * 0.2 + 0.4 * 0.4 + 0.1 * 0.1 + 0.0);
    float qn[4] = {(float)(q[0] / l), (float)(q[1] / l), (float)(q[2] / l), 0.0f};
    qn[3] = (float)std::sqrt(std::max(0.0, 1.0 - qn[0] * qn[0] - qn[1] * qn[1] - qn[2] * qn[2]));
    const float at[3] = {0.1f, -0.3f, -0.2f};
    const Seg s{{0.3, 0.1, -0.5}, {-0.2, 0.4, 0.1}, 0.03};
    const Local loc = shieldpush::ToLocal(s, at, qn);
    const double atD[3] = {at[0], at[1], at[2]};
    const Seg back = shieldpush::FromLocal(loc, atD, qn);
    double err = 0;
    for (int i = 0; i < 3; ++i) err = std::max({err, std::fabs(back.a[i] - s.a[i]), std::fabs(back.b[i] - s.b[i])});
    EXPECT(err < 1e-5, "round trip off by %.2e m", err);
    // a quarter turn about y: ahead (-z) turns to the left (-x)
    const float qy[4] = {0.0f, (float)std::sin(0.7853982), 0.0f, (float)std::cos(0.7853982)};
    const double v[3] = {0, 0, -1};
    double o[3];
    shieldpush::Rotate(qy, v, o);
    EXPECT(std::fabs(o[0] + 1.0) < 1e-5 && std::fabs(o[2]) < 1e-5, "a quarter turn about y took (0 0 -1) to (%.3f %.3f %.3f)", o[0], o[1], o[2]);
    std::printf("  frames: a capsule to a controller's frame and back within %.1e m; a quarter turn about y right\n", err);
}

void DrivenIn() {
    const Local w = Orb();
    Seg face[24];
    const int nf = Face(-0.40, 0.0, face);
    const float q[4] = {0, 0, 0, 1};
    shieldpush::Hand hand;
    const double dt = 1.0 / 90.0;
    int fresh = 0;
    double approach = 0, lag = 0, offZ = 0;
    bool touch = false;
    for (int i = 0; i <= 18; ++i) {   // 2 m/s straight ahead, 40 cm: the tip would end 30 cm into it
        const float h[3] = {0, 0, (float)(-0.40 * i / 18.0)};
        const shieldpush::Result r = hand.Step(&w, 1, h, q, face, nf, dt);
        if (r.fresh) { ++fresh; approach = r.approach; }
        lag = r.lag;
        offZ = r.off[2];
        touch = r.touch;
        EXPECT(r.gap > -0.005, "sample %d: the blade %.1f cm inside the face", i, -r.gap * 100.0);
    }
    // the tip's surface on the face's: tip z - 0.045 = -0.40 + 0.019 -> the hand at z -0.036; the controller at -0.40
    EXPECT(touch && fresh == 1, "on the face %d, touches %d", (int)touch, fresh);
    EXPECT(std::fabs(offZ - 0.364) < 0.01, "the hand %.1f cm behind its controller (36.4)", offZ * 100.0);
    EXPECT(approach > 1.5 && approach < 2.6, "the knock's speed %.2f m/s (2)", approach);
    EXPECT(!hand.Throughs(), "went through");
    std::printf("  an orb driven 40 cm ahead at 2 m/s into a face: stops on it, its hand %.1f cm behind the controller (lag %.1f), one touch at %.1f m/s\n", offZ * 100.0,
                lag * 100.0, approach);

    // pressed on it, moved 20 cm to the right: it slides along (the hand follows sideways), still held back
    double offX = 0, slideMax = 0;
    for (int i = 1; i <= 20; ++i) {
        const float h[3] = {(float)(0.01 * i), 0, -0.40f};
        const shieldpush::Result r = hand.Step(&w, 1, h, q, face, nf, dt);
        offX = r.off[0];
        offZ = r.off[2];
        touch = r.touch;
        slideMax = std::max(slideMax, r.slide);
    }
    EXPECT(std::fabs(offX) < 0.005 && touch && std::fabs(offZ - 0.364) < 0.01, "slid: x behind %.1f cm, z %.1f cm, touch %d", offX * 100.0, offZ * 100.0, (int)touch);
    EXPECT(slideMax > 0.5 && slideMax < 1.2, "slide speed %.2f m/s (0.9)", slideMax);
    std::printf("  moved 20 cm across while pressed on it: slides along (%.1f cm sideways behind, %.2f m/s along the face), held %.1f cm back\n", offX * 100.0, slideMax, offZ * 100.0);

    // pressed on: the hand gives way up to 60 cm, then the blade goes through; the hand back on its controller
    bool went = false;
    double most = 0;
    for (int i = 1; i <= 40 && !went; ++i) {
        const float h[3] = {0.2f, 0, (float)(-0.40 - 0.01 * i)};
        const shieldpush::Result r = hand.Step(&w, 1, h, q, face, nf, dt);
        most = std::max(most, r.lag);
        if (r.wentThrough) {
            went = true;
            EXPECT(r.off[0] == 0.0 && r.off[2] == 0.0 && r.through, "through but the hand %.1f cm off", r.off[2] * 100.0);
        }
    }
    EXPECT(went && most >= shieldcollide::kGiveM - 0.01 && most < shieldcollide::kGiveM + 0.02, "through %d at %.1f cm", (int)went, most * 100.0);
    // the controller 60 cm past the face's stop: the blade wholly beyond it, so clear at once - it meets the face again, from behind now (shieldcollide::Pass:
    // whichever side it is on); pulled back it stops on the back of the face, and pressed back 60 cm it goes through again
    bool cleared = false;
    {
        const float h[3] = {0.2f, 0, -0.64f};
        const shieldpush::Result r = hand.Step(&w, 1, h, q, face, nf, dt);
        cleared = r.cleared;
        EXPECT(r.off[2] == 0.0, "through: the hand %.1f cm off", r.off[2] * 100.0);
    }
    EXPECT(cleared, "never clear");
    double offBehind = 0;
    bool back = false;
    for (int i = 1; i <= 90 && !back; ++i) {
        const float h[3] = {0.2f, 0, (float)(-0.64 + 0.01 * i)};
        const shieldpush::Result r = hand.Step(&w, 1, h, q, face, nf, dt);
        if (r.wentThrough) back = true;
        else offBehind = std::min(offBehind, r.off[2]);
    }
    EXPECT(offBehind < -0.5 && back && hand.Throughs() == 2, "behind: held %.1f cm, through again %d (%u)", offBehind * 100.0, (int)back, hand.Throughs());
    std::printf("  pressed on: %.0f cm at most, then through (the hand on its controller); beyond it, pulled back it stops on the back of the face (%.0f cm) until "
                "pressed through again\n", most * 100.0, -offBehind * 100.0);
}

void NeverThrough() {   // [weight] shield_never_through: pressed on 1.22 m past the stop the blade stays on the face, the hand that far behind; then back with the controller
    const Local w = Orb();
    Seg face[24];
    const int nf = Face(-0.40, 0.0, face);
    const float q[4] = {0, 0, 0, 1};
    shieldpush::Hand hand;
    hand.SetNeverThrough(true);
    shieldpush::Result r;
    for (int i = 0; i <= 126; ++i) {   // 1 m/s to 1.26 m ahead
        const float h[3] = {0, 0, (float)(-0.01 * i)};
        r = hand.Step(&w, 1, h, q, face, nf, 1.0 / 90.0);
        EXPECT(!r.wentThrough && r.gap > -0.005, "sample %d: through %d, gap %.1f cm", i, (int)r.wentThrough, r.gap * 100.0);
    }
    EXPECT(r.touch && !r.through && hand.Throughs() == 0 && std::fabs(r.off[2] - 1.224) < 0.01, "held %.1f cm back (122.4), through %u", r.off[2] * 100.0, hand.Throughs());
    double off = r.off[2];
    for (int i = 0; i <= 140; ++i) {
        const float h[3] = {0, 0, (float)(-1.26 + 0.01 * i)};
        off = hand.Step(&w, 1, h, q, face, nf, 1.0 / 90.0).off[2];
    }
    EXPECT(std::fabs(off) < 1e-6, "back on its controller: %.1f cm", off * 100.0);
    std::printf("  shield_never_through: pressed 1.22 m past the stop, the orb stays on the face (the hand %.0f cm behind), never through; back with the controller\n", r.off[2] * 100.0);
}

void FastSwing() {   // 6 m/s across the face's plane in one sample (the hand 7 cm a step): never through
    const Local w = Orb();
    Seg face[24];
    const int nf = Face(-0.40, 0.0, face);
    const float q[4] = {0, 0, 0, 1};
    shieldpush::Hand hand;
    double worst = 1e9;
    for (int i = 0; i <= 6; ++i) {
        const float h[3] = {0, 0, (float)(0.0 - 0.067 * i)};
        const shieldpush::Result r = hand.Step(&w, 1, h, q, face, nf, 1.0 / 90.0);
        worst = std::min(worst, r.gap);
        EXPECT(r.off[2] >= 0.0, "pushed ahead %.1f cm", -r.off[2] * 100.0);
    }
    // one jump of 60 cm in a sample (a lost frame): still stopped on the near side
    shieldpush::Hand hand2;
    const float h0[3] = {0, 0, 0}, h1[3] = {0, 0, -0.55f};
    hand2.Step(&w, 1, h0, q, face, nf, 1.0 / 90.0);
    const shieldpush::Result r = hand2.Step(&w, 1, h1, q, face, nf, 1.0 / 90.0);
    EXPECT(r.touch && std::fabs(r.off[2] - 0.514) < 0.01, "a 55 cm jump: touch %d, the hand %.1f cm back (51.4)", (int)r.touch, r.off[2] * 100.0);
    EXPECT(worst > -0.005, "the blade %.1f cm inside", -worst * 100.0);
    std::printf("  a 6 m/s swing and a 55 cm jump in one sample: stopped on the near side (%.1f cm back), never inside\n", r.off[2] * 100.0);
}

void FaceOntoBlade() {   // the shield swung onto a resting orb: the hand pushed away; then the face away again: the hand back on its controller
    const Local w = Orb();
    const float q[4] = {0, 0, 0, 1}, h[3] = {0, 0, 0};
    shieldpush::Hand hand;
    Seg face[24];
    double off = 0;
    for (int i = 0; i <= 40; ++i) {
        const int nf = Face(-0.60 + 0.01 * i, 0.0, face);
        off = hand.Step(&w, 1, h, q, face, nf, 1.0 / 90.0).off[2];
    }
    EXPECT(std::fabs(off - 0.164) < 0.01, "pushed %.1f cm back (16.4)", off * 100.0);
    for (int i = 0; i <= 40; ++i) {
        const int nf = Face(-0.20 - 0.01 * i, 0.0, face);
        off = hand.Step(&w, 1, h, q, face, nf, 1.0 / 90.0).off[2];
    }
    EXPECT(std::fabs(off) < 1e-6, "not back: %.1f cm", off * 100.0);
    std::printf("  the shield swung 40 cm onto a resting orb: the hand pushed %.1f cm back, and back on its controller when it goes\n", 16.4);
}

void Off() {
    const Local w = Orb();
    const float q[4] = {0, 0, 0, 1}, h[3] = {0, 0, -0.5f};
    shieldpush::Hand hand;
    const shieldpush::Result r = hand.Step(&w, 1, h, q, nullptr, 0, 1.0 / 90.0);
    EXPECT(r.off[0] == 0.0 && r.off[1] == 0.0 && r.off[2] == 0.0 && !r.touch, "no face: off %.3f", r.off[2]);
    const float bad[4] = {0, 0, 0, 0};
    Seg face[24];
    const int nf = Face(-0.40, 0.0, face);
    const shieldpush::Result r2 = hand.Step(&w, 1, h, bad, face, nf, 1.0 / 90.0);
    EXPECT(r2.off[2] == 0.0, "a broken turn: off %.3f", r2.off[2]);
    std::printf("  no face, or a broken controller turn: the hand stays on its controller\n");
}

void Teller() {
    shieldpush::Teller t;
    shieldpush::Teller::Counts c;
    int lines = 0;
    uint64_t ms = 1000;
    for (int i = 0; i < 900; ++i, ms += 11) {   // 10 s of contact at 90 Hz
        const shieldpush::Tell s = (i / 100) % 2 ? shieldpush::Tell::Sliding : shieldpush::Tell::Resting;
        if (t.Step(s, i % 100 == 0, 0.2, 1.0, ms, &c)) ++lines;
    }
    EXPECT(lines == 4, "10 s of contact: %d lines (4: at once, 3, 6, 9 s)", lines);
    int after = 0;
    for (int i = 0; i < 900; ++i, ms += 11)
        if (t.Step(shieldpush::Tell::None, false, 0.0, 0.0, ms, &c)) ++after;
    EXPECT(after == 1, "after it ended: %d lines (1)", after);
    int idle = 0;
    for (int i = 0; i < 900; ++i, ms += 11)
        if (t.Step(shieldpush::Tell::None, false, 0.0, 0.0, ms, &c)) ++idle;
    EXPECT(idle == 0, "no contact: %d lines", idle);
    EXPECT(std::string(shieldpush::TellName(shieldpush::TellOf(true, false, 0.5))) == "sliding along it" &&
               std::string(shieldpush::TellName(shieldpush::TellOf(true, false, 0.0))) == "resting on it" &&
               std::string(shieldpush::TellName(shieldpush::TellOf(false, true, 0.0))) == "gone through",
           "names");
    std::printf("  the log: 10 s of contact told %d times, once more after it ended, never while idle\n", lines);
}

}  // namespace

int main() {
    std::printf("shield_push_test: an orb or a claw on the hero's own shield (shield_collide_unheld)\n");
    RoundTrip();
    DrivenIn();
    NeverThrough();
    FastSwing();
    FaceOntoBlade();
    Off();
    Teller();
    std::printf(g_fails ? "shield_push_test: %d FAILED\n" : "shield_push_test: all passed\n", g_fails);
    return g_fails ? 1 : 0;
}
