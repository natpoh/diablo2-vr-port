// bow_quiver_wire.cpp - [bow] quiver: arm the bow skill of the next shots from the quiver.
//
// Two ways, one choice, both for a bow. With the string drawn, a button of the string hand ([direct] quiver_next / quiver_prev) arms the next / the previous bow skill (onto the
// plain shot: disarmed). With the string not drawn, an orb on the bow's stave ([bow] quiver_orbs, below) taken with the free hand's trigger arms that skill; the armed one's orb again,
// or the plain shot's, disarms. (The reach behind a shoulder, where the quiver hung, is gone: the shoulders are the shoulder swap's and the draw from the shoulder's.)
//
// The choice is OURS, as [gameplay] melee_skill_armed's is (vr/melee_arm_game.cpp): the armed skill is kept here and shown in the mana medallion ([bow] quiver_icon); nothing is written
// into the game's skill slots, the pad's slots or the attack button A. Each release of the string with a bow or a crossbow in hand (OnRelease, before its button goes down) is decided here
// - the armed skill, or the plain arrow when the hero cannot pay it ([bow] quiver_plain_no_mana) - and the game's two reads of the attack slot for that press are answered with it by
// vrcam's slot-skill hook (ShotSkill), the one the armed melee skill uses: the game's own cast, mana, cooldown, arrows, line of sight. With no bow or crossbow in hand the armed skill does
// nothing. Nothing here shoots, casts or calls the server.
//
// The one write left (bowquiver::Cleanup): until 0.174 the quiver set its arrow skills on the A slot through the game's setter, and the game keeps that slot in the save - with a sword
// in hand every attack was a cast the game refused ("presses=9 hits=0"). With the quiver on, a bow skill on A is given back as the plain attack at a hero's load and whenever no bow or
// crossbow is in hand, through the same setter (the skill wheel's, vr/wheel_game.cpp, with its read-back and refusals).
//
// The look: with [bow] arrow_fx the arrow burns, freezes or crackles by the skill that is armed (vr/bow_quiver.h Element; the effect's ARROWFX block draws it - fire for Fire / Exploding /
// Immolation, frost for Cold / Ice / Freezing, arcane for Magic Arrow): nocked at rest from the moment the skill is armed, and through the draw. The plain shot has none. A skill with no
// element of its own, and every skill with the setting off, gets the plain glow of its colour (vr/bow_quiver.h AddGlow) at the arrow's head while the string is drawn. A cue only. The idea
// is bearstonem/d2r-pcvr's quiver preview (MIT), drawn our own way.
//
// [bow] quiver_orbs: with a bow in hand, a row of energy orbs hangs along its stave (vr/bow_orbs.h; the stave is read off the bow's own bones, skel::BowStave), one for the plain
// shot and one for every bow skill the hero has; the armed one stands out. The free hand's TRIGGER on an orb arms that skill ([bow] quiver_orb_trigger; off: the grip, as it was).
// Inside an orb's reach the trigger is the orb's (it does not start the draw and does not reach the game) and the grip does nothing; the draw does not take the string there
// (bowdrawwire::SetStartBlock); anywhere else the trigger and the string are what they always were.
//
// Idea: bearstonem/d2r-pcvr (MIT), v0.3.17 - his quiver button loads a skill arrow into the nocked arrow while the bow is drawn, and the shot calls the skill directly. This is
// written afresh and keeps the game's own path.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "log_async.h"
#include "arrow_fire_cut.h"
#include "bow_draw.h"
#include "bow_draw_wire.h"
#include "bow_orbs.h"
#include "bow_quiver.h"
#include "bow_quiver_wire.h"
#include "d2r_vr_shared.h"
#include "d2r_vr_state.h"
#include "direct_input.h"
#include "bow_mana.h"
#include "hand_vitals_game.h"
#include "melee_arm.h"   // InFlight: the release's skill, answered to the attack slot's reads while the press is young (the armed melee skill's)
#if D2RVR_FIRST_PERSON
#include "skeletons.h"   // skel::BowStave: the bow's stave off its own bones
#endif
#include "quiver_input.h"
#include "view_gate.h"
#include "wheel_game.h"
#include "wheel_types.h"

namespace gamestate { uint32_t WeaponClass(); bool MenuOpen(); uint64_t LocalPlayer(); }

