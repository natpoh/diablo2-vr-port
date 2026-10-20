// bow_quiver_wire.cpp - [bow] quiver: pick the arrow (the bolt) of the next shot from the quiver.
//
// Two ways, one choice, both for a bow. With the string drawn, a button of the string hand ([direct] quiver_next / quiver_prev) loads the next / the previous bow skill.
// With the string not drawn, an orb on the bow's stave ([bow] quiver_orbs, below) taken with the free hand's trigger picks that skill. (The reach behind a shoulder, where the quiver
// hung, with its trigger and A / X, for a bow and a crossbow, is gone: the shoulders are the shoulder swap's and the draw from the shoulder's.)
//
// Choosing a skill is the game's own business, done the way a thumb does it with the skill panel: the game's current skill on the slot the shot presses is set through the
// game's own setter - the one the skill wheel and the tiles use (vr/wheel_game.cpp, with its read-back and refusals). The bow's slot is the one the draw's release presses
// (bow_draw_button; the attack = the controller's A slot). Nothing here shoots, casts or calls the server: the shot is the game's own attack path - mana, arrows, attack rate,
// line of sight. Only the hero's learned bow skills (level above 0 from points or from an item - the list the skill panel offers) are offered.
//
// The look: with [bow] arrow_fx the arrow burns, freezes or crackles by the skill that is chosen (vr/bow_quiver.h Element; the effect's ARROWFX block draws it - fire for Fire / Exploding /
// Immolation, frost for Cold / Ice / Freezing, arcane for Magic Arrow): nocked at rest from the moment the skill is chosen (loaded by an orb or a button, or the one the game's slot
// holds), and through the draw. The plain shot has none. A skill with no element of its own, and every skill with the setting off, gets the plain glow of its colour (vr/bow_quiver.h
// AddGlow) at the arrow's head while the string is drawn. A cue only. The idea is bearstonem/d2r-pcvr's quiver preview (MIT), drawn our own way.
//
// [bow] quiver_orbs: with a bow in hand, a row of energy orbs hangs along its stave (vr/bow_orbs.h; the stave is read off the bow's own bones, skel::BowStave), one for the plain
// shot and one for every bow skill the hero has; the skill the slot holds stands out. The free hand's TRIGGER on an orb picks that skill ([bow] quiver_orb_trigger; off: the grip, as
// it was) - the same request as a step of the quiver (bowquiver::Machine::Pick), the game's own setter, never a server call. Inside an orb's reach the trigger is the orb's (it does
// not start the draw and does not reach the game) and the grip does nothing; the draw does not take the string there (bowdrawwire::SetStartBlock); anywhere else the trigger and the
// string are what they always were.
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
#if D2RVR_FIRST_PERSON
#include "skeletons.h"   // skel::BowStave: the bow's stave off its own bones
#endif
#include "quiver_input.h"
#include "view_gate.h"
#include "wheel_game.h"
#include "wheel_types.h"

namespace gamestate { uint32_t WeaponClass(); bool MenuOpen(); }

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
std::atomic<bool> g_fxMana{true};                        // [bow] arrow_fx_needs_mana: the arrow's effect goes out while the hero cannot pay the chosen skill
std::atomic<bool> g_noMana{false}, g_plainHook{false};   // [bow] quiver_plain_no_mana; vrcam's slot-skill hook is in
std::atomic<bool> g_icon{true};                          // [bow] quiver_icon: the chosen arrow skill's icon in the mana medallion
std::atomic<int> g_iconSkill{-1};                        // ... the skill (-1 none: the plain shot, nothing chosen, the tick off)
std::atomic<bool> g_iconReady{false};                    // ... the hero can pay it (the release's own check)
std::atomic<ULONGLONG> g_iconAt{0};                      // ... the frame that said so (stale: no bow in hand, another view)
std::atomic<int> g_shortSkill{-1}, g_armedSkill{-1};     // the arrow skill the hero cannot pay now (while the string is drawn) / the one the release was turned into the plain attack for
std::atomic<ULONGLONG> g_shortAt{0}, g_armedAt{0}, g_lastPressAt{0};
std::atomic<float> g_shortNeed{0.0f}, g_shortHave{0.0f}, g_costPts{0.0f}, g_lastCostPts{0.0f};
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

