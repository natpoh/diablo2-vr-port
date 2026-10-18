#pragma once
// bow_touch.h - where the string is, and what to draw about it: pure helpers for the bow draw (bow_draw.h). No game
// calls, no globals; cleanroom/tests/bow_touch_test.cpp drives them with synthetic numbers.
//
// 1. The touch point. The draw takes the string at a point 16 cm behind the bow hand along the bow (bowdraw::Params::
//    nockBehind) - a constant. The bow in the hero's hand is a model whose string sits wherever its bind pose puts it, at
//    the model's scale, turned the way [weapon_bow] turns it; the constant can be a hand's breadth off. skeletons.cpp reads
//    the string's rest place off the posed bow (the bowString bone, vr/bow_rig.h) and hands it over in the frame of the
//    hands (D2RVR_Shared's: metres from the head, in the head's turn frame, x right y up z back); ChooseNock takes it when
//    it is plausible, the constant when it is not.
// 2. The frames. The detector works in whatever frame the hands come in (direct_input.h: the room); the skeleton works in
//    the hero's model space. The conversions between the room, the hands' frame and the model are here, so each can be
//    tested against the one it inverts.
// 3. The debug vectors ([debug] bow_vectors): the bow's axis, the touch sphere (green with the free hand inside, red
//    outside), the hands, the pull line and the aim - as plain segments and balls in the frame of the hands, which vrcam
//    hands to the effect that draws them in the headset.

#include <algorithm>
#include <cmath>

#include "bow_draw.h"

