// log_queue_test.cpp - vr/log_queue.h on its own: the ring of lines and the pump that empties it, with a made-up sink (no game, no host).
//   log_queue_test.exe     exit code 0 = every check passed
#include "log_queue.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace logqueue;

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Collected {
    std::mutex m;
    std::vector<std::string> lines;
    int delayMs = 0;        // the sink is slow (the host's file)
    int delayedLines = 1;   // ... for the first this many lines
    void Add(const char* t) {
        int d = 0;
        {
            std::lock_guard<std::mutex> lock(m);
            if (delayedLines > 0) { d = delayMs; --delayedLines; }
            lines.push_back(t);
        }
        if (d) SleepMs(d);
    }
    size_t Size() {
        std::lock_guard<std::mutex> lock(m);
        return lines.size();
    }
};
static void Sink(void* user, const char* text) { static_cast<Collected*>(user)->Add(text); }

int main() {
    // the ring: lines come out as they went in, whatever their lengths, through the end of the ring
    {
        Queue q(256);
        int64_t at = 0;
        std::string t;
        CHECK(!q.Pop(&at, &t) && q.Empty());
        CHECK(q.Push("first", 11) && q.Push("", 22) && q.Push("third line", 33));
        CHECK(!q.Empty() && q.Bytes() == 3 * 12 + 5 + 0 + 10);
        CHECK(q.Pop(&at, &t) && t == "first" && at == 11);
        CHECK(q.Pop(&at, &t) && t.empty() && at == 22);
        CHECK(q.Pop(&at, &t) && t == "third line" && at == 33);
        CHECK(!q.Pop(&at, &t) && q.Empty() && q.Bytes() == 0);
        CHECK(!q.Push(nullptr, 1));
        // many rounds of uneven lines through a small ring: the wrap is invisible
        int made = 0, got = 0;
        bool ok = true;
        for (int round = 0; round < 500 && ok; ++round) {
            const int burst = 1 + round % 5;
            for (int i = 0; i < burst; ++i) {
                std::string s = "line " + std::to_string(made) + std::string((size_t)(made * 7 % 40), 'x');
                if (!q.Push(s.c_str(), made)) break;   // (full: the burst ends here)
                ++made;
            }
            while (q.Pop(&at, &t)) {
                std::string want = "line " + std::to_string(got) + std::string((size_t)(got * 7 % 40), 'x');
                ok = ok && t == want && at == got;
                ++got;
            }
        }
        CHECK(ok && made == got && made > 400);
    }
    // a full ring says so, and has room again after a Pop
    {
        Queue q(100);
        int n = 0;
        while (q.Push("0123456789", n)) ++n;   // 22 bytes a line
        CHECK(n == 4 && !q.Push("0123456789", 99));
        int64_t at = 0;
        std::string t;
        CHECK(q.Pop(&at, &t) && at == 0);
        CHECK(q.Push("0123456789", 100));
        CHECK(!q.Push(std::string(200, 'z').c_str(), 101));   // longer than the ring
    }
    // a line longer than the limit is cut to it
    {
        Queue q;
        const std::string big(kMaxText + 500, 'q');
        CHECK(q.Push(big.c_str(), 1));
        int64_t at = 0;
        std::string t;
        CHECK(q.Pop(&at, &t) && t.size() == kMaxText && t == big.substr(0, kMaxText));
    }
    // a Pop that must not wait works as any when nobody holds the lock
    {
        Queue q;
        q.Push("a", 1);
        int64_t at = 0;
        std::string t;
        CHECK(q.Pop(&at, &t, false) && t == "a");
    }
    // a line that waited says so; one that did not is left as it was
    {
        CHECK(Dated("text", 1000000000, 1000000000 + 49000000) == "text");
        const std::string late = Dated("text", 1000000000, 1000000000 + 120000000);
        CHECK(late.rfind("text [logged ", 0) == 0 && late.find("120 ms") != std::string::npos);
        CHECK(Dated("text", 5, 4) == "text");   // a clock that read backwards: no suffix
    }
    // the pump: not running, nothing is taken; running, every line comes out, in order, on another thread; stopped, the rest is written
    {
        Collected c;
        Pump p;
        CHECK(!p.Running() && !p.Post("before the start"));
        CHECK(p.Start(&Sink, &c) && p.Running() && p.Start(&Sink, &c));   // (a second Start is a no-op)
        for (int i = 0; i < 1000; ++i) CHECK(p.Post(("line " + std::to_string(i)).c_str()));
        p.Stop();
        CHECK(!p.Running() && !p.Post("after the stop"));
        CHECK(c.lines.size() == 1000);
        bool inOrder = true;
        for (size_t i = 0; i < c.lines.size(); ++i) inOrder = inOrder && c.lines[i] == "line " + std::to_string(i);
        CHECK(inOrder);
        // it can start again
        CHECK(p.Start(&Sink, &c) && p.Post("again"));
        p.Stop();
        CHECK(c.lines.size() == 1001 && c.lines.back() == "again");
    }
    // four threads at once: nothing lost, nothing twice, each thread's own lines in order (a line the full ring refused is written by its thread)
    {
        Collected c;
        Pump p;
        CHECK(p.Start(&Sink, &c));
        std::mutex dm;
        std::vector<std::string> direct;
        std::vector<std::thread> th;
        constexpr int kThreads = 4, kEach = 5000;
        for (int t = 0; t < kThreads; ++t)
            th.emplace_back([&, t] {
                for (int i = 0; i < kEach; ++i) {
                    const std::string s = std::to_string(t) + ":" + std::to_string(i);
                    if (!p.Post(s.c_str())) {
                        std::lock_guard<std::mutex> lock(dm);
                        direct.push_back(s);
                    }
                }
            });
        for (auto& x : th) x.join();
        p.Stop();
        CHECK(c.lines.size() + direct.size() == (size_t)kThreads * kEach);
        int next[kThreads] = {};
        bool orderOk = true;
        std::vector<char> seen((size_t)kThreads * kEach, 0);
        auto note = [&](const std::string& s, bool checkOrder) {
            const size_t colon = s.find(':');
            const int t = std::stoi(s.substr(0, colon)), i = std::stoi(s.substr(colon + 1));
            if (seen[(size_t)t * kEach + i]) orderOk = false;   // twice
            seen[(size_t)t * kEach + i] = 1;
            if (checkOrder) { if (i < next[t]) orderOk = false; next[t] = i; }
        };
        for (const std::string& s : c.lines) note(s, true);
        for (const std::string& s : direct) note(s, false);
        bool all = true;
        for (char v : seen) all = all && v;
        CHECK(orderOk && all);
    }
    // a Drain while the writer is inside a slow write waits only as long as it is told, then does not write; later it does
    {
        Collected c;
        c.delayMs = 120;
        c.delayedLines = 1;
        Pump p;
        CHECK(p.Start(&Sink, &c));
        p.Post("one");
        SleepMs(40);   // the writer has "one" and is stuck in the sink
        p.Post("two");
        p.Post("three");
        CHECK(!p.Drain(10));
        CHECK(p.Drain(2000));   // the writer's batch over: the rest written (by it, or by this drain), in order
        p.Stop();
        CHECK(c.lines.size() == 3 && c.lines[0] == "one" && c.lines[1].rfind("two", 0) == 0 && c.lines[2].rfind("three", 0) == 0);
    }
    // a line that waited behind a slow write says so
    {
        Collected c;
        c.delayMs = 150;
        c.delayedLines = 1;
        Pump p;
        CHECK(p.Start(&Sink, &c));
        p.Post("slow");
        SleepMs(30);
        p.Post("waits");
        p.Stop();
        CHECK(c.lines.size() == 2 && c.lines[0] == "slow" && c.lines[1].find("waits [logged ") == 0);
    }
    // a sink that hangs (the host's file): Stop gives up after about two seconds and returns, the unload does not hang on the log
    {
        Collected* c = new Collected;   // (leaked, and the pump with it: the thread that was left may still be using them)
        c->delayMs = 3500;
        c->delayedLines = 1;
        Pump* p = new Pump;
        CHECK(p->Start(&Sink, c));
        p->Post("hang");
        SleepMs(30);
        const int64_t t0 = NowNs();
        p->Stop();
        const double ms = (double)(NowNs() - t0) * 1e-6;
        std::printf("log_queue_test: Stop on a hung sink returned after %.0f ms\n", ms);
        CHECK(!p->Running() && ms > 1500.0 && ms < 3300.0);
    }
    // what is posted from a thread costs the thread next to nothing: a few hundred nanoseconds a line (told, held to a loose bound)
    {
        Collected c;
        Pump p;
        CHECK(p.Start(&Sink, &c));
        constexpr int kN = 20000;
        const char* line = "vrcam: pad: buttons 1000 LT 0 RT 0 stick 0 0 -- a line of the usual length, about so long";
        const int64_t t0 = NowNs();
        for (int i = 0; i < kN; ++i) p.Post(line);
        const double ns = (double)(NowNs() - t0) / kN;
        p.Stop();
        std::printf("log_queue_test: a posted line costs %.0f ns on the posting thread\n", ns);
        CHECK(ns < 20000.0);
        CHECK(c.lines.size() <= (size_t)kN);
    }

    if (g_fail) std::printf("log_queue_test: %d of %d checks FAILED\n", g_fail, g_n);
    else std::printf("log_queue_test: all %d checks passed\n", g_n);
    return g_fail ? 1 : 0;
}
