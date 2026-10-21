// press_trace.h - [debug] melee_press_trace: one log line for every attack press hit by touch makes, from the press to what the game did with it. Pure: no game calls, no
// globals, no windows.h (cleanroom/tests/press_trace_test.cpp). vrcam.cpp feeds it from the places the press goes through and says the line.
//
// Why (the author, 2026-10-20, test build 0.175): the Amazon with a short sword - every touch is detected and the game's attack is pressed, Stand Still held, but the game
// never strikes ("presses=15 hits=0 lost=13"); "Direct hit" (the server's attack called by us) lands, and a Barbarian or a Paladin on the same press path hits. The press
// goes through many hands on its way - the pad the game polls, the slot-skill reads that say which skill A casts, the controller's target lookups that are answered with the
// touched monster, the bow / quiver / aim code - and the log told only its start. This line tells each step of ONE press, so the step where it stops is in the log:
//   pad      the polls the game made while the press was live: how many had A, when A first went down and came up again, the OR of every button the game saw, and whether
//            the game's Stand Still (command 0x24) was down at the first A
//   slot     the game's two reads of the A slot's skill (the cast's slot selection and the unit's hand-skill setter): the slot index read, what it holds and what it was
//            answered as
//   target   the controller's target lookups - the cast flags' own look (0x1454A0: a unit found clears Stand Still), the resolver's and its hover's, the map click's: how
//            many times each asked, what it was given (the touched monster's id, another unit's, or none) and whether that was the touch's answer
//   cast     the resolver's verdict on the cast (directed along the hand = the game's target and aim assist off - a melee press must never be that)
//   hero     the hero's mode at the press and the first attack-type mode after it (none = the game never started the attack), the animation row it ran on (its action frame)
//   result   the hit the game's hit-flags reported for the press, or none in the window
// and what the plugin itself held at the press: the weapon set, the class and kind, each hand's item, the A slot and the bank the skill panel reads, the armed melee skill,
// the bow quiver's armed shot, the javelin quiver's choice, the bow draw's own state, the aim mode and the quick strike's cut.
// Read only: nothing here changes a press, a lookup or a skill.
#pragma once

#include <cstdint>
#include <cstdio>

namespace presstrace {

// The line is told this long after the press at the latest (the hit counter gives up at 1.2 s), or as soon as the hit is in and the hero's attack had time to show.
constexpr uint64_t kTellMs = 1300, kAfterHitMs = 400;

enum Lookup : int { kDispatch = 0, kResolver, kHover, kClick, kLookups };
enum SlotSite : int { kSelect = 0, kSetter, kSites };

// What the plugin held at the press.
struct Facts {
    uint32_t weaponSet = 0;      // gamestate: 1 or 2 (0 not known)
    uint32_t weaponClass = 0;    // D2RVR_WEAPON_*: 2 melee, 3 bow, 4 crossbow
    uint32_t weaponType = 0;     // D2RVR_TYPE_*
    uint32_t codeR = 0, codeL = 0;   // each hand's item code, four characters packed (gamestate::HandCode)
    int slotA = -1, bank = -1;   // the skill on the A slot as the skill panel reads it, and the weapon set's half of the controller table (0 / 16); -1 not read
    int armedMelee = -1;         // [gameplay] melee_skill_armed: what the press was decided as (meleearm::PressSkill), 0 the plain attack
    int bowShot = 0;             // [bow] quiver: the armed shot the slot reads would answer now (bowquiverwire::ShotSkill), 0 none
    int javChosen = -1;          // [hands] javelin_quiver: the chosen skill, -1 none
    bool bowInHand = false, bowOwned = false, bowAimLive = false, aimByVector = false, throwLive = false;
    int bowMode = 0;             // [bow] mode
    uint32_t cutMs = 0;          // [hands] quick_strike: how much earlier the melee rows act (0: not cut)
};

struct Ask {
    uint16_t n = 0;          // times asked in the window
    uint16_t ours = 0;       // ... answered with the touched monster
    uint32_t lastId = 0;     // the id of the unit last given (0: none)
    int firstMs = -1;        // ms after the press it first asked
};

struct Trace {
    bool active = false;
    uint32_t seq = 0, monster = 0;
    int hand = 0;
    uint64_t at = 0;
    Facts f;
    // the pad
    uint32_t polls = 0, pollsA = 0;
    int aDownMs = -1, aUpMs = -1;
    uint16_t buttonsOr = 0;
    int stillAtA = -1;       // the game's Stand Still was down at the first poll with A: 1 yes, 0 no, -1 no A
    // the slot reads
    int slot[kSites] = {-1, -1};                               // the slot index the game read (its own numbering)
    int bound[kSites] = {-2, -2}, answer[kSites] = {-2, -2};   // -2: not read in the window
    uint16_t slotReads[kSites] = {0, 0};
    // the target lookups
    Ask ask[kLookups];
    int castSkill = -2, castDirected = -1;   // the resolver's cast: the controller's skill and whether it went along the hand
    // the hero
    int modeAtPress = -1, actMode = -1, actMs = -1;
    char anim[9] = {};
    int animFrames = -1, animFlag = -1;
    // the result
    int hitMs = -1;
    bool outcome = false;    // the hit counter said hit or lost
    bool cut = false;        // told early: the next press came first
};

class Recorder {
public:
    // A press of the attack (the touch's key route). An earlier press not told yet is handed back in `prev` to be told first (cut short).
    bool Begin(uint32_t monster, int hand, uint64_t now, const Facts& f, int heroMode, Trace* prev) {
        bool handed = false;
        if (t_.active && prev) { *prev = t_; prev->cut = true; handed = true; }
        t_ = Trace{};
        t_.active = true;
        t_.seq = ++seq_;
        t_.monster = monster;
        t_.hand = hand;
        t_.at = now;
        t_.f = f;
        t_.modeAtPress = heroMode;
        return handed;
    }
    bool Active() const { return t_.active; }
    const Trace& Current() const { return t_; }

