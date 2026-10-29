// Offline checks of the crossbow's support hand (vr/xbow_grip.h): the grip latch, the wide grip capture and the barrel taken from
// the model's bolt_helper. No game, no headset. Run: build/Release/xbow_grip_test.exe   (exit code 0 = all passed)

#include "../../vr/xbow_grip.h"

#include <cmath>
#include <cstdio>

using namespace xbowgrip;

static int g_failures = 0, g_checks = 0;

static void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}
static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static bool Near(V a, V b, float eps = 1e-4f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

static M Identity() { M r; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r; }
static M Translate(V t) { M r = Identity(); SetPlace(r, t); return r; }
// a turn by deg about the unit axis k, row-vector form (as skeletons TurnAbout)
static M Turn(V k, float deg) {
    const float t = deg * 0.0174532925f, cs = std::cos(t), sn = std::sin(t), C = 1.0f - cs;
    M r;
    r.m[0] = cs + k.x * k.x * C;       r.m[1] = k.y * k.x * C + k.z * sn; r.m[2] = k.z * k.x * C - k.y * sn;
    r.m[4] = k.x * k.y * C - k.z * sn; r.m[5] = cs + k.y * k.y * C;       r.m[6] = k.z * k.y * C + k.x * sn;
    r.m[8] = k.x * k.z * C + k.y * sn; r.m[9] = k.y * k.z * C - k.x * sn; r.m[10] = cs + k.z * k.z * C;
    r.m[15] = 1.0f;
    return r;
}
static M Scaled(float s) { M r = Identity(); r.m[0] = r.m[5] = r.m[10] = s; return r; }
static V Through(const M& m, V p) {
    return {p.x * m.m[0] + p.y * m.m[4] + p.z * m.m[8] + m.m[12], p.x * m.m[1] + p.y * m.m[5] + p.z * m.m[9] + m.m[13],
            p.x * m.m[2] + p.y * m.m[6] + p.z * m.m[10] + m.m[14]};
}

static void LatchTests() {
    const uint64_t hero = 0x1000;
    const uint32_t xbow = 3, other = 7;
    {   // a squeeze already held when the latch starts (the shoulder swap's) takes nothing, even in reach
        Latch l;
        Expect(!l.Step(hero, xbow, true, 1.0f, true, false), "latch: a squeeze held from before does not grab");
        Expect(!l.Step(hero, xbow, true, 0.9f, true, false), "latch: still held, still nothing");
        Expect(!l.Step(hero, xbow, true, 0.1f, true, false), "latch: let go: nothing, armed");
        Expect(l.Step(hero, xbow, true, 0.9f, true, false), "latch: squeezed again in reach: grabs");
    }
    {   // squeezed out of reach, then the hand comes in while still squeezed: grabs (Bear's: the grip may come first)
        Latch l;
        l.Step(hero, xbow, true, 0.0f, false, false);
        Expect(!l.Step(hero, xbow, true, 1.0f, false, false), "latch: squeezed out of reach: nothing");
        Expect(l.Step(hero, xbow, true, 1.0f, true, false), "latch: held, comes into reach: grabs");
        Expect(l.Step(hero, xbow, true, 1.0f, false, false), "latch: held, leaves reach: still holds");
        Expect(l.Step(hero, xbow, true, 0.5f, false, false), "latch: half (0.4..0.6) keeps it");
        Expect(!l.Step(hero, xbow, true, 0.3f, false, false), "latch: let go: frees the hand");
    }
    {   // the weapon changes while held: let go at once, and needs a release before it grabs again
        Latch l;
        l.Step(hero, xbow, true, 0.0f, true, false);
        Expect(l.Step(hero, xbow, true, 1.0f, true, false), "swap: holds before");
        Expect(!l.Step(hero, other, true, 1.0f, true, false), "swap: another weapon key: free");
        Expect(!l.Step(hero, other, true, 1.0f, true, false), "swap: the swap's squeeze still held: free");
        Expect(!l.Step(hero, other, true, 0.0f, true, false), "swap: released");
        Expect(l.Step(hero, other, true, 1.0f, true, false), "swap: squeezed again: grabs");
        Expect(!l.Step(hero + 8, other, true, 1.0f, true, false), "swap: another hero model: free");
    }
    {   // lost tracking: free at once; back with the grip still squeezed: nothing until released
        Latch l;
        l.Step(hero, xbow, true, 0.0f, true, false);
        Expect(l.Step(hero, xbow, true, 1.0f, true, false), "tracking: holds");
        Expect(!l.Step(hero, xbow, false, 1.0f, true, false), "tracking: lost - the hand is free (no jump onto the stock)");
        Expect(!l.Step(hero, xbow, true, 1.0f, true, false), "tracking: back, still squeezed: free");
        Expect(!l.Step(hero, xbow, true, 0.0f, true, false), "tracking: released");
        Expect(l.Step(hero, xbow, true, 0.8f, true, false), "tracking: squeezed: grabs");
    }
    {   // no grip from the bridge (< 0, NaN): never held (the old code held it always)
        Latch l;
        Expect(!l.Step(hero, xbow, true, -1.0f, true, false), "no grip: free");
        Expect(!l.Step(hero, xbow, true, NAN, true, false), "NaN grip: free");
    }
    {   // busy (a belt potion in the hand): never held; free of it while still squeezed in reach: grabs
        Latch l;
        l.Step(hero, xbow, true, 0.0f, true, false);
        Expect(!l.Step(hero, xbow, true, 1.0f, true, true), "busy: free");
        Expect(l.Step(hero, xbow, true, 1.0f, true, false), "busy no more, held in reach: grabs");
        Expect(!l.Step(hero, xbow, true, 1.0f, true, true), "busy again: lets go");
    }
}

static void WideTests() {
    Expect(WideCaptureAllowed(0.8f, false, 1.0f), "wide: close pose, legacy: taken");
    Expect(!WideCaptureAllowed(4.5f, false, 1.0f), "wide: a stride's pose, legacy: refused (as before)");
    Expect(WideCaptureAllowed(4.5f, true, 1.0f), "wide: a stride's pose, placed by the controller: taken");
    Expect(!WideCaptureAllowed(4.5f, true, 0.1f), "wide: a hidden attach bone: refused");
    Expect(!WideCaptureAllowed(4.5f, true, 5.0f), "wide: an attach bone blown up: refused");
    Expect(!WideCaptureAllowed(NAN, true, 1.0f) && !WideCaptureAllowed(-1.0f, true, 1.0f), "wide: a gap that is not a number: refused");
}

static void BarrelTests() {
    // A crossbow's bind pose in its model space: the bolt along +Z from (0, 0.1, -0.3), up +Y; the limbs at z = +0.2, either side.
    const V boltAt = {0.0f, 0.1f, -0.3f};
    const M boltBind = Translate(boltAt), leftBind = Translate({-0.25f, 0.1f, 0.2f}), rightBind = Translate({0.25f, 0.1f, 0.2f});
    const M cogBind = Translate({0.0f, 0.05f, 0.0f});
    M invBolt, invL, invR, invCog;
    Inverse(boltBind, &invBolt); Inverse(leftBind, &invL); Inverse(rightBind, &invR); Inverse(cogBind, &invCog);
    M barrel;
    Expect(BarrelInCog(invCog, invBolt, invL, invR, &barrel), "barrel: a clean bind pose reads");
    // the cog posed where it was bound: the barrel's centre is on the bolt's line, level with the limbs (z 0.2), in the item's model space
    Line line;
    Expect(FromBolt(Mul(barrel, cogBind), &line), "barrel: the line reads");
    Expect(Near(line.center, {0.0f, 0.1f, 0.2f}), "barrel: centre on the bolt's line level with the limbs' middle");
    Expect(Near(line.dir, {0, 0, 1}) && Near(line.up, {0, 1, 0}), "barrel: along the bolt, up its +Y");
    Expect(Near(line.scale, 1.0f), "barrel: scale 1");

    // the whole crossbow carried by its cog (a turn, a shift) and its item/attach frames (a scale too): the barrel goes with it
    const M cogNow = Mul(Mul(cogBind, Turn({0, 1, 0}, 30.0f)), Translate({1, 2, 3}));
    const M attach = Mul(Mul(Scaled(2.0f), Turn({1, 0, 0}, -20.0f)), Translate({-4, 0, 1}));
    Expect(FromBolt(Mul(Mul(barrel, cogNow), attach), &line), "barrel carried: reads");
    const M whole = Mul(Mul(Mul(invCog, cogNow), attach), Identity());   // item model space -> world, as the cog and the attach carry it
    const V centreWant = Through(whole, {0.0f, 0.1f, 0.2f});
    const V dirWant = [&] { V d = Sub(Through(whole, {0, 0.1f, 1.2f}), centreWant); return Mul(d, 1.0f / std::sqrt(Dot(d, d))); }();
    Expect(Near(line.center, centreWant, 1e-3f), "barrel carried: the centre follows");
    Expect(Near(line.dir, dirWant, 1e-4f), "barrel carried: the direction follows");
    Expect(Near(line.scale, 2.0f, 1e-3f), "barrel carried: the scale is the attach's");

    // a turned bolt helper (a model laid along X): the barrel is its +Z wherever that points
    const M turnedBolt = Mul(Turn({0, 1, 0}, 90.0f), Translate(boltAt));
    M invTurned;
    Inverse(turnedBolt, &invTurned);
    Expect(BarrelInCog(invCog, invTurned, invL, invR, &barrel) && FromBolt(Mul(barrel, cogBind), &line), "barrel turned: reads");
    Expect(Near(line.dir, {1, 0, 0}), "barrel turned: along the helper's +Z (now +X)");
    Expect(Near(line.center, {0.0f, 0.1f, -0.3f}), "barrel turned: the limbs' middle projects onto the helper's own point");

    // refusals: a flat bolt, a not-affine cog, NaN
    M flat = invBolt; flat.m[8] = flat.m[9] = flat.m[10] = 0.0f;
    Expect(!BarrelInCog(invCog, flat, invL, invR, &barrel), "barrel: a flat bolt helper is refused");
    M bent = invCog; bent.m[3] = 0.5f;
    Expect(!BarrelInCog(bent, invBolt, invL, invR, &barrel), "barrel: a projective cog is refused");
    M bad = invL; bad.m[12] = NAN;
    Expect(!BarrelInCog(invCog, invBolt, bad, invR, &barrel), "barrel: NaN is refused");

    // up comes from the limbs, not the helper's +Y: a helper rolled 90 deg about the bolt (its +Y sideways, test-42) still gives up = +Y
    const M rolledBolt = Mul(Turn({0, 0, 1}, 90.0f), Translate(boltAt));
    M invRolled;
    Inverse(rolledBolt, &invRolled);
    Expect(BarrelInCog(invCog, invRolled, invL, invR, &barrel) && FromBolt(Mul(barrel, cogBind), &line), "barrel rolled helper: reads");
    Expect(Near(line.dir, {0, 0, 1}), "barrel rolled helper: still along the bolt");
    Expect(std::fabs(line.up.y) > 0.999f, "barrel rolled helper: up is square to the limbs (the helper's sideways +Y not used)");
    // and the trigger, below the stock, says which way is up - wherever the helper's +Y points
    const M triggerBind = Translate({0.0f, -0.05f, -0.1f});
    M invTrigger;
    Inverse(triggerBind, &invTrigger);
    for (float roll : {0.0f, 90.0f, 180.0f, -90.0f}) {
        M inv;
        Inverse(Mul(Turn({0, 0, 1}, roll), Translate(boltAt)), &inv);
        Expect(BarrelInCog(invCog, inv, invL, invR, &barrel, &invTrigger) && FromBolt(Mul(barrel, cogBind), &line) && Near(line.up, {0, 1, 0}),
               "barrel with a trigger: up is away from the trigger, whatever the helper's roll");
    }
    // a trigger above (an upside-down model) turns up over
    const M triggerAbove = Translate({0.0f, 0.3f, -0.1f});
    Inverse(triggerAbove, &invTrigger);
    Expect(BarrelInCog(invCog, invBolt, invL, invR, &barrel, &invTrigger) && FromBolt(Mul(barrel, cogBind), &line) && Near(line.up, {0, -1, 0}),
           "barrel with a trigger above: up turned over");
}

static void RenderedTests() {
    // the renderer puts the item at relation * attach in a far-off world (town coordinates): the relation comes back exact
    const M relation = Mul(Mul(Turn({0, 0, 1}, 45.0f), Turn({0, 1, 0}, -30.0f)), Translate({0.1f, -0.2f, 0.3f}));
    const M attach = Mul(Mul(Scaled(2.5f), Turn({1, 0, 0}, 70.0f)), Translate({25000.0f, 30.0f, -18000.0f}));
    const M item = Mul(relation, attach);
    M got;
    Expect(RenderedRelation(item, attach, &got), "rendered: reads");
    bool same = true;
    for (int i = 0; i < 16; ++i) same = same && Near(got.m[i], relation.m[i], 2e-3f);
    Expect(same, "rendered: the relation comes back (far-off world, scaled attach)");
    // a barrel carried by it lands where the drawn item's barrel is
    Line drawn, viaRelation;
    const M bolt = Mul(Turn({0, 1, 0}, 10.0f), Translate({0, 0.1f, 0.2f}));
    Expect(FromBolt(Mul(bolt, item), &drawn) && FromBolt(Mul(Mul(bolt, got), attach), &viaRelation), "rendered: lines read");
    Expect(Near(drawn.dir, viaRelation.dir, 1e-3f), "rendered: the same barrel direction");
    M flat = attach; flat.m[0] = flat.m[1] = flat.m[2] = 0.0f;
    Expect(!RenderedRelation(item, flat, &got), "rendered: a flat attach is refused");
}

static void PalmTests() {
    // a rig's bind: the hand bone at the wrist, turned and scaled as rigs are; the middle knuckle 9 cm along its own axis
    const M handBind = Mul(Mul(Scaled(1.3f), Turn({0.6f, 0.0f, 0.8f}, 37.0f)), Translate({0.4f, 1.1f, 0.2f}));
    const V knuckleLocal = {0.0f, 0.09f, 0.01f};
    const M knuckleBind = Mul(Translate(knuckleLocal), handBind);
    V palm;
    Expect(PalmInHand(handBind, knuckleBind, &palm), "palm: reads");
    Expect(Near(palm, Mul(knuckleLocal, 0.5f)), "palm: halfway wrist -> middle knuckle, in the hand's own frame");
    // a hand posed anywhere (another turn, the hero's scale) and put by its palm: the palm's middle lands on the point within 1 cm
    // (model units at 4 per metre: 0.04), the hand keeps its turn, and the wrist is NOT on the point
    const float upm = 4.0f;
    int within = 0, tried = 0;
    for (float a = 0.0f; a < 360.0f; a += 30.0f) {
        const M hand = Mul(Mul(Scaled(2.0f), Turn({0.0f, 0.6f, 0.8f}, a)), Translate({1.0f, 2.0f, -3.0f}));
        const V target = {0.3f * a / 360.0f, 4.0f, -1.0f};
        const M put = PlaceByPalm(hand, palm, target);
        const V p = PalmOf(put, palm), d = Sub(p, target);
        ++tried;
        if (std::sqrt(Dot(d, d)) < 0.01f * upm) ++within;
        bool sameTurn = true;
        for (int i = 0; i < 12; ++i) if (i % 4 != 3) sameTurn = sameTurn && Near(put.m[i], hand.m[i]);
        Expect(sameTurn, "palm: the hand is only moved, not turned");
        const V w = Sub(Place(put), target);
        Expect(std::sqrt(Dot(w, w)) > 0.04f, "palm: the wrist is not the point (half a hand off it)");
    }
    Expect(within == tried, "palm: the palm's middle on the point within 1 cm, every turn");
    // refusals: a knuckle on the wrist, a flat bind
    Expect(!PalmInHand(handBind, handBind, &palm), "palm: a knuckle on the wrist is refused");
    M flat = handBind; flat.m[0] = flat.m[1] = flat.m[2] = 0.0f;
    Expect(!PalmInHand(flat, knuckleBind, &palm), "palm: a flat hand bind is refused");
}

static void HoldGripTests() {
    // the crossbow's attach bone turned and scaled as the gun frame lays it; the game's palm 5 cm from its origin (the bone at the wrist)
    const V grip = {0.0f, 0.05f, 0.012f};
    const float upm = 4.0f;
    int ok = 0, n = 0;
    for (float a = 0.0f; a < 360.0f; a += 45.0f) {
        M attach = Mul(Mul(Scaled(1.7f), Turn({0.8f, 0.0f, 0.6f}, a)), Translate({9.0f, 9.0f, 9.0f}));
        const V palm = {1.0f, 3.5f, -2.0f};
        SetPlace(attach, OriginForGrip(attach, grip, palm));
        const V d = Sub(PalmOf(attach, grip), palm);
        ++n;
        if (std::sqrt(Dot(d, d)) < 0.01f * upm) ++ok;
        const V o = Sub(Place(attach), palm);
        Expect(std::sqrt(Dot(o, o)) > 0.04f, "hold grip: the attach bone's origin is not in the palm (the grip is)");
    }
    Expect(ok == n, "hold grip: the crossbow's grip in the palm within 1 cm, every turn");
    M plain = Identity();
    Expect(Near(OriginForGrip(plain, {0, 0, 0}, {1, 2, 3}), {1, 2, 3}), "hold grip: a grip at the origin = the old placing");
}

static void TurnTests() {
    const V outL = {0.0f, 0.0f, 1.0f}, acrossL = {1.0f, 0.1f, 0.0f};
    for (int mirror = 0; mirror < 2; ++mirror)
        for (float a = 0.0f; a < 360.0f; a += 60.0f) {
            M hand = Mul(Mul(Scaled(1.4f), Turn({0.6f, 0.8f, 0.0f}, a)), Translate({2.0f, 3.0f, 4.0f}));
            if (mirror) { hand.m[0] = -hand.m[0]; hand.m[1] = -hand.m[1]; hand.m[2] = -hand.m[2]; }   // a left hand's mirrored bone
            const V outW = {0.0f, 1.0f, 0.0f}, acrossW = {0.0f, 0.2f, -1.0f};
            M t;
            Expect(HandTurnTo(hand, outL, acrossL, outW, acrossW, &t), "turn: reads");
            // the hand's own out direction now points up, its knuckle line along -z (square to up), its place and lengths and handedness kept
            auto through = [&](V v) { return V{v.x * t.m[0] + v.y * t.m[4] + v.z * t.m[8], v.x * t.m[1] + v.y * t.m[5] + v.z * t.m[9], v.x * t.m[2] + v.y * t.m[6] + v.z * t.m[10]}; };
            V o = through(outL); o = Mul(o, 1.0f / std::sqrt(Dot(o, o)));
            V k = through(Sub(acrossL, Mul(outL, Dot(acrossL, outL)))); k = Mul(k, 1.0f / std::sqrt(Dot(k, k)));
            Expect(Near(o, {0, 1, 0}, 1e-3f), "turn: the palm's way out where asked");
            Expect(Near(k, {0, 0, -1}, 1e-3f), "turn: the knuckle line where asked (square to the palm's way)");
            Expect(Near(Place(t), Place(hand)), "turn: the place kept");
            bool lens = true;
            for (int r = 0; r < 3; ++r) lens = lens && Near(std::sqrt(Dot(Row(t, r), Row(t, r))), 1.4f, 1e-3f);
            Expect(lens, "turn: the axes' lengths kept");
            const float d0 = Dot(Row(hand, 0), Cross(Row(hand, 1), Row(hand, 2))), d1 = Dot(Row(t, 0), Cross(Row(t, 1), Row(t, 2)));
            Expect((d0 < 0) == (d1 < 0), "turn: the handedness kept (a mirrored left hand stays one)");
        }
    M t;
    Expect(!HandTurnTo(Identity(), {0, 0, 1}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}, &t), "turn: a knuckle line along the palm's way is refused");
    // the mirror: a right hand's palm facing left (into a stock to its left) becomes the left hand's facing right
    Expect(Near(MirrorDir({-1, 0.2f, 0}, {1, 0, 0}), {1, 0.2f, 0}), "mirror: across the side flips, the rest stays");
    Expect(Near(MirrorPoint({0.3f, -0.1f, 0.5f}, {0.1f, 0, 0}, {1, 0, 0}), {-0.1f, -0.1f, 0.5f}), "mirror: a point across the barrel's plane");
}

