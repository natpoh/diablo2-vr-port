// armed_icon.h - [gameplay] melee_skill_armed_icon: the ARMED melee skill's icon (vr/melee_arm.h) lying on the bottom of the MANA medallion on the wrist ([hud] hand_vitals, the orbs look
// with the medallion shape, vr/hand_orbs.h), under the mana liquid and the glass - the skill spends mana, so it is shown where the mana is. Pure: no game, no windows.h, no clock (the caller
// passes the seconds since the last step), so cleanroom/tests/armed_icon_test.cpp drives it. The drawing is the effect's (reshade/D2R_DepthFog.fx OneMedalCore, OrbIcon / OrbIconTex);
// the wiring - the armed skill and the mana from vr/melee_arm_game.cpp, the game's own icon from vr/wheel_icons.h, the upload and the uniforms - is a block in vr/vrcam.cpp.
//
// The author's design (2026-10-19):
//   - the game's own skill icon (the one the skill panel's tiles and the skill wheel draw: vr/wheel_icons.h, the player's own cache of the game's sprites), cut to a CIRCLE with a soft edge
//     (Bake, CircleCover) and laid on the floor of the mana medallion's dome, inside its inner circle (kIconR < handorbs::kDomeR) - it never sticks out of it;
//   - the hero can pay the next hit (melee_arm.h Decide: the skill's Skills.txt cost at his level against his mana - the very check the hit makes): the icon at full strength;
//     he cannot: the icon half see-through (kInactive), the next hit being the plain attack; it brightens again when the mana is back (a short ease, kFadeS, not a jump);
//   - nothing armed: no icon. Unticked ([gameplay] melee_skill_armed_icon), melee_skill_armed off, not F4, or another look of the vitals: the medallion exactly as it was.
// The author's tuning (2026-10-19, after trying it): two sliders, live - its SIZE, in percent of the medallion's inner circle (the dome's foot, kDomeR; 30 - 100, 87.5 = kIconR, as it was) and
// its DEPTH in the liquid (100 = on the very bottom, as it was; 0 = as high as it goes - its rim just under the glass): Placement. The height is the dome's own geometry (a half ball of radius
// kDomeR on the floor: a circle of radius r fits at height z while r^2 + z^2 <= kDomeR^2), and the effect finds the icon by the same ray-plane step it finds the floor with - its parallax
// is the medallion's own, not an offset. And two more (the same day): its OPACITY while the hero can pay the next hit (100 %, as it was) and while he cannot (35 %, kInactive, as it
// was), 0 - 100 % each - the two strengths the Fader eases between.
// And the Amazon's (2026-10-19, "the same for the Amazon: when I choose a skill the icon must show inside the mana"): the bow skill chosen from the quiver ([bow] quiver, its orbs or its
// buttons; vr/bow_quiver_wire.cpp ChosenIcon) is shown the same way while a bow is in hand, full while the hero can pay the shot (vr/bow_mana.h, the release's own check), faint when not
// ([bow] quiver_icon). Shown: a skill of the melee list (melee_arm.h kRows) or a bow skill (bow_mana.h kRows).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "melee_arm.h"

