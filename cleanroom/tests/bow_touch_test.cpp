// The bow's touch point and debug vectors (vr/bow_touch.h) on synthetic numbers, without the game and without a headset:
// the frame conversions against the ones they invert, the choice between the model's string and the constant, and the
// vector set the effect draws.
//
//   bow_touch_test

#include "../../vr/bow_touch.h"

#include <cmath>
#include <cstdio>

using namespace bowdraw;
using namespace bowtouch;

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

bool Near(Vec a, Vec b, float tol = 1e-4f) { return Length(a - b) < tol; }

void TestFrames() {
    std::printf("frames\n");
    // the room and the hands' frame: the head turned 90 degrees to the left at (1, 1.7, 2)
    const float s2 = std::sqrt(0.5f);
    const Quat head{0, s2, 0, s2};
    const float yaw = HeadYaw(head);
    const Vec headPos{1.0f, 1.7f, 2.0f};
    // a point 0.4 m ahead of the head (the room's -X, it faces left) is 0.4 m ahead in the hands' frame (-Z)
    const Vec ahead = RoomToHands(headPos + Vec{-0.4f, 0, 0}, headPos, yaw);
    EXPECT(Near(ahead, {0, 0, -0.4f}), "ahead of the head: %.2f %.2f %.2f", ahead.x, ahead.y, ahead.z);
    // and 0.3 to its right (the room's -Z when it faces -X: right of a left-facing head is toward -Z) is +X
    const Vec right = RoomToHands(headPos + Vec{0, 0, -0.3f}, headPos, yaw);
    EXPECT(Near(right, {0.3f, 0, 0}), "right of the head: %.2f %.2f %.2f", right.x, right.y, right.z);
    // round trips
    for (float y : {0.0f, 0.7f, -2.4f, 3.0f}) {
        const Vec p{0.3f, -0.2f, 0.9f};
        EXPECT(Near(HandsToRoom(RoomToHands(p, headPos, y), headPos, y), p), "room round trip, yaw %.1f", y);
    }
    // the hands' frame and the model: the inverse of what the arms do to a controller
    const Vec eye{10.0f, 1.6f, -4.0f}, offset{0.01f, -0.02f, 0.03f};
    for (float a : {0.0f, 0.9f, -1.7f, 2.9f}) {
        const float c = std::cos(a), s = std::sin(a);
        const Vec hand{0.25f, -0.35f, -0.4f};
        const Vec m = HandsToModel(hand, offset, eye, 3.5f, c, s);
        EXPECT(Near(ModelToHands(m, offset, eye, 3.5f, c, s), hand, 1e-4f), "model round trip, yaw %.1f", a);
    }
    // the model's forward (+Z, no camera turn) is the hands' forward (-Z); its right is -X, the hands' +X
    EXPECT(Near(DirToModel({0, 0, -1}, 1, 0), {0, 0, 1}), "ahead is +Z in the model");
    EXPECT(Near(DirToModel({1, 0, 0}, 1, 0), {-1, 0, 0}), "right is -X in the model");
    // the camera turned 90 degrees: what was ahead (+Z) is now +X
    EXPECT(Near(DirToModel({0, 0, -1}, 0, 1), {1, 0, 0}), "ahead, camera turned a quarter");
    // up is up
    EXPECT(Near(DirToModel({0, 1, 0}, 0.3f, 0.9f), {0, 1, 0}), "up");
    // a hand where the head is has the model's eye
    EXPECT(Near(HandsToModel({0, 0, 0}, {}, eye, 3.0f, 1, 0), eye), "no offset, no hand: the eye");
}

