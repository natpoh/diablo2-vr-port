// melee_trigger.h - [gameplay] melee_touch_only: with hit by touch on, the TRIGGER does not attack - only the blade's touch does. A part of hit by touch, on with it; the key is the way back. Pure: no game calls,
// no globals, no windows.h (cleanroom/tests/melee_trigger_test.cpp). vr/melee_trigger_wire.cpp is the game-facing half (the frames of direct_input.h).
//
// The player's words (2026-10-18): "a pull of the trigger attacks and you can skip the swing - that is wrong" (a bug of hit by touch, not a missing option); "only the bow and the crossbow hit with the
// trigger"; "the throwing weapons do not hit with the trigger either - the throw gesture throws them"; "the spells are bound to the buttons so far, not to the trigger". So, in first person (F4), while hit by
// touch is live (a weapon the touch swings is in a hand, posed and tracked), the trigger's ATTACK press is not passed to the game - the plugin's own layout leaves out of what the trigger is bound to the outputs
// that would attack (directinput::kNoShotTrigger: action:attack, pad A, RT; every other output of the binding - a skill button, a command - works as bound). That is every kind of weapon the touch swings:
// melee weapons, bare hands, throwing weapons. A bow and a crossbow are shot, not swung, and keep the trigger. Only our own controller press is withheld: nothing of the game is blocked, hooked or called,
// and the attack the blade's touch makes is a press of its own (melee_touch.cpp: directinput::Action from the touch's source), which this never sees.
//
// A skill the player bound to the trigger casts: with [openxr] skill_rt_a the skill panel's slot controls make the right trigger the game's RT slot, and when that slot holds a skill (not nothing,
// Attack or Interact) the panel owns the trigger for the press (Facts::rtSkill) - only the PLAIN attack on the trigger stays withheld. Everything the panel does not own is withheld as above.
//
// Where it stands down (the trigger works as bound, never a lock-out): hit by touch not live - no tracking, no pose, the weapon let go, a throwing weapon with its touch off, a throwing potion - a menu or a
// panel (the trigger is a click there), another view, no focus.
//
// THE RULE IS HERE: KeepsTrigger (which weapons keep the trigger) and Decide (what else it waits for). Everything else is plumbing.
//
// (claude/melee-touch-only 4428bac had a version that read the skill on the pad's A slot and withheld only the blows of the weapon, keeping spells; the player put the spells on the buttons.)
#pragma once

#include <cstdint>

#include "d2r_vr_state.h"   // D2RVR_WeaponType

namespace meleetrigger {

// A weapon that keeps the trigger: the ones that are shot. (The weapons the touch swings - melee, bare hands, throwing - do not.)
inline bool KeepsTrigger(uint32_t weaponType) { return weaponType == D2RVR_TYPE_BOW || weaponType == D2RVR_TYPE_CROSSBOW; }

enum class Why : uint8_t {
    Off,            // [gameplay] melee_touch_only is off: the way back
    NotF4,          // another view
    Unfocused,      // the headset has not the focus
    MenuOrPanel,    // a menu, a panel of the game's or of the plugin's: the trigger is a click there
    TouchNotLive,   // hit by touch is not swinging a weapon now (none in hand, no tracking, a throwing weapon with its touch off, a potion...)
    Shot,           // a bow or a crossbow: shot, not swung - the trigger shoots
    SlotSkill,      // [openxr] skill_rt_a: the trigger is the game's RT slot and that slot holds a skill - a cast the player put there, not an attack
    Dropped,        // the trigger's attack is withheld
};
inline const char* WhyText(Why w) {
    switch (w) {
        case Why::Off: return "off";
        case Why::NotF4: return "not first person";
        case Why::Unfocused: return "the headset has not the focus";
        case Why::MenuOrPanel: return "a menu or a panel is up";
        case Why::TouchNotLive: return "hit by touch is not swinging a weapon now";
        case Why::Shot: return "a bow or a crossbow is shot with the trigger";
        case Why::SlotSkill: return "the trigger casts the skill on the RT slot";
        case Why::Dropped: return "the trigger's attack is withheld";
    }
    return "?";
}

struct Facts {
    bool on = false;           // [gameplay] melee_touch_only (default on)
    bool f4 = true;            // viewgate::F4() and the frame is not another view's
    bool focused = true;       // the session has the input focus
    bool menu = false;         // a menu or a panel (the frame's kInMenu / kUiUp)
    bool touchLive = false;    // hit by touch is swinging a weapon (melee_touch.cpp Live), fresh
    uint32_t weaponType = 0;   // D2RVR_WeaponType of the weapon it swings
    bool rtSkill = false;      // the skill panel's slot controls own the trigger this frame and the RT slot holds a skill (skillwheel::TriggerCastsSkill)
};

struct Verdict {
    bool drop = false;   // withhold the triggers' attack outputs this frame
    Why why = Why::Off;
};

inline Verdict Decide(const Facts& f) {
    Verdict v;
    if (!f.on) { v.why = Why::Off; return v; }
    if (!f.f4) { v.why = Why::NotF4; return v; }
    if (!f.focused) { v.why = Why::Unfocused; return v; }
    if (f.menu) { v.why = Why::MenuOrPanel; return v; }
    if (!f.touchLive) { v.why = Why::TouchNotLive; return v; }
    if (KeepsTrigger(f.weaponType)) { v.why = Why::Shot; return v; }
    if (f.rtSkill) { v.why = Why::SlotSkill; return v; }   // (a skill the player bound to the trigger casts; only the plain attack is withheld)
    v.drop = true;
    v.why = Why::Dropped;
    return v;
}

// The frames: the verdict each frame, and what is worth a line in the log - a change of the verdict (at most one every kQuietS seconds, so a blade that goes in and out of reach does not fill the log) and
// each press of a trigger that was withheld (the first kToldPresses). The draw thread's alone.
constexpr double kQuietS = 2.0;
constexpr unsigned kToldPresses = 12;
class Gate {
public:
    struct Out {
        Verdict v;
        bool changed = false;     // the verdict differs from the one last told: tell it
        bool withheld = false;    // a trigger went down this frame and its attack was dropped: tell it (withheldCount presses so far)
        unsigned withheldCount = 0;
    };
    // `trigger`: the two triggers' values (0 left, 1 right, as directinput::Frame), `t`: seconds.
    Out Step(const Facts& f, const float trigger[2], double t) {
        Out o;
        o.v = Decide(f);
        // a change of the verdict (a drop that starts or stops, or the reason it does not): told, but not oftener than kQuietS - except the first
        if (o.v.why != toldWhy_ && (!told_ || t - toldAt_ >= kQuietS || t < toldAt_)) {
            told_ = true;
            toldWhy_ = o.v.why;
            toldAt_ = t;
            o.changed = true;
        }
        for (int h = 0; h < 2; ++h) {
            const bool down = trigger[h] >= 0.55f;
            if (down && !down_[h] && o.v.drop) {
                ++count_;
                if (count_ <= kToldPresses) o.withheld = true;
            }
            down_[h] = down;
        }
        o.withheldCount = count_;
        return o;
    }
    void Reset() { *this = Gate{}; }

private:
    bool told_ = false;
    Why toldWhy_ = Why::Off;
    double toldAt_ = 0.0;
    bool down_[2] = {false, false};
    unsigned count_ = 0;
};

}  // namespace meleetrigger
