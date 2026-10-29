// strike_force.h - [weight] strike_by_force: hit by touch judged by what a strike delivers into the body, not by the blade's speed (docs/notes/strike_force.md
// §3, §5 steps 3 and 4). Pure: the weight model's strikes (vr/strike_measure.h, measured on its contact) come in, a verdict goes out; hit by touch presses the
// game's own attack on it (vr/melee_touch.cpp), once per strike per monster. The author, 2026-10-24: «начинай делать логику по силе ударов».
//
//   score      the impulse into the body in the strike's first kWindowS (strikemeasure::Strike::window: the arrival + the hands' drive + a stab's force), and for an
//              EDGED weapon its cut too (J, the normal force times the slide) times cutWeight - a cut drawn along the body counts, not only the blow into it.
//   threshold  per weapon from its own steel (the weight model's mass of the base item): general x the family's factor x steel kg ^ massExp, never under floorNs.
//              The recorded blows (2026-10-24, 225 strikes, a Barbarian) do not grow with the steel in proportion - the hands set most of it: per kg the p25 of
//              real blows runs from 1.9 (a Large Axe, 3 kg) to 10 (a Hand Axe, 1 kg); over the square root it is 3.9 .. 10 - so massExp 0.5.
//   no hand    a strike with no hand on the weapon is never judged (a weapon falling or resting on a monster: the contact meets only a weapon in a hand anyway).
//   shared     a blade caught between monsters gives each less (§4a.1): a strike that does not count alone counts when the strikes of the same weapon begun
//              within kShareS of it sum to the threshold and its own share is at least kShareMin of that sum.
//   stuck      mode 0: one hit per strike however long the weapon stays in; mode 1: while it stays in and the player keeps pushing (the drive of the last
//              kWindowS at least the threshold), the hit repeats - each when the game's own attack is over (the hero out of his attack mode), not sooner than
//              kRepeatMinS after the last.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace strikeforce {

// heft::hold::Kind's order (cleanroom/heft/hold.h): Dagger, Sword, Axe, Mace, Scepter, Wand, Staff, Polearm, Spear, Javelin, Throwing, Other.
enum class Family { Blunt, Edged, Piercing };
inline Family FamilyOf(int holdKind) {
    switch (holdKind) {
        case 0: case 1: case 2: case 7: return Family::Edged;      // a dagger, a sword, an axe, a polearm
        case 8: case 9: case 10: return Family::Piercing;          // a spear, a javelin, a throwing weapon
        default: return Family::Blunt;                             // a mace, a scepter, a wand, a staff, the rest
    }
}
inline const char* FamilyName(Family f) { return f == Family::Edged ? "edged" : f == Family::Piercing ? "piercing" : "blunt"; }

constexpr double kShareS = 0.15, kShareMin = 0.25, kRepeatMinS = 0.15;

struct Settings {
    bool on = false;              // [weight] strike_by_force
    float general = 1.0f;         // strike_force_general_pct / 100
    float blunt = 1.5f, edged = 1.1f, piercing = 1.5f;   // strike_force_blunt / _edged / _piercing: N s per kg ^ massExp
    float massExp = 0.5f;         // strike_force_mass_exp: 0 = one threshold for every weight, 1 = in proportion to the steel
    float cutWeight = 0.1f;       // strike_force_cut_weight: N s a J of cut counts for (edged weapons)
    float floorNs = 1.0f;         // strike_force_floor_ns: never under this (a touch is 0 .. 1 N s)
    bool shared = true;           // strike_force_shared: a strike shared between monsters (§4a.1)
    int stuck = 0;                // strike_force_stuck: 0 one hit per strike, 1 hits repeat while it stays in and is pushed
};
inline Settings Clamp(Settings s) {
    const auto fin = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    s.general = fin(s.general, 0.1f, 5.0f, 1.0f);
    s.blunt = fin(s.blunt, 0.1f, 20.0f, 1.5f);
    s.edged = fin(s.edged, 0.1f, 20.0f, 1.1f);
    s.piercing = fin(s.piercing, 0.1f, 20.0f, 1.5f);
    s.massExp = fin(s.massExp, 0.0f, 1.5f, 0.5f);
    s.cutWeight = fin(s.cutWeight, 0.0f, 2.0f, 0.1f);
    s.floorNs = fin(s.floorNs, 0.0f, 20.0f, 1.0f);
    s.stuck = s.stuck == 1 ? 1 : 0;
    return s;
}

