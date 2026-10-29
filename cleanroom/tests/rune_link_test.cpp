// rune_link_test - [openxr] runes_draw: vr/link.cpp's VrRunes against a fake writer, no BodyWalk and no headset.
//
//   rune_link_test        exit code 0 = every check passed, 2 = a BodyWalk is writing the real block (nothing was touched)
//
// The writer is this process: it opens the same shared block BodyWalk's Game Link would write (Local\BodyWalk.Link.VrRunes), writes a
// circle on a panel 0.85 m ahead of the head with a trail arc as the engine would, and the game's reader (link::VrRunes) reads it back
// through the seqlock. Checks: the numbers arrive whole; no block, a wrong version, a block that was never written, a count of 0 and a
// stale stamp give nothing; the F4 rule; a count past the array. Only the VrRunes block is touched (not the Manifest or Actions: the
// plugin's Init is not called, so a BodyWalk running with a live game is not disturbed) - and not at all when another process
// is writing it.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "link.h"
#include "rune_geom.h"
#include "view_gate.h"

namespace {

int g_fail = 0, g_checks = 0;
void Expect(bool ok, const char* what) {
    ++g_checks;
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}

bool g_f4 = true;
bool TheView() { return g_f4; }

BW_LinkVrRune Circle(uint32_t id, float progress) {
    BW_LinkVrRune r{};
    r.id = id;
    r.state = BW_LINK_RUNE_DRAWING;
    r.progress = progress;
    r.score = 0.8f;
    r.fade = 1.0f;
    r.runeRgba = 0xFFC8A078u; r.litRgba = 0xFF2EB8FFu; r.trailRgba = 0xFF2828F0u;
    r.lineWidth = 0.012f; r.trailWidth = 0.010f; r.glow = 0.5f;
    r.centre[1] = 1.70f; r.centre[2] = -0.85f;
    r.right[0] = 1.0f; r.up[1] = 1.0f;
    r.halfW = r.halfH = 0.13f;
    r.pointCount = BW_LINK_RUNE_POINTS;
    for (uint32_t i = 0; i < r.pointCount; ++i) {
        const float a = 6.2831853f * (float)i / (float)(r.pointCount - 1);
        r.pts[i][0] = 0.12f * std::sin(a); r.pts[i][1] = 0.12f * std::cos(a);
    }
    r.trailCount = BW_LINK_RUNE_TRAIL;
    for (uint32_t i = 0; i < r.trailCount; ++i) {
        const float a = 4.7f * (float)i / (float)(r.trailCount - 1);
        r.trail[i][0] = 0.10f * std::sin(a); r.trail[i][1] = 1.60f + 0.10f * std::cos(a); r.trail[i][2] = -0.55f;
    }
    return r;
}

struct Writer {
    HANDLE map = nullptr;
    BW_LinkVrRunes* blk = nullptr;
    bool Open() {
        blk = (BW_LinkVrRunes*)bw::link::Open(BW_LINK_VRRUNES_NAME, &map);
        return blk != nullptr;
    }
    // The way the engine writes: one write, the rest of the block as it was.
    void Write(uint32_t count, const BW_LinkVrRune* runes, uint32_t stamp, uint32_t version = BW_LINK_VRRUNES_VERSION) {
        bw::link::WriteBegin(&blk->header);
        blk->stamp = stamp;
        blk->count = count;
        for (uint32_t i = 0; i < count && i < BW_LINK_VRRUNES_MAX; ++i) blk->rune[i] = runes[i];
        bw::link::WriteEnd(&blk->header, version, (uint32_t)sizeof(BW_LinkVrRunes));
    }
    ~Writer() { bw::link::Close(blk, map); }
};

}  // namespace

