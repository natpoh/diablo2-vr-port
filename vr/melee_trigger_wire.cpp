// melee_trigger_wire.cpp - [gameplay] melee_touch_only: with hit by touch live, the trigger does not attack (a part of hit by touch, on with it; the key is the way back).
//
// Every headset frame (direct_input.h's Listen, the draw thread) the facts are put together - the view, the focus, a menu or panel, whether hit by touch is swinging a weapon now and which kind
// (meleetouch::Live) - and melee_trigger.h decides. When it says drop, both triggers are taken with directinput::kNoShotTrigger: the plugin's own layout leaves the attack-type outputs (action:attack, pad A,
// RT) out of what each trigger is bound to, plain, chord and hold lines alike, and everything else the trigger is bound to works. That is all: only OUR controller press is withheld - the game's commands are
// not blocked or hooked, the blade's touch (a press of its own, melee_touch.cpp) is not seen here, and with BodyWalk's own input (not the plugin's) there is nothing in reach and nothing is done.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include <D2RLPlugin/api.h>

#include "direct_input.h"
#include "log_async.h"
#include "melee_touch.h"
#include "melee_trigger.h"
#include "melee_trigger_wire.h"
#include "skill_wheel.h"
#include "view_gate.h"

namespace meleetrigger {
namespace {

constexpr ULONGLONG kLiveMs = 300;   // hit by touch was swinging a weapon this long ago at most

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{true};   // default ON: Configure from the ini replaces it at the first settings read

void Log(const char* text) {
    if (g_ctx) logasync::Info(g_ctx, text);
}
void LogF(const char* fmt, ...) {
    char b[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    Log(b);
}

void OnFrame(const directinput::Frame& f, void*) {
    static Gate gate;
    if (!g_on.load()) {
        gate.Reset();
        return;
    }
    Facts facts;
    facts.on = true;
    facts.f4 = viewgate::F4() && !(f.flags & directinput::kOtherView);
    facts.focused = (f.flags & directinput::kFocused) != 0;
    facts.menu = (f.flags & (directinput::kInMenu | directinput::kUiUp)) != 0;
    facts.touchLive = meleetouch::Live(kLiveMs, &facts.weaponType);
    facts.rtSkill = skillwheel::TriggerCastsSkill();   // [openxr] skill_rt_a: the trigger is the RT slot's, a skill is on it
    const float trigger[2] = {f.hand[0].trigger, f.hand[1].trigger};   // (direct_input's hands: 0 left, 1 right)
    const Gate::Out o = gate.Step(facts, trigger, f.time);
    if (o.v.drop) {
        directinput::Take(0, directinput::kNoShotTrigger, directinput::kSrcMelee);
        directinput::Take(1, directinput::kNoShotTrigger, directinput::kSrcMelee);
    }
    if (o.changed) {
        if (o.v.why == Why::Dropped) LogF("melee touch only: the trigger no longer attacks - hit by touch is swinging the weapon (kind %u), only the blade's touch attacks (the trigger's other bindings work)", facts.weaponType);
        else if (o.v.why == Why::SlotSkill) LogF("melee touch only: %s - only the plain attack stays withheld", WhyText(o.v.why));
        else LogF("melee touch only: the trigger attacks as bound - %s", WhyText(o.v.why));
    }
    if (o.withheld) LogF("melee touch only: a trigger press was not passed to the game (weapon kind %u) - %u so far; swing the blade to attack", facts.weaponType, o.withheldCount);
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    static bool listening = false;
    if (!listening) listening = directinput::Listen(&OnFrame, nullptr);
    if (!listening) Log("melee touch only: direct_input's listener table is full - the trigger is not withheld");
}

void Configure(bool on) {
    const bool was = g_on.exchange(on);
    static bool told = false;   // (the first read says it either way: it is on by default)
    if (on != was || !told) LogF("melee touch only: %s ([gameplay] melee_touch_only) - %s", on ? "on" : "off",
                        on ? "with hit by touch swinging a weapon (anything but a bow or a crossbow) the trigger's attack is withheld; the blade's touch attacks (the plugin's own controls only)"
                           : "the trigger attacks as bound, as before hit by touch had this");
    told = true;
}

bool On() { return g_on.load(); }

}  // namespace meleetrigger
