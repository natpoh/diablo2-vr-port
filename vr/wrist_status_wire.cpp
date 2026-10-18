// wrist_status_wire.cpp - [hud] wrist_status: companions and effects over the wrists when their back is turned to the eyes (vr/wrist_status.h).
//
// direct_input.h hands every headset frame to a listener (the head and both grips in the room's space, the context flags); this one runs the glance test
// and the fade on them, one for each wrist (vr/hand_vitals.h's Reveal, told to measure the back of the hand), and keeps what xr.cpp needs to hang up to two
// quad layers. What the hero has comes from vr/wrist_status_game.cpp (read five times a second); each wrist's picture is painted on a worker thread of its own,
// only while that wrist is wanted and only when a cell or a setting changed it.
//
// Works in native OpenXR whether the plugin's own controls are on (direct_input) or not: the frames come either way. F4 only: the view gate decides at the
// moment the frame is read, and again when the layer is asked for.
//
// Nothing here writes to the game, presses anything or takes a control from anyone.

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "wrist_status_wire.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

#include <D2RLPlugin/api.h>

#include "direct_input.h"
#include "view_gate.h"
#include "wheel_icons.h"
#include "wrist_status_game.h"
#include "wrist_status_paint.h"

namespace wriststatus {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;
void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

// the settings (Configure, the update thread; read by the draw thread)
std::atomic<bool> g_on{false}, g_companions{true}, g_effects{true}, g_flip{false};
std::atomic<int> g_sides{0};
std::atomic<float> g_size{10.0f}, g_height{7.0f}, g_along{0.0f}, g_cone{40.0f}, g_opacity{1.0f};

Config Current() {
    Config c;
    c.on = g_on.load(std::memory_order_relaxed);
    c.companions = g_companions.load(std::memory_order_relaxed);
    c.effects = g_effects.load(std::memory_order_relaxed);
    c.sides = g_sides.load(std::memory_order_relaxed);
    c.sizeCm = g_size.load(std::memory_order_relaxed);
    c.heightCm = g_height.load(std::memory_order_relaxed);
    c.alongCm = g_along.load(std::memory_order_relaxed);
    c.coneDeg = g_cone.load(std::memory_order_relaxed);
    c.opacity = g_opacity.load(std::memory_order_relaxed);
    c.flip = g_flip.load(std::memory_order_relaxed);
    return c;
}

// -- the painter: one worker thread for both wrists, the newest look of each wins ---------------------------------------------------------

class Painter {
public:
    // The draw thread. A look not the same as the last one asked for (for that wrist) is a new picture with a new revision; the same look is the revision it had.
    uint32_t Submit(int side, const Look& look) {
        std::unique_lock<std::mutex> lock(m_);
        Slot& s = slot_[side & 1];
        if (s.valid && look == s.last) return s.rev;
        s.last = look;
        s.valid = true;
        s.want = look;
        s.has = true;
        s.rev = ++serial_;
        if (!th_.joinable()) th_ = std::thread([this] { Run(); });
        const uint32_t rev = s.rev;
        lock.unlock();
        cv_.notify_one();
        return rev;
    }
    bool Fetch(int side, std::vector<uint32_t>* out, uint32_t* rev) {
        std::lock_guard<std::mutex> lock(m_);
        Slot& s = slot_[side & 1];
        if (s.done == 0 || s.done == s.fetched) return false;
        out->swap(s.px);
        *rev = s.done;
        s.fetched = s.done;
        return true;
    }
    // One wrist's picture is forgotten (it is not wanted any more): the next look is painted afresh.
    void ResetSide(int side) {
        std::lock_guard<std::mutex> lock(m_);
        Slot& s = slot_[side & 1];
        s.valid = false;
        s.has = false;
        s.fetched = 0;
        s.done = 0;
        s.px.clear();
        ++s.epoch;
    }
    void Reset() {
        ResetSide(0);
        ResetSide(1);
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
    struct Slot {
        Look last, want;
        bool valid = false, has = false;
        uint32_t rev = 0, done = 0, fetched = 0, epoch = 0;
        std::vector<uint32_t> px;
    };
    void Run() {
        std::vector<uint32_t> local;
        for (;;) {
            Look look;
            uint32_t rev = 0, epoch = 0;
            int side = -1;
            {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [this] { return stop_ || slot_[0].has || slot_[1].has; });
                if (stop_) return;
                side = slot_[0].has ? 0 : 1;
                Slot& s = slot_[side];
                look = s.want;
                rev = s.rev;
                epoch = s.epoch;
                s.has = false;
            }
            wriststatuspaint::Render(look, local);
            std::lock_guard<std::mutex> lock(m_);
            Slot& s = slot_[side];
            if (epoch != s.epoch) continue;   // a reset came while painting: this picture belongs to the last session
            s.px.swap(local);
            s.done = rev;
        }
    }

