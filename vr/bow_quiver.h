// bow_quiver.h - [bow] quiver: while the string is drawn, a button of the string hand picks the bow skill the shot fires - Magic / Fire / Cold / Exploding / Ice / Guided /
// Freezing / Immolation Arrow, Multiple Shot, Strafe - without the skill panel. The idea is bearstonem/d2r-pcvr's (MIT, v0.3.17, from aSuperGod): a quiver button "loads" a skill
// arrow into the nocked arrow while the bow is drawn. Ours is only the choosing and the look:
//   - the choice is the game's current skill on the slot the draw's release presses (bow_draw_button: the A attack slot, or B / X / Y / RB, with LT) - set through the game's OWN
//     setter, the one the skill wheel and the tiles use (vr/wheel_bank.h Apply: read back, refused when the slot or the weapon set is not what the player chose from). No server call;
//   - the shot is what it already was: the draw's release presses that slot's button, and the game's own attack path does the rest - the skill's mana, arrows, attack rate, line of sight;
//   - the look is a small glow at the arrow's head and a thin line along the arrow in the loaded skill's colour: a cue, not a model.
// This header is the pure part: the table of bow skills, the list and its cycle, the controls' names, the machine that turns buttons and the game's answers into requests and
// feedback, and the glow. cleanroom/tests/bow_quiver_test.cpp drives it. The wire (vr/bow_quiver_wire.cpp) feeds it direct_input's frames and the wheel's snapshot.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

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

struct Entry {
    int32_t id = -1, owner = -1;
    bool operator==(const Entry& o) const { return id == o.id && owner == o.owner; }
};
inline constexpr size_t kMaxList = 24;

// The cycle: the plain attack (skill 0) FIRST, whenever the game offers it to bind - it is the white orb, and it must not depend on what the slot happened to hold when the quiver first
// looked (the game keeps a chosen arrow on the slot between runs, so the plain attack may never have been seen there: the white orb was gone then); then what the slot held before the
// quiver if that was neither the plain attack nor an arrow (the "home" skill, so that the cycle comes back to it) when the game offers it, then the bow
// skills the hero has now (learned or from an item), in the table's order, each skill and owner once. An item may grant a skill the hero also has: both are offered, each as an entry.
inline bool Offers(const wheel::Skill* catalog, uint32_t count, Entry e) {
    for (uint32_t i = 0; i < count && i < (uint32_t)wheel::kCatalogMax; ++i)
        if (catalog[i].id == e.id && catalog[i].owner == e.owner && catalog[i].available) return true;
    return false;
}
inline size_t BuildList(const wheel::Skill* catalog, uint32_t count, Entry home, Entry* out, size_t max) {
    size_t n = 0;
    auto add = [&](Entry e) {
        for (size_t k = 0; k < n; ++k) if (out[k] == e) return;
        if (n < max) out[n++] = e;
    };
    const Entry plain{0, -1};
    if (Offers(catalog, count, plain)) add(plain);
    if (home.id >= 0 && !FindArrow(home.id) && Offers(catalog, count, home)) add(home);
    for (const Arrow& a : kArrows)
        for (uint32_t i = 0; i < count && i < (uint32_t)wheel::kCatalogMax; ++i)
            if (catalog[i].id == a.id && catalog[i].available) add({a.id, catalog[i].owner});
    return n;
}

// The next entry after `cur` (dir +1) or the one before it (dir -1), wrapping. A current skill that is not in the list (a Teleport on the slot that is not the home, an empty slot)
// is stepped onto the first (dir +1) or the last (dir -1). False for an empty list. `same`: the list holds only the current one - nothing to change.
inline bool Next(const Entry* list, size_t n, Entry cur, int dir, Entry* out, bool* same = nullptr) {
    if (same) *same = false;
    if (!n || !out) return false;
    int at = -1;
    for (size_t i = 0; i < n; ++i) if (list[i] == cur) { at = (int)i; break; }
    if (at < 0) { *out = dir >= 0 ? list[0] : list[n - 1]; return true; }
    if (n == 1) { *out = list[0]; if (same) *same = true; return true; }
    *out = list[(size_t)(((at + (dir >= 0 ? 1 : -1)) % (int)n + (int)n) % (int)n)];
    return true;
}

// ---- the controls ---------------------------------------------------------------------------------------------------------------------------------------------------
// The names of [direct] quiver_next / quiver_prev are quiver_input.h's (weapon-neutral).
using quiverin::Control;
using quiverin::ParseControl;

// The wheel slot the draw's release presses: the draw's button (0 the attack, 1..4 B X Y RB, 5..8 the same with LT) -> the slot's number in wheel_types.h (A is 8).
inline int SlotOfButton(int button) {
    if (button == 0) return 8;
    if (button >= 1 && button <= 8) return button - 1;
    return -1;
}

