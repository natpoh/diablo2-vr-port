// bow_mana.h - [bow] quiver_plain_no_mana: what a bow skill costs and whether the hero can pay it now. Pure: no game calls, no globals, no windows.h (cleanroom/tests/bow_mana_test.cpp).
//
// With an arrow skill chosen (the quiver's orbs, its buttons, the skill panel) and too little mana, the game refuses the release: the hero says "not enough mana" and nothing is shot. The
// player may want the plain arrow instead (and the skill still chosen, for when the mana is back). The decision that the hero cannot pay is the game's own arithmetic on the game's own
// numbers: the skill's row in Skills.txt (mana, lvlmana, manashift, minmana) and the skill's level against the hero's mana. The game computes (the stats are fixed point, 1/256 of a point)
//
//     cost = (mana + lvlmana * (level - 1)) << manashift        (at least 0), and at least minmana points
//     the skill can be cast when the hero's mana >= cost
//
// - checked against what the game shows: Magic Arrow 1.5 at level 1, Fire Arrow 3, Cold Arrow 3.5, Exploding Arrow 5, Ice Arrow 4, Guided Arrow 8, Immolation Arrow 6, Freezing Arrow 9,
// Multiple Shot 4, Strafe 11. The ten rows below are the bow skills' lines of the installed game's Skills.txt (extracted from its own data, D2R 3.3, 2026-10-17); another build's numbers are
// the game's data again, and a skill that is not here is never judged (the game decides alone). The level is the skill's total level (points and items), the catalog's.
#pragma once

#include <cmath>
#include <cstdint>

namespace bowmana {

struct Row {
    int32_t id;
    int32_t mana, lvlMana, shift, minMana;   // Skills.txt: mana, lvlmana, manashift, minmana
};
inline constexpr Row kRows[] = {
    {6, 12, -1, 5, 0},    // Magic Arrow
    {7, 24, 0, 5, 1},     // Fire Arrow
    {11, 28, 1, 5, 1},    // Cold Arrow
    {12, 4, 1, 8, 1},     // Multiple Shot
    {16, 20, 1, 6, 1},    // Exploding Arrow
    {21, 16, 1, 6, 1},    // Ice Arrow
    {22, 32, -1, 6, 1},   // Guided Arrow
    {26, 11, 0, 8, 1},    // Strafe
    {27, 24, 1, 6, 1},    // Immolation Arrow
    {31, 36, 1, 6, 1},    // Freezing Arrow
};

inline const Row* Find(int32_t id) {
    for (const Row& r : kRows) if (r.id == id) return &r;
    return nullptr;
}

// The cost of row `r` at `level` in the game's fixed point (1/256 of a mana point); -1 for no row and for a level below 1 (not known). Any Skills.txt row: [gameplay] melee_skill_armed's
// rows (vr/melee_arm.h) are paid the same way.
inline int64_t RowCostRaw(const Row* r, int32_t level) {
    if (!r || level < 1) return -1;
    int64_t v = (int64_t)r->mana + (int64_t)r->lvlMana * (int64_t)(level - 1);
    if (v < 0) v = 0;
    int64_t cost = v << r->shift;
    const int64_t least = (int64_t)r->minMana << 8;
    return cost < least ? least : cost;
}
// Whether the hero cannot pay a cost (RowCostRaw) with `manaPoints` (the hero's mana in points; negative or not a number: not known). False for anything not known.
inline bool ShortOf(int64_t cost, float manaPoints) {
    if (cost < 0 || !std::isfinite(manaPoints) || manaPoints < 0.0f) return false;
    return (int64_t)std::floor((double)manaPoints * 256.0 + 0.5) < cost;
}

// The cost of `id` at `level`; -1 for a skill that is not one of these and for a level below 1 (not known).
inline int64_t CostRaw(int32_t id, int32_t level) { return RowCostRaw(Find(id), level); }
inline float CostPoints(int32_t id, int32_t level) {
    const int64_t c = CostRaw(id, level);
    return c < 0 ? -1.0f : (float)c / 256.0f;
}

// Whether the hero cannot pay `id` at `level` with `manaPoints` (the hero's mana in points; negative or not a number: not known). False for anything not known - then the game decides alone.
inline bool Short(int32_t id, int32_t level, float manaPoints) { return ShortOf(CostRaw(id, level), manaPoints); }

// The mana the hero has at the release, from the newest read (a read comes five times a second, on the game's UI thread, so it may be up to a quarter of a second old): when a cast of ours was
// pressed after that read was taken (or so soon before it that the game had not yet taken the mana), what it cost is not in the number yet, and is taken off. `readAgeMs` how old the read is,
// `sinceCastMs` how long ago the last cast that cost mana was pressed (negative: none), `lastCostPoints` what it cost.
inline constexpr double kSpendLagMs = 120.0;   // from the press to the game taking the mana
inline float Estimate(float readPoints, double readAgeMs, double sinceCastMs, float lastCostPoints) {
    if (!std::isfinite(readPoints) || readPoints < 0.0f) return -1.0f;
    if (sinceCastMs >= 0.0 && lastCostPoints > 0.0f && readAgeMs > sinceCastMs - kSpendLagMs) {
        const float left = readPoints - lastCostPoints;
        return left < 0.0f ? 0.0f : left;
    }
    return readPoints;
}

// ---- the arrow's effect while the hero cannot pay ------------------------------------------------------------------------------------------------------------------------
// [bow] arrow_fx_needs_mana: with an arrow skill chosen that the hero cannot pay (the release is then the plain attack, or the game's refusal) the arrow looks plain - the fire, the frost or
// the arcane light goes out - and lights again when the mana is back. It follows the same arithmetic as the release (Short / Estimate), at rest as much as while drawing; two things keep it
// from flickering at the edge: the mana read comes five times a second and our own last cast is taken off it by an estimate, so the state is believed only after it has held for `kDwellS`
// (the shortness that a read can show for a moment right after a cast is gone by then), and the light eases in and out over `kFadeS` instead of switching.
inline constexpr double kDwellS = 0.30;
inline constexpr double kFadeS = 0.20;
class Gate {
public:
    // `shortNow`: the hero cannot pay it this frame; `t` seconds, any steady clock. Returns whether the effect is OUT (the hero has not been able to pay for kDwellS, until he has been able for kDwellS).
    bool Step(bool shortNow, double t) {
        if (!std::isfinite(t)) return out_;                                  // (not a time: nothing is learned from it)
        if (!(t > last_) || t - last_ > 1.0) pending_ = false;               // (a gap, a time that went back: what was building up is forgotten)
        last_ = t;
        if (shortNow == out_) { pending_ = false; return out_; }
        if (!pending_) { pending_ = true; since_ = t; }
        if (t - since_ >= kDwellS) { out_ = shortNow; pending_ = false; }
        return out_;
    }
    bool Out() const { return out_; }
    void Reset() { *this = Gate{}; }

private:
    bool out_ = false, pending_ = false;
    double since_ = 0.0, last_ = -1e18;
};
// The fade: `v` (0 out, 1 lit) moved toward `target` by dt / kFadeS at most - a full swing in kFadeS seconds, never past the target; a time that is not one leaves it.
inline float Ease(float v, float target, double dt) {
    if (!std::isfinite(dt) || !(dt > 0.0)) return v;
    const float step = (float)(dt >= kFadeS ? 1.0 : dt / kFadeS);
    if (std::fabs(target - v) <= step) return target;
    return v + (target > v ? step : -step);
}

}  // namespace bowmana