void TestChoose() {
    std::printf("the touch point\n");
    const Vec bow{0, 1.2f, -0.3f}, fwd{0, 0, -1};
    NockChoice c = ChooseNock(bow, fwd, false, {});
    EXPECT(c.source == NockSource::Constant && Near(c.point, {0, 1.2f, -0.14f}), "no model: the constant, 16 cm behind");
    // the string read 22 cm behind the grip
    c = ChooseNock(bow, fwd, true, {0, 1.2f, -0.08f});
    EXPECT(c.source == NockSource::Model && Near(c.point, {0, 1.2f, -0.08f}) && std::fabs(c.modelDist - 0.22f) < 1e-4f, "the model's string is used: %.2f", c.modelDist);
    // off to the side (the model is turned from the controller): still the model's
    c = ChooseNock(bow, fwd, true, {0.15f, 1.2f, -0.2f});
    EXPECT(c.source == NockSource::Model, "a string beside the grip is the model's");
    // a misread: too near the grip, too far
    c = ChooseNock(bow, fwd, true, {0, 1.2f, -0.28f});
    EXPECT(c.source == NockSource::Constant && !c.modelSane, "2 cm from the grip is no string");
    c = ChooseNock(bow, fwd, true, {0, 1.2f, 0.5f});
    EXPECT(c.source == NockSource::Constant && !c.modelSane && c.modelDist > 0.79f, "80 cm is no string");
    c = ChooseNock(bow, fwd, true, {NAN, 0, 0});
    EXPECT(c.source == NockSource::Constant && Near(c.point, {0, 1.2f, -0.14f}), "NaN: the constant");
    // the bow pointing another way: the constant follows it
    c = ChooseNock(bow, Normalized({1, 0, 0}), false, {});
    EXPECT(Near(c.point, {-0.16f, 1.2f, -0.3f}), "the constant is behind along the bow's own axis");
}

void TestShift() {
    std::printf("the zone's shift along the bow\n");
    const Vec bow{0, 1.2f, -0.3f}, fwd{0, 0, -1};
    // the constant: + moves it away from the grip (further back, +Z here), - toward the grip
    NockChoice c = ChooseNock(bow, fwd, false, {}, 0.16f, 0.05f, 0.45f, 0.10f);
    EXPECT(Near(c.point, {0, 1.2f, -0.04f}), "constant +10 cm: 26 cm behind the grip: %.3f", c.point.z);
    c = ChooseNock(bow, fwd, false, {}, 0.16f, 0.05f, 0.45f, -0.10f);
    EXPECT(Near(c.point, {0, 1.2f, -0.24f}), "constant -10 cm: 6 cm behind the grip: %.3f", c.point.z);
    c = ChooseNock(bow, fwd, false, {}, 0.16f, 0.05f, 0.45f, 0.0f);
    EXPECT(Near(c.point, {0, 1.2f, -0.14f}), "0 = as it was");
    // the model's string: the same shift, the same direction (along the bow's axis, not along grip -> string)
    c = ChooseNock(bow, fwd, true, {0.05f, 1.2f, -0.08f}, 0.16f, 0.05f, 0.45f, 0.10f);
    EXPECT(c.source == NockSource::Model && Near(c.point, {0.05f, 1.2f, 0.02f}), "model +10 cm: %.3f %.3f %.3f", c.point.x, c.point.y, c.point.z);
    c = ChooseNock(bow, fwd, true, {0.05f, 1.2f, -0.08f}, 0.16f, 0.05f, 0.45f, -0.10f);
    EXPECT(Near(c.point, {0.05f, 1.2f, -0.18f}), "model -10 cm");
    // the sanity test is on the string as read: a shift never turns a misread into a string, nor a good one into a misread
    c = ChooseNock(bow, fwd, true, {0, 1.2f, -0.28f}, 0.16f, 0.05f, 0.45f, 0.20f);
    EXPECT(c.source == NockSource::Constant && !c.modelSane && Near(c.point, {0, 1.2f, 0.06f}), "a misread stays refused, the constant shifted");
    c = ChooseNock(bow, fwd, true, {0, 1.2f, 0.10f}, 0.16f, 0.05f, 0.45f, -0.25f);   // 40 cm behind: sane, however it is shifted
    EXPECT(c.source == NockSource::Model && c.modelSane, "a string read 40 cm behind the grip is used");
    // not finite: no shift
    c = ChooseNock(bow, fwd, false, {}, 0.16f, 0.05f, 0.45f, NAN);
    EXPECT(Near(c.point, {0, 1.2f, -0.14f}), "NaN shift is none");
    // the bow pointing another way: the shift follows the axis
    c = ChooseNock(bow, Normalized({1, 0, 0}), false, {}, 0.16f, 0.05f, 0.45f, 0.10f);
    EXPECT(Near(c.point, {-0.26f, 1.2f, -0.3f}), "along the bow's own axis");
}

