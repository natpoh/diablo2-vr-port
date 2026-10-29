// bow_quiver.h - [bow] quiver: the bow skill the next shots fire - Magic / Fire / Cold / Exploding / Ice / Guided / Freezing / Immolation Arrow, Multiple Shot, Strafe - is ARMED
// from the quiver, without the skill panel: an orb on the bow's stave taken with the free hand ([bow] quiver_orbs), or a button of the string hand while the string is drawn
// ([direct] quiver_next / quiver_prev). The idea is bearstonem/d2r-pcvr's (MIT, v0.3.17, from aSuperGod): a quiver button "loads" a skill arrow into the nocked arrow.
//
// The model is [gameplay] melee_skill_armed's (vr/melee_arm.h; the author, 2026-10-20: "it must work the same way as our logic for the armed (chosen) skills"):
//   - choosing only ARMS the skill: our own state (Machine::Armed), shown in the mana medallion ([bow] quiver_icon). Nothing is written into the game's skill slots, the pad's slots
//     or the attack button A - ever. (Until 0.174 the choice was the game's skill on the A slot, set through the wheel's setter; it stayed there after the bow left the hand, and every
//     attack with a sword went to the game as Multiple Shot or the like - refused, silently: "presses=9 hits=0". Cleanup puts the plain attack back where that old quiver left one.)
//   - each release of the string (the shot), with a bow or a crossbow in hand, the game's own shot is read as the armed skill through vrcam's slot-skill hook - the same hook the armed
//     melee skill uses (DecideShot, then meleearm::InFlight): one release, one cast, the game's own path (mana, cooldown, arrows, the missile). Never a server call.
//     A shot the hero cannot pay (the skill's cost at his level against his mana, bow_mana.h) is the plain arrow ([bow] quiver_plain_no_mana); the icon is faint then.
//   - no bow or crossbow in hand: the armed arrow skill does nothing to any attack - the touches, the trigger, A are the plain attack or the armed melee skill as ever.
//   - the same choice again (its orb, or the plain shot's orb, or a button cycling onto the plain shot) disarms.
//   - the look is a small glow at the arrow's head and a thin line along the arrow in the armed skill's colour (or its element, [bow] arrow_fx): a cue, not a model.
// This header is the pure part: the table of bow skills, the list and its cycle, the controls' names, the arming machine, the shot's decision, the cleanup of the old quiver's slot and
// the glow. cleanroom/tests/bow_quiver_armed_test.cpp drives the arming, the shot and the cleanup; bow_quiver_test.cpp the table and the glow. The wire (vr/bow_quiver_wire.cpp) feeds it direct_input's frames and the wheel's snapshot (read only, but for the cleanup).
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "bow_mana.h"
#include "bow_touch.h"
#include "quiver_input.h"
#include "wheel_types.h"

