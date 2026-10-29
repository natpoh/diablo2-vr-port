// thrust_debug_wire.cpp - [debug] thrust_debug: the thrust's readout on a plate over the weapon hand (vr/thrust_debug_wire.h).
//
// hit by touch hands over the lines (vr/thrust_debug.h) on its timer; direct_input.h hands every headset frame to OnFrame (the head and both grips in the room's
// space), which places the plate over the hand the lines are about, turned to the head, and asks the painter for a picture of the lines when they changed. xr.cpp
// hangs it as one quad layer (vr/xr_thrust_debug.h). A drawing only: nothing here touches the game or the touch.

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "thrust_debug_wire.h"

#include "thrust_debug_paint.h"

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
#include <cstring>
#include <mutex>
#include <thread>

#include <D2RLPlugin/api.h>

#include "direct_input.h"
#include "view_gate.h"

namespace thrustdebugwire {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;
void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

std::atomic<bool> g_on{false}, g_only{false};

// What is shown: the lines and the hand (set by the update thread, read by the draw thread).
struct Look {
    thrustdbg::Line lines[kMaxLines];
    size_t n = 0;
    bool operator==(const Look& o) const {
        if (n != o.n) return false;
        for (size_t i = 0; i < n; ++i)
            if (lines[i].colour != o.lines[i].colour || std::strcmp(lines[i].text, o.lines[i].text) != 0) return false;
        return true;
    }
};
std::mutex g_lookMutex;
Look g_look;
int g_side = 0;
ULONGLONG g_lookAt = 0;
// [debug] stab_debug: the stab's lines (under g_lookMutex)
std::atomic<bool> g_stabOn{false};
Look g_stabLook;
int g_stabSide = 0;
ULONGLONG g_stabAt = 0;
constexpr ULONGLONG kStabShowMs = 5000;   // (stabdbg::kShowMs)

// -- the painter: one worker thread, the newest look wins -------------------------------------------------------------------------
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
    bool Fetch(std::vector<uint32_t>* out, uint32_t* rev) {
        std::lock_guard<std::mutex> lock(m_);
        if (done_ == 0 || done_ == fetched_) return false;
        out->swap(px_);
        *rev = done_;
        fetched_ = done_;
        return true;
    }
    void Reset() {
        std::lock_guard<std::mutex> lock(m_);
        valid_ = false;
        fetched_ = 0;
        has_ = false;
        px_.clear();
        done_ = 0;
        ++epoch_;
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
            uint32_t rev, epoch;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [this] { return stop_ || has_; });
                if (stop_) return;
                look = want_;
                rev = rev_;
                epoch = epoch_;
                has_ = false;
            }
            thrustdebugpaint::Render(look.lines, look.n, local);
            std::lock_guard<std::mutex> lock(m_);
            if (epoch != epoch_) continue;   // a Reset came while painting
            px_.swap(local);
            done_ = rev;
        }
    }

    std::mutex m_;
    std::condition_variable cv_;
    std::thread th_;
    Look last_, want_;
    bool valid_ = false, has_ = false, stop_ = false;
    uint32_t rev_ = 0, done_ = 0, fetched_ = 0, epoch_ = 0;
    std::vector<uint32_t> px_;
};

Painter& P() {
    static Painter* painter = new Painter;   // joined in Shutdown
    return *painter;
}

// -- the draw thread's state and what xr.cpp is handed -----------------------------------------------------------------------------
struct Published {
    bool active = false;
    hset::Pose pose;
    ULONGLONG tick = 0;
    uint32_t needRev = 0;
};
std::mutex g_pubMutex;
Published g_pub;
bool g_wasActive = false;   // OnFrame's alone
uint32_t g_needRev = 0;

void Publish(const Published& p) {
    std::lock_guard<std::mutex> lock(g_pubMutex);
    g_pub = p;
}