static void HandoverTests() {
    const bool both[2] = {true, true};
    Handover h;
    h.Reset(0);
    auto step = [&](float r, float l, float nearM) { const float g[2] = {r, l}; return h.Step(g, both, nearM, 0.10f); };
    Expect(!step(0.0f, 0.0f, 0.05f) && h.holder == 0, "handover: at rest the right hand holds it");
    Expect(!step(0.0f, 1.0f, 0.30f) && h.holder == 0, "handover: the left squeezes far from the grip (the fore-grip): no hand-over");
    Expect(!step(0.0f, 1.0f, 0.05f) && h.holder == 0, "handover: still the same squeeze, now near: not a new press, no hand-over");
    step(0.0f, 0.0f, 0.05f);
    Expect(step(0.0f, 1.0f, 0.05f) && h.holder == 1, "handover: a new squeeze at the grip: the left hand has it");
    Expect(!step(1.0f, 1.0f, 0.05f) && h.holder == 1, "handover: the right's squeeze still held from before takes nothing back");
    step(0.0f, 1.0f, 0.05f);
    Expect(step(1.0f, 1.0f, 0.05f) && h.holder == 0, "handover: the right squeezes anew at the grip: back in the right hand");
    // an untracked or gripless taker takes nothing
    const bool rightOnly[2] = {true, false};
    h.Reset(0);
    const float g0[2] = {0.0f, 0.0f}, g1[2] = {0.0f, 1.0f};
    h.Step(g0, rightOnly, 0.05f, 0.10f);
    Expect(!h.Step(g1, rightOnly, 0.05f, 0.10f) && h.holder == 0, "handover: an untracked hand takes nothing");
    h.Reset(1);
    Expect(h.holder == 1, "handover: Reset to a home hand");
    const float none[2] = {-1.0f, -1.0f};
    Expect(!h.Step(none, both, 0.0f, 0.10f) && h.holder == 1, "handover: no grip reading: nothing changes");
}

