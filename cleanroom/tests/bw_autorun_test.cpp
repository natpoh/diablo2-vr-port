// Offline checks of tools/settings/bw_autorun.h: what D2R VR Settings starts or stops of BodyWalk and FlatVR at the switch of the 3D in the
// headset (FlatVR <-> native OpenXR) and at the game's start - by mode, already running, not installed, a second copy, the keys off; BodyWalk
// is never closed - and the steps of a gentle close of a FlatVR.exe (never killed once it is on its way out).
// Run: build/Release/bw_autorun_test.exe (exit code 0 = passed)

#include <cstdio>
#include <cwchar>
#include <initializer_list>

#include "../../tools/settings/bw_autorun.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using namespace bwauto;

static bool Idle(const Plan& p) { return !p.startBodyWalk && !p.startFlatVr && !p.stopFlatVr && !p.closeFlatVrApp; }

// FlatVR picked, BodyWalk installed, nothing running
static Facts FlatVr(When when) {
    Facts f;
    f.when = when;
    f.vr = true;
    f.native = false;
    f.bodyWalkFound = true;
    return f;
}
// native OpenXR with Controls without BodyWalk, BodyWalk running with Game Link and FlatVR started in it
static Facts Native(When when) {
    Facts f;
    f.when = when;
    f.vr = true;
    f.native = true;
    f.directInput = true;
    f.bodyWalkFound = true;
    f.bodyWalkRunning = 1;
    f.linkUp = true;
    f.linkStopsFlatVr = true;
    f.flatVrRunning = true;
    return f;
}

