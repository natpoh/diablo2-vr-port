// hand_vitals_wire.cpp - [hud] hand_vitals: life and mana on the hand when the palm turns to the face (vr/hand_vitals.h).
//
// direct_input.h hands every headset frame to a listener (the head and both grips in the room's space, the grip squeeze, the context
// flags); this one runs the palm test and the fade on them and keeps what xr.cpp needs to hang one quad layer over the hand. The
// hero's numbers come from vr/hand_vitals_game.cpp (read on the UI thread, 5 a second); the picture is painted on a worker thread of
// its own, only while the panel is wanted and only when a number or a setting changed it.
//
// Works in native OpenXR whether the plugin's own controls are on (direct_input) or not: the frames come either way. F4 only: the
// view gate decides at the moment the frame is read, and again when the layer is asked for.
//
// Nothing here writes to the game, presses anything or takes a control from anyone.

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "hand_vitals_wire.h"

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
#include "hand_vitals_game.h"
#include "hand_vitals_paint.h"
#include "view_gate.h"

namespace handvitals {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;
void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

// the settings (Configure, the update thread; read by the draw thread)
std::atomic<bool> g_on{false}, g_pulse{false}, g_flip{false};
std::atomic<int> g_hand{0}, g_style{kStyleOrbs}, g_orbHands{0};
std::atomic<float> g_size{8.0f}, g_height{6.0f}, g_opacity{1.0f}, g_cone{35.0f}, g_low{0.30f};
std::atomic<float> g_orbSize{5.0f}, g_orbOpacity{0.95f};
std::atomic<float> g_orbSpot[2][6] = {{3.0f, 0.0f, 4.0f, 0.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 4.0f, 0.0f, 0.0f, 0.0f}};   // [left, right][along, side, up cm; rotation x, y, z degrees]
std::atomic<bool> g_orbMirror{false};
std::atomic<bool> g_orbUnderShield{true};                                         // the wrist under a shield rolls its ball to the other side (OrbUnderShield)
std::atomic<float> g_orbShield[7] = {180.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};   // [turn deg; along, side, up cm; rotation x, y, z deg]
std::atomic<float> g_orbClassZ[kHeroCount] = {};   // the depth offset of each hero class, cm
std::atomic<float> g_orbShieldClassUp[kHeroCount] = {}, g_orbShieldClassAlong[kHeroCount] = {};   // each hero class's depth and along offset under a shield, cm
std::atomic<float> g_orbShieldClassRot[3][kHeroCount] = {};                                          // ... and its turn about X / Y / Z there, degrees
std::atomic<int> g_orbFill{kFillOutOfHand}, g_orbShape{kShapeMedallion}, g_orbMetal{0}, g_orbDisc{1};
std::atomic<float> g_orbPatina{0.55f};

Config Current() {
    Config c;
    c.on = g_on.load(std::memory_order_relaxed);
    c.hand = g_hand.load(std::memory_order_relaxed);
    c.sizeCm = g_size.load(std::memory_order_relaxed);
    c.heightCm = g_height.load(std::memory_order_relaxed);
    c.opacity = g_opacity.load(std::memory_order_relaxed);
    c.coneDeg = g_cone.load(std::memory_order_relaxed);
    c.pulse = g_pulse.load(std::memory_order_relaxed);
    c.lowFrac = g_low.load(std::memory_order_relaxed);
    c.flip = g_flip.load(std::memory_order_relaxed);
    c.style = g_style.load(std::memory_order_relaxed);
    c.orbHands = g_orbHands.load(std::memory_order_relaxed);
    c.orbSizeCm = g_orbSize.load(std::memory_order_relaxed);
    OrbSpot* spots[2] = {&c.orbLeft, &c.orbRight};
    for (int w = 0; w < 2; ++w) {
        spots[w]->alongCm = g_orbSpot[w][0].load(std::memory_order_relaxed);
        spots[w]->sideCm = g_orbSpot[w][1].load(std::memory_order_relaxed);
        spots[w]->upCm = g_orbSpot[w][2].load(std::memory_order_relaxed);
        spots[w]->rotXDeg = g_orbSpot[w][3].load(std::memory_order_relaxed);
        spots[w]->rotYDeg = g_orbSpot[w][4].load(std::memory_order_relaxed);
        spots[w]->rotZDeg = g_orbSpot[w][5].load(std::memory_order_relaxed);
    }
    c.orbMirror = g_orbMirror.load(std::memory_order_relaxed);
    c.orbUnderShield = g_orbUnderShield.load(std::memory_order_relaxed);
    c.orbShield.turnDeg = g_orbShield[0].load(std::memory_order_relaxed);
    c.orbShield.alongCm = g_orbShield[1].load(std::memory_order_relaxed);
    c.orbShield.sideCm = g_orbShield[2].load(std::memory_order_relaxed);
    c.orbShield.upCm = g_orbShield[3].load(std::memory_order_relaxed);
    c.orbShield.rotXDeg = g_orbShield[4].load(std::memory_order_relaxed);
    c.orbShield.rotYDeg = g_orbShield[5].load(std::memory_order_relaxed);
    c.orbShield.rotZDeg = g_orbShield[6].load(std::memory_order_relaxed);
    for (int k = 0; k < kHeroCount; ++k) c.orbClassZCm[k] = g_orbClassZ[k].load(std::memory_order_relaxed);
    for (int k = 0; k < kHeroCount; ++k) c.orbShieldClassUpCm[k] = g_orbShieldClassUp[k].load(std::memory_order_relaxed);
    for (int k = 0; k < kHeroCount; ++k) c.orbShieldClassAlongCm[k] = g_orbShieldClassAlong[k].load(std::memory_order_relaxed);
    for (int k = 0; k < kHeroCount; ++k) {
        c.orbShieldClassRotXDeg[k] = g_orbShieldClassRot[0][k].load(std::memory_order_relaxed);
        c.orbShieldClassRotYDeg[k] = g_orbShieldClassRot[1][k].load(std::memory_order_relaxed);
        c.orbShieldClassRotZDeg[k] = g_orbShieldClassRot[2][k].load(std::memory_order_relaxed);
    }
    c.orbOpacity = g_orbOpacity.load(std::memory_order_relaxed);
    c.orbFill = g_orbFill.load(std::memory_order_relaxed);
    c.orbShape = g_orbShape.load(std::memory_order_relaxed);
    c.orbMetal = g_orbMetal.load(std::memory_order_relaxed);
    c.orbDiscFaces = g_orbDisc.load(std::memory_order_relaxed);
    c.orbPatina = g_orbPatina.load(std::memory_order_relaxed);
    return c;
}

// -- the painter: one worker thread, the newest look wins ---------------------------------------------------------------------------

class Painter {
public:
    // The draw thread. A look not the same as the last one asked for is a new picture with a new revision; the same look is the revision it had.
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
    // Everything asked for is forgotten, the next look is painted afresh (the swapchain it was uploaded into is gone).
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
            handvitalspaint::Render(look, local);
            std::lock_guard<std::mutex> lock(m_);
            if (epoch != epoch_) continue;   // a Reset came while painting: this picture belongs to the last session
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
    static Painter* painter = new Painter;   // joined in Shutdown; no static destructor runs a thread's end at unload
    return *painter;
}

// -- the draw thread's state, and what is handed to xr.cpp ----------------------------------------------------------------------------

struct State {
    Reveal reveal;
    bool seen = false;
    uint32_t session = 0;
    double lastTime = 0.0;
    Vitals lastGood;
    bool haveGood = false;
    bool wasActive = false;
    bool toldUp = false;
    uint32_t needRev = 0;
};
State g_s;   // OnFrame's alone (Teardown, under the lock, between sessions)

struct Published {
    Out out;
    bool active = false;
    bool alert = false;
    double time = 0.0;
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
    const Config cfg = Current();
    if (!cfg.on || cfg.style != kStylePanel) {   // (the orbs are drawn by the effect: vr/hand_orbs.h)
        if (g_s.seen) {
            g_s = State{};
            Publish(Published{});
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
    if (dt > 0.25f) {   // a stall (a load, the headset away): the palm has to be shown again
        g_s.reveal.Reset();
        dt = 0.0f;
    }
    g_s.lastTime = f.time;

    Vitals v;
    double ageMs = 0.0;
    const bool have = game::Latest(&v, &ageMs);
    const bool fresh = have && Fresh(ageMs);
    if (fresh && v.known) {
        g_s.lastGood = v;
        g_s.haveGood = true;
    }

    In in;
    in.cfg = cfg;
    in.dt = dt;
    in.head.valid = f.head.valid == 1;
    in.head.pos = {f.head.pos[0], f.head.pos[1], f.head.pos[2]};
    in.head.rot = {f.head.rot[0], f.head.rot[1], f.head.rot[2], f.head.rot[3]};
    for (int h = 0; h < 2; ++h) {
        const directinput::Hand& src = f.hand[h];
        in.hand[h].valid = src.grip.valid == 1;
        in.hand[h].pos = {src.grip.pos[0], src.grip.pos[1], src.grip.pos[2]};
        in.hand[h].rot = {src.grip.rot[0], src.grip.rot[1], src.grip.rot[2], src.grip.rot[3]};
        in.hand[h].squeeze = src.squeeze;
    }
    Flags fl;
    fl.f4 = viewgate::F4();
    fl.otherView = (f.flags & directinput::kOtherView) != 0;
    fl.focused = (f.flags & directinput::kFocused) != 0;
    fl.inMenu = (f.flags & directinput::kInMenu) != 0;
    fl.uiUp = (f.flags & directinput::kUiUp) != 0;
    fl.vitalsFresh = fresh;
    in.gate = MakeGate(fl, fresh ? v : Vitals{});
    const Out out = g_s.reveal.Step(in);

    const bool active = g_s.reveal.Active();
    bool alert = false;
    if (active && g_s.haveGood) {
        const Look look = MakeLook(g_s.lastGood, cfg);
        alert = look.alert;
        const uint32_t rev = P().Submit(look);
        if (!g_s.wasActive) g_s.needRev = rev;   // the panel starts being wanted: it is not shown before a picture of this moment
    }
    g_s.wasActive = active;
    if (out.alpha > 0.5f && !g_s.toldUp) {
        g_s.toldUp = true;
        char b[200];
        snprintf(b, sizeof b, "vrcam: hand vitals - the panel is up at the %s hand (the palm %.0f degrees off your face)", out.side == 0 ? "left" : "right", out.angleDeg);
        Log(b);
    }

    Published p;
    p.out = out;
    p.active = active;
    p.alert = alert;
    p.time = f.time;
    p.tick = GetTickCount64();
    p.needRev = g_s.needRev;
    Publish(p);
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    game::Init(ctx);
    directinput::Listen(&OnFrame, nullptr);   // (returns at once while the panel is off)
}

void Configure(const Config& config) {
    const Config c = Sanitized(config);
    const bool was = g_on.exchange(c.on);
    g_hand.store(c.hand);
    g_size.store(c.sizeCm);
    g_height.store(c.heightCm);
    g_opacity.store(c.opacity);
    g_cone.store(c.coneDeg);
    g_pulse.store(c.pulse);
    g_low.store(c.lowFrac);
    g_flip.store(c.flip);
    g_orbHands.store(c.orbHands);
    g_orbSize.store(c.orbSizeCm);
    const OrbSpot spots[2] = {c.orbLeft, c.orbRight};
    for (int w = 0; w < 2; ++w) {
        g_orbSpot[w][0].store(spots[w].alongCm);
        g_orbSpot[w][1].store(spots[w].sideCm);
        g_orbSpot[w][2].store(spots[w].upCm);
        g_orbSpot[w][3].store(spots[w].rotXDeg);
        g_orbSpot[w][4].store(spots[w].rotYDeg);
        g_orbSpot[w][5].store(spots[w].rotZDeg);
    }
    g_orbMirror.store(c.orbMirror);
    g_orbUnderShield.store(c.orbUnderShield);
    g_orbShield[0].store(c.orbShield.turnDeg);
    g_orbShield[1].store(c.orbShield.alongCm);
    g_orbShield[2].store(c.orbShield.sideCm);
    g_orbShield[3].store(c.orbShield.upCm);
    g_orbShield[4].store(c.orbShield.rotXDeg);
    g_orbShield[5].store(c.orbShield.rotYDeg);
    g_orbShield[6].store(c.orbShield.rotZDeg);
    for (int k = 0; k < kHeroCount; ++k) g_orbClassZ[k].store(c.orbClassZCm[k]);
    for (int k = 0; k < kHeroCount; ++k) g_orbShieldClassUp[k].store(c.orbShieldClassUpCm[k]);
    for (int k = 0; k < kHeroCount; ++k) g_orbShieldClassAlong[k].store(c.orbShieldClassAlongCm[k]);
    for (int k = 0; k < kHeroCount; ++k) {
        g_orbShieldClassRot[0][k].store(c.orbShieldClassRotXDeg[k]);
        g_orbShieldClassRot[1][k].store(c.orbShieldClassRotYDeg[k]);
        g_orbShieldClassRot[2][k].store(c.orbShieldClassRotZDeg[k]);
    }
    g_orbOpacity.store(c.orbOpacity);
    g_orbFill.store(c.orbFill);
    g_orbShape.store(c.orbShape);
    g_orbMetal.store(c.orbMetal);
    g_orbDisc.store(c.orbDiscFaces);
    g_orbPatina.store(c.orbPatina);
    const int wasStyle = g_style.exchange(c.style);
    if (c.on != was || (c.on && c.style != wasStyle)) {
        char b[420];
        if (c.style == kStylePanel)
            snprintf(b, sizeof b, "vrcam: hand vitals %s ([hud] hand_vitals; the palm panel, %s hand, %.0f cm wide, %.0f cm up, cone %.0f degrees%s)", c.on ? "on" : "off",
                     c.hand == 0 ? "left" : c.hand == 1 ? "right" : "either", c.sizeCm, c.heightCm, c.coneDeg, c.pulse ? ", soft pulse under the low-life threshold" : "");
        else
            snprintf(b, sizeof b, "vrcam: hand vitals %s ([hud] hand_vitals; %s on the wrists, %.0f cm; left wrist along/side/up %.1f/%.1f/%.1f cm, right %.1f/%.1f/%.1f%s%s%s)", c.on ? "on" : "off",
                     c.style == kStyleGlobes ? "the game's own globes" : "3D glass orbs", c.orbSizeCm,
                     c.orbLeft.alongCm, c.orbLeft.sideCm, c.orbLeft.upCm,
                     c.orbMirror ? c.orbLeft.alongCm : c.orbRight.alongCm, c.orbMirror ? c.orbLeft.sideCm : c.orbRight.sideCm, c.orbMirror ? c.orbLeft.upCm : c.orbRight.upCm,
                     c.orbMirror ? " (mirrored)" : "", c.orbHands == 1 ? ", life right and mana left" : c.orbHands == 2 ? ", both on the left" : c.orbHands == 3 ? ", both on the right" : "",
                     c.pulse ? ", soft heartbeat under the low-life threshold" : "");
        Log(b);
    }
}

bool Enabled() { return g_on.load(std::memory_order_relaxed); }
bool PanelOn() { return g_on.load(std::memory_order_relaxed) && g_style.load(std::memory_order_relaxed) == kStylePanel; }
bool OrbsOn() { return g_on.load(std::memory_order_relaxed) && g_style.load(std::memory_order_relaxed) != kStylePanel; }
Config Settings() { return Current(); }

bool Snapshot(Vitals* out) {
    Vitals v;
    double ageMs = 0.0;
    if (!game::Latest(&v, &ageMs) || !Fresh(ageMs) || !v.known) return false;
    *out = v;
    return true;
}

void Tick(bool inGame) {
    if (!g_on.load(std::memory_order_relaxed)) return;
    game::Tick(inGame && viewgate::F4());
}

bool Draw(Layer* layer, uint32_t* needRevision) {
    if (!PanelOn() || !viewgate::F4()) return false;
    Published p;
    {
        std::lock_guard<std::mutex> lock(g_pubMutex);
        p = g_pub;
    }
    // No frame lately (a load, the headset away, the view changed): nothing, rather than a panel frozen in the air.
    if (!p.active || GetTickCount64() - p.tick > 250) return false;
    const float a = std::clamp(p.out.alpha, 0.0f, 1.0f);
    layer->visible = a > 0.0f && p.out.pose.valid;
    layer->pose = p.out.pose;
    const float grow = kShrinkWhenFaded + (1.0f - kShrinkWhenFaded) * a;
    layer->widthM = p.out.widthM * grow;
    layer->heightM = p.out.heightM * grow;
    layer->colorScale = a * Breath(p.alert, p.time);
    *needRevision = p.needRev;
    return true;
}

bool Picture(std::vector<uint32_t>* px, uint32_t* revision, uint32_t* width, uint32_t* height) {
    if (!P().Fetch(px, revision)) return false;
    *width = (uint32_t)handvitalspaint::kW;
    *height = (uint32_t)handvitalspaint::kH;
    return true;
}

void Teardown() {
    g_s = State{};
    Publish(Published{});
    P().Reset();
}

void Shutdown() {
    directinput::Unlisten(&OnFrame, nullptr);
    P().Stop();
    g_s = State{};
    Publish(Published{});
}

}  // namespace handvitals