    std::mutex m_;
    std::condition_variable cv_;
    std::thread th_;
    Slot slot_[2];
    uint32_t serial_ = 0;
    bool stop_ = false;
};

Painter& P() {
    static Painter* painter = new Painter;   // joined in Shutdown; no static destructor runs a thread's end at unload
    return *painter;
}

// -- the draw thread's state, and what is handed to xr.cpp ----------------------------------------------------------------------------

struct Wrist {
    handvitals::Reveal reveal;
    bool wasActive = false, toldUp = false;
    uint32_t needRev = 0;
};
struct State {
    Wrist w[2];
    bool seen = false;
    uint32_t session = 0;
    double lastTime = 0.0;
};
State g_s;   // OnFrame's alone (Teardown, under the lock, between sessions)

struct Published {
    handvitals::Out out;
    bool active = false;
    ULONGLONG tick = 0;
    uint32_t needRev = 0;
    float widthM = 0.0f, heightM = 0.0f;
};
std::mutex g_pubMutex;
Published g_pub[2];

void Publish(int side, const Published& p) {
    std::lock_guard<std::mutex> lock(g_pubMutex);
    g_pub[side & 1] = p;
}
void PublishNone() {
    std::lock_guard<std::mutex> lock(g_pubMutex);
    g_pub[0] = Published{};
    g_pub[1] = Published{};
}

void OnFrame(const directinput::Frame& f, void*) {
    const Config cfg = Current();
    if (!cfg.on) {
        if (g_s.seen) {
            g_s = State{};
            PublishNone();
            P().Reset();
        }
        return;
    }
    if (!g_s.seen || f.session != g_s.session) {   // a new native session forgets everything
        g_s = State{};
        g_s.seen = true;
        g_s.session = f.session;
    }
    float dt = 0.0f;
    if (g_s.lastTime > 0.0 && f.time > g_s.lastTime) dt = (float)(f.time - g_s.lastTime);
    if (dt > 0.25f) {   // a stall (a load, the headset away): the wrists have to be shown again
        for (Wrist& w : g_s.w) w.reveal.Reset();
        dt = 0.0f;
    }
    g_s.lastTime = f.time;

    Roster roster;
    Status status;
    double ageMs = 0.0;
    const bool have = game::Latest(&roster, &status, &ageMs);
    const bool fresh = have && Fresh(ageMs);
    const uint32_t icons = wheelicons::Revision();

    handvitals::In base;
    base.dt = dt;
    base.head.valid = f.head.valid == 1;
    base.head.pos = {f.head.pos[0], f.head.pos[1], f.head.pos[2]};
    base.head.rot = {f.head.rot[0], f.head.rot[1], f.head.rot[2], f.head.rot[3]};
    for (int h = 0; h < 2; ++h) {
        const directinput::Hand& src = f.hand[h];
        base.hand[h].valid = src.grip.valid == 1;
        const hset::Q rot{src.grip.rot[0], src.grip.rot[1], src.grip.rot[2], src.grip.rot[3]};
        base.hand[h].pos = Anchor({src.grip.pos[0], src.grip.pos[1], src.grip.pos[2]}, rot, cfg.alongCm);
        base.hand[h].rot = rot;
        base.hand[h].squeeze = src.squeeze;
    }
    handvitals::Flags fl;
    fl.f4 = viewgate::F4();
    fl.otherView = (f.flags & directinput::kOtherView) != 0;
    fl.focused = (f.flags & directinput::kFocused) != 0;
    fl.inMenu = (f.flags & directinput::kInMenu) != 0;
    fl.uiUp = (f.flags & directinput::kUiUp) != 0;

    for (int side = 0; side < 2; ++side) {
        Wrist& w = g_s.w[side];
        const Look look = MakeLook(fresh ? roster : Roster{}, fresh ? status : Status{}, ContentFor(cfg, side), cfg, icons);
        handvitals::In in = base;
        in.cfg = RevealConfig(cfg, side);
        in.gate.hideNow = !fl.f4 || fl.otherView;
        in.gate.hide = !fl.focused || fl.inMenu || fl.uiUp || !fresh || look.n == 0;   // (a wrist with nothing to show does not come up)
        const handvitals::Out out = w.reveal.Step(in);

        const bool active = w.reveal.Active();
        if (active && look.n > 0) {
            const uint32_t rev = P().Submit(side, look);
            if (!w.wasActive) w.needRev = rev;   // the panel starts being wanted: it is not shown before a picture of this moment
        } else if (!active && w.wasActive) {
            P().ResetSide(side);                 // gone: its picture is forgotten, a later glance paints afresh (the cells may have changed)
        }
        w.wasActive = active;
        if (out.alpha > 0.5f && !w.toldUp) {
            w.toldUp = true;
            char b[220];
            snprintf(b, sizeof b, "vrcam: wrist status - the panel is up at the %s wrist (%d cells, the back of the hand %.0f degrees off your face)", side == 0 ? "left" : "right", (int)look.n, out.angleDeg);
            Log(b);
        }
        Published p;
        p.out = out;
        p.active = active;
        p.tick = GetTickCount64();
        p.needRev = w.needRev;
        p.widthM = PanelWidthM(cfg);
        p.heightM = PanelHeightM(cfg);
        Publish(side, p);
    }
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    game::Init(ctx);
    directinput::Listen(&OnFrame, nullptr);   // (returns at once while the box is off)
}

void Configure(const Config& config) {
    const Config c = Sanitized(config);
    const bool was = g_on.exchange(c.on);
    g_companions.store(c.companions);
    g_effects.store(c.effects);
    g_sides.store(c.sides);
    g_size.store(c.sizeCm);
    g_height.store(c.heightCm);
    g_along.store(c.alongCm);
    g_cone.store(c.coneDeg);
    g_opacity.store(c.opacity);
    g_flip.store(c.flip);
    if (c.on != was) {
        char b[360];
        snprintf(b, sizeof b, "vrcam: wrist status %s ([hud] wrist_status; %s%s%s, %.0f cm wide, %.1f cm up, %.1f cm along the arm, cone %.0f degrees)", c.on ? "on" : "off",
                 c.companions ? "summons and the mercenary" : "", c.companions && c.effects ? " and " : "", c.effects ? "buffs, auras and curses" : "", c.sizeCm, c.heightCm, c.alongCm, c.coneDeg);
        Log(b);
    }
}

bool Enabled() { return g_on.load(std::memory_order_relaxed); }
Config Settings() { return Current(); }

void Tick(bool inGame) {
    game::Tick(g_on.load(std::memory_order_relaxed) && inGame && viewgate::F4());   // (not wanted: the server's lookup is let alone, the last read dropped)
}

bool Draw(int side, Layer* layer, uint32_t* needRevision) {
    if (!Enabled() || !viewgate::F4() || side < 0 || side > 1) return false;
    Published p;
    {
        std::lock_guard<std::mutex> lock(g_pubMutex);
        p = g_pub[side];
    }
    // No frame lately (a load, the headset away, the view changed): nothing, rather than a panel frozen in the air.
    if (!p.active || GetTickCount64() - p.tick > 250) return false;
    const float a = std::clamp(p.out.alpha, 0.0f, 1.0f);
    layer->visible = a > 0.0f && p.out.pose.valid;
    layer->pose = p.out.pose;
    const float grow = handvitals::kShrinkWhenFaded + (1.0f - handvitals::kShrinkWhenFaded) * a;
    layer->widthM = p.widthM * grow;
    layer->heightM = p.heightM * grow;
    layer->colorScale = a;
    *needRevision = p.needRev;
    return true;
}

bool Picture(int side, std::vector<uint32_t>* px, uint32_t* revision, uint32_t* width, uint32_t* height) {
    if (side < 0 || side > 1 || !P().Fetch(side, px, revision)) return false;
    *width = (uint32_t)kPicW;
    *height = (uint32_t)kPicH;
    return true;
}

void Teardown() {
    g_s = State{};
    PublishNone();
    P().Reset();
}

void Shutdown() {
    directinput::Unlisten(&OnFrame, nullptr);
    P().Stop();
    g_s = State{};
    PublishNone();
}

}  // namespace wriststatus
