// sky_view_eyes_test - vr/sky_view_eyes.h against a model of ReShade's one constant buffer per effect: every present's effects write
// both eyes' sky cameras into it, and the graphics card draws a present's effects only after `lag` later presents have written theirs
// (not past a render the CPU waited for: the replay's left eye waits for the last replay on the GPU, [stereo] replay_fx_sync).
// Each eye's sky must be drawn with the camera its picture was drawn with. The rule before (the left build wrote the right eye's view
// whenever the replay was wanted) gave the right eye the next pair's camera in pairs drawn in two passes - walking, where the game's
// biome sets keep the replay settling (the author, 2026-10-21: the sky juddered in the right eye only).
// Eyes by turns (TurnsModel): each present's effects wait for the last one's renders, its overlays included ([stereo] turn_fx_fence).
#include "sky_view_eyes.h"

#include <algorithm>
#include <cstdio>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

// A camera: the pair it was built in, and how - the left eye's, the left moved across (a replayed right eye), the right eye's own pass.
enum Kind { kLeft, kMoved, kRightOwn };
struct Cam {
    int pair = -1;
    int kind = -1;
    bool operator==(const Cam& o) const { return pair == o.pair && kind == o.kind; }
};

struct Model {
    bool oldRule = false;   // before: the left build wrote the right eye's view whenever the replay was wanted, each build its own eye always
    int lag = 1;
    Cam sky[2], next;
    bool nextOk = false;
    bool lastReplayed = false;   // the last pair was replayed: the next replayed pair's left eye waits for it on the GPU
    bool ring = false;           // [stereo] pair_fx_sync: each render writes only once the render two before it is drawn (FxRing)
    // waitBack: before its write the CPU waited until the render this many before it was drawn (0: no wait)
    struct Render { int eye; Cam world; int waitBack; };
    std::vector<Render> renders;
    std::vector<Cam> written[2];   // each render's write of the buffer's arrays, eye 0 and eye 1

    void Build(int eye, int pair, bool rightReplayed) {
        if (oldRule) {
            sky[eye] = {pair, eye ? kRightOwn : kLeft};
            if (eye == 0) sky[1] = {pair, kMoved};   // (the replay on, wanted - whether it draws this pair or not)
            return;
        }
        if (skyeyes::OwnEye(eye, rightReplayed)) sky[eye] = {pair, eye ? kRightOwn : kLeft};
        if (skyeyes::KeepForRight(eye, rightReplayed)) { next = {pair, kMoved}; nextOk = true; }
    }
    void Commit() {   // replay::RightFromLeft
        if (!oldRule && nextOk) { sky[1] = next; nextOk = false; }
    }
    void Effects(int eye, Cam world, bool waitedLast) {
        int back = waitedLast ? 1 : 0;
        if (ring) {   // as vrcam pairfx: the fence values of the renders so far, the one before last waited for
            skyeyes::FxRing r;
            for (size_t i = 0; i < renders.size(); ++i) r.Rendered(i + 1);
            const unsigned long long v = r.WaitBeforeWriting();
            if (v) {
                const int b = (int)(renders.size() + 1 - v);   // render v-1 (0-based) is this many before this one
                back = back ? std::min(back, b) : b;
            }
        }
        written[0].push_back(sky[0]);
        written[1].push_back(sky[1]);
        renders.push_back({eye, world, back});
    }
    void TwoPasses(int p) {   // the replay settling (or off): each eye a pass of its own and a present
        Build(0, p, false); Effects(0, {p, kLeft}, false);
        Build(1, p, false); Effects(1, {p, kRightOwn}, false);
        lastReplayed = false;
    }
    void Replayed(int p) {    // one pass: the left eye's effects at the held present, the right eye's after the replay
        Build(0, p, true); Effects(0, {p, kLeft}, lastReplayed);
        Build(1, p, true);    // d2rcam::Refresh with the right eye, before the replay
        Commit();
        Effects(1, {p, kMoved}, false);
        lastReplayed = true;
    }
    // the renders whose eye read another camera than its picture's
    int Wrong(int eye) const {
        int n = 0;
        for (size_t k = 0; k < renders.size(); ++k) {
            if (renders[k].eye != eye) continue;
            size_t r = std::min(k + (size_t)lag, renders.size() - 1);
            for (size_t j = k + 1; j <= r; ++j)   // the CPU waited for render k (or a later one) before writing j
                if (renders[j].waitBack > 0 && j - (size_t)renders[j].waitBack >= k) { r = j - 1; break; }
            if (!(written[eye][r] == renders[k].world)) ++n;
        }
        return n;
    }
};