static void HandoverNearTests() {
    Expect(HandoverNear(0.18f, 0.30f, 0.25f) == 0.18f, "near: 18 cm from the grip, 30 from the fore-grip: counts (0.170: controllers never got within 12)");
    Expect(std::isinf(HandoverNear(0.18f, 0.10f, 0.25f)), "near: nearer the fore-grip: the support's squeeze, not a hand-over");
    Expect(std::isinf(HandoverNear(0.30f, 0.50f, 0.25f)), "near: beyond the reach: nothing");
    Expect(HandoverNear(0.20f, -1.0f, 0.25f) == 0.20f, "near: no fore-grip place yet: the grip alone decides");
    Expect(std::isinf(HandoverNear(NAN, 0.3f, 0.25f)), "near: NaN: nothing");
    // and through Handover: a new squeeze 18 cm from the grip, farther from the fore-grip, takes it
    Handover h;
    h.Reset(0);
    const bool both[2] = {true, true};
    const float rest[2] = {0.0f, 0.0f}, squeeze[2] = {0.0f, 1.0f};
    h.Step(rest, both, HandoverNear(0.18f, 0.30f, 0.25f), 0.25f);
    Expect(h.Step(squeeze, both, HandoverNear(0.18f, 0.30f, 0.25f), 0.25f) && h.holder == 1, "near: the left takes it 18 cm off the grip");
}

