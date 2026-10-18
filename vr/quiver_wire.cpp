// quiver_wire.cpp - [hands] javelin_quiver: see quiver_wire.h. The detector and the list are vr/quiver_ctl.h and vr/quiver.h (pure, with tests); here they are fed
// from direct_input.h's headset frames and the skill wheel's read of the hero's skills, and what they decide goes out as: the trigger of a hand in the reach
// taken from the layout (directinput::Take), a pulse in the hand (directinput::Haptic), and the choice published for the cast's hook.
//
// Nothing here presses, casts or writes in the game.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>

#include <D2RLPlugin/api.h>

#include "direct_input.h"
#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "quiver_ctl.h"
#include "quiver_wire.h"
#include "shoulder_swap.h"
#include "throw_gesture_wire.h"
#include "view_gate.h"
#include "wheel_game.h"
#include "wheel_types.h"

namespace gamestate { bool MenuOpen(); uint32_t WeaponHand(); uint32_t HandCode(int side); }

namespace quiverwire {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false}, g_reach{true}, g_listening{false}, g_wants{false};
std::atomic<int32_t> g_skill{quiver::kDefault};
std::atomic<bool> g_ranged{true};
std::mutex g_infoMutex;
Info g_info;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

// The draw thread's (OnFrame) alone.
struct State {
    quiver::Controller ctl;
    shoulderswap::Heading heading;
    std::unique_ptr<wheel::Snapshot> snap = std::make_unique<wheel::Snapshot>();
    uint32_t rev = 0;
    quiver::Learned learned[wheel::kCatalogMax];
    size_t nLearned = 0;
    bool haveSnap = false;
    int ticksLeft[2] = {};
    double tickAt[2] = {};
    int32_t loggedSkill = quiver::kDefault;
    int loggedCount = -1;
    quiver::Kind loggedKind = quiver::Kind::None;
    bool seen = false;
};
State* g_state = nullptr;

void Publish(const quiver::Selector& sel, bool thrown) {
    const quiver::Entry& e = sel.Current();
    const bool active = thrown && sel.CanCycle();
    g_skill.store(thrown ? e.id : quiver::kDefault);
    g_ranged.store(e.ranged);
    std::lock_guard<std::mutex> lock(g_infoMutex);
    g_info = Info();
    std::snprintf(g_info.name, sizeof g_info.name, "%s", e.name);
    g_info.position = active ? sel.Position() : 0;
    g_info.count = active ? sel.Count() : 0;
    g_info.skill = thrown ? e.id : quiver::kDefault;
    g_info.ranged = e.ranged;
    g_info.active = active;
}

void Clear() {
    g_skill.store(quiver::kDefault);
    g_ranged.store(true);
    g_wants.store(false);
    std::lock_guard<std::mutex> lock(g_infoMutex);
    g_info = Info();
}

