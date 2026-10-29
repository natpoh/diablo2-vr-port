// depth_pick_test - vr/depth_pick.h: the depth-stencil for our effects is picked once and kept; looked for again only when it is
// destroyed, the swap chain is made again, the screen's size changes, or the game stops binding it (not at an area change); never picked
// without two frames of counts behind it; never copied in a frame the game did not bind it in.
#include "depth_pick.h"

#include <cstdio>
#include <cstring>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using depthpick::Cache;
static const uint64_t A = 0x1000, B = 0x2000;

// A frame as vrcam has it: the game binds its scene depth-stencil (or not), then our effect passes check and copy, then the present.
static bool Frame(Cache& c, uint64_t boundByGame, uint32_t w = 1920, uint32_t h = 1920) {
    if (!c.Counting() && c.WantsSight()) c.Seen(boundByGame);
    c.Check(w, h);
    const bool copy = c.CopyOk();
    c.FrameEnd();
    return copy;
}
static Cache& Picked(Cache& c, uint64_t res, uint32_t w = 1920, uint32_t h = 1920) {
    c.FrameEnd(); c.FrameEnd();
    CHECK(c.Commit(res, true, w, h));
    return c;
}

int main() {
    {   // the first pick waits for two frames of counts and a bind this frame, then holds
        Cache c;
        CHECK(c.Counting() && !c.CopyOk());
        CHECK(!c.Commit(A, true, 1920, 1920));    // no counts yet
        c.FrameEnd();
        CHECK(!c.Commit(A, true, 1920, 1920));    // one frame
        c.FrameEnd();
        CHECK(!c.Commit(A, false, 1920, 1920));   // counted, but not bound this frame: never
        CHECK(c.Commit(A, true, 1920, 1920));
        CHECK(!c.Counting() && c.CopyOk());          // its bind this frame came while counting
        CHECK(c.picks == 1 && c.others == 0);
        CHECK(std::strcmp(c.why, "the first pick") == 0);
        c.FrameEnd();
        // held: the game binds it every frame, nothing lets it go, each frame copies, a second Commit is refused
        for (int i = 0; i < 1000; ++i) CHECK(Frame(c, A));
        CHECK(c.pick.load() == A && c.counted == 2);
        CHECK(!c.Commit(B, true, 1920, 1920));
    }
    {   // the bind hook stops looking once it has seen the pick this frame
        Cache c;
        Picked(c, A);
        c.FrameEnd();                                // a new frame: not seen yet
        CHECK(c.WantsSight() && !c.CopyOk());
        c.Seen(B);                                   // another depth-stencil (a shadow map): still looking
        CHECK(c.WantsSight());
        c.Seen(A);
        CHECK(!c.WantsSight() && c.CopyOk());
    }
    {   // walking: the area changes every few seconds (test-38, 10:25) - nothing of that reaches the pick: held, a copy every frame
        Cache c;
        Picked(c, A);
        c.FrameEnd();
        for (int i = 0; i < 600; ++i) CHECK(Frame(c, A));
        CHECK(c.pick.load() == A && c.picks == 1);
    }
    {   // the window's or the render size changes: let go; another buffer is counted as one
        Cache c;
        Picked(c, A);
        const char* why = c.Check(2048, 2048);
        CHECK(why && std::strcmp(why, "the window's or the render size changed") == 0);
        c.FrameEnd(); c.FrameEnd();
        CHECK(c.Commit(B, true, 2048, 2048));
        CHECK(!c.sameAsBefore && c.others == 1);
        CHECK(c.Check(1920, 1920) != nullptr);      // and back: let go again
        c.FrameEnd(); c.FrameEnd();
        CHECK(c.Commit(B, true, 1920, 1920));
        CHECK(c.sameAsBefore && c.others == 1);     // the same buffer is told as such
        CHECK(c.Check(1920, 1920) == nullptr);   // held at the size it was picked for
    }
    {   // the picked buffer destroyed (the destroy hook, another thread): no copy at once, the reason at the next check
        Cache c;
        Picked(c, A);
        c.Destroyed(B);                             // another buffer going: nothing
        CHECK(c.pick.load() == A);
        c.Destroyed(A);
        CHECK(c.Counting() && !c.CopyOk());
        const char* why = c.Check(1920, 1920);
        CHECK(why && std::strcmp(why, "the picked buffer was destroyed") == 0);
        CHECK(c.Check(1920, 1920) == nullptr);   // told once
        c.Destroyed(0);                             // a null handle: nothing
        c.FrameEnd(); c.FrameEnd();
        CHECK(c.Commit(B, true, 1920, 1920));
        CHECK(c.others == 1);
    }
    {   // test-38's crash: the render size slider makes the swap chain again 2048 -> 1920 -> 2048 while the effects are
        // reloading (no effect pass, so no size check in between); the game frees the old depth's memory but releases the
        // buffer itself later (no destroy event) and binds a new one. The old pick must never be copied again.
        Cache c;
        Picked(c, A, 2048, 2048);
        c.FrameEnd();
        CHECK(Frame(c, A, 2048, 2048));             // played normally
        c.SwapChainChanged();                       // ResizeBuffers 1920 (destroy + init events)
        CHECK(c.Counting() && !c.CopyOk());         // no copy from here on
        c.SwapChainChanged();                       // ResizeBuffers 2048: nothing more to let go
        for (int i = 0; i < 30; ++i) c.FrameEnd();  // the effects reloading: presents, no effect pass
        // the replay's first pair: the left eye's pass checks first - the size is 2048 again
        const char* why = c.Check(2048, 2048);
        CHECK(why && std::strcmp(why, "the swap chain was made again") == 0);
        CHECK(!c.CopyOk());                         // left eye: no copy
        CHECK(c.Check(2048, 2048) == nullptr);
        CHECK(!c.CopyOk());                         // right eye (the same frame): no copy
        c.FrameEnd(); c.FrameEnd();
        CHECK(!c.Commit(A, false, 2048, 2048));  // the old buffer, not bound any more: never picked again
        CHECK(c.Commit(B, true, 2048, 2048));    // the game's new one
        CHECK(c.CopyOk() && c.others == 1);
    }
    {   // the same without the swap chain event: the game simply stops binding the pick - one frame, then it goes
        Cache c;
        Picked(c, A);
        c.FrameEnd();
        CHECK(Frame(c, A));
        CHECK(!Frame(c, B));                        // B bound, A not: no copy of A this frame
        CHECK(c.Counting());                        // let go at the frame's end
        const char* why = c.Check(1920, 1920);
        CHECK(why && std::strcmp(why, "the game did not bind it in a frame") == 0);
        CHECK(!c.CopyOk());
    }
    {   // a pair: the left pass binds it, both eyes copy in that frame; the replay binds nothing of its own
        Cache c;
        Picked(c, A);
        c.FrameEnd();
        c.Seen(A);                                  // the left pass
        CHECK(c.Check(1920, 1920) == nullptr && c.CopyOk());   // left eye's effects
        CHECK(c.Check(1920, 1920) == nullptr && c.CopyOk());   // right eye's effects after the replay
        c.FrameEnd();                               // the pair's one present
        CHECK(!c.Counting() && !c.CopyOk());        // held; the next pair must see it again
    }
    {   // no buffer to pick: nothing held
        Cache c;
        c.FrameEnd(); c.FrameEnd();
        CHECK(!c.Commit(0, true, 1920, 1920));
        CHECK(c.Counting() && !c.CopyOk());
    }

    if (g_fail) { std::printf("depth_pick_test: %d FAILED\n", g_fail); return 1; }
    std::printf("depth_pick_test: picked once and held; let go on a destroy / a swap chain made again / a new size / a "
                "frame without its bind, held while walking; never copied in a frame the game did not bind it (test-38's resize): all passed\n");
    return 0;
}
