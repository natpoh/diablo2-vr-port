// vr/stats_leak.h: [debug] stats_leak_trace's bookkeeping - births and frees, a list born before the trace, the last holder's own lists left
// out of the leak, the verdict against the game's pool count, the log line (game RVAs, ours marked). Run: stats_leak_test.exe
#include "stats_leak.h"

#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

int main() {
    using namespace statsleak;
    const uintptr_t gameBase = 0x140000000ull;
    const size_t gameSize = 0x2789000;
    const Range ours{0x7FF800000000ull, 0x400000};

    Birth game;   // made by the game's own call chain (MonsterMode.cpp's 0x44786F)
    game.args[0] = 0x4004; game.args[2] = 5; game.args[3] = 1234;
    game.frames[0] = gameBase + 0x447874; game.frames[1] = gameBase + 0x4475C0 + 0x100; game.frameCount = 2;
    Birth mine = game;   // the same, but our hook was on the stack
    mine.frames[2] = ours.base + 0x1234; mine.frameCount = 3;

    Check(FirstIn(game, ours) == -1, "a game-only stack: not ours");
    Check(FirstIn(mine, ours) == 2, "our frame found where it is");

    Tracker t;
    t.Born(0x1000, game);
    t.Born(0x2000, mine);
    t.Born(0x3000, game);
    t.Born(0, game);   // a failed alloc: nothing
    Check(t.Live() == 3 && t.BornCount() == 3, "three born");
    t.Freed(0x1000);
    t.Freed(0x9000);   // born before the trace began
    Check(t.Live() == 2 && t.FreedCount() == 1 && t.FreedUnknown() == 1, "a known free and an unknown one");
    Check(!t.Knows(0x1000) && t.Knows(0x2000), "the freed one forgotten");

    // the last holder carries 0x3000: only 0x2000 leaks
    std::unordered_set<uintptr_t> carried{0x3000};
    auto leaks = t.Leaked(carried);
    Check(leaks.size() == 1 && leaks[0].list == 0x2000, "the carried list is not a leak");

    // nothing carried: both, oldest first
    leaks = t.Leaked({});
    Check(leaks.size() == 2 && leaks[0].list == 0x2000 && leaks[1].list == 0x3000, "oldest first");

    // an address reused after its free is a new birth
    t.Freed(0x2000);
    t.Born(0x2000, game);
    leaks = t.Leaked({});
    Check(leaks.size() == 2 && leaks[1].list == 0x2000 && FirstIn(leaks[1].birth, ours) == -1, "a reused address: its new birth");

    // the verdict against the game's own count
    Verdict v = Judge(3, 3, 0);
    Check(!v.leak && v.notCarried == 0, "all carried: no leak (the game does not assert)");
    v = Judge(5, 3, 1);
    Check(v.leak && v.notCarried == 2 && v.untracked == 1, "two not carried, one known: one born before the trace");
    v = Judge(4, 3, 3);   // (more known than the pool says - a free racing the walk): never below 0
    Check(v.leak && v.untracked == 0, "untracked never negative");

    // the log line: game RVAs, ours marked
    char line[600];
    Leak l{0x2000, mine};
    Describe(l, gameBase, gameSize, ours, line, sizeof line);
    const std::string s = line;
    Check(s.find("flags 0x4004") != std::string::npos, "the flags are told");
    Check(s.find("ids 5 1234") != std::string::npos, "the ids are told");
    Check(s.find(" 0x447874") != std::string::npos, "the game's frame as an RVA");
    Check(s.find("vrcam+0x1234") != std::string::npos, "our frame as ours");
    Check(s.find("D2R VR ON ITS STACK") != std::string::npos, "ours called out");
    Leak g{0x3000, game};
    Describe(g, gameBase, gameSize, ours, line, sizeof line);
    Check(std::strstr(line, "ON ITS STACK") == nullptr, "a game-only stack not called out");
    Birth odd = game; odd.frames[0] = 0x12345; odd.frameCount = 1;
    Describe(Leak{0x4000, odd}, gameBase, gameSize, ours, line, sizeof line);
    Check(std::strstr(line, "?12345") != nullptr, "a frame of neither module");
    char tiny[24];
    Describe(l, gameBase, gameSize, ours, tiny, sizeof tiny);   // never past the buffer
    Check(std::strlen(tiny) < sizeof tiny, "a short buffer: cut, not overrun");

    if (g_fail) { std::printf("%d FAILED\n", g_fail); return 1; }
    std::printf("stats_leak_test: all passed\n");
    return 0;
}