// Eyes by turns (vr/sky_view_eyes.h FenceEffects): one eye drawn a frame, both eyes' cameras written by its build (the eye made's moved
// across), three renders a present - the eye drawn's effects (its sky), its overlays, the eye made's overlays. The graphics card is
// `lag` renders behind; a render whose CPU waited for a mark reads nothing written after it, and the renders before the mark are past.
struct TurnsModel {
    int lag = 3;
    bool fxFence = false, turnFence = true;
    Cam sky[2];
    struct Render { int eye; Cam world; };
    std::vector<Render> renders;
    std::vector<Cam> written[2];
    std::vector<int> waitedFor;   // the render index the CPU waited to be past before this render's write (-1: none)
    int mark = -1;                // the last mark: the renders up to here are past on the graphics card when it is waited for
    void Render1(int eye, Cam world, int waited) {
        written[0].push_back(sky[0]);
        written[1].push_back(sky[1]);
        renders.push_back({eye, world});
        waitedFor.push_back(waited);
    }
    void Frame(int f) {
        const int d = f & 1, o = d ^ 1;
        sky[d] = {f, d ? kRightOwn : kLeft};   // the eye drawn's own camera (VrViewInner)
        sky[o] = {f, kMoved};                  // the eye made's: this one moved across
        const bool fence = skyeyes::FenceEffects(fxFence, true, turnFence);
        const int waited = fence ? mark : -1;  // scenedepth::Bind: WaitLastFx before the constants go in
        Render1(d, {f, d ? kRightOwn : kLeft}, waited);   // the effects: the eye drawn's sky
        if (fence && !skyeyes::MarkAfterOverlays(true, turnFence)) mark = (int)renders.size() - 1;   // [render] fx_fence's mark: here
        Render1(d, {f, d ? kRightOwn : kLeft}, -1);       // D2R_Overlay on the eye drawn
        Render1(o, {f, kMoved}, -1);                      // D2R_OverlayMade on the warped picture of the other eye
        if (fence && skyeyes::MarkAfterOverlays(true, turnFence)) mark = (int)renders.size() - 1;
    }
    int Wrong(int eye) const {
        int n = 0;
        for (size_t k = 0; k < renders.size(); ++k) {
            if (renders[k].eye != eye) continue;
            size_t r = std::min(k + (size_t)lag, renders.size() - 1);
            for (size_t j = k + 1; j <= r; ++j)
                if (waitedFor[j] >= (int)k) { r = j - 1; break; }
            if (!(written[eye][r] == renders[k].world)) ++n;
        }
        return n;
    }
};

