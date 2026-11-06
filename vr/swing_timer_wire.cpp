// swing_timer_wire.cpp - [hud] swing_timer on the game: see vr/swing_timer_wire.h for what comes in and goes out, vr/swing_timer.h for why.
// Only shown: nothing here touches the game, the touch or an attack.

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "swing_timer_wire.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

#include <D2RLPlugin/api.h>

#include "direct_input.h"
#include "hit_outcome.h"
#include "move_attack_wire.h"
#include "view_gate.h"

namespace swingtimer {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;
void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

std::atomic<bool> g_on{false}, g_numbers{true};
std::atomic<bool> g_pace{false};   // [gameplay] swing_pace_joined
bool Fed() { return g_on.load(std::memory_order_relaxed) || g_pace.load(std::memory_order_relaxed); }
std::atomic<int> g_style{0};

// Our server's attack start, lock-free from its thread: the time (low 40 bits, ms), the frames (12 bits), the green frame (12 bits).
std::atomic<uint64_t> g_serverNote{0};
uint64_t Pack(uint64_t ms, int32_t frames, int32_t green) {
    return (ms & 0xFFFFFFFFFFull) | (uint64_t)(std::clamp(frames, 0, 4095)) << 40 | (uint64_t)(std::clamp(green, 0, 4095)) << 52;
}

// The timer's state: the update thread (client mode, touches), the draw thread (the headset) and the render thread (the monitor) under one lock.
std::mutex g_m;
State g_state;
uint64_t g_noteSeen = 0;    // the last server note taken
uint32_t g_told = 0;        // a few log lines of what the numbers saw

// Under g_m: the server's newest start, then the outcomes of our hero's blows since the last step.
void TakeNews(uint64_t nowMs) {
    const uint64_t note = g_serverNote.load(std::memory_order_acquire);
    if (note && note != g_noteSeen) {
        g_noteSeen = note;
        const uint64_t at = (nowMs & ~0xFFFFFFFFFFull) | (note & 0xFFFFFFFFFFull);   // (the time's high bits back: they are now's)
        g_state.ServerStart(at <= nowMs ? at : nowMs, (int32_t)((note >> 40) & 0xFFF), (int32_t)((note >> 52) & 0xFFF));
    }
    if (!hitoutcome::LabelsWanted()) return;
    const uint32_t hero = moveattack::HeroId();
    hitoutcome::Record r;
    uint32_t lost = 0;
    while (hitoutcome::LabelRing().Pop(&r, &lost)) {
        if (hero && r.attacker && r.attacker != hero) continue;   // a joined hero's blow on our server: his, not ours
        if (r.kind == hitoutcome::Kind::Roll) {
            g_state.Roll(r.flags, r.ms);
        } else if (r.kind == hitoutcome::Kind::Damage) {
            const int32_t took = r.lifeBefore >= 0 && r.lifeAfter >= 0 ? r.lifeBefore - r.lifeAfter : r.recordLife >= 0 ? r.recordLife : 0;
            g_state.Damage(took, (r.flags & hitoutcome::kCrit) != 0, r.ms);
            if (g_told < 6) {
                ++g_told;
                char b[200];
                snprintf(b, sizeof b, "vrcam: swing timer - our blow took %d life%s (monster class %u)", (int)Whole(took), (r.flags & hitoutcome::kCrit) ? ", a crit" : "",
                         r.monClass);
                Log(b);
            }
        }
    }
}

// -- the painter: one worker thread, the newest look wins (vr/thrust_debug_wire.cpp's) -----------------------------------------------
class Painter {
public:
    uint32_t Submit(const Look& look) {
        std::unique_lock<std::mutex> lock(m_);
        if (valid_ && look == last_) return rev_;
        last_ = look;
        valid_ = true;
        want_ = look;
        has_ = true;
        ++rev_;
        if (!th_.joinable()) th_ = std::thread([this] { Run(); });
        lock.unlock();
        cv_.notify_one();
        return rev_;
    }
    bool Fetch(std::vector<uint32_t>* out, uint32_t* rev, uint32_t after) {
        std::lock_guard<std::mutex> lock(m_);
        if (done_ == 0 || done_ <= after) return false;
        *out = px_;
        *rev = done_;
        return true;
    }
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(m_);
            stop_ = true;
        }
        cv_.notify_all();
        if (th_.joinable()) th_.join();
    }