Scene Base() {
    Scene s;
    s.bow = {0, 1.2f, -0.3f};
    s.forward = {0, 0, -1};
    s.nock = s.nockConst = {0, 1.2f, -0.14f};
    s.str = {0.5f, 1.0f, 0.2f};
    s.aim = Normalized(s.bow - s.str);
    return s;
}

void TestVectors() {
    std::printf("the debug vectors\n");
    Scene s = Base();
    VectorSet v = MakeVectors(s);
    // the touch ball first: red with the hand outside it
    EXPECT(v.nball >= 1 && v.ball[0].r == s.reach && Near(v.ball[0].c, s.nock), "the touch sphere is the touch radius at the nock point");
    EXPECT(v.ball[0].rgb[0] > 0.9f && v.ball[0].rgb[1] < 0.3f, "red outside");
    EXPECT(std::fabs(v.pull - Length(s.str - s.nock)) < 1e-5f, "the pull line's length: %.3f", v.pull);
    // inside: green
    s.atString = true;
    v = MakeVectors(s);
    EXPECT(v.ball[0].rgb[1] > 0.9f && v.ball[0].rgb[0] < 0.3f, "green inside");
    // drawing: greener fill and the aim line
    const int segsIdle = v.nseg;
    const float fillIdle = v.ball[0].fill;
    s.drawing = true;
    v = MakeVectors(s);
    EXPECT(v.nseg == segsIdle + 1, "the aim line joins while drawing: %d -> %d", segsIdle, v.nseg);
    EXPECT(v.ball[0].fill > fillIdle, "the sphere is tinted deeper once the string is taken");
    const Seg& aim = v.seg[v.nseg - 1];
    EXPECT(Near(aim.a, s.str) && Near(aim.b, s.str + s.aim * 1.5f), "the aim runs 1.5 m from the free hand along the line");
    // the bow's axis and the pull line
    EXPECT(Near(v.seg[0].a, s.bow) && Near(v.seg[0].b, s.bow + s.forward * 0.5f), "the bow's axis, 0.5 m");
    EXPECT(Near(v.seg[1].a, s.nock) && Near(v.seg[1].b, s.str), "the pull line, nock to hand");
    // the model's string is a ball of its own, only when read
    s.drawing = false;
    const int balls = MakeVectors(s).nball;
    s.haveModel = true;
    s.nockModel = {0.02f, 1.2f, -0.1f};
    v = MakeVectors(s);
    EXPECT(v.nball == balls + 1, "the model's string joins");
    EXPECT(Near(v.ball[v.nball - 1].c, s.nockModel) && v.ball[v.nball - 1].rgb[0] > 0.9f && v.ball[v.nball - 1].rgb[2] > 0.9f, "magenta, at the model's point");
    // never more than the effect can hold
    EXPECT(v.nseg <= kMaxSegs && v.nball <= kMaxBalls, "within the limits: %d segs, %d balls", v.nseg, v.nball);
    // the aim after a shot (not drawing): shown while it is live
    s.aimOwned = true;
    EXPECT(MakeVectors(s).nseg == 3, "the aim shows for the 0.4 s after a shot");
    // the bow model's own axis (the lime line): there when read, from the bow hand along it, and the four segments still fit
    s.drawing = true;
    const int segsNoAxis = MakeVectors(s).nseg;
    s.haveModelAxis = true;
    s.modelAxis = Normalized({0.1f, 0.05f, -1.0f});
    v = MakeVectors(s);
    EXPECT(v.nseg == segsNoAxis + 1 && v.nseg <= kMaxSegs, "the bow's real axis is one more segment: %d -> %d", segsNoAxis, v.nseg);
    const Seg& real = v.seg[v.nseg - 1];
    EXPECT(Near(real.a, s.bow) && Near(real.b, s.bow + s.modelAxis * 0.7f), "from the bow hand along the model's axis, 0.7 m (a little past the cyan line)");
    EXPECT(real.rgb[1] > 0.9f && real.rgb[0] > 0.4f && real.rgb[2] < 0.3f, "lime, not the cyan or blue of the others");
    s.haveModelAxis = false;
    EXPECT(MakeVectors(s).nseg == segsNoAxis, "not read, not drawn");
    // the arrow's rest point ([bow] arrow_rest_*): a small orange dot where it is; the effect holds six balls, so it takes the constant's
    // (cyan) place instead of joining - with everything else shown there are still six at most
    s.drawing = true;
    s.atString = true;
    s.pullNeed = 0.30f;
    s.haveModel = true;
    s.nockModel = {0.02f, 1.2f, -0.1f};
    const VectorSet without = MakeVectors(s);
    s.haveRest = true;
    s.rest = {0.0f, 1.23f, -0.3f};
    const VectorSet with = MakeVectors(s);
    EXPECT(without.nball == kMaxBalls && with.nball == kMaxBalls, "all six balls in use either way: %d %d", without.nball, with.nball);
    bool dot = false, cyan = false;
    for (int i = 0; i < with.nball; ++i) {
        const Ball& b = with.ball[i];
        if (Near(b.c, s.rest) && std::fabs(b.r - 0.012f) < 1e-6f && b.rgb[0] > 0.9f && b.rgb[1] > 0.4f && b.rgb[1] < 0.6f && b.rgb[2] < 0.1f && b.fill == 1.0f) dot = true;
        if (Near(b.c, s.nockConst) && b.rgb[2] > 0.9f && b.rgb[0] < 0.2f) cyan = true;
    }
    EXPECT(dot, "an orange dot at the rest point");
    EXPECT(!cyan, "the constant's cyan point gives way to it");
    bool cyanBefore = false;
    for (int i = 0; i < without.nball; ++i) if (Near(without.ball[i].c, s.nockConst) && without.ball[i].rgb[2] > 0.9f && without.ball[i].rgb[0] < 0.2f) cyanBefore = true;
    EXPECT(cyanBefore, "(and it is there when no rest point is)");
    s.haveRest = false;
    EXPECT(MakeVectors(s).nball == without.nball, "not made, not drawn");
}