// rune_link_test --follow [seconds]: no checks of its own - reads what another process writes (tools/rune_fake_writer.cpp, or the
// real engine) the way the game does, and says what it saw: the state changes, and whether each rune makes a panel and a trail board.
int Follow(double seconds) {
    viewgate::Set(&TheView);
    BW_LinkVrRune got[BW_LINK_VRRUNES_MAX];
    const DWORD t0 = GetTickCount();
    uint32_t lastCount = 99, lastState = 99, frames = 0, withPanel = 0, withTrail = 0, shown = 0, maxTrail = 0;
    float maxProgress = 0.0f, maxFlash = 0.0f;
    static runes::TrailBoard board;
    while ((GetTickCount() - t0) / 1000.0 < seconds) {
        const uint32_t n = link::VrRunes(got, BW_LINK_VRRUNES_MAX);
        if (n != lastCount || (n && got[0].state != lastState)) {
            lastCount = n;
            lastState = n ? got[0].state : 0;
            printf("  %5.2f s: %u rune(s)%s\n", (GetTickCount() - t0) / 1000.0, n, n ? (got[0].state == BW_LINK_RUNE_DRAWING ? ", drawing" : got[0].state == BW_LINK_RUNE_FIRED ? ", fired" : ", failed") : "");
        }
        for (uint32_t i = 0; i < n; ++i) {
            ++frames;
            const runes::Look look = runes::LookOf(got[i]);
            runes::Panel p;
            shown += look.shown;
            withPanel += look.shown && runes::PicturePanel(got[i], look, &p);
            withTrail += look.shown && runes::ProjectTrail(got[i], look, {0.0f, 1.6f, 0.0f}, {0.0f, 0.0f, -1.0f}, &board);
            maxProgress = std::max(maxProgress, got[i].progress);
            maxFlash = std::max(maxFlash, got[i].flash);
            maxTrail = std::max(maxTrail, got[i].trailCount);
        }
        Sleep(8);
    }
    printf("rune_link_test --follow: %u rune frames read, %u shown, %u with a panel, %u with a trail board (head assumed at 0 1.6 0); the most progress %.2f, the most flash %.2f, up to %u trail points\n",
           frames, shown, withPanel, withTrail, maxProgress, maxFlash, maxTrail);
    viewgate::Set(nullptr);
    return frames ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "--follow")) return Follow(argc > 2 ? atof(argv[2]) : 9.0);
    // another process writing the real block (BodyWalk with the runes engine): leave it alone
    if (HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, BW_LINK_VRRUNES_NAME)) {
        BW_LinkHeader h{};
        if (const void* v = MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof h)) { memcpy(&h, v, sizeof h); UnmapViewOfFile(v); }
        CloseHandle(m);
        if (h.magic == BW_LINK_MAGIC && h.writerPid != GetCurrentProcessId() && bw::link::WriterAlive(h.writerPid)) {
            printf("rune_link_test: process %u is writing the real VrRunes block - nothing touched, test not run\n", h.writerPid);
            return 2;
        }
    }
    viewgate::Set(&TheView);
    BW_LinkVrRune got[BW_LINK_VRRUNES_MAX + 2];

    // 1. no writer yet: the reader makes the block and finds it empty (never written)
    Expect(link::VrRunes(got, BW_LINK_VRRUNES_MAX) == 0, "a block nobody has written: nothing");

    Writer w;
    Expect(w.Open(), "the writer opens the same block");
    const uint32_t now = D2RVRStampNow();
    BW_LinkVrRune a = Circle(11, 0.40f), b = Circle(12, 0.75f);
    b.state = BW_LINK_RUNE_FIRED; b.flash = 0.6f; b.pts[5][0] = 0.0123f; b.trail[50][2] = -0.57f;

    // 2. one rune, then two: the numbers arrive whole
    BW_LinkVrRune two[2] = {a, b};
    w.Write(1, two, now);
    Sleep(2);
    uint32_t n = link::VrRunes(got, BW_LINK_VRRUNES_MAX);
    Expect(n == 1 && got[0].id == 11 && got[0].progress == 0.40f && got[0].pointCount == 48 && got[0].trailCount == 96, "one rune on show: it arrives with its counts");
    Expect(std::fabs(got[0].pts[10][0] - a.pts[10][0]) < 1e-9f && got[0].trail[95][2] == a.trail[95][2] && got[0].litRgba == a.litRgba && got[0].halfW == 0.13f, "points, trail, colours, half sizes");
    w.Write(2, two, D2RVRStampNow());
    n = link::VrRunes(got, BW_LINK_VRRUNES_MAX);
    Expect(n == 2 && got[1].id == 12 && got[1].state == BW_LINK_RUNE_FIRED && got[1].flash == 0.6f && got[1].pts[5][0] == 0.0123f && got[1].trail[50][2] == -0.57f, "two runes: the second whole");
    Expect(link::VrRunes(got, 1) == 1 && got[0].id == 11, "room for one: the first");
    Expect(link::VrRunes(got, 0) == 0, "room for none: none");

    // 3. the end: the engine writes count 0, then nothing
    w.Write(0, nullptr, D2RVRStampNow());
    Expect(link::VrRunes(got, BW_LINK_VRRUNES_MAX) == 0, "count 0: nothing");

    // 4. the stamp: half a second
    w.Write(1, two, D2RVRStampNow() - 4000u);   // 0.4 s old
    Expect(link::VrRunes(got, BW_LINK_VRRUNES_MAX) == 1, "a block 0.4 s old is live");
    w.Write(1, two, D2RVRStampNow() - 6000u);   // 0.6 s old
    Expect(link::VrRunes(got, BW_LINK_VRRUNES_MAX) == 0, "a block 0.6 s old is stale: nothing drawn, no fade of our own");
    w.Write(1, two, D2RVRStampNow() + 20000u);  // a stamp from the future (a clock that is off): the unsigned difference is huge
    Expect(link::VrRunes(got, BW_LINK_VRRUNES_MAX) == 0, "a stamp ahead of our clock reads as stale, not as fresh for ever");
    w.Write(1, two, D2RVRStampNow());
    Expect(link::VrRunes(got, BW_LINK_VRRUNES_MAX) == 1, "a fresh write brings it back");

    // 5. the version, the magic, the count
    w.Write(1, two, D2RVRStampNow(), 2);
    Expect(link::VrRunes(got, BW_LINK_VRRUNES_MAX) == 0, "version 2 is not ours: nothing");
    w.Write(1, two, D2RVRStampNow());
    w.blk->header.magic = 0;
    Expect(link::VrRunes(got, BW_LINK_VRRUNES_MAX) == 0, "no magic: the block is not live");
    w.Write(1, two, D2RVRStampNow());
    bw::link::WriteBegin(&w.blk->header);
    w.blk->count = 9;
    bw::link::WriteEnd(&w.blk->header, BW_LINK_VRRUNES_VERSION, (uint32_t)sizeof(BW_LinkVrRunes));
    Expect(link::VrRunes(got, BW_LINK_VRRUNES_MAX + 2) == BW_LINK_VRRUNES_MAX, "a count of 9 is held to the four the block holds");

    // 5b. a writer caught mid-write (or preempted there): the last whole copy is used while it is fresh, nothing once it is stale
    w.Write(1, two, D2RVRStampNow());
    n = link::VrRunes(got, BW_LINK_VRRUNES_MAX);
    bw::link::WriteBegin(&w.blk->header);   // the sequence is odd now: no read gets a whole copy
    n = link::VrRunes(got, BW_LINK_VRRUNES_MAX);
    Expect(n == 1 && got[0].id == 11, "a write in progress: the last whole copy, not a torn one and not a blink");
    bw::link::WriteEnd(&w.blk->header, BW_LINK_VRRUNES_VERSION, (uint32_t)sizeof(BW_LinkVrRunes));
    w.Write(1, two, D2RVRStampNow() - 4900u);   // 0.49 s old
    link::VrRunes(got, BW_LINK_VRRUNES_MAX);
    bw::link::WriteBegin(&w.blk->header);
    Sleep(40);
    Expect(link::VrRunes(got, BW_LINK_VRRUNES_MAX) == 0, "a writer that died mid-write: the last copy goes stale in half a second like any other");
    bw::link::WriteEnd(&w.blk->header, BW_LINK_VRRUNES_VERSION, (uint32_t)sizeof(BW_LinkVrRunes));

    // 6. F4 only
    w.Write(2, two, D2RVRStampNow());
    g_f4 = false;
    Expect(link::VrRunes(got, BW_LINK_VRRUNES_MAX) == 0, "another view (F1 - F3, flat): nothing");
    g_f4 = true;
    Expect(link::VrRunes(got, BW_LINK_VRRUNES_MAX) == 2, "and back in F4: both again");

    // 7. what a reader gets is what the geometry takes
    w.Write(1, two, D2RVRStampNow());
    n = link::VrRunes(got, BW_LINK_VRRUNES_MAX);
    const runes::Look look = runes::LookOf(got[0]);
    runes::Panel p;
    static runes::TrailBoard board;
    Expect(n == 1 && look.shown && runes::PicturePanel(got[0], look, &p) && std::fabs(p.centre.z + 0.85f) < 1e-6f, "the block's rune makes a panel 0.85 m ahead");
    Expect(runes::ProjectTrail(got[0], look, {0.0f, 1.65f, 0.0f}, {0.0f, 0.0f, -1.0f}, &board) && board.segs > 90 && board.blob, "and a trail board from the head");
    Expect(runes::SegmentLit(0, got[0].progress, got[0].pointCount) && !runes::SegmentLit(46, got[0].progress, got[0].pointCount) &&
           runes::LitSegments(got[0].progress, got[0].pointCount) == 19, "40% of 47 segments: 19 lit (i < 18.8)");

    viewgate::Set(nullptr);
    printf("rune_link_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