namespace bowquiver {

// ---- the bow's skills ------------------------------------------------------------------------------------------------------------------------------------------------
// The skills a bow fires, as the game's skills table has them (skills.txt: the rows whose weapon type is a missile weapon and whose animation is the attack's - the ids, not the data).
// The colour is the element's: magic blue, fire orange, cold blue-white, plain physical pale.
struct Arrow {
    int32_t id;
    const char* name;      // for the log when the game gave no name
    float rgb[3];
};
inline constexpr Arrow kArrows[] = {
    {6, "Magic Arrow", {0.45f, 0.60f, 1.00f}},
    {7, "Fire Arrow", {1.00f, 0.45f, 0.10f}},
    {11, "Cold Arrow", {0.40f, 0.85f, 1.00f}},
    {12, "Multiple Shot", {0.95f, 0.95f, 0.80f}},
    {16, "Exploding Arrow", {1.00f, 0.25f, 0.05f}},
    {21, "Ice Arrow", {0.60f, 0.95f, 1.00f}},
    {22, "Guided Arrow", {0.75f, 0.50f, 1.00f}},
    {26, "Strafe", {1.00f, 0.90f, 0.40f}},
    {27, "Immolation Arrow", {1.00f, 0.60f, 0.00f}},
    {31, "Freezing Arrow", {0.20f, 0.55f, 1.00f}},
};
inline const Arrow* FindArrow(int32_t id) {
    for (const Arrow& a : kArrows) if (a.id == id) return &a;
    return nullptr;
}

// [bow] draw_shoots_arrow: what the game's read of the A slot gets for a bow draw's release (its press is "the game's attack - a bow's own arrow"): `bound`, the
// skill the slot holds (0 Attack); `armed`, the quiver's armed skill for this release (ShotSkill, 0 none). An armed skill first; a bow skill on the slot stays;
// anything else there (a spell, a warcry, a melee skill - which casts instead and no arrow flies) is read as the plain Attack, the bow's own arrow.
inline int32_t DrawSlotAnswer(int32_t bound, int32_t armed) {
    if (armed > 0) return armed;
    return bound > 0 && FindArrow(bound) ? bound : 0;
}

struct Entry {
    int32_t id = -1, owner = -1;
    bool operator==(const Entry& o) const { return id == o.id && owner == o.owner; }
};
inline constexpr size_t kMaxList = 24;
inline constexpr Entry kPlain{0, -1};   // the plain shot (the Skills row 0, Attack): the disarmed state, the white orb

// The list the orbs and the buttons go through: the plain shot FIRST (it is the white orb: taking it disarms), then the bow skills the hero has now (learned or from an item, the
// catalog's `available`), in the table's order, each skill and owner once. An item may grant a skill the hero also has: both are offered, each as an entry.
inline bool Offers(const wheel::Skill* catalog, uint32_t count, Entry e) {
    for (uint32_t i = 0; i < count && i < (uint32_t)wheel::kCatalogMax; ++i)
        if (catalog[i].id == e.id && catalog[i].owner == e.owner && catalog[i].available) return true;
    return false;
}
inline size_t BuildList(const wheel::Skill* catalog, uint32_t count, Entry* out, size_t max) {
    size_t n = 0;
    auto add = [&](Entry e) {
        for (size_t k = 0; k < n; ++k) if (out[k] == e) return;
        if (n < max) out[n++] = e;
    };
    add(kPlain);
    for (const Arrow& a : kArrows)
        for (uint32_t i = 0; i < count && i < (uint32_t)wheel::kCatalogMax; ++i)
            if (catalog[i].id == a.id && catalog[i].available) add({a.id, catalog[i].owner});
    return n;
}

// The orbs hang on the bow only when there is a choice: the plain shot and at least one bow skill (the author, 2026-10-23: "it is needed only when there is more than one
// choice"). A hero with no bow skill gets no lone white orb.
inline bool OrbsWanted(size_t listed) { return listed >= 2; }

// The next entry after `cur` (dir +1) or the one before it (dir -1), wrapping. A current entry that is not in the list is stepped onto the first (dir +1) or the last (dir -1). False for
// an empty list.
inline bool Next(const Entry* list, size_t n, Entry cur, int dir, Entry* out) {
    if (!n || !out) return false;
    int at = -1;
    for (size_t i = 0; i < n; ++i) if (list[i] == cur) { at = (int)i; break; }
    if (at < 0) { *out = dir >= 0 ? list[0] : list[n - 1]; return true; }
    *out = list[(size_t)(((at + (dir >= 0 ? 1 : -1)) % (int)n + (int)n) % (int)n)];
    return true;
}

// ---- the controls ---------------------------------------------------------------------------------------------------------------------------------------------------
// The names of [direct] quiver_next / quiver_prev are quiver_input.h's (weapon-neutral).
using quiverin::Control;
using quiverin::ParseControl;

// ---- the arming -----------------------------------------------------------------------------------------------------------------------------------------------------
// Armed: a skill is armed now (none was). Switched: another one instead. Disarmed: the plain shot again. Refused: the orb's skill is not the hero's now. NoArrows: the hero has no bow
// skill to arm. Unreadable: the hero's skills could not be read now (no snapshot of the game's skills).
enum class Feedback : uint8_t { None, Armed, Switched, Disarmed, Refused, NoArrows, Unreadable };

struct In {
    double t = 0.0;                          // seconds, monotonic
    bool allowed = false;                    // the feature is on, F4, a bow in hand, no menu or panel
    bool drawing = false;                    // the string is drawn (a bow): the two controls are live
    const wheel::Snapshot* snap = nullptr;   // the wheel's newest snapshot of the hero's skills (null: none) - read for the list, never written
};

struct Out {
    bool take = false;                       // the string is drawn: the two controls are the quiver's now, keep them from the layout
    Feedback feedback = Feedback::None;
    int which = 0;                           // the control that was pressed: 0 next, 1 prev
    Entry entry;                             // the skill armed / disarmed / refused (for the log)
    int index = 0, count = 0;                // its place in the list
};

class Machine {
public:
    static constexpr double kMinGapS = 0.12;      // between two choices