void OnFrame(const directinput::Frame& f, void*) {
    if (!g_on.load()) {
        if (g_state && g_state->seen) { *g_state = State(); Clear(); }
        return;
    }
    if (!g_state) g_state = new State();
    State& s = *g_state;
    s.seen = true;
    const bool focused = (f.flags & directinput::kFocused) != 0;
    // F4 only (vr/view_gate.h): another view is a menu as far as the quiver goes
    const bool menu = (f.flags & (directinput::kInMenu | directinput::kOtherView | directinput::kUiUp)) != 0 || !viewgate::F4() || gamestate::MenuOpen();
    const bool play = focused && !menu;
    if (f.head.valid == 1) s.heading.Update(f.head.rot);

    quiver::FrameIn in;
    in.t = f.time;
    in.play = play;
    in.zoneOn = g_reach.load();
    in.session = f.session;
    in.epoch = f.epoch;
    const int side = (int)gamestate::WeaponHand() & 1;
    in.weapon = quiver::KindOfCode(gamestate::HandCode(side));
    {
        const quiver::Kind other = quiver::KindOfCode(gamestate::HandCode(side ^ 1));
        in.otherHandThrown = other != quiver::Kind::None && other != quiver::Kind::Potion;
    }
    // the hero's skills: the skill wheel's newest read (its catalog), fresh within 3 s
    if (wheelgame::Latest(s.snap.get(), &s.rev) || !s.haveSnap) {
        s.nLearned = 0;
        for (uint32_t i = 0; i < s.snap->count && i < (uint32_t)wheel::kCatalogMax; ++i) {
            const wheel::Skill& k = s.snap->catalog[i];
            if (!k.known || !k.available || k.level <= 0 || k.id < 0) continue;
            s.learned[s.nLearned++] = {k.id, k.level, k.name};
        }
    }
    s.haveSnap = true;
    in.learned = s.learned;
    in.nLearned = s.nLearned;
    in.learnedKnown = s.snap->ready && wheel::Fresh(*s.snap, GetTickCount64(), s.snap->session, 3000);
    in.layoutStep = directinput::TakeQuiverStep();
    for (int hand = 0; hand < 2; ++hand) {
        const directinput::Hand& h = f.hand[hand];
        quiver::HandSample& hs = in.hand[hand];
        hs.tracked = focused && f.head.valid == 1 && h.active && h.grip.valid == 1 && s.heading.Relative(f.head.pos, h.grip.pos, hs.pos);
        hs.trigger = h.trigger;
    }
    in.rightTriggerIsThrows = throwwire::TriggerIsThrows();   // [hands] throw_gesture: the right trigger is the throw's own control

    const quiver::FrameOut out = s.ctl.Step(in);
    const bool thrown = quiver::IsThrownKind(in.weapon) && in.weapon != quiver::Kind::Potion;
    g_wants.store(thrown && play);
    for (int hand = 0; hand < 2; ++hand) {
        if (out.take[hand]) directinput::Take(hand, directinput::kTrigger | directinput::kTakeTrigger, directinput::kSrcOther2);
        if (out.tick[hand]) { s.ticksLeft[hand] = out.tick[hand]; s.tickAt[hand] = f.time; }
        if (s.ticksLeft[hand] > 0 && f.time >= s.tickAt[hand]) {
            directinput::Haptic(hand, 0.7f, 40.0f);
            --s.ticksLeft[hand];
            s.tickAt[hand] = f.time + 0.12;
        }
    }
    const quiver::Selector& sel = s.ctl.Sel();
    Publish(sel, thrown);

    // the log: what is offered when it changes, and every step
    const quiver::Entry& cur = sel.Current();
    char m[400];
    if (thrown && (sel.Count() != s.loggedCount || in.weapon != s.loggedKind)) {
        s.loggedCount = sel.Count();
        s.loggedKind = in.weapon;
        int n = std::snprintf(m, sizeof m, "vrcam: javelin quiver - a %s in hand, %d to choose from:", quiver::KindName(in.weapon), sel.Count());
        for (int i = 0; i < sel.Pool_().n && n > 0 && n < (int)sizeof m - 60; ++i)
            n += std::snprintf(m + n, sizeof m - (size_t)n, " %s%s", sel.Pool_().e[i].name, i + 1 < sel.Pool_().n ? "," : "");
        if (sel.CanCycle()) std::snprintf(m + n, sizeof m - (size_t)n, " - reach a hand behind a shoulder and tap its trigger (right: next, left: previous)");
        Log(m);
    } else if (!thrown && s.loggedKind != quiver::Kind::None) {
        s.loggedKind = quiver::Kind::None;
        s.loggedCount = -1;
        s.loggedSkill = quiver::kDefault;
        Log("vrcam: javelin quiver - no thrown weapon in hand: the choice is cleared");
    }
    if (out.stepped || (out.changed && cur.id != s.loggedSkill)) {
        s.loggedSkill = cur.id;
        std::snprintf(m, sizeof m, "vrcam: javelin quiver - %s (%d of %d)%s: %s", cur.name, sel.Position(), sel.Count(),
                      out.stepped ? "" : " (the list changed)",
                      cur.id == quiver::kDefault ? "the next throw is the plain Throw" : cur.ranged ? "the next throw casts it (a cast at a spot)" : "the plain attack is it; a throw stays the plain Throw");
        Log(m);
    }
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) { g_ctx = ctx; }

void Configure(bool on, bool reach) {
    const bool was = g_on.exchange(on);
    g_reach.store(reach);
    if (on && !g_listening.exchange(true)) {
        if (!directinput::Listen(&OnFrame, nullptr)) {
            g_listening.store(false);
            Log("vrcam: javelin quiver - the headset frames could not be listened to (the listener table is full) - the quiver does nothing");
        }
    }
    if (on != was) Log(on ? "vrcam: javelin quiver - on: with a javelin or a throwing weapon in hand, the next throw's skill is chosen from the quiver (F4 only)"
                          : "vrcam: javelin quiver - off");
    if (!on) Clear();
}

bool On() { return g_on.load(); }

bool Chosen(int32_t* skill, bool* ranged) {
    if (!g_on.load()) return false;
    const int32_t s = g_skill.load();
    if (s < 0) return false;
    *skill = s;
    *ranged = g_ranged.load();
    return true;
}

bool WantsSkills() { return g_on.load() && g_wants.load(); }

bool Current(Info* out) {
    if (!out || !g_on.load()) return false;
    std::lock_guard<std::mutex> lock(g_infoMutex);
    *out = g_info;
    return g_info.active;
}

}  // namespace quiverwire
