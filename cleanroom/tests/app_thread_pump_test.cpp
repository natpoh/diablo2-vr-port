// Co-op join crash (2026-11-11): the network SDK's client calls only on the game's app thread (vr/app_thread_pump.h).
//
// The pump is the scheduling alone: a queue that only the "app thread" drains, fed from any other thread. Here the app thread is a real
// std::thread that waits for submitted tokens and drains - the plugin's timer is another real thread. The properties:
//   - a closure posted from the timer thread never runs on it (not inline, not ever), only on the app thread;
//   - the latest closure of a slot wins, other slots are independent;
//   - a failed submit is asked again by the next Post at once; a lost job is replaced after the lease and the late one does nothing;
//   - many posters at once: every call ends up on the app thread, none lost beyond the "latest wins" rule.
#include "app_thread_pump.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace {

int g_fails = 0;
void Check(bool ok, const char* what) {
    if (!ok) { ++g_fails; std::printf("FAIL: %s\n", what); }
}

// the fake loader: runOnUiThread queues the token, the app thread's loop runs Drain for it
uint32_t Tid() { return (uint32_t)std::hash<std::thread::id>{}(std::this_thread::get_id()) | 1u; }

struct Loader {
    appthread::Pump pump;
    std::mutex mu;
    std::condition_variable cv;
    std::deque<uint32_t> tokens;
    std::atomic<bool> stop{false};
    std::atomic<bool> refuse{false};   // submit fails (the thread service said no)
    std::atomic<bool> hold{false};     // the app thread does not run its jobs for now (a stall, a menu, a loading screen)
    std::atomic<int> submitted{0};
    std::atomic<uint32_t> appTid{0};
    std::thread app;
};
Loader* g_l = nullptr;

bool Submit(uint32_t token) {
    if (g_l->refuse.load()) return false;
    {
        std::lock_guard<std::mutex> lock(g_l->mu);
        g_l->tokens.push_back(token);
    }
    ++g_l->submitted;
    g_l->cv.notify_all();
    return true;
}

void AppLoop(Loader* l) {
    l->appTid.store(Tid());
    for (;;) {
        uint32_t token = 0;
        {
            std::unique_lock<std::mutex> lock(l->mu);
            l->cv.wait_for(lock, std::chrono::milliseconds(2), [&] { return l->stop.load() || (!l->hold.load() && !l->tokens.empty()); });
            if (l->stop.load() && l->tokens.empty()) return;
            if (l->hold.load() || l->tokens.empty()) continue;
            token = l->tokens.front();
            l->tokens.pop_front();
        }
        l->pump.Drain(token, Tid());
    }
}