namespace armedicon {

// ---- where and how it lies --------------------------------------------------------------------------------------------------------
constexpr float kIconR = 0.70f;     // the icon's circle, in units of the medallion's base radius (handorbs::kDomeR = 0.80 is the dome's foot; the bezel's inner edge is just under it)
constexpr float kEdge = 0.10f;      // the soft edge: the last tenth of the circle's radius fades out
constexpr float kInactive = 0.35f;  // the strength of an icon the hero cannot pay for (the next hit is the plain attack)
constexpr float kFadeS = 0.25f;     // the ease between the two strengths, and in and out, seconds for the whole way
constexpr int kTex = 128;           // the texture the icon is baked into (D2R_DepthFog.fx OrbIconTex: the same size)
constexpr float kInset = 0.07f;     // the game's icon tile carries a frame at its edge: the circle is taken from its middle, this much of the side in from each edge
constexpr float kDomeR = 0.80f;     // the dome's radius over the base's (vr/hand_orbs.h kDomeR; D2R_DepthFog.fx OneMedalCore's Rd0): the inner circle the size is a percent of

// ---- the sliders: size and depth -------------------------------------------------------------------------------------------------
constexpr float kSizeMinPct = 30.0f, kSizeMaxPct = 100.0f;
constexpr float kSizeDefPct = kIconR / kDomeR * 100.0f;   // 87.5: the icon as it first was
constexpr float kDepthDefPct = 100.0f;                    // on the bottom, as it first was
constexpr float kOpacityDefPct = 100.0f, kFaintDefPct = kInactive * 100.0f;   // the two opacities' defaults: 100 and 35, as they were
constexpr float kGlassGap = 0.97f;                        // "on the glass": the circle's rim this much of the way to the glass, never through it
struct Place {
    float radius = kIconR;   // the circle's radius over the base's (OrbIcon.z)
    float lift = 0.0f;       // its height over the floor over the base's radius (OrbIconLift): 0 on the bottom
    float opacity = 1.0f;    // the icon's strength while the hero can pay the next hit (In::full)
    float faint = kInactive; // ... and while he cannot (In::faint)
};
// size: percent of the inner circle; depth: 100 the bottom, 0 the highest the circle fits under the glass; opacityPct / faintPct: percent, with the mana and without. Out of range or not a
// number: clamped, or the default.
inline Place Placement(float sizePct, float depthPct, float opacityPct = kOpacityDefPct, float faintPct = kFaintDefPct) {
    if (!std::isfinite(sizePct)) sizePct = kSizeDefPct;
    if (!std::isfinite(depthPct)) depthPct = kDepthDefPct;
    if (!std::isfinite(opacityPct)) opacityPct = kOpacityDefPct;
    if (!std::isfinite(faintPct)) faintPct = kFaintDefPct;
    Place p;
    p.opacity = std::clamp(opacityPct, 0.0f, 100.0f) * 0.01f;
    p.faint = std::clamp(faintPct, 0.0f, 100.0f) * 0.01f;
    const float frac = std::clamp(sizePct, kSizeMinPct, kSizeMaxPct) / 100.0f;
    p.radius = kDomeR * frac;
    const float top = kDomeR * std::sqrt(std::max(1.0f - frac * frac, 0.0f)) * kGlassGap;   // the dome's own height over a circle that wide
    p.lift = (1.0f - std::clamp(depthPct, 0.0f, 100.0f) * 0.01f) * top;
    return p;
}

// The circle mask: how much of a point `r` icon radii from the middle is the icon (1 inside, 0 from the circle out, a smooth step over the last `edge`). The effect has the same line.
inline float Smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline float CircleCover(float r, float edge = kEdge) {
    if (!std::isfinite(r) || r >= 1.0f) return 0.0f;
    return 1.0f - Smoothstep(1.0f - edge, 1.0f, r);
}

// ---- what to draw -------------------------------------------------------------------------------------------------------------
struct In {
    bool on = false;          // [gameplay] melee_skill_armed_icon
    bool armedOn = false;     // [gameplay] melee_skill_armed
    bool f4 = false;          // VR first person
    bool medallion = false;   // the vitals are drawn as the medallions (the orbs look, the medallion shape, the glass - not the game's globes): there is a bottom to lie on
    int32_t armed = -1;       // the armed skill, -1 none
    bool enough = false;      // the hero can pay the next hit (Ready)
    float full = 1.0f;        // the strength then (Place::opacity)
    float faint = kInactive;  // ... and when he cannot (Place::faint)
};
struct Out {
    bool show = false;        // the effect draws the icon (OrbIcon.x)
    int32_t skill = -1;       // whose icon (the texture's)
    float alpha = 0.0f;       // its strength 0 - 1 (OrbIcon.y)
};

// The next hit is the armed skill: melee_arm.h's own decision for a touch now (`level` the hero's in it, -1 not read; `manaPoints` negative: not read - the game decides, so full strength).
inline bool Ready(int32_t armed, int32_t level, float manaPoints) { return armed >= 0 && meleearm::Decide(armed, level, manaPoints).skill == armed; }

// A skill whose icon may lie there: one of the melee list's, or a bow skill (the Amazon's chosen arrow).
inline bool Shown(int32_t skill) { return meleearm::IsMelee(skill) || bowmana::Find(skill) != nullptr; }

inline bool Wanted(const In& in) { return in.on && in.armedOn && in.f4 && in.medallion; }
inline float Level(float v, float def) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : def; }
inline float Target(const In& in) { return Wanted(in) && Shown(in.armed) ? (in.enough ? Level(in.full, 1.0f) : Level(in.faint, kInactive)) : 0.0f; }

// Whose icon: a bow's chosen skill while a bow is in hand (`bowSkill` >= 0: the quiver's, fresh), else the armed melee skill. -1 none.
struct Source {
    int32_t skill = -1;
    bool enough = false;
};
inline Source Pick(int32_t meleeSkill, bool meleeReady, int32_t bowSkill, bool bowReady) {
    if (bowSkill >= 0) return Source{bowSkill, bowReady};
    if (meleeSkill >= 0) return Source{meleeSkill, meleeReady};
    return Source{};
}

