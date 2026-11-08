// skill_link_wire.cpp - [gestures] skill_link, the wiring: see skill_link_wire.h. The decisions are vr/skill_link.h's (pure, tested).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

#include <D2RLPlugin/api.h>

#include "banner.h"   // a select the game cannot take: said on the HUD
#include "direct_input.h"
#include "haptics.h"  // a select taken: a pulse in the right hand
#include "link.h"
#include "log_async.h"
#include "skill_link.h"
#include "skill_link_wire.h"
#include "rune_flash.h"   // a gesture recognised: the trail's flash
#include "skill_wheel.h"
#include "trigger_roles_wire.h"   // the Glyphs triggers: which hand's slot a select fills, and that it is armed
#include "view_gate.h"
#include "wheel_game.h"

namespace skilllinkwire {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false};
std::atomic<bool> g_engine{false};   // SetEngine
std::atomic<int> g_spare{skilllink::kDefaultSpareSlot};
std::atomic<bool> g_settle{true};   // [gestures] skill_link_settle
std::mutex g_lock;   // the tick's state below and the record wish (RequestRecord: any thread)
uint32_t g_recordSeq = 0;
int32_t g_recordSkill = -1;
uint32_t g_recState = BW_LINK_REC_NONE;
int32_t g_recStateSkill = -1;
BW_LinkSkills g_latest;            // LatestSkills (under g_lock)
uint32_t g_latestRev = 0;
bool g_haveLatest = false;
int32_t g_engineCasts[16];         // CastFromEngine's, not yet taken by the tick (under g_lock)
int g_engineCastCount = 0;
int32_t g_engineArms[8];           // ArmFromEngine's (under g_lock)
int g_engineArmHands[8];
int g_engineArmCount = 0;
uint64_t g_armSeq = 0;             // the setter's request for an arm, until its answer
std::atomic<int32_t> g_armed[2] = {{-1}, {-1}};   // the skill a select gesture put on each hand's trigger
std::atomic<uint64_t> g_armedAt[2] = {{0}, {0}};
int g_armedSlot[2] = {-1, -1};     // the slot each hand's armed skill was put on (the tick's): changed there since, it is no longer armed
std::atomic<bool> g_selectIcon{true};

void Log(const char* text) {
    if (g_ctx) logasync::Info(g_ctx, text);
}
void LogF(const char* fmt, ...) {
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    Log(b);
}

// The tick's own (vrcam's timer thread only).
wheel::Snapshot g_snap;   // 12 KB each: not on the stack
uint32_t g_snapRev = 0;
BW_LinkSkills g_out, g_sent;
bool g_haveSent = false;
uint64_t g_sentAt = 0;
uint32_t g_revision = 0;
BW_LinkCasts g_casts;
skilllink::CastReader g_reader;
skilllink::Caster g_caster;
uint64_t g_assignSeq = 0;      // our setter request's sequence (wheelgame's channel is shared with the panel's editor)
uint64_t g_heldAt = 0;         // the held skills' presses renewed last
skilllink::CastWatch g_watch;  // a press on the spare slot: did the hero's animation turn into a cast? (logged; one retry)
int g_watchSlot = -1, g_watchTry = 0;
int32_t g_watchSkill = -1;
uint64_t g_pressAt = 0;
int64_t g_watchShownMs = -1;
bool g_wasOn = false;

const char* SlotName(int slot) { return slot >= 0 && slot < wheel::kSlotCount ? wheel::kSlotDefs[slot].label : "?"; }

void Press(int slot, uint32_t ms) {
    const skilllink::Press p = skilllink::SlotPress(slot);
    directinput::PadPulse({p.buttons, p.lt, p.rt}, ms ? ms : p.ms);
}

void Reset() {
    g_reader.Reset();
    g_caster.Reset();
    g_haveSent = false;
    g_assignSeq = 0;
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) { g_ctx = ctx; }

void Configure(bool on, int spareSlot, bool settle) {
    g_settle.store(settle);
    const bool was = g_on.exchange(on);
    g_spare.store(spareSlot >= -1 && spareSlot < wheel::kSlotCount ? spareSlot : skilllink::kDefaultSpareSlot);
    if (on != was)
        LogF("vrcam: skill link - %s ([gestures] skill_link; the spare slot %s)", on ? "on: the hero's skills go to BodyWalk, its casts come back (F4)" : "off",
             spareSlot < 0 ? "none: only skills on a slot are cast" : SlotName(spareSlot));
}

bool On() { return g_on.load(); }

void RequestRecord(int32_t skill) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_recordSkill = skill;
    ++g_recordSeq;
}

void SetEngine(bool on) { g_engine.store(on); }