void TestPullRing() {
    std::printf("the ring where the pull is enough\n");
    Scene s = Base();
    s.pullNeed = 0.30f;
    // far from the string, nothing taken: no ring (nothing to explain yet)
    VectorSet v = MakeVectors(s);
    const int plain = v.nball;
    // the string near: a ring round the BOW hand at the radius, red while the pull is short
    s.atString = true;
    v = MakeVectors(s);
    EXPECT(v.nball == plain + 1, "the ring joins when the string is near: %d -> %d", plain, v.nball);
    const Ball& ring = v.ball[v.nball - 1];
    EXPECT(Near(ring.c, s.bow) && std::fabs(ring.r - 0.30f) < 1e-6f && ring.fill == 0.0f, "round the bow hand at the radius, no tint");
    EXPECT(ring.rgb[0] > 0.9f && ring.rgb[1] < 0.4f, "red until the pull is enough");
    // enough: green
    s.pullEnough = true;
    v = MakeVectors(s);
    const Ball& ok = v.ball[v.nball - 1];
    EXPECT(ok.rgb[1] > 0.9f && ok.rgb[0] < 0.4f, "green once it is");
    // taken: still there
    s.atString = false;
    s.drawing = true;
    EXPECT(MakeVectors(s).nball == v.nball, "and while drawing");
    // nothing to draw without a radius
    s.pullNeed = 0.0f;
    EXPECT(MakeVectors(s).nball == v.nball - 1, "no radius, no ring");
    // everything at once still fits the effect's balls (touch, bow, free hand, constant, model, ring)
    s.pullNeed = 0.30f;
    s.haveModel = true;
    s.nockModel = {0.02f, 1.2f, -0.1f};
    v = MakeVectors(s);
    EXPECT(v.nball <= kMaxBalls && v.nball == 6 && v.nseg <= kMaxSegs, "all of it fits: %d balls, %d segs", v.nball, v.nseg);
    EXPECT(Near(v.ball[5].c, s.bow), "the ring is the last ball, not dropped by the limit");
}