namespace bowtouch {

using bowdraw::Vec;

// -- frames ------------------------------------------------------------------------------------------------------------

// A point of the room (OpenXR stage axes) into the hands' frame: metres from the head, turned by the head's yaw
// (bowdraw::HeadYaw). Exactly what vr/vrcam.cpp XrIntoShared does to a hand.
inline Vec RoomToHands(Vec room, Vec headPos, float yaw) { return bowdraw::ToHeadTurn(room - headPos, yaw); }
inline Vec HandsToRoom(Vec hands, Vec headPos, float yaw) { return headPos + bowdraw::ToHeadTurn(hands, -yaw); }

// [bow] quiver_orbs: which of a bow's two limb ends, in the hands' frame (y up), points up - 0 for `a`, 1 for `b`. A bow has no top and bottom of its own: how it hangs in the hand
// decides, and the rig's "upper" limb may well hang down - the orbs go on the limb that is really up. Within `margin` (3 cm) of level the choice stays as it was (`was`), so a bow
// held flat does not flip its row from one limb to the other with every tremor of the hand; with no earlier choice, the higher.
inline int HigherEnd(Vec a, Vec b, int was = -1, float margin = 0.03f) {
    if (a.y - b.y > margin) return 0;
    if (b.y - a.y > margin) return 1;
    return was == 0 || was == 1 ? was : (a.y >= b.y ? 0 : 1);
}

// The hands' frame into the hero's model space (vr/skeletons.cpp ToModel with the eye as the origin): a direction in the
// head's turn frame (x right, y up, z back) has its right at -x and ahead at +z in the model, turned by the camera's yaw
// in the model (c = cos, s = sin).
inline Vec DirToModel(Vec v, float c, float s) {
    const Vec m = {-v.x, v.y, -v.z};
    return {m.x * c + m.z * s, m.y, -m.x * s + m.z * c};
}
inline Vec DirFromModel(Vec o, float c, float s) {
    const Vec m = {o.x * c - o.z * s, o.y, o.x * s + o.z * c};
    return {-m.x, m.y, -m.z};
}
// A hand's place in the model (ControllerPoint): the eye, plus the hand (with the ini's offset) in model units - upm of them
// to the metre. And back: a point of the model as the hand that would be there.
inline Vec HandsToModel(Vec hands, Vec offset, Vec eye, float upm, float c, float s) { return eye + DirToModel(hands + offset, c, s) * upm; }
inline Vec ModelToHands(Vec model, Vec offset, Vec eye, float upm, float c, float s) {
    return DirFromModel((model - eye) * (1.0f / upm), c, s) - offset;
}

// -- the touch point ---------------------------------------------------------------------------------------------------

enum class NockSource { Constant, Model };
struct NockChoice { Vec point; NockSource source = NockSource::Constant; float modelDist = 0.0f; bool modelSane = false; };

// The point the free hand has to reach. `model`: the string's rest place read off the posed bow, in the frame of the
// hands (haveModel false: not read, or too old). It is used when it lies between minM and maxM from the bow hand - a bow
// whose string reads 2 cm or 80 cm from the grip is a misread - else the constant: nockBehind behind the hand along `forward`.
//
// `shift` (metres, [hands] bow_draw_nock_shift_cm): the centre of the zone moved along the bow's axis, + away from the grip (further
// toward the archer), - toward it - for the bow's own string and the constant alike, so the player can lay the zone on the string he
// sees. The sanity test is on the string as it was read, before the shift.
inline NockChoice ChooseNock(Vec bowHand, Vec forward, bool haveModel, Vec model, float nockBehind = 0.16f, float minM = 0.05f, float maxM = 0.45f,
                             float shift = 0.0f) {
    NockChoice c;
    c.point = bowHand - forward * nockBehind;
    if (haveModel && bowdraw::Finite(model)) {
        c.modelDist = bowdraw::Length(model - bowHand);
        c.modelSane = c.modelDist >= minM && c.modelDist <= maxM;
        if (c.modelSane) { c.point = model; c.source = NockSource::Model; }
    }
    if (std::isfinite(shift)) c.point = c.point - forward * shift;
    return c;
}

// -- the debug vectors ---------------------------------------------------------------------------------------------------

struct Scene {
    Vec bow, str;            // the hands
    Vec forward;             // the bow's axis, unit
    Vec nock;                // the touch point in use
    Vec nockConst;           // the constant's, for comparison
    Vec nockModel;           // the model's
    bool haveModel = false;  // nockModel was read
    float reach = 0.28f;     // the touch radius
    bool atString = false;   // the free hand is inside it
    bool drawing = false;    // the string is taken
    bool aimOwned = false;   // `aim` is live (while drawing, and 0.4 s after a shot)
    Vec aim;                 // the draw line, unit
    Vec modelAxis;           // the bow model's own forward axis as the hero's pose has it (skel::BowAxis), unit
    bool haveModelAxis = false;
    Vec rest;                // the point the arrow rests on, fixed in the bow hand's own frame ([bow] arrow_rest_*, skel::BowRestPoint)
    bool haveRest = false;
    float pullNeed = 0.0f;   // the hands' gap from which a release shoots: a ring of this radius round the bow hand
    bool pullEnough = false; // the free hand has pulled far enough (a release shoots now)
};

// What a piece of the picture is, so that the aim ray ([debug] bow_aim_ray) can take the place of one that shows the same: the effect draws four segments and six balls.
enum Tag : int { kTagNone = 0, kTagAimLine = 1, kTagMinorPoint = 2 };
struct Seg { Vec a, b; float rgb[3]; int tag = kTagNone; };
struct Ball { Vec c; float r; float rgb[3]; float fill; int tag = kTagNone; };   // fill: how much the inside is tinted, 0 = the ring alone
inline constexpr int kMaxSegs = 4, kMaxBalls = 6;
struct VectorSet {
    Seg seg[kMaxSegs];
    Ball ball[kMaxBalls];
    int nseg = 0, nball = 0;
    float pull = 0.0f;       // the length of the pull line, nock to the free hand (m): for the log
};

inline void Rgb(float* o, float r, float g, float b) { o[0] = r; o[1] = g; o[2] = b; }

inline VectorSet MakeVectors(const Scene& s, float axisLen = 0.5f, float aimLen = 1.5f) {
    VectorSet v;
    auto seg = [&](Vec a, Vec b, float r, float g, float bl, int tag = kTagNone) {
        if (v.nseg >= kMaxSegs) return;
        Seg& q = v.seg[v.nseg++];
        q.a = a; q.b = b; Rgb(q.rgb, r, g, bl); q.tag = tag;
    };
    auto ball = [&](Vec c, float rad, float r, float g, float bl, float fill, int tag = kTagNone) {
        if (v.nball >= kMaxBalls) return;
        Ball& q = v.ball[v.nball++];
        q.c = c; q.r = rad; Rgb(q.rgb, r, g, bl); q.fill = fill; q.tag = tag;
    };
    // the touch sphere: green with the free hand inside it, red outside; tinted deeper once the string is taken
    if (s.atString || s.drawing) ball(s.nock, s.reach, 0.1f, 1.0f, 0.2f, s.drawing ? 0.22f : 0.12f);
    else ball(s.nock, s.reach, 1.0f, 0.15f, 0.1f, 0.08f);
    ball(s.bow, 0.025f, 1.0f, 1.0f, 1.0f, 1.0f);                    // the bow hand: white
    ball(s.str, 0.025f, 1.0f, 0.9f, 0.1f, 1.0f);                    // the free hand: yellow
    // The arrow's rest point, a small orange dot in the bow hand's frame, for tuning [bow] arrow_rest_*. The effect draws six balls at most
    // and all six are in use with a string read off the model and the pull ring, so the constant's point - a comparison only - gives way.
    if (s.haveRest) ball(s.rest, 0.012f, 1.0f, 0.5f, 0.0f, 1.0f, kTagMinorPoint);
    else ball(s.nockConst, 0.012f, 0.1f, 0.9f, 1.0f, 1.0f, kTagMinorPoint);          // the constant's point: cyan
    if (s.haveModel) ball(s.nockModel, 0.012f, 1.0f, 0.2f, 1.0f, 1.0f);   // the model's string: magenta
    // the pull that is enough: a ring round the bow hand at the radius from which a release shoots - the free hand outside it has pulled
    // far enough. Red until it has, green after (only while the string is near or taken; ring alone, no tint)
    if ((s.atString || s.drawing) && s.pullNeed > 0.0f) {
        if (s.pullEnough) ball(s.bow, s.pullNeed, 0.2f, 1.0f, 0.3f, 0.0f);
        else ball(s.bow, s.pullNeed, 1.0f, 0.2f, 0.15f, 0.0f);
    }
    seg(s.bow, s.bow + s.forward * axisLen, 0.1f, 0.9f, 1.0f);      // the bow's axis: cyan
    seg(s.nock, s.str, 1.0f, 0.55f, 0.1f);                           // the pull: orange
    if (s.drawing || s.aimOwned) seg(s.str, s.str + s.aim * aimLen, 0.2f, 0.4f, 1.0f, kTagAimLine);   // the aim: blue
    if (s.haveModelAxis) seg(s.bow, s.bow + s.modelAxis * (axisLen * 1.4f), 0.55f, 1.0f, 0.15f);   // the bow model's own axis: lime - on the blue line when the bow lies along the hands
    v.pull = bowdraw::Length(s.str - s.nock);
    return v;
}

// ---- [debug] bow_aim_ray: where the arrow goes, as a long ray -----------------------------------------------------------------------------------------------------------
// From the arrow's release point (the bow model's arrow rest point when it is read, else the bow hand's own point) along the line the bow code shoots along (the detector's aim: the
// string hand through that point), to where it meets the floor (a full ball there) or `maxLen` out (a ring). Same colours as the crossbow's ray (xbow_barrel_line): green = the line
// goes through the bow model's own arrow rest point, orange = the plain hand-set line (no rest point read, or [bow] arrow_rest_aims off). The frame is the caller's (the hands' frame:
// metres from the head, y up); `floorY` is the floor's height in it (minus the user's eye height).
struct AimRay {
    Vec a, b;               // the start and the end
    bool fromModel = false; // the line is the model's (green), else the hand-set one (orange)
    bool floor = false;     // the end is on the floor (a full ball), else `maxLen` out (a ring)
};
inline constexpr float kAimRayLen = 8.0f;
inline bool MakeAimRay(Vec start, Vec dir, bool fromModel, float floorY, AimRay* out, float maxLen = kAimRayLen) {
    const float len = bowdraw::Length(dir);
    if (!out || !std::isfinite(len) || len < 1e-6f || !std::isfinite(start.x + start.y + start.z) || !std::isfinite(floorY)) return false;
    const Vec d = dir * (1.0f / len);
    float t = maxLen;
    bool floor = false;
    if (d.y < -0.02f) {   // heading down: the floor is met where y reaches floorY, if that is in front of the start and nearer than maxLen
        const float hit = (floorY - start.y) / d.y;
        if (hit > 0.0f && hit < maxLen) { t = hit; floor = true; }
    }
    out->a = start;
    out->b = start + d * t;
    out->fromModel = fromModel;
    out->floor = floor;
    return true;
}
// Adds the ray to a vector set: a segment and a ball at its end. The effect has room for four segments and six balls, which [debug] bow_vectors can fill; the ray is the same line as the
// blue aim segment and longer, so it takes that one's place, and when the balls are full it takes the place of the small comparison point (the constant's, or the arrow rest).
// False when there is still no room (the ray is dropped; the vectors stay).
inline bool AddAimRay(VectorSet& v, const AimRay& r) {
    auto dropSeg = [&](int tag) {
        for (int i = 0; i < v.nseg; ++i)
            if (v.seg[i].tag == tag) { for (int k = i; k + 1 < v.nseg; ++k) v.seg[k] = v.seg[k + 1]; --v.nseg; return true; }
        return false;
    };
    auto dropBall = [&](int tag) {
        for (int i = 0; i < v.nball; ++i)
            if (v.ball[i].tag == tag) { for (int k = i; k + 1 < v.nball; ++k) v.ball[k] = v.ball[k + 1]; --v.nball; return true; }
        return false;
    };
    if (v.nseg >= kMaxSegs) dropSeg(kTagAimLine);
    if (v.nball >= kMaxBalls) dropBall(kTagMinorPoint);
    if (v.nseg >= kMaxSegs || v.nball >= kMaxBalls) return false;
    const float rgb[3] = {r.fromModel ? 0.2f : 1.0f, r.fromModel ? 1.0f : 0.45f, r.fromModel ? 0.3f : 0.05f};
    Seg& s = v.seg[v.nseg++];
    s.a = r.a; s.b = r.b; Rgb(s.rgb, rgb[0], rgb[1], rgb[2]); s.tag = kTagNone;
    Ball& q = v.ball[v.nball++];
    q.c = r.b; q.r = 0.06f; Rgb(q.rgb, rgb[0], rgb[1], rgb[2]); q.fill = r.floor ? 1.0f : 0.0f; q.tag = kTagNone;
    return true;
}

}  // namespace bowtouch