private:
    void Run() {
        std::vector<uint32_t> local;
        for (;;) {
            Look look;
            uint32_t rev;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [this] { return stop_ || has_; });
                if (stop_) return;
                look = want_;
                rev = rev_;
                has_ = false;
            }
            swingtimerpaint::Render(look, local);
            std::lock_guard<std::mutex> lock(m_);
            px_.swap(local);
            done_ = rev;
        }
    }

    std::mutex m_;
    std::condition_variable cv_;
    std::thread th_;
    Look last_, want_;
    bool valid_ = false, has_ = false, stop_ = false;
    uint32_t rev_ = 0, done_ = 0;
    std::vector<uint32_t> px_;
};

Painter& P() {
    static Painter* painter = new Painter;   // joined in Shutdown
    return *painter;
}

// One step of the timer: the news taken, the look of now, a picture asked for when it changed. *rev: the picture of this look.
Look Step(uint64_t nowMs, uint32_t* rev) {
    Look look;
    {
        std::lock_guard<std::mutex> lock(g_m);
        TakeNews(nowMs);
        look = g_state.Frame(nowMs, g_style.load(std::memory_order_relaxed) == 1 ? Style::Bar : Style::Ring, g_numbers.load(std::memory_order_relaxed));
    }
    *rev = P().Submit(look);
    return look;
}

// -- the headset: one quad held before the eyes ---------------------------------------------------------------------------------------
struct Published {
    bool active = false;
    Layer layer;
    ULONGLONG tick = 0;
    uint32_t needRev = 0;
};
std::mutex g_pubMutex;
Published g_pub;

void Publish(const Published& p) {
    std::lock_guard<std::mutex> lock(g_pubMutex);
    g_pub = p;
}

void OnFrame(const directinput::Frame& f, void*) {
    if (!g_on.load(std::memory_order_relaxed) || !viewgate::F4() || f.head.valid != 1) {
        Publish(Published{});
        return;
    }
    const ULONGLONG nowMs = GetTickCount64();
    uint32_t rev = 0;
    const Look look = Step(nowMs, &rev);
    if (!look.visible) {
        Publish(Published{});
        return;
    }
    Published p;
    const hset::Q q{f.head.rot[0], f.head.rot[1], f.head.rot[2], f.head.rot[3]};
    const hset::V3 head{f.head.pos[0], f.head.pos[1], f.head.pos[2]};
    const bool bar = look.style == Style::Bar;
    p.layer.widthM = bar ? kBarQuadM : kRingQuadM;
    p.layer.heightM = p.layer.widthM * kPicH / kPicW;
    p.layer.pose.p = hset::Add(head, hset::Rot(q, {0.0f, bar ? kBarUpM : 0.0f, -kDistM}));
    p.layer.pose.q = q;
    p.layer.pose.valid = true;
    p.active = true;
    p.tick = nowMs;
    p.needRev = rev;   // this moment's picture, not an older one
    Publish(p);
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    directinput::Listen(&OnFrame, nullptr);
}

void SetWanted(bool on, int style, bool numbers) {
    const bool was = g_on.exchange(on);
    const int wasStyle = g_style.exchange(style == 1 ? 1 : 0);
    const bool wasNumbers = g_numbers.exchange(numbers);
    hitoutcome::LabelsFlag().store(on && numbers);   // the observers on the game's hit flags and damage go in for the numbers (vrcam's timer)
    moveattack::SetSwingWanted(Fed());               // the server's scheduler for the attack's times
    if (was != on || (on && (wasStyle != g_style.load() || wasNumbers != numbers))) {
        if (!on) {
            Log("vrcam: swing timer - [hud] swing_timer=0");
            return;
        }
        char b[300];
        snprintf(b, sizeof b, "vrcam: swing timer - [hud] swing_timer=1: after an attack the game started, a %s fills until the game lets the next one in (red "
                 "refused, amber kept for the end, green in), then melts away; numbers %s. Only shown (VR first person, the monitor's F2 / F3)",
                 g_style.load() == 1 ? "bar at the top" : "ring in the middle", numbers ? "on (our server's blows: single player or the host)" : "off");
        Log(b);
    }
}
bool Wanted() { return g_on.load(std::memory_order_relaxed); }