    // The armed bow skill (id -1: none, the plain shot).
    Entry Armed() const { return armed_; }
    // Forgets the armed skill (a new hero, the tick off). Not on a weapon swap: the armed skill waits for the bow, as the armed melee skill waits for its weapon.
    void Clear() { armed_ = Entry{}; changedAt_ = -1e9; }
    // Forgets the controls' edges (another view, another weapon): a button held into the next draw is no press.
    void Forget() { wasDown_[0] = wasDown_[1] = false; }

    // The armed skill whose arrow is in the bow, and how long it has been armed. False for none (the plain shot).
    bool Shown(const In& in, Entry* out, double* since) const {
        if (armed_.id < 0 || !FindArrow(armed_.id)) return false;
        if (out) *out = armed_;
        if (since) *since = in.t - changedAt_;
        return true;
    }
    // The orb that stands out: the armed skill, else the plain shot.
    Entry Current() const { return armed_.id >= 0 ? armed_ : kPlain; }
    // The list the orbs and the buttons go through now (the plain shot first). 0: the hero's skills are not read.
    size_t List(const In& in, Entry* out, size_t max) const {
        const wheel::Snapshot* s = in.snap;
        if (!s || !s->ready) return 0;
        return BuildList(s->catalog, s->count, out, max);
    }

    // `ctlSet`: which of the two controls exist; `down`: the raw state of each. A press while the string is drawn steps the armed skill along the list (onto the plain shot: disarmed).
    Out Step(const In& in, const bool ctlSet[2], const bool down[2]) {
        Out o;
        const bool live = in.allowed && in.drawing;
        o.take = live && (ctlSet[0] || ctlSet[1]);
        // the edges, from the raw controls (kept every step, drawn or not: a button held into the draw is no press)
        const bool edge[2] = {ctlSet[0] && down[0] && !wasDown_[0], ctlSet[1] && down[1] && !wasDown_[1]};
        wasDown_[0] = down[0];
        wasDown_[1] = down[1];
        if (!live || (!edge[0] && !edge[1])) return o;
        if (in.t - stepAt_ < kMinGapS) return o;   // the press is not queued
        const int which = edge[0] ? 0 : 1;
        Out r = Go(in, which, edge[0] ? +1 : -1, nullptr);
        r.take = o.take;
        return r;
    }

    // The player took THIS orb: arms its skill; the armed one's orb again, or the plain shot's, disarms. A skill the hero does not have (not in the list) is refused.
    Out Pick(const In& in, Entry target) {
        if (!in.allowed || in.t - stepAt_ < kMinGapS) return Out{};
        return Go(in, 0, +1, &target);
    }

private:
    Out Go(const In& in, int which, int dir, const Entry* want) {
        Out o;
        o.which = which;
        stepAt_ = in.t;
        const wheel::Snapshot* s = in.snap;
        if (!s || !s->ready) {
            o.feedback = Feedback::Unreadable;
            return o;
        }
        Entry list[kMaxList];
        const size_t n = BuildList(s->catalog, s->count, list, kMaxList);
        size_t arrows = 0;
        for (size_t i = 0; i < n; ++i) arrows += FindArrow(list[i].id) != nullptr;
        Entry next;
        if (want) {
            if (want->id == kPlain.id || (armed_.id >= 0 && want->id == armed_.id)) {   // the plain shot's orb, or the armed one's again: disarmed
                if (armed_.id < 0) return o;                                            // (nothing armed: nothing to do)
                o.entry = armed_;
                Disarm(in.t);
                o.feedback = Feedback::Disarmed;
                return o;
            }
            bool offered = false;
            for (size_t i = 0; i < n; ++i) offered = offered || list[i] == *want;
            if (!offered) { o.feedback = Feedback::Refused; o.entry = *want; return o; }
            next = *want;
        } else {
            if (!arrows) { o.feedback = Feedback::NoArrows; return o; }
            Next(list, n, Current(), dir, &next);
            if (next.id == kPlain.id) {
                o.entry = armed_;
                Disarm(in.t);
                o.feedback = Feedback::Disarmed;
                o.count = (int)n;
                return o;
            }
        }
        for (size_t i = 0; i < n; ++i) if (list[i] == next) o.index = (int)i;
        o.count = (int)n;
        o.feedback = armed_.id >= 0 ? Feedback::Switched : Feedback::Armed;
        armed_ = next;
        changedAt_ = in.t;
        o.entry = next;
        return o;
    }
    void Disarm(double t) { armed_ = Entry{}; changedAt_ = t; }