    // One poll of the pad by the game: the buttons it got, the game's Stand Still, the hero's mode now (and the row it runs on, when it is an attack's).
    void Poll(uint64_t now, uint16_t buttons, bool standStillDown, int heroMode) {
        if (!t_.active) return;
        const int ms = Ms(now);
        ++t_.polls;
        t_.buttonsOr = (uint16_t)(t_.buttonsOr | buttons);
        if (buttons & kPadA) {
            ++t_.pollsA;
            if (t_.aDownMs < 0) { t_.aDownMs = ms; t_.stillAtA = standStillDown ? 1 : 0; }
        } else if (t_.aDownMs >= 0 && t_.aUpMs < 0) {
            t_.aUpMs = ms;
        }
        if (t_.actMode < 0 && IsAttackMode(heroMode) && ms >= 0) { t_.actMode = heroMode; t_.actMs = ms; }
    }
    // The row the hero's attack runs on, once (the first one read after the attack mode was seen).
    void Anim(const char* name, int frames, int flag) {
        if (!t_.active || t_.anim[0] || !name) return;
        for (int i = 0; i < 8 && name[i]; ++i) t_.anim[i] = name[i];
        t_.animFrames = frames;
        t_.animFlag = flag;
    }
    bool WantsAnim() const { return t_.active && t_.actMode >= 0 && !t_.anim[0]; }

    void Slot(int site, int slot, int bound, int answer) {
        if (!t_.active || site < 0 || site >= kSites) return;
        ++t_.slotReads[site];
        t_.slot[site] = slot;
        t_.bound[site] = bound;
        t_.answer[site] = answer;
    }
    void Lookup(int which, uint64_t now, uint32_t givenId, bool ours) {
        if (!t_.active || which < 0 || which >= kLookups) return;
        Ask& a = t_.ask[which];
        if (a.firstMs < 0) a.firstMs = Ms(now);
        ++a.n;
        if (ours) ++a.ours;
        a.lastId = givenId;
    }
    void Cast(int skill, bool directed) {
        if (!t_.active) return;
        t_.castSkill = skill;
        t_.castDirected = directed ? 1 : 0;
    }
    // The hit counter's verdict on the press (the first after Begin is this press's).
    void Outcome(bool hit, double ms) {
        if (!t_.active || t_.outcome) return;
        t_.outcome = true;
        t_.hitMs = hit ? (int)(ms + 0.5) : -1;
    }
    // Time to tell it: hands the trace over and ends it.
    bool Due(uint64_t now, Trace* out) {
        if (!t_.active) return false;
        const uint64_t age = now >= t_.at ? now - t_.at : 0;
        const bool done = age >= kTellMs || (t_.outcome && t_.hitMs >= 0 && age >= (uint64_t)t_.hitMs + kAfterHitMs && !WantsAnim());
        if (!done) return false;
        *out = t_;
        t_.active = false;
        return true;
    }

