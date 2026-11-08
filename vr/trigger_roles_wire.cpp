// trigger_roles_wire.cpp - see trigger_roles_wire.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "banner.h"
#include "point_cast_wire.h"   // a point skill armed: its slot held while aiming (point_cast's arc and circle)
#include "direct_input.h"
#include "gesture_actions.h"
#include "skill_link_abi.h"
#include "skill_link_wire.h"
#include "skill_wheel.h"
#include "trigger_roles_wire.h"
#include "view_gate.h"
#include "wheel_cast.h"

namespace trigroleswire {
namespace {

namespace di = directinput;

std::atomic<int> g_role[2] = {{0}, {0}};
std::atomic<bool> g_castHold{false};
std::atomic<double> g_recognisedAt{-1.0};
std::atomic<int> g_aimHand{-1};      // a glyph trigger aiming now (0 left, 1 right), -1 none
std::atomic<int32_t> g_aimSkill{-1};
std::atomic<int> g_castHand{-1};     // the cast's hand: while aiming and kCastKeepMs after
std::atomic<uint64_t> g_castUntil{0};
constexpr uint64_t kCastKeepMs = 400;
std::mutex g_actionLock;
char g_action[2][32] = {};          // (under g_actionLock)
std::atomic<bool> g_registered{false};
std::atomic<bool> g_pen[2] = {{false}, {false}};
std::atomic<bool> g_armed[2] = {{false}, {false}};
std::atomic<int> g_lastStroke{-1};
std::atomic<uint64_t> g_lastStrokeAt{0};
trigroles::Hand g_hand[2];          // (the listener's thread)
bool g_actionDownCmd[2] = {false, false};   // an Action's command / D2R action held (to let it go)
uint32_t g_actionDownBits[2] = {0, 0};
uint32_t g_actionDownCode[2] = {0, 0};
ULONGLONG g_tapRtAt[2] = {0, 0};    // a left tap's RT, due a tick after its LT

// An item action at the press (the hero's own tome / scroll, the skill cast as a skill is).
void ItemAction(const gact::Def& d) {
    static BW_LinkSkills sk;   // (12 KB: not on the stack; the listener's thread)
    uint32_t rev = 0;
    int32_t found = -1;
    if (skilllinkwire::LatestSkills(&sk, &rev)) {
        const int32_t* ids = d.code == gact::kItemTownPortal ? gact::kTownPortalSkills : gact::kIdentifySkills;
        for (int k = 0; k < 2 && found < 0; ++k)
            for (uint32_t i = 0; i < std::min<uint32_t>(sk.count, BW_LINK_SKILLS_MAX); ++i)
                if (sk.skill[i].id == ids[k]) { found = ids[k]; break; }
    }
    if (found < 0) banner::Show(d.code == gact::kItemTownPortal ? "No Town Portal tome or scroll" : "No Identify tome or scroll", false, GetTickCount64(), 2500);
    else skilllinkwire::CastFromEngine(found);
}

void OnFrame(const di::Frame& f, void*) {
    const int roles[2] = {g_role[0].load(), g_role[1].load()};
    if (!roles[0] && !roles[1]) { g_pen[0] = g_pen[1] = false; return; }
    const bool allowed = viewgate::F4() && (f.flags & di::kFocused) && (f.flags & (di::kDirect | di::kUiUp | di::kOtherView | di::kInMenu)) == di::kDirect &&
                         !skillwheel::IsOpen();
    const ULONGLONG now = GetTickCount64();
    for (int h = 0; h < 2; ++h) {
        const trigroles::Role role = trigroles::RoleOf(roles[h]);
        trigroles::In in;
        in.time = f.time;
        in.allowed = allowed;
        in.trigger = f.hand[h].trigger;
        in.posOk = f.hand[h].grip.valid != 0;
        for (int k = 0; k < 3; ++k) in.pos[k] = f.hand[h].grip.pos[k];
        in.armed = g_armed[h].load();
        in.rule = g_castHold.load() ? trigroles::CastRule::Hold : trigroles::CastRule::Release;
        in.recognisedAt = g_recognisedAt.load();
        const int32_t armedSkill = in.armed ? skilllinkwire::ArmedSkill(h) : -1;
        in.armedPoint = armedSkill >= 0 && pointwire::On() && pointwire::IsPoint(armedSkill);
        const trigroles::Press before = g_hand[h].State();
        const trigroles::Out o = g_hand[h].Step(role, h, in);
        g_pen[h] = o.pen;
        if (o.aiming) {
            g_aimHand = h;
            g_aimSkill = armedSkill;
            g_castHand = h;
            g_castUntil = now + kCastKeepMs;
        } else if (g_aimHand.load() == h) {
            g_aimHand = -1;
            g_aimSkill = -1;
        }
        if (o.castTap || o.aimEnd) {   // the cast lands at the release: its point along this hand a moment more
            g_castHand = h;
            g_castUntil = now + kCastKeepMs;
        }
        if (before == trigroles::Press::Stroke || o.pen) {
            g_lastStroke = h;
            g_lastStrokeAt = now;
        }
        if (o.take) di::Take(h, di::kTrigger | di::kTakeTrigger, di::kSrcOther0);
        // the armed slot / Shift: the pad's own triggers, held a frame at a time (the game's poll reads them)
        if (o.castHold || (role == trigroles::Role::Shift && o.lt)) di::PadPulse(di::PadPress{0, uint8_t(o.lt ? 255 : 0), uint8_t(o.rt ? 255 : 0)}, wheel::kHoldMs);
        if (o.castTap) {
            if (h == 0) {   // the second layer: LT now, RT a tick later
                di::PadPulse(di::PadPress{0, 255, 0}, 120);
                g_tapRtAt[h] = now + 30;
            } else {
                di::PadPulse(di::PadPress{0, 0, 255}, 65);
            }
        }
        if (g_tapRtAt[h] && now >= g_tapRtAt[h]) {
            di::PadPulse(di::PadPress{0, 255, 255}, 65);
            g_tapRtAt[h] = 0;
        }
        // an Action, the game's own way
        if (role == trigroles::Role::Action) {
            char id[32];
            {
                std::lock_guard<std::mutex> l(g_actionLock);
                std::memcpy(id, g_action[h], sizeof id);
            }
            const gact::Def* d = gact::All().Find(id);
            if (d) {
                if (o.actionDown) {
                    if (d->kind == gact::Kind::Item) ItemAction(*d);
                    else if (d->kind == gact::Kind::Command) { di::Command(d->code, true); g_actionDownCmd[h] = true; g_actionDownCode[h] = d->code; }
                    else if (d->kind == gact::Kind::Act) { di::Action(d->code, true); g_actionDownBits[h] = d->code; }
                }
                if (o.actionHeld && d->kind == gact::Kind::Pad)
                    di::PadPulse(di::PadPress{uint16_t(d->code & 0xFFFFu), uint8_t((d->code & gact::kPadLT) ? 255 : 0), uint8_t((d->code & gact::kPadRT) ? 255 : 0)}, wheel::kHoldMs);
            }
        }
        if ((o.actionUp || role != trigroles::Role::Action || !allowed) && (g_actionDownCmd[h] || g_actionDownBits[h])) {   // let go
            if (g_actionDownCmd[h]) di::Command(g_actionDownCode[h], false);
            if (g_actionDownBits[h]) di::Action(g_actionDownBits[h], false);
            g_actionDownCmd[h] = false;
            g_actionDownBits[h] = 0;
        }
    }
}

}  // namespace

void Recognised(double time) { g_recognisedAt = time; }

void Configure(int roleLeft, int roleRight, const char* actionLeft, const char* actionRight, bool castHold) {
    g_castHold = castHold;
    g_role[0] = std::clamp(roleLeft, 0, 3);
    g_role[1] = std::clamp(roleRight, 0, 3);
    {
        std::lock_guard<std::mutex> l(g_actionLock);
        std::snprintf(g_action[0], sizeof g_action[0], "%s", actionLeft ? actionLeft : "");
        std::snprintf(g_action[1], sizeof g_action[1], "%s", actionRight ? actionRight : "");
    }
    if ((roleLeft || roleRight) && !g_registered.load()) {
        if (di::Listen(&OnFrame, nullptr)) g_registered = true;   // (kept: both 0 is handled in the frame)
    }
}

bool Active() { return g_role[0].load() != 0 || g_role[1].load() != 0; }
trigroles::Role RoleOf(int hand) { return trigroles::RoleOf(g_role[hand & 1].load()); }
const char* ActionOf(int hand) {
    static thread_local char out[32];
    std::lock_guard<std::mutex> l(g_actionLock);
    std::memcpy(out, g_action[hand & 1], sizeof out);
    return out;
}
bool Pen(int hand) { return g_pen[hand & 1].load(); }
bool Aiming(int* hand, int32_t* skill) {
    const int h = g_aimHand.load();
    if (h < 0) return false;
    if (hand) *hand = h;
    if (skill) *skill = g_aimSkill.load();
    return true;
}
int CastHand() { return GetTickCount64() < g_castUntil.load() ? g_castHand.load() : -1; }
int LastStrokeHand(uint64_t* at) {
    if (at) *at = g_lastStrokeAt.load();
    return g_lastStroke.load();
}
void SetArmed(int hand, bool armed) { g_armed[hand & 1] = armed; }

}  // namespace trigroleswire