// ---- the machine ----------------------------------------------------------------------------------------------------------------------------------------------------
// Loaded: the game took it. Refused: the game's setter said no (the slot or the weapon set is not what it was). NoArrows: the hero has no bow skill to load. OnlyOne: one only, nothing
// to cycle. Unreadable: the game's skill slots could not be read now (no fresh snapshot, not in the controller layout, the slot is not one this can tell).
enum class Feedback : uint8_t { None, Loaded, Refused, NoArrows, OnlyOne, Unreadable };

struct In {
    double t = 0.0;                          // seconds, monotonic
    bool allowed = false;                    // the feature is on, F4, a bow in hand, no menu or panel
    bool drawing = false;                    // the string is drawn (a bow): the two controls are live
    const wheel::Snapshot* snap = nullptr;   // the wheel's newest snapshot of the game's skill slots (null: none)
    uint64_t now = 0;                        // GetTickCount64, for Fresh()
    uint64_t session = 0;                    // directinput::Session(), for Fresh()
    int slot = -1;                           // the slot the release presses (SlotOfButton)
    wheel::Reply reply;                      // the game's answer to the request in flight
};

struct Out {
    bool take = false;                       // the string is drawn: the two controls are the quiver's now, keep them from the layout
    bool submit = false;
    wheel::Request req;
    Feedback feedback = Feedback::None;
    int which = 0;                           // the control that was pressed: 0 next, 1 prev
    Entry loaded;                            // the skill that was loaded / refused (for the log)
    int index = 0, count = 0;                // its place in the list
};

class Machine {
public:
    static constexpr double kMinGapS = 0.12;      // between two steps
    static constexpr double kAnswerS = 1.5;       // a request not answered in this long is given up
    static constexpr double kRememberS = 1.2;     // the loaded skill is believed over a snapshot this long (the snapshot lags the game by up to 0.4 s)

    // The skill whose arrow is in the bow: the one just loaded, else what the slot holds in the snapshot, if it is a bow skill. False for no glow (a plain shot, another skill).
    bool Shown(const In& in, Entry* out, double* since) const {
        Entry e;
        double ago = 1e9;
        if (haveLoaded_ && in.t - loadedAt_ < kRememberS) { e = loaded_; ago = in.t - loadedAt_; }
        else if (in.snap && in.snap->ready && in.slot >= 0 && in.slot < wheel::kSlotCount && in.snap->slot[in.slot].known && in.snap->slot[in.slot].available) e = {in.snap->slot[in.slot].id, in.snap->slot[in.slot].owner};
        else return false;
        if (!FindArrow(e.id)) return false;
        if (out) *out = e;
        if (since) *since = ago;
        return true;
    }
    // The skill the slot holds now, an arrow or not (the plain shot): the one just loaded, else the snapshot's. For the orb that stands out.
    bool Current(const In& in, Entry* out) const {
        if (haveLoaded_ && in.t - loadedAt_ < kRememberS) { if (out) *out = loaded_; return true; }
        if (in.snap && in.snap->ready && in.slot >= 0 && in.slot < wheel::kSlotCount && in.snap->slot[in.slot].known && in.snap->slot[in.slot].id >= 0) {
            if (out) *out = {in.snap->slot[in.slot].id, in.snap->slot[in.slot].owner};
            return true;
        }
        return false;
    }
    // The list the cycle goes through now (the plain shot first), for the orbs; what the slot held before the first arrow is the plain shot's place.
    size_t List(const In& in, Entry* out, size_t max) {
        const wheel::Snapshot* s = in.snap;
        if (!s || !s->ready || in.slot < 0 || in.slot >= wheel::kSlotCount || !s->slot[in.slot].known) return 0;
        const wheel::Skill& on = s->slot[in.slot];
        Entry cur{on.id, on.owner};
        if (haveLoaded_ && in.t - loadedAt_ < kRememberS) cur = loaded_;
        if (cur.id >= 0 && !FindArrow(cur.id)) home_ = cur;
        return BuildList(s->catalog, s->count, home_, out, max);
    }
    // The quiver has a skill it believes is on the slot (just loaded, the snapshot may not show it yet): a snapshot that differs from it is not news.
    bool Believed(double t) const { return haveLoaded_ && t - loadedAt_ < kRememberS; }
    void Forget() { pending_ = false; haveLoaded_ = false; wasDown_[0] = wasDown_[1] = false; }