// [debug] bow_aim_ray: the ray, clipped at the floor, and its place in the effect's four segments and six balls.
void TestAimRay() {
    std::puts("the arrow's ray");
    AimRay r;
    // forward and down from the bow hand's height (the head is the frame's origin, the floor 1.7 m below it): meets the floor
    const Vec start{-0.2f, -0.4f, -0.4f};
    EXPECT(MakeAimRay(start, {0.0f, -0.5f, -1.0f}, true, -1.7f, &r), "made");
    EXPECT(r.floor && r.fromModel && Near(r.a, start) && Near(r.b, {-0.2f, -1.7f, -3.0f}, 1e-3f), "the floor at %.3f %.3f %.3f (floor %d)", r.b.x, r.b.y, r.b.z, r.floor);
    // level: 8 m out, a ring
    EXPECT(MakeAimRay(start, {0.0f, 0.0f, -2.0f}, false, -1.7f, &r) && !r.floor && !r.fromModel && std::fabs(Length(r.b - start) - 8.0f) < 1e-4f && Near(r.b, {-0.2f, -0.4f, -8.4f}, 1e-3f), "level: 8 m out");
    // up, and a grazing descent (under 0.02 of a unit): no floor
    EXPECT(MakeAimRay(start, {0.0f, 1.0f, -1.0f}, false, -1.7f, &r) && !r.floor, "up");
    EXPECT(MakeAimRay(start, {0.0f, -0.01f, -1.0f}, false, -1.7f, &r) && !r.floor && std::fabs(Length(r.b - start) - 8.0f) < 1e-4f, "a grazing descent has no floor");
    // down, but the floor is farther than 8 m: 8 m, a ring
    EXPECT(MakeAimRay(start, {0.0f, -0.1f, -1.0f}, false, -1.7f, &r) && !r.floor && std::fabs(Length(r.b - start) - 8.0f) < 1e-4f, "the floor beyond 8 m is not drawn");
    // a hand below the floor (a squat): the floor is behind it
    EXPECT(MakeAimRay({0, -2.0f, 0}, {0.0f, -1.0f, -1.0f}, false, -1.7f, &r) && !r.floor, "below the floor: no floor hit");
    // a length of its own, a direction that is not a unit
    EXPECT(MakeAimRay(start, {0.0f, 0.0f, -5.0f}, false, -1.7f, &r, 3.0f) && std::fabs(Length(r.b - start) - 3.0f) < 1e-4f, "maxLen");
    // nonsense: refused, the output untouched
    AimRay keep = r;
    EXPECT(!MakeAimRay(start, {0, 0, 0}, false, -1.7f, &r) && !MakeAimRay(start, {NAN, 0, -1}, false, -1.7f, &r) && !MakeAimRay({NAN, 0, 0}, {0, 0, -1}, false, -1.7f, &r) &&
           !MakeAimRay(start, {0, 0, -1}, false, NAN, &r) && !MakeAimRay(start, {0, 0, -1}, false, -1.7f, nullptr) && Near(r.b, keep.b), "zero, NaN: no ray");

    // into an empty set: a segment and a ball, orange for the hand-set line, green for the model's; a full ball at the floor, a ring out in the air
    VectorSet v;
    MakeAimRay(start, {0.0f, -0.5f, -1.0f}, false, -1.7f, &r);
    EXPECT(AddAimRay(v, r) && v.nseg == 1 && v.nball == 1, "added");
    EXPECT(Near(v.seg[0].a, r.a) && Near(v.seg[0].b, r.b) && v.seg[0].rgb[0] == 1.0f && v.seg[0].rgb[1] == 0.45f && v.seg[0].rgb[2] == 0.05f, "orange, from the start to the end");
    EXPECT(Near(v.ball[0].c, r.b) && v.ball[0].r == 0.06f && v.ball[0].fill == 1.0f, "a full ball on the floor");
    VectorSet w;
    MakeAimRay(start, {0.0f, 0.0f, -1.0f}, true, -1.7f, &r);
    AddAimRay(w, r);
    EXPECT(w.seg[0].rgb[0] == 0.2f && w.seg[0].rgb[1] == 1.0f && w.seg[0].rgb[2] == 0.3f && w.ball[0].fill == 0.0f, "green from the model, a ring in the air");

    // into the fullest picture bow_vectors draws: four segments and six balls. The ray takes the blue aim line's place and the small comparison point's
    Scene s = Base();
    s.atString = true; s.drawing = true; s.haveModel = true; s.nockModel = s.nock; s.haveModelAxis = true; s.modelAxis = {0, 0, -1}; s.pullNeed = 0.3f;
    VectorSet full = MakeVectors(s);
    EXPECT(full.nseg == kMaxSegs && full.nball == kMaxBalls, "the fullest picture: %d segments, %d balls", full.nseg, full.nball);
    int aimLines = 0, minors = 0;
    for (int i = 0; i < full.nseg; ++i) aimLines += full.seg[i].tag == kTagAimLine;
    for (int i = 0; i < full.nball; ++i) minors += full.ball[i].tag == kTagMinorPoint;
    EXPECT(aimLines == 1 && minors == 1, "the aim line and the comparison point are tagged");
    MakeAimRay(start, {0.0f, -0.5f, -1.0f}, true, -1.7f, &r);
    EXPECT(AddAimRay(full, r) && full.nseg == kMaxSegs && full.nball == kMaxBalls, "the ray fits");
    aimLines = 0; minors = 0;
    for (int i = 0; i < full.nseg; ++i) aimLines += full.seg[i].tag == kTagAimLine;
    for (int i = 0; i < full.nball; ++i) minors += full.ball[i].tag == kTagMinorPoint;
    EXPECT(aimLines == 0 && minors == 0, "... in the place of the blue aim line and of the comparison point");
    EXPECT(Near(full.seg[full.nseg - 1].b, r.b) && Near(full.ball[full.nball - 1].c, r.b), "last in the lists");
    EXPECT(Near(full.seg[0].a, s.bow) && Near(full.seg[1].a, s.nock), "the rest keep their order (the bow's axis, the pull)");

    // nothing to give way: refused, the set as it was
    VectorSet crowded;
    for (int i = 0; i < kMaxSegs; ++i) { crowded.seg[i].a = {0, 0, (float)i}; crowded.seg[i].b = {1, 0, (float)i}; ++crowded.nseg; }
    for (int i = 0; i < kMaxBalls; ++i) { crowded.ball[i].c = {0, 1, (float)i}; crowded.ball[i].r = 0.01f; ++crowded.nball; }
    const VectorSet before = crowded;
    EXPECT(!AddAimRay(crowded, r) && crowded.nseg == before.nseg && crowded.nball == before.nball && Near(crowded.seg[2].a, before.seg[2].a), "a crowded picture keeps what it has");
}

}  // namespace