// What a strike delivered so far (strikemeasure::Strike, the weight model's live one).
struct Strike {
    uint32_t id = 0;              // the monster
    uint32_t serial = 0;          // the strike (a new one each time a contact begins)
    int hands = 0;                // the most hands on the weapon during it
    double window = 0;            // N s: the arrival + the drive + the stab in the first kWindowS
    double cut = 0;               // J
    double recent = 0;            // N s: the drive of about the last kWindowS (the hands pushing it on now)
    double startS = 0;            // s: when it began (the weight model's clock)
    float along = -1.0f;          // the furthest toward the tip it touched along the weapon (0 the butt end, 1 the tip); < 0 not known
};

inline float Threshold(const Settings& s, Family f, float steelKg) {
    const float k = f == Family::Edged ? s.edged : f == Family::Piercing ? s.piercing : s.blunt;
    const float m = std::isfinite(steelKg) && steelKg > 0.05f ? steelKg : 0.05f;
    return std::max(s.floorNs, s.general * k * std::pow(m, s.massExp));
}
inline float Score(const Settings& s, Family f, const Strike& k) {
    return static_cast<float>(k.window + (f == Family::Edged ? s.cutWeight * k.cut : 0.0));
}

enum class Why { Counts, NoHand, TooWeak, Shared };
inline const char* WhyText(Why w) {
    return w == Why::NoHand ? "no hand on the weapon" : w == Why::TooWeak ? "too weak" : w == Why::Shared ? "a blow shared between monsters" : "a strike";
}
struct Verdict {
    Why why = Why::TooWeak;
    float score = 0.0f, need = 0.0f;
    bool Counts() const { return why == Why::Counts || why == Why::Shared; }
};

// One strike on its own.
inline Verdict Judge(const Settings& s, Family f, float steelKg, const Strike& k) {
    Verdict v;
    v.score = Score(s, f, k);
    v.need = Threshold(s, f, steelKg);
    if (k.hands <= 0) { v.why = Why::NoHand; return v; }
    v.why = v.score >= v.need ? Why::Counts : Why::TooWeak;
    return v;
}

// The strikes of one weapon at once (each monster its own, up to n): each judged alone, and with `shared` a weak one among others begun within kShareS of it
// counts when their sum reaches the threshold and its share is kShareMin of it. out[i] for in[i].
inline void JudgeAll(const Settings& s, Family f, float steelKg, const Strike* in, int n, Verdict* out) {
    for (int i = 0; i < n; ++i) out[i] = Judge(s, f, steelKg, in[i]);
    if (!s.shared) return;
    for (int i = 0; i < n; ++i) {
        if (out[i].why != Why::TooWeak) continue;
        double sum = 0.0;
        int with = 0;
        for (int j = 0; j < n; ++j)
            if (in[j].hands > 0 && std::fabs(in[j].startS - in[i].startS) <= kShareS) { sum += out[j].score; ++with; }
        if (with >= 2 && sum >= out[i].need && out[i].score >= kShareMin * sum) out[i].why = Why::Shared;
    }
}

// The hero's unit in an attack, a kick, a skill or a sequence (PlrMode 7 A1, 8 A2, 10 .. 16: cast, throw, kick, S1 .. S4): the game's own attack is not over yet.
inline bool HeroAttacking(int mode) { return mode == 7 || mode == 8 || (mode >= 10 && mode <= 16) || mode == 18; }

// [weight] strike_force_stuck 1: a strike already pressed, still on - is the next hit due? The weapon still pushed in (its recent drive at least the threshold),
// a hand on it, the game's own attack over (heroAttacking false) and kRepeatMinS since the last press.
inline bool RepeatDue(const Settings& s, Family f, float steelKg, const Strike& k, bool heroAttacking, double sinceLastPressS) {
    if (s.stuck != 1 || k.hands <= 0 || heroAttacking || sinceLastPressS < kRepeatMinS) return false;
    return k.recent >= Threshold(s, f, steelKg);
}

}  // namespace strikeforce