    bool wasDown_[2] = {false, false};
    double stepAt_ = -1e9, changedAt_ = -1e9;
    Entry armed_;
};

// ---- the shot -------------------------------------------------------------------------------------------------------------------------------------------------------
// What a release of the string is read as: the armed skill, or the plain shot (Attack, 0 - "as the game has it"). Decided at the release, before its button goes down, and answered to
// the game's two reads of the attack slot while the press is young (meleearm::InFlight, vrcam's slot-skill hook).
enum class ShotWhy : uint8_t { NotArmed, NoBow, Skill, ManaUnknown, NoMana, ShortGameDecides, NotLearned };
inline const char* ShotWhyName(ShotWhy w) {
    switch (w) {
        case ShotWhy::NoBow: return "the plain attack - no bow or crossbow in hand (the armed arrow skill waits for one)";
        case ShotWhy::Skill: return "the armed skill";
        case ShotWhy::ManaUnknown: return "the armed skill (the mana not read - the game decides)";
        case ShotWhy::NoMana: return "the plain arrow - too little mana for the armed skill ([bow] quiver_plain_no_mana)";
        case ShotWhy::ShortGameDecides: return "the armed skill, which the hero cannot pay - the game refuses it ([bow] quiver_plain_no_mana off)";
        case ShotWhy::NotLearned: return "the plain arrow - the hero has no level in the armed skill";
        default: return "the plain shot - nothing armed";
    }
}
struct Shot {
    int32_t skill = 0;          // the release is read as this: the armed skill or Attack (0)
    ShotWhy why = ShotWhy::NotArmed;
    float cost = -1.0f;         // the armed skill's cost in mana points at the level used (-1 not known)
    bool levelGuessed = false;  // the level was not read: the first level's cost stands in
    bool canPay = true;         // for the icon (faint when false) and [bow] arrow_fx_needs_mana
};
// `armed` (-1 none), `ranged` a bow or a crossbow in hand, the hero's total level in it (-1 not read, 0 none), the hero's mana in points (negative: not read), [bow] quiver_plain_no_mana.
inline Shot DecideShot(int32_t armed, bool ranged, int32_t level, float manaPoints, bool plainNoMana) {
    Shot h;
    const bowmana::Row* row = bowmana::Find(armed);
    if (armed < 0 || !FindArrow(armed)) return h;
    if (!ranged) { h.why = ShotWhy::NoBow; return h; }
    if (level == 0) { h.why = ShotWhy::NotLearned; h.canPay = false; return h; }
    h.levelGuessed = level < 1;
    const int64_t raw = bowmana::RowCostRaw(row, level < 1 ? 1 : level);
    h.cost = raw < 0 ? -1.0f : (float)raw / 256.0f;
    if (bowmana::ShortOf(raw, manaPoints)) {
        h.canPay = false;
        if (plainNoMana) { h.why = ShotWhy::NoMana; return h; }
        h.skill = armed;
        h.why = ShotWhy::ShortGameDecides;
        return h;
    }
    h.skill = armed;
    h.why = !std::isfinite(manaPoints) || manaPoints < 0.0f ? ShotWhy::ManaUnknown : ShotWhy::Skill;
    return h;
}
// The hero's total level in `e` from the snapshot's catalog: the entry's own, else the skill's from any owner; 0 when the catalog (read) has it nowhere available; -1 not read.
inline int32_t LevelOf(const wheel::Snapshot* s, Entry e) {
    if (!s || !s->ready || e.id < 0) return -1;
    int32_t any = 0;
    for (uint32_t i = 0; i < s->count && i < (uint32_t)wheel::kCatalogMax; ++i) {
        const wheel::Skill& k = s->catalog[i];
        if (k.id != e.id || !k.available) continue;
        if (k.owner == e.owner) return k.level > 0 ? k.level : -1;
        if (k.level > any) any = k.level;
    }
    return any;
}

// ---- the cleanup of the old quiver's slot ---------------------------------------------------------------------------------------------------------------------------
// Until 0.174 the quiver set its arrow skills on the attack's slot (the controller's A, wheel slot 8) through the game's setter, and the game keeps that slot in the save: a bow skill sits
// there with a sword in hand, and every attack - the touch's included - is a cast the game refuses. With the quiver on, a bow skill on the A slot is given back as the plain attack (what the
// old quiver kept as "what was there" was never saved; Attack is the slot's own default) through the same setter, the one write the quiver still makes:
//   - once at a hero's first fresh read (the load), whatever is in hand - the old quiver's last arrow is still there;
//   - whenever no bow or crossbow is in hand (put away, a weapon swap): a bow skill on A is no use to a melee weapon. With a bow in hand later, a skill the player himself puts on A with the
//     skill panel is left alone (and then read by the game as it is: the armed skill answers only the plain attack's reads).
// A refused or unanswered request is tried again after kRetryS, kMaxTries times for one hero; then given up (vrcam's hook still reads a bow skill on A as the plain attack with no bow).
inline constexpr int kAttackSlot = 8;   // wheel_types.h kSlotDefs: A
struct CleanIn {
    bool on = false;                         // [bow] quiver
    bool ranged = false;                     // a bow or a crossbow in hand
    uint64_t hero = 0;                       // gamestate::LocalPlayer (0: none)
    double t = 0.0;                          // seconds, monotonic
    const wheel::Snapshot* snap = nullptr;
    bool fresh = false;                      // wheel::Fresh(snap)
    wheel::Reply reply;
};
enum class CleanEvent : uint8_t { None, Asked, Done, Refused, GaveUp };
struct CleanOut {
    bool submit = false;
    wheel::Request req;
    CleanEvent event = CleanEvent::None;
    Entry was;                               // the bow skill taken off A
};
class Cleanup {
public:
    static constexpr double kAnswerS = 1.5, kRetryS = 2.0;
    static constexpr int kMaxTries = 5;