void TestHigherEnd() {
    std::printf("the limb that points up\n");
    // an upright bow: the upper limb's end is the higher; the same bow hung upside down (the rig's "upper" limb below the hand): the other end is
    const Vec top{0.0f, 0.3f, 0.0f}, bottom{0.0f, -0.3f, 0.0f};
    EXPECT(HigherEnd(top, bottom) == 0 && HigherEnd(bottom, top) == 1, "the higher end, whichever is asked first");
    EXPECT(HigherEnd(top, bottom, 1) == 0 && HigherEnd(bottom, top, 0) == 1, "clearly up: the earlier choice does not hold it");
    // a bow lying on its side, the ends within 3 cm of level: the earlier choice stays (no flipping with every tremor), and with none the higher
    const Vec left{-0.3f, 0.01f, 0.0f}, right{0.3f, -0.01f, 0.0f};
    EXPECT(HigherEnd(left, right) == 0 && HigherEnd(right, left) == 1, "level, no earlier choice: the slightly higher");
    EXPECT(HigherEnd(left, right, 1) == 1 && HigherEnd(left, right, 0) == 0, "level: the earlier choice stays");
    // tilted past the margin the other limb takes over
    EXPECT(HigherEnd({-0.3f, 0.04f, 0.0f}, {0.3f, -0.04f, 0.0f}, 1) == 0, "8 cm apart in height: it flips");
    EXPECT(HigherEnd(left, right, 5) == 0 && HigherEnd({NAN, 0, 0}, {0, 0, 0}) == 0, "nonsense in: still 0 or 1");
}

int main() {
    TestHigherEnd();
    TestAimRay();
    TestFrames();
    TestChoose();
    TestShift();
    TestVectors();
    TestPullRing();
    std::printf(g_fails ? "\n%d FAILED\n" : "\nall passed\n", g_fails);
    return g_fails ? 1 : 0;
}