bool LatestSkills(BW_LinkSkills* out, uint32_t* revision) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_haveLatest) return false;
    if (revision) *revision = g_latestRev;
    if (out) *out = g_latest;
    return true;
}

void CastFromEngine(int32_t skill) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_engineCastCount < 16) g_engineCasts[g_engineCastCount++] = skill;
}

void ArmFromEngine(int32_t skill, int hand) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_engineArmCount < 8) {
        g_engineArmHands[g_engineArmCount] = hand ? 1 : 0;
        g_engineArms[g_engineArmCount++] = skill;
    }
}

void SetSelectIcon(bool on) { g_selectIcon = on; }
bool SelectIcon() { return g_selectIcon.load(); }

int32_t ArmedSkill(int hand, uint64_t* at) {
    if (at) *at = g_armedAt[hand & 1].load();
    return g_armed[hand & 1].load();
}

uint32_t RecordState(int32_t* skill) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (skill) *skill = g_recStateSkill;
    return g_recState;
}

void Tick(bool play, int heroMode) {
    const bool on = g_on.load(), engine = g_engine.load();
    if (!on && g_wasOn) {   // the list emptied for BodyWalk: no set, nothing to bind or cast
        g_wasOn = false;
        std::memset((char*)&g_out + sizeof(BW_LinkHeader), 0, sizeof g_out - sizeof(BW_LinkHeader));
        g_out.revision = ++g_revision;
        link::PublishSkills(g_out);
        link::SetSkillLink(false, false);
        Reset();
    }
    if (!on && !engine) {
        std::lock_guard<std::mutex> lock(g_lock);
        g_engineCastCount = 0;
        return;
    }
    g_wasOn = on;
    const uint64_t now = GetTickCount64();
    // the skill panel's reads of the game (its UI thread): kept going while the panel itself is off
    if (!skillwheel::Enabled()) wheelgame::Tick(true, false);
    wheelgame::Latest(&g_snap, &g_snapRev);

    // the list: written when it changed, and once a second (BodyWalk takes 3 s of stillness for a game gone)
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_out.recordSequence = g_recordSeq;
        g_out.recordSkill = g_recordSkill;
    }
    skilllink::Fill(g_snap, nullptr, &g_out);
    const bool changed = !g_haveSent || !skilllink::SameList(g_out, g_sent);
    if (changed) g_out.revision = ++g_revision;
    if (changed || !g_haveLatest) {   // the engine's copy (LatestSkills): the class and skills it is told
        std::lock_guard<std::mutex> lock(g_lock);
        g_latest = g_out;
        g_latestRev = g_out.revision;
        g_haveLatest = true;
    }
    if (changed || now - g_sentAt >= skilllink::kPublishMs || g_out.recordSequence != g_sent.recordSequence) {
        if (on) link::PublishSkills(g_out);   // (to BodyWalk only with skill_link)
        if (changed)
            LogF("vrcam: skill link - the skills %s: %s, %u skills (revision %u)", on ? "to BodyWalk" : "for the gesture engine", g_out.setKey[0] ? g_out.setKey : "no hero",
                 g_out.count, g_out.revision);
        g_sent = g_out;
        g_haveSent = true;
        g_sentAt = now;
    }

    // the gesture engine's casts (the game's own way, as BodyWalk's below)
    {
        int32_t casts[16];
        int n = 0;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            n = g_engineCastCount;
            for (int i = 0; i < n; ++i) casts[i] = g_engineCasts[i];
            g_engineCastCount = 0;
        }
        for (int i = 0; i < n; ++i) {
            if (!play || !g_snap.ready) {
                LogF("vrcam: skill link - the gesture engine's cast of skill %d dropped: %s", casts[i],
                     !viewgate::F4() ? "not in VR first person (F4)" : !g_snap.ready ? "the game's skills are not read" : "a menu is open");
                continue;
            }
            LogF("vrcam: skill link - the gesture engine recognised a gesture for skill %d: cast", casts[i]);
            g_caster.Start(casts[i], now);
        }
    }

    // the gesture engine's selects: the skill onto the right trigger's slot by the game's setter, nothing cast (the trigger casts it from now on)
    {
        int32_t arms[8];
        int hands[8];
        int n = 0;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            n = g_engineArmCount;
            for (int i = 0; i < n; ++i) { arms[i] = g_engineArms[i]; hands[i] = g_engineArmHands[i]; }
            g_engineArmCount = 0;
        }
        for (int h = 0; h < 2; ++h)   // an armed skill whose slot holds something else now (the player set it, another bank): no longer armed
            if (g_armed[h].load() >= 0 && g_armedSlot[h] >= 0 && g_snap.ready && g_snap.slot[g_armedSlot[h]].known && g_snap.slot[g_armedSlot[h]].id != g_armed[h].load() &&
                !g_armSeq && now - g_armedAt[h].load() > 1500) {
                LogF("vrcam: skill link - select: the %s trigger's slot (%s) holds another skill now: skill %d is no longer armed", h ? "right" : "left", SlotName(g_armedSlot[h]),
                     g_armed[h].load());
                g_armed[h] = -1;
                g_armedSlot[h] = -1;
                trigroleswire::SetArmed(h, false);
            }
        if (g_armSeq) {   // the last one's answer
            const wheel::Reply r = wheelgame::ReplyNow();
            if (r.sequence == g_armSeq && r.result == wheel::Result::Refused) {
                banner::Show("The game would not put that skill on the trigger", false, now, 2500);
                LogF("vrcam: skill link - select: the game's setter refused");
            }
            if (r.sequence == g_armSeq && r.result != wheel::Result::Pending) g_armSeq = 0;
        }
        for (int i = 0; i < n; ++i) {
            const int32_t skill = arms[i];
            const int hand = hands[i];
            if (!play || !g_snap.ready) {
                LogF("vrcam: skill link - the gesture engine's select of skill %d dropped: %s", skill,
                     !viewgate::F4() ? "not in VR first person (F4)" : !g_snap.ready ? "the game's skills are not read" : "a menu is open");
                continue;
            }
            // the drawing hand's trigger's own slot: the left's LT + RT (the game's second layer); the right's RT when its trigger is the glyphs' or
            // [openxr] skill_rt_a makes it the RT slot, else A (the trigger is pad A)
            const int slot = hand == 0 ? 11 : (trigroleswire::RoleOf(1) == trigroles::Role::Glyphs || skillwheel::SlotControlsOn()) ? 9 : 8;
            const wheel::Skill* k = skilllink::Find(g_snap, skill);
            if (!k) { LogF("vrcam: skill link - select: the hero does not have skill %d now", skill); continue; }
            if (g_snap.slot[slot].known && g_snap.slot[slot].id == skill) {   // already there: nothing to set
                g_armed[hand].store(skill);
                g_armedAt[hand].store(now);
                g_armedSlot[hand] = slot;
                trigroleswire::SetArmed(hand, true);
                haptics::Pulse(hand ? haptics::kRight : haptics::kLeft, 0.7f, 45);
                LogF("vrcam: skill link - select: skill %d is already on the %s trigger (%s)", skill, hand ? "right" : "left", SlotName(slot));
                continue;
            }
            if (!g_snap.editable) {
                banner::Show("Skills can be set only in the game's controller layout", false, now, 2500);
                LogF("vrcam: skill link - select of skill %d: the game is not in its controller layout (its slots cannot be set)", skill);
                continue;
            }
            static uint64_t seq = 0x5E1EC70000000000ull;   // apart from the panel's editor's and the cast's own numbers
            wheel::Request r;
            r.op = wheel::Op::Assign;
            r.sequence = ++seq;
            r.session = g_snap.session;
            r.slot = slot;
            r.skill = k->id;
            r.owner = k->owner;
            r.bank = g_snap.bank;
            r.prevSkill = g_snap.slot[slot].known ? g_snap.slot[slot].id : -1;
            r.prevOwner = g_snap.slot[slot].known ? g_snap.slot[slot].owner : -1;
            g_armSeq = r.sequence;
            wheelgame::Submit(r);
            g_armed[hand].store(skill);
            g_armedAt[hand].store(now);
            g_armedSlot[hand] = slot;
            trigroleswire::SetArmed(hand, true);
            haptics::Pulse(hand ? haptics::kRight : haptics::kLeft, 0.7f, 45);   // (felt on a PS VR2 too: vr/haptics.h's weak pulses are not)
            LogF("vrcam: skill link - select: skill %d put on the %s trigger (%s) by the game's setter - the trigger casts it from now on", skill, hand ? "right" : "left",
                 SlotName(slot));
        }
    }

    // BodyWalk's casts
    const bool haveCasts = on && link::Casts(&g_casts);
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_recState = haveCasts ? g_casts.recordState : BW_LINK_REC_NONE;
        g_recStateSkill = haveCasts ? g_casts.recordSkill : -1;
    }
    // BodyWalk is given the hands (and, while it records, the controls) also with the plugin as the input
    link::SetSkillLink(haveCasts, haveCasts && (g_casts.recordState == BW_LINK_REC_READY || g_casts.recordState == BW_LINK_REC_DRAWING));
    if (haveCasts) {
        BW_LinkCast fresh[BW_LINK_CASTS_RING];
        const int n = g_reader.Step(g_casts, D2RVRStampNow(), fresh, (int)BW_LINK_CASTS_RING);
        for (int i = 0; i < n; ++i) {
            if (!play || !g_snap.ready) {
                LogF("vrcam: skill link - BodyWalk's cast of skill %d dropped: %s", fresh[i].skill,
                     !viewgate::F4() ? "not in VR first person (F4)" : !g_snap.ready ? "the game's skills are not read" : "a menu is open");
                continue;
            }
            if (fresh[i].source == BW_LINK_CAST_GESTURE) runeflash::Shared().Trigger(now);   // (a drawn gesture recognised: its trail flashes)
            g_caster.Start(fresh[i].skill, now);
        }
    }

    // the cast under way: the slot pressed, or the skill put on the spare slot first
    if (g_caster.Busy()) {
        bool refused = false;
        if (g_assignSeq) {
            const wheel::Reply r = wheelgame::ReplyNow();
            if (r.sequence == g_assignSeq && r.result == wheel::Result::Refused) refused = true;
            if (r.sequence == g_assignSeq && r.result != wheel::Result::Pending) g_assignSeq = 0;
        }
        const int32_t skill = g_caster.Skill();
        const skilllink::Plan p = g_caster.Step(g_snap, now, g_spare.load(), refused, g_settle.load() ? skilllink::kSettleMs : 0);
        switch (p.what) {
            case skilllink::Plan::Do::Press:
                if (play) {
                    Press(p.slot, 0);
                    if (p.settledMs >= 0) {
                        LogF("vrcam: skill link - skill %d: the game's %s pressed %lld ms after the game's table showed it there%s", skill, SlotName(p.slot),
                             (long long)p.settledMs, g_settle.load() ? " ([gestures] skill_link_settle)" : " (no settle)");
                        g_watch.Start(now, heroMode);   // and watched: did it turn into a cast?
                        g_watchSlot = p.slot;
                        g_watchSkill = skill;
                        g_watchTry = 1;
                        g_pressAt = now;
                        g_watchShownMs = p.settledMs;
                    } else
                        LogF("vrcam: skill link - skill %d cast: the game's %s pressed", skill, SlotName(p.slot));
                }
                break;
            case skilllink::Plan::Do::Assign: {
                static uint64_t seq = 0x5C11000000000000ull;   // apart from the panel's editor's own numbers
                wheel::Request r = p.assign;
                r.sequence = ++seq;
                g_assignSeq = r.sequence;
                wheelgame::Submit(r);
                LogF("vrcam: skill link - skill %d is on no slot: put on %s by the game's setter, then pressed", skill, SlotName(p.slot));
                break;
            }
            case skilllink::Plan::Do::GiveUp:
                LogF("vrcam: skill link - skill %d not cast: %s", skill, p.why);
                break;
            default: break;
        }
    }

    // the press on the spare slot watched: a cast, or none (refused - or a skill with no animation of its own, an aura) and once more
    switch (g_watch.Step(now, heroMode)) {
        case skilllink::CastWatch::Ev::Cast:
            LogF("vrcam: skill link - skill %d cast: the hero's animation turned into it %llu ms after press %d (%lld ms after the table showed it)",
                 g_watchSkill, (unsigned long long)(now - g_pressAt), g_watchTry, (long long)g_watchShownMs);
            break;
        case skilllink::CastWatch::Ev::NoCast:
            if (g_watchTry < skilllink::kPressTries && play) {
                LogF("vrcam: skill link - skill %d REFUSED? no cast within %llu ms of press %d (pressed %lld ms after the table showed it): pressed again",
                     g_watchSkill, (unsigned long long)skilllink::kCastSeenMs, g_watchTry, (long long)g_watchShownMs);
                if (skilllink::SlotOf(g_snap, g_watchSkill) == g_watchSlot) {
                    Press(g_watchSlot, 0);
                    ++g_watchTry;
                    g_watchShownMs += (long long)(now - g_pressAt);
                    g_pressAt = now;
                    g_watch.Start(now, heroMode);
                }
            } else {
                LogF("vrcam: skill link - skill %d: no cast after %d presses (refused each time - or a skill with no cast animation, an aura)",
                     g_watchSkill, g_watchTry);
            }
            break;
        default: break;
    }

    // the held ones (a button held in BodyWalk): their slots' presses renewed while held
    if (haveCasts && play && g_snap.ready && g_casts.heldCount && now - g_heldAt >= 20) {
        g_heldAt = now;
        for (uint32_t i = 0; i < g_casts.heldCount && i < BW_LINK_CASTS_HELD; ++i) {
            const int slot = skilllink::SlotOf(g_snap, g_casts.held[i]);
            if (slot >= 0) Press(slot, skilllink::kHeldMs);
        }
    }
}

}  // namespace skilllinkwire