    static constexpr uint16_t kPadA = 0x1000;
    // The hero's attack-type modes: attack 1 / 2 (7 / 8), cast (10), throw (11), kick (12 is dead - not here), the specials (13..16)... as bow_latency counts them.
    static bool IsAttackMode(int m) { return m == 7 || m == 8 || (m >= 10 && m <= 16 && m != 12); }

private:
    int Ms(uint64_t now) const { return now >= t_.at ? (int)(now - t_.at) : 0; }
    Trace t_;
    uint32_t seq_ = 0;
};

inline void Code(uint32_t c, char out[5]) {
    for (int i = 0; i < 4; ++i) {
        const char ch = (char)((c >> (8 * i)) & 0xFF);
        out[i] = c ? (ch >= 32 && ch < 127 ? ch : '?') : '-';
    }
    if (!c) { out[0] = '-'; out[1] = 0; }
    out[4] = 0;
}

// What the trace says the press came to, in a few words - the first step that did not happen.
inline const char* Verdict(const Trace& t) {
    if (t.pollsA == 0) return "A NEVER reached the game's pad poll (held back or taken off before the game polled)";
    if (t.answer[kSelect] > 0) return "A reached the game, and its slot read answered a SKILL, not the plain attack";
    if (t.slotReads[kSelect] == 0) return "A reached the game, but the game never read the A slot's skill (no cast started from the pad)";
    if (t.castDirected == 1) return "the cast went ALONG THE HAND (directed): the touched monster was not its target";
    if (t.ask[kResolver].n + t.ask[kHover].n + t.ask[kClick].n > 0 && t.ask[kResolver].ours + t.ask[kHover].ours + t.ask[kClick].ours == 0)
        return "the game asked for its target but was NOT given the touched monster";
    if (t.actMode < 0) return "the game read the slot but the hero never went into an attack mode";
    if (t.hitMs < 0) return "the hero attacked, but no hit came in the window";
    return "the hero attacked and the hit came";
}

inline int Format(const Trace& t, char* out, size_t n) {
    char r[5], l[5];
    Code(t.f.codeR, r);
    Code(t.f.codeL, l);
    char asks[200];
    static const char* const kAskName[kLookups] = {"flags", "resolver", "hover", "click"};
    int k = 0;
    for (int i = 0; i < kLookups && k >= 0 && k < (int)sizeof asks; ++i) {
        const Ask& a = t.ask[i];
        if (!a.n) k += std::snprintf(asks + k, sizeof asks - k, "%s%s -", i ? ", " : "", kAskName[i]);
        else if (a.lastId) k += std::snprintf(asks + k, sizeof asks - k, "%s%s x%u at %d ms (ours %u, last unit %u)", i ? ", " : "", kAskName[i], a.n, a.firstMs, a.ours, a.lastId);
        else k += std::snprintf(asks + k, sizeof asks - k, "%s%s x%u at %d ms (ours %u, last none)", i ? ", " : "", kAskName[i], a.n, a.firstMs, a.ours);
    }
    char anim[64] = "-";
    if (t.anim[0]) std::snprintf(anim, sizeof anim, "%s %d frames, the action frame %d", t.anim, t.animFrames, t.animFlag);
    char hit[48];
    if (t.hitMs >= 0) std::snprintf(hit, sizeof hit, "HIT %d ms", t.hitMs);
    else std::snprintf(hit, sizeof hit, "%s", t.outcome ? "LOST (no hit)" : "none yet");
    return std::snprintf(out, n,
                         "melee press %u: monster %u, %c hand - %s | pad: %u polls, A in %u (down %d ms, up %d ms), buttons seen %04X, Stand Still at A %s"
                         " | slot A: select [%d] %d->%d (x%u), setter [%d] %d->%d (x%u); panel A %d, bank %d | target: %s | cast: skill %d, %s"
                         " | hero: mode %d at the press, attack mode %d at %d ms, row %s | result: %s"
                         " | held: set %u, class %u, kind %u, R '%s' L '%s', armed melee %d, bow shot %d, javelin quiver %d, bow in hand %d, draw owns %d, draw aim %d, aim by hand %d (bow mode %d), throw aim %d, quick strike %u ms%s",
                         t.seq, t.monster, t.hand ? 'L' : 'R', Verdict(t), t.polls, t.pollsA, t.aDownMs, t.aUpMs, t.buttonsOr,
                         t.stillAtA < 0 ? "-" : t.stillAtA ? "down" : "NOT down",
                         t.slot[kSelect], t.bound[kSelect], t.answer[kSelect], t.slotReads[kSelect], t.slot[kSetter], t.bound[kSetter], t.answer[kSetter], t.slotReads[kSetter], t.f.slotA, t.f.bank, asks,
                         t.castSkill, t.castDirected < 0 ? "no resolution" : t.castDirected ? "DIRECTED along the hand" : "the game's own target",
                         t.modeAtPress, t.actMode, t.actMs, anim, hit,
                         t.f.weaponSet, t.f.weaponClass, t.f.weaponType, r, l, t.f.armedMelee, t.f.bowShot, t.f.javChosen, t.f.bowInHand, t.f.bowOwned, t.f.bowAimLive, t.f.aimByVector,
                         t.f.bowMode, t.f.throwLive, t.f.cutMs, t.cut ? " (cut short: the next press came first)" : "");
}

}  // namespace presstrace