static void HandoverLetGoTests() {
    const bool both[2] = {true, true};
    Handover h;
    h.Reset(0);
    auto step = [&](float r, float l, bool support) { const float g[2] = {r, l}; return h.Step(g, both, INFINITY, 0.25f, support); };
    step(1.0f, 0.0f, false);   // the right hand squeezes its grip, the left is free
    Expect(!step(1.0f, 1.0f, true) && h.holder == 0, "let go: the left takes the fore-grip (the support): still the right's");
    Expect(step(0.0f, 1.0f, true) && h.holder == 1, "let go: the right lets go while the left holds it: the left has it");
    Expect(!step(0.0f, 1.0f, false) && h.holder == 1, "let go: it stays in the left");
    // back: the right takes the fore-grip (support), the left lets go
    step(1.0f, 1.0f, true);
    Expect(step(1.0f, 0.0f, true) && h.holder == 0, "let go: back into the right the same way");
    // letting go with nothing else holding it: it stays (a crossbow is never dropped)
    h.Reset(0);
    step(1.0f, 0.0f, false);
    Expect(!step(0.0f, 0.0f, false) && h.holder == 0, "let go: nobody else holds it: it stays in the right");
    // an untracked other hand never takes it this way
    h.Reset(0);
    const bool rightOnly[2] = {true, false};
    const float g1[2] = {1.0f, 1.0f}, g0[2] = {0.0f, 1.0f};
    h.Step(g1, rightOnly, INFINITY, 0.25f, true);
    Expect(!h.Step(g0, rightOnly, INFINITY, 0.25f, true) && h.holder == 0, "let go: an untracked other hand takes nothing");
}