void OnFrame(const directinput::Frame& f, void*) {
    Look look;
    int side = 0;
    ULONGLONG at = 0;
    const bool thrustOn = g_on.load(std::memory_order_relaxed), stabOn = g_stabOn.load(std::memory_order_relaxed);
    const ULONGLONG nowMs = GetTickCount64();
    if (thrustOn || stabOn) {
        std::lock_guard<std::mutex> lock(g_lookMutex);
        const bool stabFresh = stabOn && g_stabLook.n > 0 && nowMs - g_stabAt < kStabShowMs;
        const bool thrustFresh = thrustOn && g_look.n > 0 && nowMs - g_lookAt < 1000;
        if (stabFresh) {   // the stab's lines first, then the thrust's while there is room
            look = g_stabLook;
            side = g_stabSide;
            at = nowMs;
            for (size_t i = 0; thrustFresh && i < g_look.n && look.n < kMaxLines; ++i) look.lines[look.n++] = g_look.lines[i];
        } else if (thrustFresh) {
            look = g_look;
            side = g_side;
            at = g_lookAt;
        }
    }
    // F4 only, and only while hit by touch (or the stab) is feeding it (it stops when the touch stands down: another view, a menu, no weapon)
    const bool active = (thrustOn || stabOn) && viewgate::F4() && look.n > 0 && nowMs - at < 1000;
    if (!active) {
        if (g_wasActive) {
            g_wasActive = false;
            Publish(Published{});
            P().Reset();
        }
        return;
    }
    const directinput::Hand& hand = f.hand[side == 0 ? 1 : 0];   // the skeleton's side 0 is the right hand, direct_input's hand 1
    Published p;
    if (hand.grip.valid == 1 && f.head.valid == 1) {
        const hset::V3 h{hand.grip.pos[0], hand.grip.pos[1], hand.grip.pos[2]}, e{f.head.pos[0], f.head.pos[1], f.head.pos[2]};
        p.pose = handvitals::PanelPose(h, e, kUpCm);
    }
    const uint32_t rev = P().Submit(look);
    if (!g_wasActive) {
        g_needRev = rev;   // not shown before a picture of this moment
        static bool told = false;
        if (!told) {
            told = true;
            Log("vrcam: thrust debug - the readout is up over the weapon hand ([debug] thrust_debug)");
        }
    }
    g_wasActive = true;
    p.active = true;
    p.tick = GetTickCount64();
    p.needRev = g_needRev;
    Publish(p);
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    directinput::Listen(&OnFrame, nullptr);
}

void SetWanted(bool on) {
    if (g_on.exchange(on) != on) Log(on ? "vrcam: thrust debug on ([debug] thrust_debug: the thrust's readout over the weapon hand, the blade tinted, a log line per thrust)" : "vrcam: thrust debug off");
}
bool Wanted() { return g_on.load(std::memory_order_relaxed); }
void SetOnly(bool on) {
    if (g_only.exchange(on) != on && g_on.load()) Log(on ? "vrcam: thrust debug - only the thrusts are shown ([debug] thrust_debug_only: the swing's colours, marks and log lines are left out)" : "vrcam: thrust debug - the swing is shown too");
}
bool Only() { return g_on.load(std::memory_order_relaxed) && g_only.load(std::memory_order_relaxed); }

void SetLines(const thrustdbg::Line* lines, size_t n, int side) {
    std::lock_guard<std::mutex> lock(g_lookMutex);
    g_look.n = std::min(n, kMaxLines);
    for (size_t i = 0; i < g_look.n; ++i) g_look.lines[i] = lines[i];
    g_side = side & 1;
    g_lookAt = GetTickCount64();
}

void SetStabWanted(bool on) {
    if (g_stabOn.exchange(on) != on) Log(on ? "vrcam: stab debug on ([debug] stab_debug: the stab's readout over the weapon hand, its marks with the melee boxes)" : "vrcam: stab debug off");
}
bool StabWanted() { return g_stabOn.load(std::memory_order_relaxed); }

void SetStabLines(const thrustdbg::Line* lines, size_t n, int side) {
    std::lock_guard<std::mutex> lock(g_lookMutex);
    g_stabLook.n = std::min(n, kMaxLines);
    for (size_t i = 0; i < g_stabLook.n; ++i) g_stabLook.lines[i] = lines[i];
    g_stabSide = side & 1;
    g_stabAt = GetTickCount64();
}

bool Draw(Layer* layer, uint32_t* needRevision) {
    if (!(g_on.load(std::memory_order_relaxed) || g_stabOn.load(std::memory_order_relaxed)) || !viewgate::F4()) return false;
    Published p;
    {
        std::lock_guard<std::mutex> lock(g_pubMutex);
        p = g_pub;
    }
    if (!p.active || !p.pose.valid || GetTickCount64() - p.tick > 250) return false;
    layer->pose = p.pose;
    layer->widthM = kWidthM;
    layer->heightM = kHeightM;
    *needRevision = p.needRev;
    return true;
}

bool Picture(std::vector<uint32_t>* px, uint32_t* revision) { return P().Fetch(px, revision); }

void Teardown() {
    g_wasActive = false;
    Publish(Published{});
    P().Reset();
}

void Shutdown() {
    directinput::Unlisten(&OnFrame, nullptr);
    P().Stop();
    g_wasActive = false;
    Publish(Published{});
}

}  // namespace thrustdebugwire