// The strength eased toward its target; the skill held while an icon fades out after a disarm. A setting off (or a view without the medallion) drops the icon at once: the medallion as before.
class Fader {
public:
    Out Step(const In& in, float dt) {
        if (!Wanted(in)) {
            Reset();
            return Out{};
        }
        const float target = Target(in);
        if (target > 0.0f) {
            if (skill_ != in.armed && alpha_ > 0.0f) alpha_ = std::min(alpha_, std::min(target, Level(in.faint, kInactive)));   // another skill armed: its icon comes in from faint, not from the last one's strength
            skill_ = in.armed;
        }
        const float step = std::isfinite(dt) && dt > 0.0f ? std::min(dt, 0.25f) / kFadeS : 0.0f;
        alpha_ = alpha_ < target ? std::min(target, alpha_ + step) : std::max(target, alpha_ - step);
        if (alpha_ <= 0.0f) skill_ = -1;
        Out o;
        o.show = alpha_ > 0.0f && skill_ >= 0;
        o.skill = o.show ? skill_ : -1;
        o.alpha = o.show ? alpha_ : 0.0f;
        return o;
    }
    void Reset() {
        alpha_ = 0.0f;
        skill_ = -1;
    }

private:
    float alpha_ = 0.0f;
    int32_t skill_ = -1;
};

// ---- the picture ---------------------------------------------------------------------------------------------------------------
// The game's icon (`rgba` straight R G B A, w x h) baked into the effect's texture: kTex x kTex, PREMULTIPLIED R G B A (the effect filters it linearly; a straight colour would fringe at the
// edge), the circle taken from the icon's middle (kInset), its alpha times CircleCover - every pixel from the circle out is 0, so neither the texture's clamp nor its filtering can put
// anything outside it. False (out cleared) when there is no picture.
inline bool Bake(const uint8_t* rgba, unsigned w, unsigned h, std::vector<uint8_t>& out) {
    out.assign((size_t)kTex * kTex * 4, 0);
    if (!rgba || !w || !h) return false;
    auto px = [&](int x, int y, int c) -> float {   // premultiplied, 0 - 1
        x = std::clamp(x, 0, (int)w - 1);
        y = std::clamp(y, 0, (int)h - 1);
        const uint8_t* p = rgba + ((size_t)y * w + x) * 4;
        const float a = p[3] / 255.0f;
        return c == 3 ? a : p[c] / 255.0f * a;
    };
    const float span = 1.0f - 2.0f * kInset;
    for (int y = 0; y < kTex; ++y)
        for (int x = 0; x < kTex; ++x) {
            const float u = (x + 0.5f) / kTex * 2.0f - 1.0f, v = (y + 0.5f) / kTex * 2.0f - 1.0f;
            const float cover = CircleCover(std::sqrt(u * u + v * v));
            if (cover <= 0.0f) continue;
            const float sx = (0.5f + 0.5f * u * span) * w - 0.5f, sy = (0.5f + 0.5f * v * span) * h - 0.5f;   // bilinear in the source
            const int x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
            const float fx = sx - x0, fy = sy - y0;
            uint8_t* d = &out[((size_t)y * kTex + x) * 4];
            for (int c = 0; c < 4; ++c) {
                const float top = px(x0, y0, c) * (1.0f - fx) + px(x0 + 1, y0, c) * fx;
                const float bot = px(x0, y0 + 1, c) * (1.0f - fx) + px(x0 + 1, y0 + 1, c) * fx;
                const float val = std::clamp((top * (1.0f - fy) + bot * fy) * cover, 0.0f, 1.0f);
                d[c] = (uint8_t)(val * 255.0f + 0.5f);
            }
        }
    return true;
}

// ---- the effect's numbers ------------------------------------------------------------------------------------------------------
// OrbIcon: x 1 = draw it, y the strength, z the circle's radius over the base's, w the soft edge (a fraction of that radius). OrbIconLift (Place::lift) goes on its own.
inline void Uniform(const Out& o, float u[4], const Place& p = Place{}) {
    u[0] = o.show ? 1.0f : 0.0f;
    u[1] = o.show ? std::clamp(o.alpha, 0.0f, 1.0f) : 0.0f;
    u[2] = p.radius;
    u[3] = kEdge;
}

}  // namespace armedicon