    // `ctlSet`: which of the two controls exist; `down`: the raw state of each.
    Out Step(const In& in, const bool ctlSet[2], const bool down[2]) {
        Out o;
        // the answer to the request in flight
        if (pending_) {
            if (in.reply.sequence == seq_ && in.reply.result == wheel::Result::Applied) {
                pending_ = false;
                loaded_ = target_;
                haveLoaded_ = true;
                loadedAt_ = in.t;
                o.feedback = Feedback::Loaded;
                o.which = which_;
                o.loaded = target_;
                o.index = targetIndex_;
                o.count = targetCount_;
            } else if (in.reply.sequence == seq_ && in.reply.result == wheel::Result::Refused) {
                pending_ = false;
                haveLoaded_ = false;   // (what was believed about the slot was wrong: the snapshot decides again)
                o.feedback = Feedback::Refused;
                o.which = which_;
                o.loaded = target_;
            } else if (in.t - sentAt_ > kAnswerS) {
                pending_ = false;
            }
        }
        const bool live = in.allowed && in.drawing;
        o.take = in.allowed && in.drawing && (ctlSet[0] || ctlSet[1]);
        // the edges, from the raw controls (kept every step, drawn or not: a button held into the draw is no press)
        const bool edge[2] = {ctlSet[0] && down[0] && !wasDown_[0], ctlSet[1] && down[1] && !wasDown_[1]};
        wasDown_[0] = down[0];
        wasDown_[1] = down[1];
        if (!live || (!edge[0] && !edge[1])) return o;
        if (pending_ || in.t - stepAt_ < kMinGapS) return o;   // one at a time; the press is not queued
        const int which = edge[0] ? 0 : 1, dir = edge[0] ? +1 : -1;
        Out r = Go(in, which, dir, nullptr);
        if (r.submit) { o.submit = true; o.req = r.req; }          // (an answer to the last request, if it came this very step, keeps its feedback)
        if (r.feedback != Feedback::None) { o.feedback = r.feedback; o.loaded = r.loaded; o.count = r.count; o.index = r.index; }
        o.which = which;
        return o;
    }

    // The player picked THIS skill (an orb on the bow, taken by the hand): the same request as a step, to `target`. Called after Step in the same frame; nothing when a request
    // is in flight or the last step was just now. A target that the hero does not have (not in the list) is refused, and one that is already on the slot changes nothing.
    Out Pick(const In& in, Entry target) {
        Out o;
        if (!in.allowed || pending_ || in.t - stepAt_ < kMinGapS) return o;
        return Go(in, 0, +1, &target);
    }

private:
    // One request to the game's setter: the next skill in the direction `dir` of the list, or `want`. Sets the feedback when there is nothing to ask.
    Out Go(const In& in, int which, int dir, const Entry* want) {
        Out o;
        const wheel::Snapshot* s = in.snap;
        stepAt_ = in.t;
        o.which = which;
        if (!s || !wheel::Fresh(*s, in.now, in.session) || !s->editable || in.slot < 0 || in.slot >= wheel::kSlotCount || !s->slot[in.slot].known) {
            o.feedback = Feedback::Unreadable;
            return o;
        }
        const wheel::Skill& on = s->slot[in.slot];
        Entry cur{on.id, on.owner};
        if (haveLoaded_ && in.t - loadedAt_ < kRememberS) cur = loaded_;   // (the game holds what was last applied; the snapshot may not show it yet)
        if (cur.id >= 0 && !FindArrow(cur.id)) home_ = cur;                 // what the slot held that is not a bow skill: the cycle comes back to it
        Entry list[kMaxList];
        const size_t n = BuildList(s->catalog, s->count, home_, list, kMaxList);
        size_t arrows = 0;   // (the home skill alone is no arrow: a hero with no bow skill learned gets the hint and nothing else)
        for (size_t i = 0; i < n; ++i) arrows += FindArrow(list[i].id) != nullptr;
        if (!arrows) { o.feedback = Feedback::NoArrows; return o; }
        Entry next;
        if (want) {
            bool offered = false;
            for (size_t i = 0; i < n; ++i) offered = offered || list[i] == *want;
            if (!offered) { o.feedback = Feedback::Refused; o.loaded = *want; return o; }
            if (*want == cur) { o.feedback = Feedback::OnlyOne; o.loaded = cur; o.count = 1; return o; }   // already the one
            next = *want;
        } else {
            bool same = false;
            Next(list, n, cur, dir, &next, &same);
            if (same) { o.feedback = Feedback::OnlyOne; o.loaded = next; o.count = 1; return o; }   // one arrow only: nothing to cycle
        }
        int at = 0;
        for (size_t i = 0; i < n; ++i) if (list[i] == next) at = (int)i;
        wheel::Request r;
        r.op = wheel::Op::Assign;
        r.sequence = kSeqBase + (++count_);   // (the reply carries the sequence in 56 bits; the wheel's own count from 1)
        r.session = s->session;
        r.slot = in.slot;
        r.skill = next.id;
        r.owner = next.owner;
        r.bank = s->bank;
        r.prevSkill = cur.id;                // what the game's table holds now (the loaded one while the snapshot lags): the request is refused if it is not so
        r.prevOwner = cur.owner;
        seq_ = r.sequence;
        pending_ = true;
        sentAt_ = in.t;
        target_ = next;
        targetIndex_ = at;
        targetCount_ = (int)n;
        which_ = which;
        o.submit = true;
        o.req = r;
        return o;
    }

    static constexpr uint64_t kSeqBase = 1ull << 48;
    bool wasDown_[2] = {false, false};
    bool pending_ = false, haveLoaded_ = false;
    uint64_t seq_ = 0, count_ = 0;
    double sentAt_ = -1e9, stepAt_ = -1e9, loadedAt_ = -1e9;
    Entry target_, loaded_, home_;
    int targetIndex_ = 0, targetCount_ = 0, which_ = 0;
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