int main() {
    {   // eyes by turns: the sky shook in both eyes in head turns (the author, test-0176)
        CHECK(skyeyes::FenceEffects(false, true, true) && !skyeyes::FenceEffects(false, true, false) && !skyeyes::FenceEffects(false, false, true));
        CHECK(skyeyes::FenceEffects(true, false, false) && skyeyes::MarkAfterOverlays(true, true) && !skyeyes::MarkAfterOverlays(false, true));
        TurnsModel before; before.turnFence = false;   // 0.175: nothing waited, the card a frame (three renders) behind
        for (int f = 0; f < 30; ++f) before.Frame(f);
        CHECK(before.Wrong(0) > 0 && before.Wrong(1) > 0);   // both eyes' skies (and the eye made's overlays) with the next frame's cameras
        TurnsModel effectsOnly; effectsOnly.turnFence = false; effectsOnly.fxFence = true;   // [render] fx_fence: marked after the effects alone
        for (int f = 0; f < 30; ++f) effectsOnly.Frame(f);
        int madeWrong = 0;   // the eye made's overlays still read the next frame's cameras
        for (size_t k = 0; k + 1 < effectsOnly.renders.size(); ++k)
            if (effectsOnly.renders[k].world.kind == kMoved) {
                size_t r = std::min(k + (size_t)effectsOnly.lag, effectsOnly.renders.size() - 1);
                for (size_t j = k + 1; j <= r; ++j) if (effectsOnly.waitedFor[j] >= (int)k) { r = j - 1; break; }
                if (!(effectsOnly.written[effectsOnly.renders[k].eye][r] == effectsOnly.renders[k].world)) ++madeWrong;
            }
        CHECK(madeWrong > 0);
        for (int lag = 0; lag <= 6; ++lag) {   // now: whatever the card's lag, each render reads its own frame's cameras
            TurnsModel now; now.lag = lag;
            for (int f = 0; f < 30; ++f) now.Frame(f);
            CHECK(now.Wrong(0) == 0 && now.Wrong(1) == 0);
        }
        TurnsModel keen; keen.lag = 0; keen.turnFence = false;   // no lag: right without waiting too (why it shook only now and then)
        for (int f = 0; f < 30; ++f) keen.Frame(f);
        CHECK(keen.Wrong(0) == 0 && keen.Wrong(1) == 0);
    }
    {   // the rule itself
        CHECK(skyeyes::OwnEye(0, false) && skyeyes::OwnEye(0, true));
        CHECK(skyeyes::OwnEye(1, false) && !skyeyes::OwnEye(1, true));
        CHECK(skyeyes::KeepForRight(0, true) && !skyeyes::KeepForRight(0, false));
        CHECK(!skyeyes::KeepForRight(1, true) && !skyeyes::KeepForRight(1, false));
    }
    {   // walking: every pair in two passes, the graphics card a present behind - the right eye juddered, the left did not
        Model before; before.oldRule = true;
        for (int p = 0; p < 30; ++p) before.TwoPasses(p);
        CHECK(before.Wrong(1) == 29);   // each but the last read the next pair's camera
        CHECK(before.Wrong(0) == 0);
        Model now;
        for (int p = 0; p < 30; ++p) now.TwoPasses(p);
        CHECK(now.Wrong(1) == 0);
        CHECK(now.Wrong(0) == 0);
    }
    {   // standing: the replay draws every right eye - its sky is the left camera moved across, as its picture
        Model now;
        for (int p = 0; p < 30; ++p) now.Replayed(p);
        CHECK(now.Wrong(0) == 0);
        CHECK(now.Wrong(1) == 0);
    }
    for (int lag = 0; lag <= 1; ++lag) {   // stopping, walking, stopping: the switches between the two ways too
        Model now; now.lag = lag;
        int p = 0;
        for (int i = 0; i < 10; ++i) now.Replayed(p++);
        for (int i = 0; i < 10; ++i) now.TwoPasses(p++);
        for (int i = 0; i < 10; ++i) now.Replayed(p++);
        now.TwoPasses(p++);
        now.Replayed(p++);
        CHECK(now.Wrong(0) == 0);
        CHECK(now.Wrong(1) == 0);
    }
    {   // the ring itself: the render before last
        skyeyes::FxRing r;
        CHECK(r.WaitBeforeWriting() == 0);
        r.Rendered(5);
        CHECK(r.WaitBeforeWriting() == 0);
        r.Rendered(6);
        CHECK(r.WaitBeforeWriting() == 5);
        r.Rendered(7);
        CHECK(r.WaitBeforeWriting() == 6);
    }
    for (int lag = 2; lag <= 3; ++lag) {   // the graphics card two or three presents behind (0.176: "still shakes now and then")
        Model bare; bare.lag = lag;        // the eyes' rule alone: the next pair's values reach this pair's effects in both eyes
        for (int p = 0; p < 30; ++p) bare.TwoPasses(p);
        CHECK(bare.Wrong(0) > 0 && bare.Wrong(1) > 0);
        Model now; now.lag = lag; now.ring = true;   // with pair_fx_sync: none, in two passes, replayed and between the two
        int p = 0;
        for (int i = 0; i < 10; ++i) now.TwoPasses(p++);
        for (int i = 0; i < 10; ++i) now.Replayed(p++);
        for (int i = 0; i < 10; ++i) now.TwoPasses(p++);
        now.Replayed(p++);
        now.TwoPasses(p++);
        CHECK(now.Wrong(0) == 0);
        CHECK(now.Wrong(1) == 0);
        Model before; before.lag = lag; before.ring = true; before.oldRule = true;   // the ring needs the eyes' rule: one present's lead
        for (int i = 0; i < 30; ++i) before.TwoPasses(i);
        CHECK(before.Wrong(1) > 0);
    }
    {   // no lag: the old rule was right too (the judder needs the graphics card behind)
        Model before; before.oldRule = true; before.lag = 0;
        for (int p = 0; p < 10; ++p) before.TwoPasses(p);
        CHECK(before.Wrong(0) == 0 && before.Wrong(1) == 0);
    }
    if (g_fail) { std::printf("sky_view_eyes_test: %d FAILED\n", g_fail); return 1; }
    std::printf("sky_view_eyes_test: all passed\n");
    return 0;
}