    // The wheel's reads are wanted: a hero not yet checked, or a bow skill last seen on A.
    bool Wants() const { return on_ && (loadPending_ || arrowSeen_); }

    CleanOut Step(const CleanIn& in) {
        CleanOut o;
        on_ = in.on;
        if (in.hero != hero_) { hero_ = in.hero; loadPending_ = true; arrowSeen_ = false; tries_ = 0; pending_ = false; gaveUp_ = false; askedAt_ = -1e9; }
        if (pending_) {
            if (in.reply.sequence == seq_ && in.reply.result == wheel::Result::Applied) {
                pending_ = false; arrowSeen_ = false; loadPending_ = false; tries_ = 0;
                o.event = CleanEvent::Done; o.was = target_;
                return o;
            }
            if (in.reply.sequence == seq_ && in.reply.result == wheel::Result::Refused) { pending_ = false; o.event = CleanEvent::Refused; o.was = target_; return o; }
            if (in.t - sentAt_ <= kAnswerS) return o;
            pending_ = false;   // no answer: asked again below, after kRetryS
        }
        if (!in.on || !hero_ || !in.snap || !in.fresh || !in.snap->editable) return o;
        const wheel::Skill& a = in.snap->slot[kAttackSlot];
        if (!a.known) return o;
        arrowSeen_ = FindArrow(a.id) != nullptr;
        if (!arrowSeen_) { loadPending_ = false; return o; }
        if (in.ranged && !loadPending_) return o;   // a bow in hand after the load: the player's own choice on A is left alone
        if (tries_ >= kMaxTries) {
            if (!gaveUp_) { gaveUp_ = true; o.event = CleanEvent::GaveUp; o.was = {a.id, a.owner}; }
            return o;
        }
        if (in.t - askedAt_ < kRetryS) return o;
        ++tries_;
        askedAt_ = sentAt_ = in.t;
        wheel::Request& r = o.req;
        r.op = wheel::Op::Assign;
        r.sequence = kSeqBase + (++count_);
        r.session = in.snap->session;
        r.slot = kAttackSlot;
        r.skill = kPlain.id;
        r.owner = kPlain.owner;
        r.bank = in.snap->bank;
        r.prevSkill = a.id;   // refused if the slot holds anything else by then
        r.prevOwner = a.owner;
        seq_ = r.sequence;
        pending_ = true;
        target_ = {a.id, a.owner};
        o.submit = true;
        o.event = CleanEvent::Asked;
        o.was = target_;
        return o;
    }

private:
    static constexpr uint64_t kSeqBase = 3ull << 48;   // (the wheel counts from 1, the old bow quiver's choices were 1 << 48 on)
    bool on_ = false, loadPending_ = false, arrowSeen_ = false, pending_ = false, gaveUp_ = false;
    uint64_t hero_ = 0, seq_ = 0, count_ = 0;
    double sentAt_ = -1e9, askedAt_ = -1e9;
    int tries_ = 0;
    Entry target_;
};

// ---- the glow ---------------------------------------------------------------------------------------------------------------------------------------------------------
// The arrow's cue in the headset: a ball at the arrow's head (a ring round it that swells for a moment when the skill has just been loaded) and a thin line along the arrow, in the
// skill's colour. The effect's debug overlay draws it (four segments and six balls); it gives way to bow_vectors when that has the overlay full.
inline constexpr float kPulseS = 0.45f;
inline float GlowRadius(float sinceLoadedS) {
    const float base = 0.012f;
    if (!(sinceLoadedS >= 0.0f) || sinceLoadedS >= kPulseS) return base;
    const float k = 1.0f - sinceLoadedS / kPulseS;
    return base * (1.0f + 1.4f * k * k);
}

struct Glow {
    bowtouch::Vec head, tail;   // the arrow's head and the start of its trail (the hands' frame)
    bool line = true;           // the trail is drawn; false: a ball only
    float scale = 1.0f;         // of the ball's size
    float rgb[3] = {1, 1, 1};
    float sinceS = 1e9f;        // since the skill was loaded
};

// Adds the glow to the effect's picture: one segment and two balls. False (the picture as it was) when there is no room.
inline bool AddGlow(bowtouch::VectorSet& v, const Glow& g) {
    if ((g.line && v.nseg + 1 > bowtouch::kMaxSegs) || v.nball + 2 > bowtouch::kMaxBalls) return false;
    if (g.line) {
        bowtouch::Seg& s = v.seg[v.nseg++];
        s.a = g.tail; s.b = g.head;
        for (int k = 0; k < 3; ++k) s.rgb[k] = g.rgb[k];
    }
    const float r = GlowRadius(g.sinceS) * g.scale;
    bowtouch::Ball& core = v.ball[v.nball++];
    core.c = g.head; core.r = r; core.fill = 1.0f;
    for (int k = 0; k < 3; ++k) core.rgb[k] = g.rgb[k];
    bowtouch::Ball& halo = v.ball[v.nball++];
    halo.c = g.head; halo.r = r * 1.9f; halo.fill = 0.0f;
    for (int k = 0; k < 3; ++k) halo.rgb[k] = g.rgb[k];
    return true;
}

// The arrow on the string: the head sits a little ahead of the rest point (the bow hand's own when none was read) along the shot's line, the trail runs back along it toward the
// string hand, at most `trail` metres and never past it. False for a line that is not one.
inline bool MakeGlow(bowtouch::Vec rest, bowtouch::Vec stringHand, const Arrow& a, float sinceS, Glow* out, float ahead = 0.05f, float trail = 0.30f) {
    if (!out) return false;
    const bowtouch::Vec d = rest - stringHand;
    const float len = bowdraw::Length(d);
    if (!(len > 0.05f) || !std::isfinite(len)) return false;
    const bowtouch::Vec u = d * (1.0f / len);
    out->head = rest + u * ahead;
    const float back = std::fmin(trail, len + ahead);
    out->tail = out->head - u * back;
    out->line = true;
    out->scale = 1.0f;
    for (int k = 0; k < 3; ++k) out->rgb[k] = a.rgb[k];
    out->sinceS = sinceS;
    return true;
}

// ---- the element of the arrow ---------------------------------------------------------------------------------------------------------------------------------------
// [bow] arrow_fx: the nocked arrow burns, freezes or crackles by the skill that is loaded (the effect's ARROWFX block in reshade/D2R_DepthFog.fx). Fire, Exploding and Immolation Arrow
// burn; Cold, Ice and Freezing Arrow freeze; Magic Arrow is arcane. The plain shot, Guided Arrow, Multiple Shot and Strafe have no element of their own here: they keep the coloured glow
// above. The idea is bearstonem/d2r-pcvr's quiver preview (MIT, v0.3.17: the loaded skill's own look on the nocked arrow); his is the game's missile model and particles made on the
// string, this is our own procedural light in the effect - nothing of the game's is created or touched, and the shot is the game's own as ever.
enum class Element : uint8_t { None, Fire, Frost, Arcane };
struct ElementFx {
    Element kind = Element::None;
    float heavy = 1.0f;   // the skill's weight in the brightness: the plain ones 1, the bursting and the freezing ones more
};
inline ElementFx ElementOf(int32_t skillId) {
    switch (skillId) {
        case 7:  return {Element::Fire, 1.00f};    // Fire Arrow
        case 16: return {Element::Fire, 1.30f};    // Exploding Arrow
        case 27: return {Element::Fire, 1.15f};    // Immolation Arrow
        case 11: return {Element::Frost, 1.00f};   // Cold Arrow
        case 21: return {Element::Frost, 1.10f};   // Ice Arrow
        case 31: return {Element::Frost, 1.30f};   // Freezing Arrow
        case 6:  return {Element::Arcane, 1.00f};  // Magic Arrow
        default: return {};
    }
}
// What the effect is told: AfOn, 0 for none.
inline int EffectCode(Element e) { return e == Element::Fire ? 1 : e == Element::Frost ? 2 : e == Element::Arcane ? 3 : 0; }
// A skill just loaded swells the effect for a moment (the glow's pulse, longer-lived: the flames flare), then it settles to 1.
inline float Swell(float sinceLoadedS) {
    if (!(sinceLoadedS >= 0.0f) || sinceLoadedS >= kPulseS) return 1.0f;
    const float k = 1.0f - sinceLoadedS / kPulseS;
    return 1.0f + 0.9f * k * k;
}

struct ArrowFx {
    Element kind = Element::None;
    bowtouch::Vec tail, head;   // the arrow, from its nock to its head (the hands' frame)
    float strength = 1.0f;      // the skill's weight times the swell of a skill just loaded
};
// The effect for `skillId` on the bow's own arrow as it is drawn (skel::BowArrow: its nock and head off the arrow bone - nocked at rest before the pull, on the drawn
// string during it), as it is: nothing moved, lengthened or turned here. False for a skill with no element and for an arrow that is not one (the same point, NaN).
// [bow] arrow_fx_along_cm: the effect's centre moved along the arrow's shaft by `alongM` metres - forward (toward the arrowhead) for more, back for less - the
// user's own fine setting on top of where the head is found. The nock stays; the head never comes nearer the nock than 2 cm. A NaN or no shaft: as it was.
inline void MoveHead(ArrowFx* fx, float alongM) {
    if (!fx || !std::isfinite(alongM) || alongM == 0.0f) return;
    const bowtouch::Vec d = fx->head - fx->tail;
    const float len = bowdraw::Length(d);
    if (!(len > 1e-4f) || !std::isfinite(len)) return;
    const float to = std::fmax(0.02f, len + alongM);
    fx->head = fx->tail + d * (to / len);
}

inline bool MakeArrowFx(bowtouch::Vec nock, bowtouch::Vec head, int32_t skillId, float sinceS, ArrowFx* out) {
    if (!out) return false;
    const ElementFx e = ElementOf(skillId);
    if (e.kind == Element::None) return false;
    const float len = bowdraw::Length(head - nock);
    if (!(len > 0.02f) || !std::isfinite(len) || !bowdraw::Finite(nock) || !bowdraw::Finite(head)) return false;
    out->kind = e.kind;
    out->tail = nock;
    out->head = head;
    out->strength = e.heavy * Swell(sinceS);
    return true;
}


}  // namespace bowquiver