void Start(Loader& l) {
    g_l = &l;
    l.pump.SetSubmit(&Submit);
    l.app = std::thread(AppLoop, &l);
}
void Stop(Loader& l) {
    l.stop.store(true);
    l.cv.notify_all();
    l.app.join();
    g_l = nullptr;
}
template <class F> bool WaitFor(F f, int ms = 2000) {
    for (int i = 0; i < ms; ++i) {
        if (f()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return f();
}

// ---- 1: never on the poster's thread --------------------------------------------------------------------------------------------------

void NeverOnTheTimer() {
    Loader l;
    Start(l);
    const uint32_t timer = Tid();
    std::atomic<int> ran{0}, onTimer{0}, onApp{0};
    uint64_t now = 1000;
    // the timer thread (this one) posts 200 rounds, 1 ms apart: the calls come back on the app thread only
    for (int i = 0; i < 200; ++i) {
        const bool ok = l.pump.Post(appthread::kHostRules, [&] {
            ++ran;
            if (Tid() == timer) ++onTimer;
            if (Tid() == l.appTid.load()) ++onApp;
        }, now, timer);
        Check(ok, "a post is accepted");
        now += 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(WaitFor([&] { return ran.load() > 0; }), "the stored closure runs");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    Check(ran.load() > 0 && onTimer.load() == 0, "no closure ever ran on the timer thread");
    Check(onApp.load() == ran.load(), "every closure ran on the app thread");
    Check(l.pump.AppTid() == l.appTid.load() && l.pump.AppTid() != timer, "the pump knows the app thread's id, not the timer's");
    Check(l.pump.OnAppThread(l.appTid.load()) && !l.pump.OnAppThread(timer), "OnAppThread tells them apart");
    Check(l.pump.PosterTid() == timer, "the poster thread is noted for the log line");
    Stop(l);
}

void PostNeverRunsInline() {
    Loader l;
    Start(l);
    l.hold.store(true);   // the app thread is not running jobs: nothing may run, however often the timer posts
    std::atomic<int> ran{0};
    for (int i = 0; i < 50; ++i) l.pump.Post(appthread::kHitShare, [&] { ++ran; }, 1000 + i, Tid());
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Check(ran.load() == 0, "while the app thread does not run its jobs, no closure runs anywhere (Post is never inline)");
    Check(l.submitted.load() == 1, "one job is asked for, not 50 (a queued one is waiting)");
    l.hold.store(false);
    l.cv.notify_all();
    Check(WaitFor([&] { return ran.load() == 1; }), "it runs when the app thread comes");
    Stop(l);
}

// ---- 2: the latest wins, slots are independent ----------------------------------------------------------------------------------------

void LatestWins() {
    Loader l;
    Start(l);
    l.hold.store(true);
    std::atomic<int> last{0}, other{0}, ran{0};
    for (int i = 1; i <= 10; ++i) l.pump.Post(appthread::kAttackPathJoined, [&, i] { last = i; ++ran; }, 1000, Tid());
    l.pump.Post(appthread::kAttackPathFront, [&] { other = 7; ++ran; }, 1000, Tid());
    l.hold.store(false);
    l.cv.notify_all();
    Check(WaitFor([&] { return ran.load() == 2; }), "two slots, two runs");
    Check(last.load() == 10, "the latest closure of a slot is the one that runs");
    Check(other.load() == 7, "the other slot's closure runs too");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Check(ran.load() == 2, "nothing runs twice");
    // a closure that has run is gone: a second job (a stale one) finds nothing to do
    Check(!l.pump.Drain(12345, Tid()), "a token that was never handed out does nothing");
    Stop(l);
}

// ---- 3: failed submit, lost job -------------------------------------------------------------------------------------------------------

void FailedSubmitIsAskedAgain() {
    Loader l;
    Start(l);
    l.refuse.store(true);
    std::atomic<int> ran{0};
    l.pump.Post(appthread::kHostRules, [&] { ++ran; }, 1000, Tid());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Check(ran.load() == 0 && l.submitted.load() == 0, "no thread service: nothing queued, nothing run");
    l.refuse.store(false);
    l.pump.Post(appthread::kHostRules, [&] { ++ran; }, 1001, Tid());   // (1 ms later: not a new lease - the refused token was given back)
    Check(WaitFor([&] { return ran.load() == 1; }), "the next post asks again at once and the run comes");
    Stop(l);
}

void LostJobIsReplaced() {
    appthread::Pump p;
    std::vector<uint32_t> tokens;
    static std::vector<uint32_t>* sink = nullptr;
    sink = &tokens;
    p.SetSubmit([](uint32_t t) { sink->push_back(t); return true; });
    int ran = 0;
    p.Post(appthread::kHitShare, [&] { ran = 1; }, 1000, 1);
    Check(tokens.size() == 1, "a job is asked for");
    p.Post(appthread::kHitShare, [&] { ran = 2; }, 1500, 1);
    Check(tokens.size() == 1, "inside the lease no second job");
    p.Post(appthread::kHitShare, [&] { ran = 3; }, 2100, 1);
    Check(tokens.size() == 2, "after the lease the first job is taken for lost and another is queued");
    Check(!p.Drain(tokens[0], 9) && ran == 0, "the lost job coming late finds its token gone and does nothing");
    Check(p.Drain(tokens[1], 9) && ran == 3, "the newer job runs the latest closure");
    Check(p.AppTid() == 9, "(the app thread's id is the one that drained)");
}

void ClearStopsEverything() {
    appthread::Pump p;
    std::vector<uint32_t> tokens;
    static std::vector<uint32_t>* sink = nullptr;
    sink = &tokens;
    p.SetSubmit([](uint32_t t) { sink->push_back(t); return true; });
    int ran = 0;
    p.Post(appthread::kHostRules, [&] { ++ran; }, 1000, 1);
    p.Clear();
    Check(!p.Post(appthread::kHostRules, [&] { ++ran; }, 5000, 1), "after Clear (unload) a post is refused");
    p.Drain(tokens[0], 2);
    Check(ran == 0, "after Clear what was stored does not run");
}

// ---- 4: many posters ------------------------------------------------------------------------------------------------------------------

void ManyPosters() {
    Loader l;
    Start(l);
    std::atomic<int> wrongThread{0}, ran{0};
    std::atomic<bool> go{true};
    std::vector<std::thread> posters;
    for (int t = 0; t < 4; ++t) {
        posters.emplace_back([&, t] {
            const uint32_t me = Tid();
            uint64_t now = 1000 + (uint64_t)t;
            while (go.load()) {
                l.pump.Post(static_cast<appthread::Slot>(t % appthread::kSlotCount), [&, me] {
                    ++ran;
                    if (Tid() == me || Tid() != l.appTid.load()) ++wrongThread;
                }, now, me);
                now += 1;
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    go.store(false);
    for (auto& t : posters) t.join();
    Check(WaitFor([&] { return ran.load() > 20; }), "work came through");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    Check(wrongThread.load() == 0, "four posting threads: every call ran on the app thread, none on a poster");
    Stop(l);
}

}  // namespace

int main() {
    NeverOnTheTimer();
    PostNeverRunsInline();
    LatestWins();
    FailedSubmitIsAskedAgain();
    LostJobIsReplaced();
    ClearStopsEverything();
    ManyPosters();
    std::printf("app_thread_pump_test: %s (%d failed)\n", g_fails ? "FAILED" : "ok", g_fails);
    return g_fails ? 1 : 0;
}