void SetPace(bool on) {
    if (g_pace.exchange(on) == on) return;
    moveattack::SetSwingWanted(Fed());
    Log(on ? "vrcam: swing timer - [gameplay] swing_pace_joined=1: joined to another's game, a touch the host's gate would refuse is not sent and one in its "
             "last moments is sent when the client's clock turns green (VR first person)"
           : "vrcam: swing timer - [gameplay] swing_pace_joined=0: every touch is sent as it comes");
}
bool Active() { return Fed(); }

Pace PaceTouch(uint64_t nowMs) {
    if (!g_pace.load(std::memory_order_relaxed)) return Pace::Send;
    std::lock_guard<std::mutex> lock(g_m);
    TakeNews(nowMs);
    return g_state.PaceAt(nowMs);
}

void NoteServerStart(uint64_t nowMs, int32_t frames, int32_t greenFrame) {
    if (!Fed() || frames < 1) return;
    uint64_t v = Pack(nowMs, frames, greenFrame);
    if (v == g_serverNote.load(std::memory_order_relaxed)) v ^= 1;   // (two starts in one millisecond with the same frames: still a new note)
    g_serverNote.store(v, std::memory_order_release);
}

void NoteClientMode(bool known, uint32_t mode, uint64_t nowMs) {
    if (!Fed()) return;
    std::lock_guard<std::mutex> lock(g_m);
    TakeNews(nowMs);   // (a server start first: it decides whether the client's start counts)
    g_state.ClientMode(known, mode, nowMs);
}

void NoteTouch(uint64_t nowMs) {
    if (!g_on.load(std::memory_order_relaxed)) return;
    bool soon = false;
    {
        std::lock_guard<std::mutex> lock(g_m);
        TakeNews(nowMs);
        soon = g_state.Touch(nowMs);
    }
    static std::atomic<int> told{0};
    if (soon && told.fetch_add(1) < 4) Log("vrcam: swing timer - a touch while red: the game refuses an attack now (\"too soon\")");
}

bool Draw(Layer* layer, uint32_t* needRevision) {
    if (!g_on.load(std::memory_order_relaxed) || !viewgate::F4()) return false;
    Published p;
    {
        std::lock_guard<std::mutex> lock(g_pubMutex);
        p = g_pub;
    }
    if (!p.active || !p.layer.pose.valid || GetTickCount64() - p.tick > 250) return false;
    *layer = p.layer;
    *needRevision = p.needRev;
    return true;
}

bool Monitor(uint64_t nowMs, float aspect, float box[4], uint32_t* needRevision) {
    if (!g_on.load(std::memory_order_relaxed) || aspect <= 0.0f) return false;
    uint32_t rev = 0;
    const Look look = Step(nowMs, &rev);
    if (!look.visible) return false;
    const float picAspect = (float)kPicW / (float)kPicH;   // the picture's width per height
    if (look.style == Style::Bar) {
        const float w = kMonBarW, h = w * aspect / picAspect;   // (uv: a width share times the screen's aspect is a height share)
        box[0] = 0.5f - 0.5f * w;
        box[1] = kMonBarTop;
        box[2] = 0.5f + 0.5f * w;
        box[3] = kMonBarTop + h;
    } else {
        const float h = kMonRingH, w = h * picAspect / aspect;
        box[0] = 0.5f - 0.5f * w;
        box[1] = 0.5f - 0.5f * h;
        box[2] = 0.5f + 0.5f * w;
        box[3] = 0.5f + 0.5f * h;
    }
    *needRevision = rev;
    return true;
}

bool Picture(std::vector<uint32_t>* px, uint32_t* revision, uint32_t after) { return P().Fetch(px, revision, after); }

void Teardown() { Publish(Published{}); }

void Shutdown() {
    directinput::Unlisten(&OnFrame, nullptr);
    P().Stop();
    Publish(Published{});
}

}  // namespace swingtimer