static void SupportTests() {
    Line line{{0, 1, 0}, {0, 0, 1}, {0, 1, 0}, 1.0f};
    const float upm = 4.0f;
    // the right palm 50 cm behind the front of the stock, the left 30 cm ahead of it: 20 cm behind the front, 7 cm below
    V p = Support(line, {0, 1, -0.5f * upm}, 0.30f, 0.07f, upm);
    Expect(Near(p, {0, 1 - 0.07f * upm, -0.20f * upm}), "support: 30 cm ahead of the right palm, 7 cm under the barrel");
    // 30 cm ahead would be past the limbs: held 6 cm behind them
    p = Support(line, {0, 1, -0.2f * upm}, 0.30f, 0.07f, upm);
    Expect(Near(p, {0, 1 - 0.07f * upm, -kBackM * upm}), "support: never past the limbs (6 cm behind)");
    // the right palm off the barrel's line: only its place ALONG the barrel counts
    p = Support(line, {0.3f * upm, 0.5f, -0.5f * upm}, 0.30f, 0.07f, upm);
    Expect(Near(p, {0, 1 - 0.07f * upm, -0.20f * upm}), "support: the right palm's side offset does not move the left");
    // drop 0: on the barrel
    p = Support(line, {0, 1, -0.5f * upm}, 0.30f, 0.0f, upm);
    Expect(Near(p, {0, 1, -0.20f * upm}), "support: drop 0 = on the barrel");
    // SupportAt (xbow_support_back_cm): by the crossbow alone - 8 cm behind the limbs, 7 below; a tilted barrel keeps its own down
    p = SupportAt(line, 0.08f, 0.07f, upm);
    Expect(Near(p, {0, 1 - 0.07f * upm, -0.08f * upm}), "support at: 8 cm behind the front of the stock, 7 cm below the barrel");
    const Line tilted{{0, 0, 0}, {0, 0.6f, 0.8f}, {0, 0.8f, -0.6f}, 1.0f};
    p = SupportAt(tilted, 0.10f, 0.05f, upm);
    Expect(Near(p, {0, -0.06f * upm - 0.04f * upm, -0.08f * upm + 0.03f * upm}), "support at: a tilted barrel - back along it, down along its own up");
}

int main() {
    LatchTests();
    WideTests();
    BarrelTests();
    RenderedTests();
    PalmTests();
    HoldGripTests();
    TurnTests();
    HandoverTests();
    HandoverNearTests();
    HandoverLetGoTests();
    SupportTests();
    if (g_failures) {
        std::printf("%d of %d FAILED\n", g_failures, g_checks);
        return 1;
    }
    std::printf("xbow_grip_test: all %d checks passed\n", g_checks);
    return 0;
}