namespace bowquiverwire {
namespace {

constexpr ULONGLONG kFreshMs = 250;

const D2RL::PluginContext* g_ctx = nullptr;
bowdrawwire::HandFn g_weaponHand = nullptr;
std::atomic<bool> g_on{false}, g_orbs{false}, g_orbTrigger{true};   // [bow] quiver, quiver_orbs, quiver_orb_trigger (the trigger picks an orb; off: the grip)
std::atomic<ULONGLONG> g_blockAt{0};               // the free hand is at an orb (the draw is held back), when
std::atomic<float> g_tune[7] = {};                  // [bow] quiver_orb_*: size, grab radius, start, spacing, x, y, z (metres); NaN = not set yet: the defaults
std::atomic<float> g_turn[3] = {};                  // [bow] quiver_orb_rot_*: the row's turn about x, y, z (degrees); NaN = not set yet: none
struct TuneInit { TuneInit() { for (std::atomic<float>& a : g_tune) a.store(NAN); for (std::atomic<float>& a : g_turn) a.store(NAN); } } g_tuneInit;
std::atomic<uint32_t> g_ctl[2] = {};               // the two controls, packed: (rel + 1) | (hand + 1) << 4 | mask << 8
std::atomic<ULONGLONG> g_slotsAt{0}, g_drawAt{0};   // the last frame the quiver could act in (a bow in hand) / the string was drawn or the orbs were shown

SRWLOCK g_lock = SRWLOCK_INIT;
struct GlowState {
    bool valid = false;
    bowquiver::Glow glow;
    ULONGLONG at = 0;
} g_glow;

struct OrbsState {
    bool valid = false;
    OrbDraw draw;
    ULONGLONG at = 0;
} g_orbsState;

struct FxState {
    bool valid = false;
    bowquiver::ArrowFx fx;
    ULONGLONG at = 0;
} g_fxState;
std::atomic<bool> g_fxMana{true};                        // [bow] arrow_fx_needs_mana: the arrow's effect goes out while the hero cannot pay the armed skill
std::atomic<bool> g_noMana{true}, g_hook{false};         // [bow] quiver_plain_no_mana; vrcam's slot-skill hook is in
std::atomic<bool> g_icon{true};                          // [bow] quiver_icon: the armed arrow skill's icon in the mana medallion
std::atomic<int> g_iconSkill{-1};                        // ... the skill (-1 none: nothing armed, the tick off)
std::atomic<bool> g_iconReady{false};                    // ... the hero can pay it (the release's own decision)
std::atomic<ULONGLONG> g_iconAt{0};                      // ... the frame that said so (stale: no bow or crossbow in hand, another view)
std::atomic<int> g_armed{-1};                            // the armed bow skill (-1 none), mirrored from the frame's thread
std::atomic<bool> g_cleanWants{false};                   // bowquiver::Cleanup wants the wheel's reads
// The release's decision, every frame with a bow or a crossbow in hand (bowquiver::DecideShot): OnRelease takes the newest.
std::atomic<int> g_decSkill{0}, g_decWhy{0}, g_decLevel{-1};
std::atomic<float> g_decCost{-1.0f}, g_decMana{-1.0f};
std::atomic<bool> g_decGuessed{false};
std::atomic<ULONGLONG> g_decAt{0};
std::atomic<ULONGLONG> g_lastPressAt{0};                 // our last armed shot that took mana (it is not in the read at once) ...
std::atomic<float> g_lastCostPts{0.0f};                  // ... and what it took
SRWLOCK g_flightLock = SRWLOCK_INIT;
meleearm::InFlight g_flight;                             // the release's skill, for the game's two reads of the attack slot
std::atomic<bool> g_arrowFx{false}, g_fxLacks{false};   // [bow] arrow_fx; the effect file has no ARROWFX uniforms
std::atomic<float> g_fxSize{1.0f}, g_fxBright{1.0f};     // the sliders, as factors
std::atomic<float> g_fxAlong{0.0f};                      // [bow] arrow_fx_along_cm: the effect's centre moved along the shaft, metres (forward +)
std::atomic<bool> g_fireCut{true};                       // [bow] arrow_fire_cut: the arrow's effect goes out at the release that shoots (vr/arrow_fire_cut.h)
std::atomic<float> g_fireCutMs{0.0f};                    // ... [bow] arrow_fire_cut_ms after it
std::atomic<unsigned> g_shots{0};                        // the releases that shot so far (OnRelease, the draw thread)

bowdrawwire::DrawInfo g_draw;   // the draw's word about this very frame (the draw thread's)
uint32_t g_drawCounter = 0;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

uint32_t Pack(const quiverin::Control& c) { return (uint32_t)(c.rel + 1) | (uint32_t)(c.hand + 1) << 4 | c.mask << 8; }
quiverin::Control Unpack(uint32_t v) {
    quiverin::Control c;
    c.rel = (int)(v & 0xF) - 1;
    c.hand = (int)((v >> 4) & 0xF) - 1;
    c.mask = (v >> 8) & 0xF;
    return c;
}

void SetOrbs(bool valid, const boworbs::Layout* l = nullptr) {
    AcquireSRWLockExclusive(&g_lock);
    g_orbsState.valid = valid && l && l->n > 0;
    if (g_orbsState.valid) {
        OrbDraw& d = g_orbsState.draw;
        d = OrbDraw{};
        d.n = l->n;
        for (int i = 0; i < l->n; ++i) {
            d.pos[i][0] = l->orb[i].c.x; d.pos[i][1] = l->orb[i].c.y; d.pos[i][2] = l->orb[i].c.z;
            d.radius[i] = l->orb[i].r;
            for (int k = 0; k < 3; ++k) d.rgb[i][k] = l->orb[i].rgb[k];
            d.current[i] = l->orb[i].current;
            d.hover[i] = l->orb[i].hover;
        }
    }
    g_orbsState.at = GetTickCount64();
    ReleaseSRWLockExclusive(&g_lock);
}

void PutFx(bool valid, const bowquiver::ArrowFx& fx) {
    AcquireSRWLockExclusive(&g_lock);
    g_fxState.valid = valid;
    if (valid) g_fxState.fx = fx;
    g_fxState.at = GetTickCount64();
    ReleaseSRWLockExclusive(&g_lock);
}

void SetGlow(bool valid, const bowquiver::Glow& g) {
    AcquireSRWLockExclusive(&g_lock);
    g_glow.valid = valid;
    if (valid) g_glow.glow = g;
    g_glow.at = GetTickCount64();
    ReleaseSRWLockExclusive(&g_lock);
}

// The game's own name for a skill from the snapshot's list, else ours.
const char* NameOf(const wheel::Snapshot* snap, bowquiver::Entry e) {
    if (snap)
        for (uint32_t i = 0; i < snap->count && i < (uint32_t)wheel::kCatalogMax; ++i)
            if (snap->catalog[i].id == e.id && snap->catalog[i].owner == e.owner && snap->catalog[i].name[0]) return snap->catalog[i].name;
    if (const bowquiver::Arrow* a = bowquiver::FindArrow(e.id)) return a->name;
    return "a skill";
}

bowdraw::Vec V(const float p[3]) { return {p[0], p[1], p[2]}; }
bowdraw::Quat Q(const float q[4]) { return {q[0], q[1], q[2], q[3]}; }

// The draw's word, taken in the draw's own listener (registered before this one: the same frame's).
void OnDrawHook(const bowdrawwire::DrawInfo& ds, const directinput::Frame& f) {
    g_draw = ds;
    g_drawCounter = f.counter;
}

// The hero's mana in points now (negative: not read): the latest read, less what our last armed shot took if the read has not caught up.
float ManaNow(ULONGLONG now) {
    handvitals::Vitals v;
    double age = 1e9;
    if (!handvitals::game::Latest(&v, &age) || !v.known || age >= 3000.0) return -1.0f;
    const ULONGLONG pressed = g_lastPressAt.load();
    return bowmana::Estimate(v.mana, age, pressed ? (double)(now - pressed) : -1.0, g_lastCostPts.load());
}

void OnFrame(const directinput::Frame& f, void*) {
    static bowquiver::Machine machine;
    static bowquiver::Cleanup cleanup;
    static wheel::Snapshot snap;   // (12 KB: not on the stack)
    static uint32_t rev = 0, session = 0;
    static bool wasLive = false;
    static uint64_t heroWas = 0;
    static int told = 0;
    static int srcHand[2] = {-1, -1};   // the hand that last pressed "next" / "previous" (for the buzz)
    static double changeAt = -1e9;      // the frame time of the last arming
    static boworbs::Layout layout;      // [bow] quiver_orbs
    static boworbs::Grab grab;
    static double drawEndedAt = -1e9;
    static int hoverWas = -1;
    static bool toldStave = false;
    static double nearTold = -1e9;
    static int nearLines = 0;

    const ULONGLONG nowMs = GetTickCount64();
    const uint32_t wc = gamestate::WeaponClass();
    const bool bow = wc == D2RVR_WEAPON_BOW;
    const bool ranged = bow || wc == D2RVR_WEAPON_CROSSBOW;
    const bool on = g_on.load();
    const bool have = wheelgame::Latest(&snap, &rev);
    const wheel::Snapshot* sp = have ? &snap : nullptr;
    const uint64_t hero = gamestate::LocalPlayer();

    {   // the old quiver's arrow skill on the A slot: given back as the plain attack (bowquiver::Cleanup - the one write the quiver still makes)
        bowquiver::CleanIn ci;
        ci.on = on;
        ci.ranged = ranged;
        ci.hero = hero;
        ci.t = f.time;
        ci.snap = sp;
        ci.fresh = have && wheel::Fresh(snap, nowMs, directinput::Session());
        ci.reply = wheelgame::ReplyNow();
        const bowquiver::CleanOut co = cleanup.Step(ci);
        g_cleanWants.store(cleanup.Wants());
        if (co.submit) wheelgame::Submit(co.req);
        char b[320];
        switch (co.event) {
            case bowquiver::CleanEvent::Asked:
                snprintf(b, sizeof b, "vrcam: bow quiver - the A (attack) slot holds %s (%d), left there by the old quiver or the skill panel; %s - the game is asked to put the plain attack back",
                         NameOf(sp, co.was), co.was.id, ranged ? "the hero's load" : "no bow or crossbow in hand");
                Log(b);
                break;
            case bowquiver::CleanEvent::Done:
                snprintf(b, sizeof b, "vrcam: bow quiver - the A slot holds the plain attack again (it held %s): every attack is the game's own, the quiver arms its skills only for the bow's shot", NameOf(sp, co.was));
                Log(b);
                break;
            case bowquiver::CleanEvent::Refused:
                snprintf(b, sizeof b, "vrcam: bow quiver - the game did not take the plain attack back on the A slot (it holds %s; the slot or the weapon set changed meanwhile) - tried again", NameOf(sp, co.was));
                Log(b);
                break;
            case bowquiver::CleanEvent::GaveUp:
                snprintf(b, sizeof b, "vrcam: bow quiver - the A slot still holds %s after %d tries: put the plain attack back on A with the skill panel (with no bow in hand the attack is read as the plain attack meanwhile)",
                         NameOf(sp, co.was), bowquiver::Cleanup::kMaxTries);
                Log(b);
                break;
            default: break;
        }
    }

    if (!on || hero != heroWas) {   // unticked, another hero or game: nothing armed
        if (heroWas && hero != heroWas && machine.Armed().id >= 0) Log("vrcam: bow quiver - another game or hero: nothing armed");
        heroWas = hero;
        machine.Clear();
        g_armed.store(-1);
    }
    const bool f4 = viewgate::F4() && (f.flags & directinput::kOtherView) == 0;
    const bowquiver::Entry armedNow = machine.Armed();

    // The release's decision, every frame with a bow or a crossbow in hand (bowquiver::DecideShot: the armed skill's cost at the hero's level against his mana, bow_mana.h): OnRelease takes the
    // newest. The icon and [bow] arrow_fx_needs_mana show the same decision. The mana is the newest read, less what our own last shot took if that is not in it yet.
    bowquiver::Shot dec;
    if (on && f4 && ranged) {
        g_slotsAt.store(nowMs);   // (the hero's skills and their levels: the wheel's reads)
        float mana = -1.0f;
        int32_t level = -1;
        if (armedNow.id >= 0) {
            handvitals::game::Tick(true);   // (the hero's mana, five times a second, on the game's UI thread)
            mana = ManaNow(nowMs);
            level = bowquiver::LevelOf(sp, armedNow);
        }
        dec = bowquiver::DecideShot(armedNow.id, true, level, mana, g_noMana.load());
        g_decSkill.store(dec.skill);
        g_decWhy.store((int)dec.why);
        g_decLevel.store(level);
        g_decCost.store(dec.cost);
        g_decMana.store(mana);
        g_decGuessed.store(dec.levelGuessed);
        g_decAt.store(nowMs);
        g_iconSkill.store(g_icon.load() && armedNow.id >= 0 ? armedNow.id : -1);   // [bow] quiver_icon (vr/armed_icon.h; vrcam's effect thread reads it, ChosenIcon)
        g_iconReady.store(dec.canPay);
        g_iconAt.store(nowMs);
    }

    if (!on || !f4 || !bow) {
        if (wasLive) {   // unticked, another view or another weapon: nothing is held, the glow goes (the armed skill stays: it waits for the bow)
            machine.Forget();
            grab.Reset();
            SetGlow(false, {});
            PutFx(false, {});
            SetOrbs(false);
            wasLive = false;
        }
        return;
    }
    if (!wasLive || f.session != session) {   // a new native session forgets the controls' edges and the grab
        session = f.session;
        machine.Forget();
        grab.Reset();
    }
    wasLive = true;
    const bool focused = (f.flags & directinput::kFocused) != 0;
    const bool menu = (f.flags & (directinput::kInMenu | directinput::kUiUp)) != 0 || gamestate::MenuOpen();
    const bool allowed = focused && !menu;
    const float yaw = f.head.valid == 1 ? bowdraw::HeadYaw(Q(f.head.rot)) : 0.0f;
    const bowdraw::Vec head = V(f.head.pos);

    const int holdVr = g_weaponHand ? g_weaponHand() : 1;   // vrcam's: 0 right, 1 left
    const int holdX = holdVr ? 0 : 1;                       // direct_input's: 0 left, 1 right
    const bowdrawwire::DrawInfo ds = g_drawCounter == f.counter ? g_draw : bowdrawwire::DrawInfo{};
    const bool drawing = allowed && ds.active && ds.drawing;

    // the two controls of the draw: a button of the string hand, live while the string is drawn
    quiverin::Control c[2] = {Unpack(g_ctl[0].load()), Unpack(g_ctl[1].load())};
    bool set[2] = {c[0].Set(), c[1].Set()}, down[2] = {false, false};
    if (set[0] && set[1] && c[0].Hand(holdX) == c[1].Hand(holdX) && c[0].mask == c[1].mask) set[1] = false;   // the same control twice: it arms the next
    for (int i = 0; i < 2; ++i) {
        const int h = set[i] ? c[i].Hand(holdX) : -1;
        down[i] = drawing && h >= 0 && h < 2 && (f.hand[h].buttons & c[i].mask) != 0;
        if (down[i]) srcHand[i] = h;
    }

    if (drawing) g_drawAt.store(nowMs);

    bowquiver::In in;
    in.t = f.time;
    in.allowed = allowed;
    in.drawing = drawing;
    in.snap = sp;
    bowquiver::Out o = machine.Step(in, set, down);

    // [bow] arrow_fx_needs_mana: the arrow's effect goes out while the hero cannot pay the armed skill (the release's own decision, above), eased in and out
    float fxFade = 1.0f;   // the arrow's effect: 1 lit, 0 out (the arrow looks plain)
    {
        static bowmana::Gate fxGate;
        static float fade = 1.0f;
        static double fadeT = 0.0;
        static bool outWas = false;
        const bool shortRaw = armedNow.id >= 0 && !dec.canPay;
        const bool out = g_fxMana.load() ? fxGate.Step(shortRaw, f.time) : (fxGate.Reset(), false);
        fade = bowmana::Ease(fade, out ? 0.0f : 1.0f, fadeT > 0.0 ? f.time - fadeT : 0.0);
        fadeT = f.time;
        fxFade = fade;
        if (out != outWas) {
            outWas = out;
            char b[260];
            if (out) snprintf(b, sizeof b, "vrcam: bow arrow fx - the hero cannot pay %s (it costs %.2f, he has %.2f): the arrow looks plain until the mana is back ([bow] arrow_fx_needs_mana)",
                              NameOf(sp, armedNow), dec.cost, g_decMana.load());
            else snprintf(b, sizeof b, "vrcam: bow arrow fx - the mana is back: the arrow's effect lights again");
            Log(b);
        }
    }
    // the bow's stave: the orbs' row hangs along it, and the arrow nocked at rest lies across it - read once a frame, when either wants it
    boworbs::Stave st;
    bool haveStave = false, staveRead = false;
    auto readStave = [&]() -> bool {
        if (staveRead) return haveStave;
        staveRead = true;
#if D2RVR_FIRST_PERSON
        float g3[3], t3[3], b3[3];
        if (skel::BowStave(g3, t3, b3)) {
            st.grip = V(g3); st.tip = V(t3); st.back = V(b3);
            st.fromModel = true;
            haveStave = true;
        }
#endif
        if (!haveStave && f.hand[holdX].grip.valid == 1) {   // the bow's bones were not read: up from the bow hand, perpendicular to where it points
            const bowdraw::Vec fwd = bowdraw::ToHeadTurn(bowdraw::BowForward(Q(f.hand[holdX].grip.rot)), yaw);
            haveStave = boworbs::StaveFromHand(bowtouch::RoomToHands(V(f.hand[holdX].grip.pos), head, yaw), fwd, &st);
            if (haveStave && !toldStave) { toldStave = true; Log("vrcam: bow quiver - the bow's bones were not read: the orbs and the arrow's effect use the bow hand's controller until they are"); }
        }
        return haveStave;
    };

    // [bow] quiver_orbs: a row of orbs along the bow's stave, the free hand's trigger (or grip) takes one
    bool orbsShown = false;
    if (drawing) drawEndedAt = f.time;
    // The orbs never go with a bow in hand in F4 and the tick on - not through a menu, the draw, the release or the 0.6 s after, not with the hand near them (boworbs.h): only the TAKING waits
    // (not while the string is drawn, nor for 0.6 s after it, nor in a menu: the free hand is the string hand then).
    const bool settled = boworbs::Settled(drawing, f.time - drawEndedAt) && allowed;
    static double layoutGoodAt = -1e9;
    static bool hadLayout = false;
    if (g_orbs.load() && f.head.valid == 1 && have) {
        readStave();
        if (haveStave) {   // where the row goes: said once, and again when the source or the limb that is up changes
            static int srcWas = -1, upWas = -2;
            const float len = bowdraw::Length(st.tip - st.grip);
            const int up = st.tip.y >= st.grip.y ? 1 : -1;
            if ((st.fromModel ? 1 : 0) != srcWas || up != upWas) {
                srcWas = st.fromModel ? 1 : 0;
                upWas = up;
                char sb[240];
                snprintf(sb, sizeof sb, "vrcam: bow quiver orbs - the stave: %s, %.0f cm long, %.0f deg from the vertical, the archer's side %.2f %.2f %.2f (hands' frame)",
                         st.fromModel ? "off the bow's bones (the limb that points up)" : "from the bow hand's controller", len * 100.0f,
                         len > 1e-3f ? std::acos(std::clamp((st.tip.y - st.grip.y) / len, -1.0f, 1.0f)) * 57.2958f : 0.0f, st.back.x, st.back.y, st.back.z);
                Log(sb);
            }
        }
        bowquiver::Entry list[bowquiver::kMaxList];
        const size_t n = haveStave ? machine.List(in, list, bowquiver::kMaxList) : 0;
        const bowquiver::Entry cur = machine.Current();
        boworbs::Tuning tn;
        {
            const float v[7] = {g_tune[0].load(), g_tune[1].load(), g_tune[2].load(), g_tune[3].load(), g_tune[4].load(), g_tune[5].load(), g_tune[6].load()};
            if (std::isfinite(v[0])) tn.radius = v[0];
            if (std::isfinite(v[1])) tn.reach = v[1];
            if (std::isfinite(v[2])) tn.start = v[2];
            if (std::isfinite(v[3])) tn.spacing = v[3];
            for (int k = 0; k < 3; ++k) if (std::isfinite(v[4 + k])) tn.offset[k] = v[4 + k];
            for (int k = 0; k < 3; ++k) { const float a = g_turn[k].load(); if (std::isfinite(a)) tn.turnDeg[k] = a; }
        }
        if (haveStave && n > 0 && boworbs::MakeLayout(st, list, n, &cur, tn, &layout)) {
            const int freeX = 1 - holdX;
            const directinput::Hand& fh = f.hand[freeX];
            boworbs::GrabIn gi;
            gi.tracked = focused && fh.active && fh.grip.valid == 1;
            gi.allowed = settled;   // (up always, but taken only when settled: the string hand holds the string then)
            gi.hand = gi.tracked ? bowtouch::RoomToHands(V(fh.grip.pos), head, yaw) : bowdraw::Vec{};
            const bool byTrigger = g_orbTrigger.load();   // [bow] quiver_orb_trigger: the trigger picks (the default); off: the grip, as it was
            const char* pickName = byTrigger ? "trigger" : "grip";
            const float press = byTrigger ? fh.trigger : fh.squeeze;
            gi.press = press;
            gi.latch = byTrigger;   // (a trigger pressed at an orb stays the orb's until it is let go: the hand leaving the orb with it down must not start the game's attack)
            const boworbs::GrabOut go = grab.Step(gi, layout);
            if (go.claim) {   // the pick control is the orb's, the grip does nothing there (trigger mode), and the draw's start is held back
                directinput::Take(freeX, byTrigger ? (directinput::kTrigger | directinput::kTakeTrigger | directinput::kSqueeze | directinput::kTakeSqueeze)
                                                   : (directinput::kSqueeze | directinput::kTakeSqueeze), directinput::kSrcBowDraw);
                g_blockAt.store(nowMs);
            }
            boworbs::Approach(&layout, settled && gi.tracked ? gi.hand : bowdraw::Vec{NAN, 0.0f, 0.0f}, go.hover);   // the orbs ahead of the hand grow and brighten as it nears, the one it is at most
            const wheel::Snapshot* sp0 = have ? &snap : nullptr;
            char lb[260];
            if (go.hover != hoverWas) {
                if (go.hover >= 0) {
                    directinput::Haptic(freeX, 0.15f, 15.0f);   // the hand has found an orb
                    snprintf(lb, sizeof lb, "vrcam: bow quiver orbs - the hand is at orb %d (%s): %.1f cm from its middle, grab radius %.1f cm, orb %.1f cm, the %s hand",
                             go.hover + 1, NameOf(sp0, layout.orb[go.hover].entry), go.dist * 100.0f, layout.reach * 100.0f, layout.orb[go.hover].r * 100.0f, freeX ? "right" : "left");
                } else {
                    snprintf(lb, sizeof lb, "vrcam: bow quiver orbs - the hand left orb %d: %.1f cm from the nearest orb now (grab radius %.1f cm)", hoverWas + 1, go.dist * 100.0f, layout.reach * 100.0f);
                }
                Log(lb);
                hoverWas = go.hover;
            } else if (go.hover < 0 && go.dist < 0.35f && f.time - nearTold > 1.5 && nearLines < 40) {   // near the row but not at an orb: how far (to tune the grab radius by)
                nearTold = f.time;
                ++nearLines;
                snprintf(lb, sizeof lb, "vrcam: bow quiver orbs - the hand is %.1f cm from the nearest orb (grab radius %.1f cm, %d orbs, %s %.2f)", go.dist * 100.0f, layout.reach * 100.0f, layout.n, pickName, press);
                Log(lb);
            }
            if (go.pick >= 0) {
                snprintf(lb, sizeof lb, "vrcam: bow quiver orbs - %s %.2f on orb %d (%s), %.1f cm from its middle", pickName, press, go.pick + 1, NameOf(sp0, layout.orb[go.pick].entry), go.dist * 100.0f);
                Log(lb);
                const bowquiver::Out p = machine.Pick(in, layout.orb[go.pick].entry);
                if (p.feedback != bowquiver::Feedback::None) { o.feedback = p.feedback; o.entry = p.entry; o.count = p.count; o.index = p.index; }
                o.which = 0;
                srcHand[0] = freeX;
            }
            orbsShown = true;
            layoutGoodAt = f.time;
            hadLayout = true;
            g_drawAt.store(nowMs);
        }
    }
    if (orbsShown) SetOrbs(true, &layout);
    else if (g_orbs.load() && boworbs::Holds(hadLayout, f.time - layoutGoodAt)) SetOrbs(true, &layout);   // a frame that cannot read the stave keeps the last good row for a moment: no blinking
    else { SetOrbs(false); grab.Reset(); hoverWas = -1; hadLayout = false; }

    if (o.take)   // while the string is drawn the quiver's buttons are not the layout's: nothing bound to them fires (the press stays masked until let go)
        for (int i = 0; i < 2; ++i)
            if (c[i].Set()) directinput::Take(c[i].Hand(holdX), c[i].mask, directinput::kSrcBowDraw);

    if (o.feedback != bowquiver::Feedback::None) {
        const int hand = srcHand[o.which] >= 0 ? srcHand[o.which] : 1 - holdX;
        char b[360];
        switch (o.feedback) {
            case bowquiver::Feedback::Armed:
            case bowquiver::Feedback::Switched:
                changeAt = f.time;
                g_armed.store(o.entry.id);
                directinput::Haptic(hand, 0.55f, 40.0f);
                snprintf(b, sizeof b, "vrcam: bow quiver - %s armed%s (%d of %d): each release with a bow or a crossbow in hand is %s, the plain arrow when the mana is short; the A slot is not touched",
                         NameOf(sp, o.entry), o.feedback == bowquiver::Feedback::Switched ? " instead" : "", o.index + 1, o.count, NameOf(sp, o.entry));
                Log(b);
                break;
            case bowquiver::Feedback::Disarmed:
                changeAt = f.time;
                g_armed.store(-1);
                directinput::Haptic(hand, 0.25f, 25.0f);
                snprintf(b, sizeof b, "vrcam: bow quiver - %s disarmed: each release is the plain shot", NameOf(sp, o.entry));
                Log(b);
                break;
            case bowquiver::Feedback::Refused:
                directinput::Haptic(hand, 0.20f, 90.0f);
                snprintf(b, sizeof b, "vrcam: bow quiver - %s is not the hero's now: not armed", NameOf(sp, o.entry));
                Log(b);
                break;
            case bowquiver::Feedback::NoArrows:
                directinput::Haptic(hand, 0.20f, 90.0f);
                if (told < 6) { ++told; Log("vrcam: bow quiver - no bow skills learned (points or items): nothing to take out of the quiver"); }
                break;
            case bowquiver::Feedback::Unreadable:
                directinput::Haptic(hand, 0.20f, 90.0f);
                if (told < 6) { ++told; Log("vrcam: bow quiver - the hero's skills cannot be read now (the skill wheel's read of the game): nothing armed"); }
                break;
            default: break;
        }
    }

    // the look: the armed skill's element (the effect's fire, frost or arcane) on the bow's own arrow as it is drawn - from the moment the skill is armed, the arrow
    // nocked at rest as much as on the drawn string (skel::BowArrow, off the arrow bone); without an element, or with [bow] arrow_fx off, its colour while the string is drawn
    bool glowing = false, fxing = false;
    bowquiver::Entry shown;
    double since = 0.0;
    const bowquiver::Arrow* a = allowed && machine.Shown(in, &shown, &since) ? bowquiver::FindArrow(shown.id) : nullptr;
    bool fireLit = true;
    {   // [bow] arrow_fire_cut: our effect on the arrow goes out at the release that shoots (or the set delay after it), not when the game hides the shot arrow; visual only
        static arrowcut::Cut cut;
        static unsigned shotsSeen = 0;
        const unsigned shots = g_shots.load();
        float n3[3], h3[3];
        arrowcut::In ci;
        ci.t = f.time;
        ci.on = g_fireCut.load();
        ci.delayS = arrowcut::DelayS(g_fireCutMs.load());
        ci.shot = shots != shotsSeen;
        ci.drawing = drawing;
        ci.arrowRead = skel::BowArrow(n3, h3);
        ci.skill = a ? shown.id : -1;
        shotsSeen = shots;
        const bool wasOut = cut.Out();
        fireLit = cut.Step(ci);
        if (cut.Out() != wasOut && a && bowquiver::ElementOf(shown.id).kind != bowquiver::Element::None) {
            static int told = 0;
            if (told < 6) {
                ++told;
                Log(cut.Out() ? "vrcam: bow arrow fx - the arrow was shot: its effect is out until the next arrow ([bow] arrow_fire_cut)"
                              : "vrcam: bow arrow fx - the next arrow: its effect lights again");
            }
        }
    }
    if (a && f.head.valid == 1) {
        const float ago = (float)(f.time - changeAt);
        bowquiver::ArrowFx fx;
        bowquiver::Glow g;
        float nock[3], tip[3];
        bool drawn = false;
        if (fxFade <= 0.02f) {
            // the hero cannot pay the chosen skill: a plain arrow - no effect, no coloured glow - until the mana is back
        } else if (!fireLit) {
            // [bow] arrow_fire_cut: the arrow was shot - its effect is out until the next arrow (a new draw, the game showing an arrow again)
        } else if (g_arrowFx.load() && !g_fxLacks.load() && bowquiver::ElementOf(shown.id).kind != bowquiver::Element::None) {
            if (skel::BowArrow(nock, tip, &drawn) && bowquiver::MakeArrowFx({nock[0], nock[1], nock[2]}, {tip[0], tip[1], tip[2]}, shown.id, ago, &fx)) {
                fx.strength *= fxFade;   // (eased out while the hero cannot pay, in again when the mana is back)
                PutFx(true, fx);
                fxing = true;
                static int toldId = -1;
                if (toldId != shown.id) {   // said when the armed skill changes
                    toldId = shown.id;
                    char b[360];
                    snprintf(b, sizeof b, "vrcam: bow arrow fx - %s: %s on the bow's own arrow (%s; %s), weight %.2f", a->name,
                             fx.kind == bowquiver::Element::Fire ? "fire" : fx.kind == bowquiver::Element::Frost ? "frost" : "arcane light",
                             drawn ? "drawn" : "nocked at rest", skel::BowArrowWho().c_str(), bowquiver::ElementOf(shown.id).heavy);
                    Log(b);
                }
            } else {
                static ULONGLONG toldNone = 0;   // the arrow is not read: no effect, never a line between the controllers in its place
                if (GetTickCount64() - toldNone > 10000) {
                    toldNone = GetTickCount64();
                    Log("vrcam: bow arrow fx - the bow's own arrow is not read (a bow rig that is not known, the arrow hidden after a shot, the hands not placed): no effect on it");
                }
            }
        } else if (drawing && ds.haveArrow && fxFade > 0.5f && bowquiver::MakeGlow(ds.rest, ds.stringHand, *a, ago, &g)) {
            SetGlow(true, g);
            glowing = true;
        }
    }
    if (!glowing) SetGlow(false, {});
    if (!fxing) PutFx(false, {});
}

}  // namespace

void Init(const D2RL::PluginContext* ctx, bowdrawwire::HandFn weaponHand) {
    g_ctx = ctx;
    g_weaponHand = weaponHand;
    bowdrawwire::SetDrawHook(&OnDrawHook);
    static bool listening = false;
    if (!listening) listening = directinput::Listen(&OnFrame, nullptr);   // (after the draw's own listener: it hands its word over in the same frame)
    if (!listening) Log("vrcam: bow quiver - direct_input's listener table is full: the quiver does nothing");
    if (g_on.load())
        Log("vrcam: bow quiver on - a button of the string hand while the string is drawn arms the next bow skill; each release is then that skill ([bow] quiver)");
}

void Configure(bool on, bool orbs, bool orbTrigger, const char* next, const char* prev) {
    quiverin::Control cn, cp;
    const bool okN = quiverin::ParseControl(next, &cn), okP = quiverin::ParseControl(prev, &cp);
    if (!okN) cn = quiverin::Control{};
    if (!okP) cp = quiverin::Control{};
    g_ctl[0].store(Pack(cn));
    g_ctl[1].store(Pack(cp));
    const bool was = g_on.exchange(on), wasOrbs = g_orbs.exchange(orbs), wasTrigger = g_orbTrigger.exchange(orbTrigger);
    if (!g_ctx) return;   // before Init: Init tells
    char b[360];
    if (!okN || !okP) {
        snprintf(b, sizeof b, "vrcam: bow quiver - [direct] %s is not a control the quiver knows (left_x left_y left_stick left_menu right_a right_b right_stick right_menu string_primary string_secondary string_stick bow_primary bow_secondary bow_stick none)",
                 !okN ? "quiver_next" : "quiver_prev");
        Log(b);
    }
    if (was != on || (on && (wasOrbs != orbs || (orbs && wasTrigger != orbTrigger)))) {
        snprintf(b, sizeof b, "vrcam: bow quiver %s%s", on ? "on - a button of the string hand while the string is drawn arms the next bow skill, each release with a bow or crossbow is then that skill; the A slot is never written ([bow] quiver)" : "off - nothing armed",
                 on && orbs ? (orbTrigger ? "; and the orbs along the bow's stave, the free hand's trigger arms one ([bow] quiver_orbs)" : "; and the orbs along the bow's stave, the free hand's grip arms one ([bow] quiver_orbs, quiver_orb_trigger off)") : "");
        Log(b);
    }
}

bool WantsSlots() {
    if (!g_on.load()) return false;
    if (g_cleanWants.load()) return true;   // (the old quiver's arrow skill on A: the cleanup reads the slot)
    const ULONGLONG at = g_slotsAt.load();
    return at && GetTickCount64() - at < 1000;
}

bool Drawing() {
    if (!g_on.load()) return false;
    const ULONGLONG at = g_drawAt.load();
    return at && GetTickCount64() - at < kFreshMs;
}

void SetOrbTuning(float sizeCm, float reachCm, float startCm, float spacingCm, float xCm, float yCm, float zCm, float turnXDeg, float turnYDeg, float turnZDeg) {
    const float v[7] = {sizeCm, reachCm, startCm, spacingCm, xCm, yCm, zCm};
    for (int k = 0; k < 7; ++k) g_tune[k].store(std::isfinite(v[k]) ? v[k] * 0.01f : NAN);
    const float a[3] = {turnXDeg, turnYDeg, turnZDeg};
    for (int k = 0; k < 3; ++k) g_turn[k].store(std::isfinite(a[k]) ? a[k] : NAN);   // (degrees as they are)
}

bool WantsOrbs() { return g_on.load() && g_orbs.load(); }

bool OrbsWanted() {
    if (!WantsOrbs()) return false;
    AcquireSRWLockShared(&g_lock);
    const bool want = g_orbsState.valid && GetTickCount64() - g_orbsState.at < kFreshMs;
    ReleaseSRWLockShared(&g_lock);
    return want;
}

bool Orbs(OrbDraw* out) {
    if (!out || !WantsOrbs()) return false;
    AcquireSRWLockShared(&g_lock);
    const bool fresh = g_orbsState.valid && GetTickCount64() - g_orbsState.at < kFreshMs;
    if (fresh) *out = g_orbsState.draw;
    ReleaseSRWLockShared(&g_lock);
    return fresh;
}

bool BlocksDraw() {
    if (!WantsOrbs()) return false;
    const ULONGLONG at = g_blockAt.load();
    return at && GetTickCount64() - at < kFreshMs;
}

void SetPlainNoMana(bool on) {
    const bool was = g_noMana.exchange(on);
    if (was != on && g_ctx) Log(on ? "vrcam: bow quiver - with an arrow skill armed and too little mana (by the game's own numbers) the release is the plain arrow ([bow] quiver_plain_no_mana)"
                                   : "vrcam: bow quiver - [bow] quiver_plain_no_mana off: a release the hero cannot pay is the armed skill all the same, and the game refuses it");
}

bool WantsHook() { return g_on.load(); }

void SetArrowFxNeedsMana(bool on) { g_fxMana.store(on); }

void SetIcon(bool on) {
    const bool was = g_icon.exchange(on);
    static bool told = false;
    if (told && was == on) return;
    told = true;
    if (on) Log("vrcam: bow quiver - the armed arrow skill's icon lies in the mana medallion on the wrist, full while the hero can pay the shot, faint when he cannot ([bow] quiver_icon; a bow or crossbow in hand, the vitals' medallions, F4)");
    else Log("vrcam: bow quiver - no icon in the mana medallion ([bow] quiver_icon=0)");
}

bool ChosenIcon(int* skill, bool* ready) {
    *skill = -1;
    *ready = false;
    if (!g_on.load() || !g_icon.load()) return false;
    const ULONGLONG at = g_iconAt.load();
    if (!at || GetTickCount64() - at >= kFreshMs) return false;   // (no bow or crossbow in hand: the frames stop saying so - no icon, the armed melee skill's shows)
    const int sk = g_iconSkill.load();
    if (sk < 0) return false;
    *skill = sk;
    *ready = g_iconReady.load();
    return true;
}

void SetHookReady(bool ready) {
    if (g_hook.exchange(ready) != ready && g_ctx && g_on.load())
        Log(ready ? "vrcam: bow quiver - the slot-skill hook is in: each release with a bow skill armed is read by the game as that skill"
                  : "vrcam: bow quiver - the game's slot-skill reads were not found (another build?): an armed bow skill cannot be shot, every release is the plain shot");
}

int ShotSkill() {
    if (!g_on.load()) return 0;
    AcquireSRWLockShared(&g_flightLock);
    const int skill = g_flight.Answer(GetTickCount64());
    ReleaseSRWLockShared(&g_flightLock);
    return skill;
}

void OnRelease() {
    g_shots.fetch_add(1);   // [bow] arrow_fire_cut: the arrow's effect goes out (the quiver's next frame)
    if (!g_on.load()) return;
    const int armed = g_armed.load();
    if (armed < 0) return;   // nothing armed: the game's own shot, as the slot has it
    const ULONGLONG now = GetTickCount64();
    static unsigned told = 0;
    if (!g_hook.load()) {
        if (told++ < 3) Log("vrcam: bow quiver - a release with a bow skill armed, but the slot-skill hook is not in: the plain shot");
        return;
    }
    if (now - g_decAt.load() >= 400) {   // (no frame with a bow or crossbow in hand just now: nothing to read it as)
        if (told++ < 6) Log("vrcam: bow quiver - a release with no fresh word on the bow in hand: the plain shot");
        return;
    }
    const int skill = g_decSkill.load();
    AcquireSRWLockExclusive(&g_flightLock);
    g_flight.Set(skill, now);   // one release, one cast: the game's two reads of the attack slot for this press, while it is young
    ReleaseSRWLockExclusive(&g_flightLock);
    const float cost = g_decCost.load();
    if (skill > 0 && cost > 0.0f) {   // a cast that takes mana: what it took, until the next read has it
        g_lastCostPts.store(cost);
        g_lastPressAt.store(now);
    }
    static int whyWas = -1;
    const int why = g_decWhy.load();
    if (++told <= 20 || why != whyWas) {
        whyWas = why;
        char m[48];
        const float mana = g_decMana.load();
        if (mana >= 0.0f) snprintf(m, sizeof m, "the hero has %.2f", mana);
        else snprintf(m, sizeof m, "the mana not read");
        char b[360];
        snprintf(b, sizeof b, "vrcam: bow quiver - a release: %s (%s, level %d%s, %.2f mana; %s)", bowquiver::ShotWhyName((bowquiver::ShotWhy)why),
                 bowquiver::FindArrow(armed) ? bowquiver::FindArrow(armed)->name : "?", g_decLevel.load(), g_decGuessed.load() ? " - not read, the first level's cost stands in" : "",
                 cost, m);
        Log(b);
    }
}

void SetArrowFx(bool on, float sizePct, float brightPct, float alongCm) {
    const bool was = g_arrowFx.exchange(on);
    g_fxSize.store(std::isfinite(sizePct) ? std::clamp(sizePct, 30.0f, 300.0f) * 0.01f : 1.0f);
    g_fxBright.store(std::isfinite(brightPct) ? std::clamp(brightPct, 20.0f, 300.0f) * 0.01f : 1.0f);
    g_fxAlong.store(std::isfinite(alongCm) ? std::clamp(alongCm, -20.0f, 60.0f) * 0.01f : 0.0f);
    if (was != on && g_ctx) Log(on ? "vrcam: bow arrow fx on - the nocked arrow burns, freezes or crackles by the armed skill ([bow] arrow_fx)" : "vrcam: bow arrow fx off - the coloured glow ([bow] arrow_fx)");
}

void SetArrowFireCut(bool on, float ms) {
    const bool was = g_fireCut.exchange(on);
    g_fireCutMs.store(std::isfinite(ms) ? std::clamp(ms, 0.0f, arrowcut::kMaxDelayMs) : 0.0f);
    if (was != on && g_ctx) Log(on ? "vrcam: bow arrow fx - the arrow's effect goes out at the release ([bow] arrow_fire_cut)"
                                   : "vrcam: bow arrow fx - [bow] arrow_fire_cut off: the effect stays on the arrow until the game hides it");
}

bool WantsArrowFx() { return g_on.load() && g_arrowFx.load() && !g_fxLacks.load(); }

bool ArrowFxWanted() {
    if (!g_on.load() || !g_arrowFx.load() || g_fxLacks.load()) return false;
    AcquireSRWLockShared(&g_lock);
    const bool want = g_fxState.valid && GetTickCount64() - g_fxState.at < kFreshMs;
    ReleaseSRWLockShared(&g_lock);
    return want;
}

bool ArrowFx(ArrowFxDraw* out) {
    if (!out || !g_on.load() || !g_arrowFx.load() || g_fxLacks.load()) return false;
    AcquireSRWLockShared(&g_lock);
    const bool fresh = g_fxState.valid && GetTickCount64() - g_fxState.at < kFreshMs;
    bowquiver::ArrowFx fx = g_fxState.fx;
    ReleaseSRWLockShared(&g_lock);
    if (!fresh) return false;
    bowquiver::MoveHead(&fx, g_fxAlong.load());   // [bow] arrow_fx_along_cm: the user's own shift along the shaft, at rest and drawn alike
    out->kind = bowquiver::EffectCode(fx.kind);
    out->tail[0] = fx.tail.x; out->tail[1] = fx.tail.y; out->tail[2] = fx.tail.z;
    out->head[0] = fx.head.x; out->head[1] = fx.head.y; out->head[2] = fx.head.z;
    out->size = 0.045f * g_fxSize.load();
    out->strength = fx.strength * g_fxBright.load();
    return out->kind > 0;
}

void ArrowFxEffectMissing() {
    if (!g_fxLacks.exchange(true)) Log("vrcam: bow arrow fx - D2R_DepthFog.fx has no AfOn: it is an older effect, so the coloured glow stands in (copy the new effect in)");
}

bool GlowWanted() {
    if (!g_on.load()) return false;
    AcquireSRWLockShared(&g_lock);
    const bool want = g_glow.valid && GetTickCount64() - g_glow.at < kFreshMs;
    ReleaseSRWLockShared(&g_lock);
    return want;
}

bool AddGlow(bowtouch::VectorSet* vs) {
    if (!vs || !g_on.load()) return false;
    AcquireSRWLockShared(&g_lock);
    const bool fresh = g_glow.valid && GetTickCount64() - g_glow.at < kFreshMs;
    const bowquiver::Glow g = g_glow.glow;
    ReleaseSRWLockShared(&g_lock);
    return fresh && bowquiver::AddGlow(*vs, g);
}

}  // namespace bowquiverwire
