// swapchain_pick_test - vr/swapchain_pick.h: which D3D12 swap chain is taken as the game's (an overlay plugin's tiny probe never is), and the
// game's queue held by a counted reference whose old value outlives one more change (crash report 396, 2026-11-21). Pure, no GPU.
// Run: swapchain_pick_test.exe - prints each failed check and "N passed, M failed"; exit code = failures.
#include <cstdio>

#include "swapchain_pick.h"

namespace {
int g_pass = 0, g_fail = 0;
void Check(bool ok, const char* what) {
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("FAILED: %s\n", what); }
}

using swapchainpick::Adopt;
using swapchainpick::HeldRef;
using swapchainpick::Pick;

// A COM-like object that counts its references; "freed" when the count reaches 0 (a use after that is the bug 396 was).
struct FakeQueue {
    int refs = 1;   // the creator's (the swap chain's) reference
    bool freed = false;
    int usesAfterFree = 0;
    unsigned long AddRef() {
        if (freed) ++usesAfterFree;
        return (unsigned long)++refs;
    }
    unsigned long Release() {
        if (freed) { ++usesAfterFree; return 0; }
        if (--refs == 0) freed = true;
        return (unsigned long)refs;
    }
    void Use() {   // the resize drain's Signal on it
        if (freed) ++usesAfterFree;
    }
};
}  // namespace

int main() {
    // ---- which swap chain is the game's -------------------------------------------------------------------------------------------------------
    Check(Adopt(64, 64) == Pick::LeaveProbe, "report 396: the 64x64 swap chain of another plugin is a probe");
    Check(Adopt(1, 1) == Pick::LeaveProbe, "a 1x1 probe");
    Check(Adopt(100, 100) == Pick::LeaveProbe, "a 100x100 probe (the usual overlay hook's dummy)");
    Check(Adopt(255, 255) == Pick::LeaveProbe, "just under the probe size both ways");
    Check(Adopt(1920, 1057) == Pick::Adopt, "report 396: the game's 1920x1057 is the game's");
    Check(Adopt(1920, 1080) == Pick::Adopt, "a 1080p window");
    Check(Adopt(1347, 1347) == Pick::Adopt, "D2R VR's square window");
    Check(Adopt(2496, 2688) == Pick::Adopt, "the headset's render size");
    Check(Adopt(800, 600) == Pick::Adopt, "a small windowed game");
    Check(Adopt(256, 64) == Pick::Adopt, "one side as big as kProbeSide: not a probe");
    Check(Adopt(64, 300) == Pick::Adopt, "a tall narrow window: not a probe (only both sides small)");
    Check(Adopt(0, 0) == Pick::LeaveProbe, "an empty size (never the game's)");

    // ---- the queue held -----------------------------------------------------------------------------------------------------------------------
    {   // report 396's order, now with the probe left out: the game's queue is the one kept, and stays alive after the probe goes
        FakeQueue game, probe;
        HeldRef<FakeQueue> held;
        held.Store(&game);
        Check(held.Get() == &game && game.refs == 2, "the game's queue taken with a reference of ours");
        if (Adopt(64, 64) == Pick::Adopt) held.Store(&probe);   // (left out: the probe is never stored)
        probe.Release();   // the plugin frees its probe
        Check(probe.freed && held.Get() == &game, "the probe freed; the queue kept is still the game's");
        held.Get()->Use();
        Check(game.usesAfterFree == 0 && probe.usesAfterFree == 0, "the resize drain signals on a live queue");
    }
    {   // even if a probe were stored (the old Took), the reference keeps it alive until two changes later
        FakeQueue game, probe;
        HeldRef<FakeQueue> held;
        held.Store(&game);
        held.Store(&probe);
        probe.Release();   // the plugin lets its probe go: our reference keeps it
        Check(!probe.freed, "a stored queue is not freed under us when its maker lets it go");
        held.Get()->Use();
        Check(probe.usesAfterFree == 0, "using it after its maker let go: still a live object");
        Check(!game.freed && game.refs == 2, "the replaced queue still held (one change late) - a reader of a moment ago is safe");
        FakeQueue third;
        held.Store(&third);
        Check(game.refs == 1, "two changes later the first queue's reference is given back");
        Check(probe.refs == 1 && !probe.freed, "the second one is the one held back now");
        held.Clear();
        Check(probe.freed && third.refs == 1 && !third.freed, "Clear gives back both of ours, nothing more");
        Check(game.usesAfterFree == 0 && probe.usesAfterFree == 0 && third.usesAfterFree == 0, "no use after free anywhere");
    }
    {   // the same queue stored again (the game's swap chain made again on the same queue): no extra reference, nothing released
        FakeQueue game;
        HeldRef<FakeQueue> held;
        held.Store(&game);
        held.Store(&game);
        held.Store(&game);
        Check(game.refs == 2, "the same queue again: one reference of ours, not three");
        held.Clear();
        Check(game.refs == 1 && !game.freed, "Clear: the creator's reference left");
    }
    {   // null: nothing held
        HeldRef<FakeQueue> held;
        held.Store(nullptr);
        Check(held.Get() == nullptr, "nothing stored: Get is null");
        FakeQueue q;
        held.Store(&q);
        held.Store(nullptr);
        Check(held.Get() == nullptr && q.refs == 2, "null stored after a queue: that queue held back one change");
        held.Store(nullptr);
        Check(q.refs == 2, "null again is no change: still held back");
        FakeQueue r;
        held.Store(&r);
        Check(q.refs == 1 && r.refs == 2, "the next real change gives it back");
        held.Clear();
    }
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