int main() {
    // --- flat mode: never anything, whatever runs and whatever the keys say
    for (When w : {When::ModeSwitch, When::GameStart}) {
        Facts f = FlatVr(w);
        f.vr = false;
        CHECK(Idle(Decide(f)) && Decide(f).why == Why::Flat);
        f = Native(w);
        f.vr = false;
        f.flatVrAppRunning = 1;
        CHECK(Idle(Decide(f)) && Decide(f).why == Why::Flat);
    }

    // --- FlatVR, switched to in Settings
    {
        Plan p = Decide(FlatVr(When::ModeSwitch));
        CHECK(p.startBodyWalk && p.startFlatVr && !p.stopFlatVr && !p.closeFlatVrApp && p.why == Why::FlatVrStart);
        Facts f = FlatVr(When::ModeSwitch);
        f.bodyWalkRunning = 1;   // BodyWalk already up: not a second one, only FlatVR's Start
        p = Decide(f);
        CHECK(!p.startBodyWalk && p.startFlatVr && p.why == Why::FlatVrStart);
        f.bodyWalkRunning = 2;   // two copies running: still none started
        p = Decide(f);
        CHECK(!p.startBodyWalk && p.startFlatVr);
        f.flatVrRunning = true;   // both up: nothing to do
        p = Decide(f);
        CHECK(Idle(p) && p.why == Why::Nothing);
        f = FlatVr(When::ModeSwitch);
        f.bodyWalkFound = false;   // not installed: no BodyWalk started, the log says why; FlatVR's Start waits for one started by hand
        p = Decide(f);
        CHECK(!p.startBodyWalk && p.startFlatVr && p.why == Why::FlatVrNoBodyWalk);
        f = FlatVr(When::ModeSwitch);
        f.startOn = false;   // the key off: the switch starts nothing (as before)
        CHECK(Idle(Decide(f)) && Decide(f).why == Why::Nothing);
        f.closeOn = false;   // (the other key has nothing to say in FlatVR)
        CHECK(Idle(Decide(f)));
        f = FlatVr(When::ModeSwitch);
        f.closeOn = false;
        CHECK(Decide(f).startBodyWalk && Decide(f).startFlatVr);
    }

    // --- FlatVR at the game's start: as before 2026-10-29 whatever the key says (BodyWalk when none runs, FlatVR once Game Link is up)
    for (bool key : {true, false}) {
        Facts f = FlatVr(When::GameStart);
        f.startOn = key;
        Plan p = Decide(f);
        CHECK(p.startBodyWalk && p.startFlatVr && !p.stopFlatVr);
        f.bodyWalkRunning = 1;
        p = Decide(f);
        CHECK(!p.startBodyWalk && p.startFlatVr);
        f.flatVrRunning = true;
        CHECK(Idle(Decide(f)));
        f = FlatVr(When::GameStart);
        f.startOn = key;
        f.bodyWalkFound = false;
        CHECK(!Decide(f).startBodyWalk && Decide(f).startFlatVr && Decide(f).why == Why::FlatVrNoBodyWalk);
        f = FlatVr(When::GameStart);
        f.startOn = key;
        f.flatVrAppRunning = 1;   // a FlatVR of its own running: left alone in FlatVR mode
        CHECK(!Decide(f).closeFlatVrApp);
    }

    // --- native, with or without Controls without BodyWalk: only FlatVR stopped; BodyWalk stays, at the switch and at the start
    for (When w : {When::ModeSwitch, When::GameStart})
        for (bool direct : {true, false}) {
            Facts f = Native(w);
            f.directInput = direct;
            Plan p = Decide(f);
            CHECK(p.stopFlatVr && !p.startBodyWalk && !p.startFlatVr && !p.closeFlatVrApp && p.why == Why::NativeStopFlatVr);
            f.bodyWalkRunning = 2;   // a second copy: still only FlatVR's Stop
            CHECK(Decide(f).stopFlatVr && !Decide(f).startBodyWalk);
            f = Native(w);
            f.directInput = direct;
            f.flatVrRunning = false;   // FlatVR already stopped: nothing (BodyWalk stays)
            CHECK(Idle(Decide(f)) && Decide(f).why == Why::Nothing);
            f = Native(w);
            f.directInput = direct;
            f.flatVrAppRunning = 1;   // FlatVR on its own holds the headset: that one closed, BodyWalk stays
            p = Decide(f);
            CHECK(p.closeFlatVrApp && !p.stopFlatVr && !p.startBodyWalk && p.why == Why::NativeCloseFlatVr);
            f.bodyWalkRunning = 0;   // ... with no BodyWalk at all too
            f.linkUp = f.linkStopsFlatVr = false;
            p = Decide(f);
            CHECK(p.closeFlatVrApp && !p.stopFlatVr && p.why == Why::NativeCloseFlatVr);
            f = Native(w);
            f.directInput = direct;
            f.linkUp = f.linkStopsFlatVr = false;   // a BodyWalk with no Game Link (Lite) and its FlatVR holding the headset: nothing it can be
            p = Decide(f);                          // told - left as it is (BodyWalk is never closed), the log says what to do
            CHECK(Idle(p) && p.why == Why::NativeCannotStop);
            f.linkUp = true;   // Game Link up, but this BodyWalk cannot stop FlatVR: the same
            CHECK(Idle(Decide(f)) && Decide(f).why == Why::NativeCannotStop);
            f = Native(w);
            f.directInput = direct;
            f.closeOn = false;   // the key off: as before - nothing stopped or closed
            f.flatVrAppRunning = 1;
            CHECK(Idle(Decide(f)) && Decide(f).why == Why::Nothing);
            f.startOn = false;
            CHECK(Idle(Decide(f)));
        }
    {   // native through BodyWalk's Mapping at the start, none running: BodyWalk started as before (the key on or off); the switch starts none;
        // with Controls without BodyWalk none is started
        Facts f = Native(When::GameStart);
        f.directInput = false;
        f.bodyWalkRunning = 0;
        f.flatVrRunning = false;
        f.linkUp = f.linkStopsFlatVr = false;
        Plan p = Decide(f);
        CHECK(p.startBodyWalk && !p.startFlatVr && !p.stopFlatVr && p.why == Why::NativeStartBodyWalk);
        f.closeOn = false;
        CHECK(Decide(f).startBodyWalk && Decide(f).why == Why::NativeStartBodyWalk);
        f.bodyWalkFound = false;
        CHECK(Idle(Decide(f)));
        f = Native(When::ModeSwitch);
        f.directInput = false;
        f.bodyWalkRunning = 0;
        f.flatVrRunning = false;
        CHECK(Idle(Decide(f)));
        f = Native(When::GameStart);
        f.bodyWalkRunning = 0;
        f.flatVrRunning = false;
        CHECK(Idle(Decide(f)));
    }
    {   // every combination: the rules that always hold
        for (int bits = 0; bits < (1 << 12); ++bits) {
            Facts f;
            f.when = bits & 1 ? When::GameStart : When::ModeSwitch;
            f.vr = (bits & 2) != 0;
            f.native = (bits & 4) != 0;
            f.directInput = (bits & 8) != 0;
            f.startOn = (bits & 16) != 0;
            f.closeOn = (bits & 32) != 0;
            f.bodyWalkFound = (bits & 64) != 0;
            f.bodyWalkRunning = bits & 128 ? 1 : 0;
            f.linkUp = (bits & 256) != 0;
            f.linkStopsFlatVr = f.linkUp && (bits & 512) != 0;
            f.flatVrRunning = (bits & 1024) != 0;
            f.flatVrAppRunning = bits & 2048 ? 1 : 0;
            const Plan p = Decide(f);
            CHECK(!(p.startFlatVr && p.stopFlatVr));
            CHECK(!(p.startBodyWalk && f.bodyWalkRunning));   // never a second BodyWalk
            CHECK(!(p.startBodyWalk && !f.bodyWalkFound));
            CHECK(f.vr || Idle(p));
            CHECK(f.native || (!p.closeFlatVrApp && !p.stopFlatVr));          // FlatVR mode stops nothing
            CHECK(!f.native || !p.startFlatVr);                                // native starts no FlatVR
            CHECK(!f.native || f.closeOn || (!p.closeFlatVrApp && !p.stopFlatVr));
            CHECK(!p.stopFlatVr || f.linkStopsFlatVr);                         // asked only of a BodyWalk that takes it
            CHECK(!p.closeFlatVrApp || f.flatVrAppRunning);
            CHECK(!(f.native && f.directInput && p.startBodyWalk));            // native with its own controls starts no BodyWalk
            CHECK(f.native || f.when != When::ModeSwitch || f.startOn || Idle(p));
            CHECK(std::wcslen(Text(p.why)) > 0);
        }
    }

    // --- the gentle close's steps
    CHECK(NextCloseStep(true, true, true, 0) == CloseStep::Done);
    CHECK(NextCloseStep(true, false, false, kCloseWaitMs + kExitWaitMs + 1) == CloseStep::Done);
    CHECK(NextCloseStep(false, true, false, 0) == CloseStep::Wait);                    // just asked
    CHECK(NextCloseStep(false, true, true, kCloseWaitMs - 1) == CloseStep::Wait);      // not answering yet, but not waited long enough
    CHECK(NextCloseStep(false, true, true, kCloseWaitMs) == CloseStep::Kill);          // the last resort: window there, not answering
    CHECK(NextCloseStep(false, true, false, kCloseWaitMs) == CloseStep::GiveUp);       // answers and kept its window (asks something): left
    CHECK(NextCloseStep(false, false, false, kCloseWaitMs) == CloseStep::Wait);        // window gone: on its way out, writing its settings
    CHECK(NextCloseStep(false, false, true, kCloseWaitMs + kExitWaitMs - 1) == CloseStep::Wait);
    CHECK(NextCloseStep(false, false, true, kCloseWaitMs + kExitWaitMs) == CloseStep::GiveUp);   // never killed once its window is gone
    for (unsigned ms = 0; ms < 60000; ms += 250) CHECK(NextCloseStep(false, false, true, ms) != CloseStep::Kill);

    std::printf("bw_autorun_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