void OnFrame(const directinput::Frame& f, void*) {
    static bowquiver::Machine machine;
    static wheel::Snapshot snap;   // (12 KB: not on the stack)
    static uint32_t rev = 0, session = 0;
    static bool wasLive = false;
    static int told = 0;
    static int srcHand[2] = {-1, -1};   // the hand that last pressed "next" / "previous" (for the buzz)
    static double changeAt = -1e9;      // the frame time of the last load
    static boworbs::Layout layout;      // [bow] quiver_orbs
    static boworbs::Grab grab;
    static double drawEndedAt = -1e9;
    static int hoverWas = -1;
    static bool toldStave = false;
    static double nearTold = -1e9;
    static int nearLines = 0;

    const bool bow = gamestate::WeaponClass() == D2RVR_WEAPON_BOW;
    const bool f4 = viewgate::F4() && (f.flags & directinput::kOtherView) == 0;
    if (!g_on.load() || !f4 || !bow) {
        if (wasLive) {   // unticked, another view or another weapon: nothing is held, the glow goes
            machine.Forget();
            grab.Reset();
            SetGlow(false, {});
            PutFx(false, {});
            SetOrbs(false);
            wasLive = false;
        }
        return;
    }
    if (!wasLive || f.session != session) {   // a new native session forgets everything
        session = f.session;
        machine.Forget();
        grab.Reset();
    }
    wasLive = true;
    const ULONGLONG nowMs = GetTickCount64();
    const bool focused = (f.flags & directinput::kFocused) != 0;
    const bool menu = (f.flags & (directinput::kInMenu | directinput::kUiUp)) != 0 || gamestate::MenuOpen();
    const bool allowed = focused && !menu;
    if (allowed) g_slotsAt.store(nowMs);
    const float yaw = f.head.valid == 1 ? bowdraw::HeadYaw(Q(f.head.rot)) : 0.0f;
    const bowdraw::Vec head = V(f.head.pos);

    const int holdVr = g_weaponHand ? g_weaponHand() : 1;   // vrcam's: 0 right, 1 left
    const int holdX = holdVr ? 0 : 1;                       // direct_input's: 0 left, 1 right
    const bowdrawwire::DrawInfo ds = g_drawCounter == f.counter ? g_draw : bowdrawwire::DrawInfo{};
    const bool drawing = allowed && ds.active && ds.drawing;
    const int slot = bowquiver::SlotOfButton(ds.active ? ds.button : 0);   // (no draw: the attack, the A slot)

    // the two controls of the draw: a button of the string hand, live while the string is drawn
    quiverin::Control c[2] = {Unpack(g_ctl[0].load()), Unpack(g_ctl[1].load())};
    bool set[2] = {c[0].Set(), c[1].Set()}, down[2] = {false, false};
    if (set[0] && set[1] && c[0].Hand(holdX) == c[1].Hand(holdX) && c[0].mask == c[1].mask) set[1] = false;   // the same control twice: it loads the next
    for (int i = 0; i < 2; ++i) {
        const int h = set[i] ? c[i].Hand(holdX) : -1;
        down[i] = drawing && h >= 0 && h < 2 && (f.hand[h].buttons & c[i].mask) != 0;
        if (down[i]) srcHand[i] = h;
    }

    if (drawing) g_drawAt.store(nowMs);

    const bool have = wheelgame::Latest(&snap, &rev);
    bowquiver::In in;
    in.t = f.time;
    in.allowed = allowed;
    in.drawing = drawing;
    in.snap = have ? &snap : nullptr;
    in.now = nowMs;
    in.session = directinput::Session();
    in.slot = slot;
    in.reply = wheelgame::ReplyNow();
    bowquiver::Out o = machine.Step(in, set, down);

    // What the game does with the shot's skill, said in the log so that a plain shot where a skill was chosen can be told from what the plugin did: the plugin only ever presses the game's
    // own attack (the pad's A) and asks the game's setter for a skill on the A slot - it never picks the plain attack by itself. So at every release the skill on the slot and the hero's mana
    // are written down, and a change of that slot that the quiver did not ask for (the game putting the attack back, the skill panel, the wheel) is told when it is seen.
    {
        static int slotWas = -2, slotIdxWas = -1;
        static ULONGLONG askedAt = 0;
        static bool drawingWas = false;
        const bool slotRead = have && snap.ready && in.slot >= 0 && in.slot < wheel::kSlotCount && snap.slot[in.slot].known;
        if (o.submit) askedAt = nowMs;
        if (slotRead) {
            handvitals::game::Tick(true);   // (the hero's mana, five times a second, on the game's UI thread)
            const bowquiver::Entry now{snap.slot[in.slot].id, snap.slot[in.slot].owner};
            char vit[64] = "mana not read";
            handvitals::Vitals v;
            double age = 1e9;
            if (handvitals::game::Latest(&v, &age) && v.known && age < 3000.0) snprintf(vit, sizeof vit, "mana %.0f of %.0f", v.mana, v.maxMana);
            char b[300];
            if (slotWas != -2 && slotIdxWas == in.slot && now.id != slotWas && nowMs - askedAt > 3000 && !machine.Believed(in.t)) {
                snprintf(b, sizeof b, "vrcam: bow quiver - the %s slot now holds %s (%d), it held skill %d, and the quiver did not ask for it (the game itself, the skill panel or the wheel); %s",
                         wheel::kSlotDefs[in.slot].label, NameOf(&snap, now), now.id, slotWas, vit);
                Log(b);
            }
            slotWas = now.id;
            slotIdxWas = in.slot;
            if (drawingWas && !drawing && bowquiver::FindArrow(now.id)) {   // the string let go with an arrow skill chosen: a shot, or a draw that ended with none
                snprintf(b, sizeof b, "vrcam: bow quiver - the string let go with %s (%d) on the %s slot; %s", NameOf(&snap, now), now.id, wheel::kSlotDefs[in.slot].label, vit);
                Log(b);
            }
        } else if (!have) {
            slotWas = -2;
        }
        drawingWas = drawing;
    }

    // [bow] quiver_plain_no_mana and arrow_fx_needs_mana: whether the hero can pay the chosen arrow skill (the game's own numbers, vr/bow_mana.h: the skill's row, its level, the hero's mana),
    // every frame, at rest as much as while drawn. OnRelease turns the release into the plain attack when he cannot; the arrow's effect goes out (fxFade) while he cannot. The mana is the
    // newest read, less what our own last cast took if that is not in it yet.
    float fxFade = 1.0f;   // the arrow's effect: 1 lit, 0 out (the arrow looks plain)
    {
        static bowmana::Gate fxGate;
        static float fade = 1.0f;
        static double fadeT = 0.0;
        static bool outWas = false;
        int shortSkill = -1;
        float need = 0.0f, haveMana = 0.0f, cost = 0.0f;
        bowquiver::Entry chosen;
        double sinceChosen = 0.0;
        bool shortRaw = false;
        if ((g_noMana.load() || g_fxMana.load() || g_icon.load()) && have && snap.ready && machine.Shown(in, &chosen, &sinceChosen) && bowmana::Find(chosen.id)) {
            int level = 0;
            for (uint32_t i = 0; i < snap.count && i < (uint32_t)wheel::kCatalogMax; ++i)
                if (snap.catalog[i].id == chosen.id && snap.catalog[i].owner == chosen.owner) { level = snap.catalog[i].level; break; }
            cost = bowmana::CostPoints(chosen.id, level);
            handvitals::Vitals v;
            double age = 1e9;
            if (cost >= 0.0f && handvitals::game::Latest(&v, &age) && v.known && age < 3000.0) {
                const ULONGLONG pressed = g_lastPressAt.load();
                haveMana = bowmana::Estimate(v.mana, age, pressed ? (double)(nowMs - pressed) : -1.0, g_lastCostPts.load());
                if (bowmana::Short(chosen.id, level, haveMana)) { shortSkill = chosen.id; need = cost; shortRaw = true; }
            }
        }
        // [bow] quiver_icon: the chosen arrow skill and whether the hero can pay it, for its icon in the mana medallion (vr/armed_icon.h; vrcam's effect thread reads it, ChosenIcon)
        g_iconSkill.store(g_icon.load() && have && snap.ready && chosen.id >= 0 && bowmana::Find(chosen.id) ? chosen.id : -1);
        g_iconReady.store(!shortRaw);
        g_iconAt.store(nowMs);
        g_shortNeed.store(need);   // (every frame: the release reads what the frame before it said)
        g_shortHave.store(haveMana);
        g_costPts.store(cost > 0.0f ? cost : 0.0f);
        g_shortSkill.store(shortSkill);
        g_shortAt.store(nowMs);
        {   // the arrow's effect: out while the hero cannot pay (believed after kDwellS), eased in and out
            const bool out = g_fxMana.load() ? fxGate.Step(shortRaw, f.time) : (fxGate.Reset(), false);
            fade = bowmana::Ease(fade, out ? 0.0f : 1.0f, fadeT > 0.0 ? f.time - fadeT : 0.0);
            fadeT = f.time;
            fxFade = fade;
            if (out != outWas) {
                outWas = out;
                char b[260];
                if (out) snprintf(b, sizeof b, "vrcam: bow arrow fx - the hero cannot pay %s (it costs %.2f, he has %.2f): the arrow looks plain until the mana is back ([bow] arrow_fx_needs_mana)",
                                  NameOf(have ? &snap : nullptr, bowquiver::Entry{chosen.id, chosen.owner}), need, haveMana);
                else snprintf(b, sizeof b, "vrcam: bow arrow fx - the mana is back: the arrow's effect lights again");
                Log(b);
            }
        }
        static ULONGLONG toldArmed = 0;
        const ULONGLONG armedAt = g_armedAt.load();
        if (armedAt && armedAt != toldArmed) {   // this release was turned into the plain attack: said once, with the numbers
            toldArmed = armedAt;
            const int sk = g_armedSkill.load();
            char b[260];
            snprintf(b, sizeof b, "vrcam: bow quiver - not enough mana for %s: it costs %.2f and the hero has %.2f (the game's own numbers) - this release is the plain attack, the skill stays chosen ([bow] quiver_plain_no_mana)",
                     NameOf(have ? &snap : nullptr, bowquiver::Entry{sk, -1}), g_shortNeed.load(), g_shortHave.load());
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
        bowquiver::Entry cur;
        const bool haveCur = machine.Current(in, &cur);
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
        if (haveStave && n > 0 && boworbs::MakeLayout(st, list, n, haveCur ? &cur : nullptr, tn, &layout)) {
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
                if (p.submit) { o.submit = true; o.req = p.req; }
                if (p.feedback != bowquiver::Feedback::None) { o.feedback = p.feedback; o.loaded = p.loaded; o.count = p.count; o.index = p.index; }
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
    if (o.submit) wheelgame::Submit(o.req);

    if (o.feedback != bowquiver::Feedback::None) {
        const int hand = srcHand[o.which] >= 0 ? srcHand[o.which] : 1 - holdX;
        const char* slotName = slot >= 0 && slot < wheel::kSlotCount ? wheel::kSlotDefs[slot].label : "?";
        const wheel::Snapshot* sp = have ? &snap : nullptr;
        char b[300];
        switch (o.feedback) {
            case bowquiver::Feedback::Loaded:
                changeAt = f.time;
                directinput::Haptic(hand, 0.55f, 40.0f);
                snprintf(b, sizeof b, "vrcam: bow quiver - %s loaded on the %s slot (%d of %d) with the bow in hand", NameOf(sp, o.loaded), slotName, o.index + 1, o.count);
                Log(b);
                break;
            case bowquiver::Feedback::Refused:
                directinput::Haptic(hand, 0.20f, 90.0f);
                snprintf(b, sizeof b, "vrcam: bow quiver - the game did not take %s on the %s slot (the slot or the weapon set changed meanwhile)", NameOf(sp, o.loaded), slotName);
                Log(b);
                break;
            case bowquiver::Feedback::NoArrows:
                directinput::Haptic(hand, 0.20f, 90.0f);
                if (told < 6) { ++told; Log("vrcam: bow quiver - no bow skills learned (points or items): nothing to take out of the quiver"); }
                break;
            case bowquiver::Feedback::OnlyOne:
                directinput::Haptic(hand, 0.20f, 25.0f);
                break;
            case bowquiver::Feedback::Unreadable:
                directinput::Haptic(hand, 0.20f, 90.0f);
                if (told < 6) {
                    ++told;
                    snprintf(b, sizeof b, "vrcam: bow quiver - the game's skill slots cannot be read now (it is not in its controller layout, or the %s slot is not one the plugin can tell)", slotName);
                    Log(b);
                }
                break;
            default: break;
        }
    }

    // the look: the loaded skill's element (the effect's fire, frost or arcane) on the bow's own arrow as it is drawn - from the moment the skill is loaded, the arrow
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
                if (toldId != shown.id) {   // said when the loaded skill changes
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
        Log("vrcam: bow quiver on - a button of the string hand while the string is drawn loads the next bow skill ([bow] quiver)");
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
        snprintf(b, sizeof b, "vrcam: bow quiver %s%s", on ? "on - a button of the string hand while the string is drawn loads the next bow skill into the bow ([bow] quiver)" : "off",
                 on && orbs ? (orbTrigger ? "; and the orbs along the bow's stave, the free hand's trigger picks one ([bow] quiver_orbs)" : "; and the orbs along the bow's stave, the free hand's grip picks one ([bow] quiver_orbs, quiver_orb_trigger off)") : "");
        Log(b);
    }
}

bool WantsSlots() {
    if (!g_on.load()) return false;
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
    if (was != on && g_ctx) Log(on ? "vrcam: bow quiver - with an arrow skill chosen and too little mana (by the game's own numbers) the release is the plain attack ([bow] quiver_plain_no_mana)"
                                   : "vrcam: bow quiver - [bow] quiver_plain_no_mana off: the game refuses a release the hero cannot pay, as it does");
    if (!on) g_armedSkill.store(-1);
}

bool PlainWanted() { return g_on.load() && g_noMana.load(); }

void SetArrowFxNeedsMana(bool on) { g_fxMana.store(on); }

void SetIcon(bool on) {
    const bool was = g_icon.exchange(on);
    static bool told = false;
    if (told && was == on) return;
    told = true;
    if (on) Log("vrcam: bow quiver - the chosen arrow skill's icon lies in the mana medallion on the wrist, full while the hero can pay the shot, faint when he cannot ([bow] quiver_icon; the vitals' medallions, F4)");
    else Log("vrcam: bow quiver - no icon in the mana medallion ([bow] quiver_icon=0)");
}

bool ChosenIcon(int* skill, bool* ready) {
    *skill = -1;
    *ready = false;
    if (!g_on.load() || !g_icon.load()) return false;
    const ULONGLONG at = g_iconAt.load();
    if (!at || GetTickCount64() - at >= kFreshMs) return false;
    const int sk = g_iconSkill.load();
    if (sk < 0) return false;
    *skill = sk;
    *ready = g_iconReady.load();
    return true;
}

void SetPlainHookReady(bool ready) {
    if (g_plainHook.exchange(ready) != ready && g_ctx && g_noMana.load())
        Log(ready ? "vrcam: bow quiver - the slot-skill hook is in: a release the hero cannot pay can be the plain attack ([bow] quiver_plain_no_mana)"
                  : "vrcam: bow quiver - [bow] quiver_plain_no_mana cannot work: the game's slot-skill reads were not found (another build?); the game's refusal stays");
}

bool PlainArmed(int skill) {
    if (skill < 0 || g_armedSkill.load() != skill) return false;
    const ULONGLONG at = g_armedAt.load();
    return at && GetTickCount64() - at < 300;
}

void OnRelease() {
    g_shots.fetch_add(1);   // [bow] arrow_fire_cut: the arrow's effect goes out (the quiver's next frame)
    if (!g_noMana.load() || !g_on.load()) return;
    const ULONGLONG now = GetTickCount64();
    const int skill = g_shortSkill.load();
    if (skill >= 0 && now - g_shortAt.load() < 400) {   // the hero cannot pay it: this release is the plain attack (vrcam's slot-skill hook answers it)
        if (!g_plainHook.load()) return;                  // (no hook: the game refuses, and nothing is spent)
        g_armedSkill.store(skill);
        g_armedAt.store(now);
        return;
    }
    const float cost = g_costPts.load();   // a cast that takes mana: what it took, until the next read has it
    if (cost > 0.0f && now - g_shortAt.load() < 400) {
        g_lastCostPts.store(cost);
        g_lastPressAt.store(now);
    }
}

void SetArrowFx(bool on, float sizePct, float brightPct, float alongCm) {
    const bool was = g_arrowFx.exchange(on);
    g_fxSize.store(std::isfinite(sizePct) ? std::clamp(sizePct, 30.0f, 300.0f) * 0.01f : 1.0f);
    g_fxBright.store(std::isfinite(brightPct) ? std::clamp(brightPct, 20.0f, 300.0f) * 0.01f : 1.0f);
    g_fxAlong.store(std::isfinite(alongCm) ? std::clamp(alongCm, -20.0f, 60.0f) * 0.01f : 0.0f);
    if (was != on && g_ctx) Log(on ? "vrcam: bow arrow fx on - the nocked arrow burns, freezes or crackles by the loaded skill ([bow] arrow_fx)" : "vrcam: bow arrow fx off - the coloured glow ([bow] arrow_fx)");
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
