// direct_layout.h - the pure logic of direct input (vr/direct_input.h): which VR control does what in the game,
// the release gates that keep a held control from leaking across a menu or a focus change, the rebinding table of
// the ini's [direct] section, and the head / hand maths for D2RVR_Shared. No game, no OpenXR, no globals - the
// unit test (cleanroom/tests/direct_input_test.cpp) drives it with synthetic controls.
//
// The ideas of the gates come from bearstonem/d2r-pcvr (MIT, steamvr/menu_controls.h `ReleaseGate`, input.h
// `MenuTriggerGate`): a control held when the context changes - a menu opens or closes, the input focus goes - does
// nothing in the new context until it has been let go. The layout, the table and the ini syntax are ours.
#pragma once

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "direct_input.h"
#include "hold_tap.h"

namespace directinput {
namespace layout {

// What the game's XInput poll gets (XInput's own fields).
struct Pad {
    uint16_t buttons = 0;
    uint8_t lt = 0, rt = 0;
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

// The controls of the two controllers, Touch names (Index: A/B on both hands; WMR/Vive: the menu and the
// trackpad's click stand in).
enum Ctl : uint8_t {
    kLTrigger, kLGrip, kLX, kLY, kLStick, kLMenu,
    kRTrigger, kRGrip, kRA, kRB, kRStick, kRMenu,
    kCtlCount
};
inline int HandOf(Ctl c) { return c >= kRTrigger ? 1 : 0; }   // 0 left, 1 right
inline Ctl GripOf(int hand) { return hand ? kRGrip : kLGrip; }
inline bool IsAnalog(Ctl c) { return c == kLTrigger || c == kRTrigger || c == kLGrip || c == kRGrip; }
inline const char* Name(Ctl c) {
    static const char* const n[kCtlCount] = {"left_trigger", "left_grip", "left_x", "left_y", "left_stick", "left_menu",
                                             "right_trigger", "right_grip", "right_a", "right_b", "right_stick", "right_menu"};
    return n[c];
}

// The controls as they are now: every control 0..1 (a button is 0 or 1), the sticks -1..1 (x right, y up).
struct Controls {
    float v[kCtlCount] = {};
    float stick[2][2] = {};
};

// The controls whose shots are dropped this frame (bit per Ctl), from the frame's takes (Take with kNoShotTrigger / kNoShotSqueeze; hand 0
// left, 1 right). A control that is taken in full is hidden from the layout anyway.
inline uint32_t NoShotMask(const uint32_t take[2]) {
    uint32_t m = 0;
    for (int h = 0; h < 2; ++h) {
        if (take[h] & kNoShotTrigger) m |= 1u << (h ? kRTrigger : kLTrigger);
        if (take[h] & kNoShotSqueeze) m |= 1u << (h ? kRGrip : kLGrip);
    }
    return m;
}

// -- thresholds ---------------------------------------------------------------------------------------------------------
constexpr float kTriggerOn = 0.55f, kTriggerOff = 0.35f;   // a trigger as a button (hysteresis)
constexpr float kGripOn = 0.60f, kGripOff = 0.40f;
constexpr float kReleased = 0.10f;                         // the gate lets a control go below this
constexpr float kStickReleased = 0.12f;                    // and a stick when it is back in this circle

// -- the release gate ---------------------------------------------------------------------------------------------------
// Every control held when the context value changes (or when Suspend is called) reads as let go until it really is -
// "a held grip does not prevent a newly pressed face button or the other stick". A gesture that keeps a control
// for itself passes it as `taken`: it stays masked after the gesture lets it go, until the player lets go too.
class ReleaseGate {
public:
    void Suspend() {
        for (bool& m : mask_) m = true;
        stick_[0] = stick_[1] = true;
        started_ = true;
    }
    Controls Step(uint32_t context, Controls c, const bool* taken = nullptr) {
        if (!started_ || context != context_) { Suspend(); context_ = context; }
        for (int i = 0; i < kCtlCount; ++i) {
            if (taken && taken[i]) mask_[i] = true;
            else if (c.v[i] <= kReleased) mask_[i] = false;
            if (mask_[i]) c.v[i] = 0.0f;
        }
        for (int h = 0; h < 2; ++h) {
            if (std::max(std::fabs(c.stick[h][0]), std::fabs(c.stick[h][1])) <= kStickReleased) stick_[h] = false;
            if (stick_[h]) c.stick[h][0] = c.stick[h][1] = 0.0f;
        }
        return c;
    }
    bool Masked(Ctl c) const { return mask_[c]; }
private:
    uint32_t context_ = 0;
    bool started_ = false;
    bool mask_[kCtlCount] = {};
    bool stick_[2] = {};
};

// -- outputs ------------------------------------------------------------------------------------------------------------
// What a control can do. A pad button is an XInput bit the game reads in its own poll; an action is a D2RVRAction
// bit (shared/d2r_vr_shared.h, the same numbers - direct_input.cpp asserts it); a command is the game's own key
// command (the index in its table 0x22A7930).
// kOutSkillPanel is a request, not a button: "toggle the skill panel" (directinput::TakeSkillPanelToggle).
enum OutKind : uint8_t { kOutNone, kOutPad, kOutLT, kOutRT, kOutAction, kOutCommand, kOutSkillPanel };
struct Out {
    OutKind kind = kOutNone;
    uint32_t value = 0;
};
constexpr int kMaxOuts = 3;
constexpr uint32_t kCommands = 0x45;   // D2RVR_GAME_COMMANDS

namespace act {
constexpr uint32_t kAttack = 1u << 0, kInteract = 1u << 1, kInventory = 1u << 2, kMap = 1u << 3, kSwap = 1u << 4,
                   kView1 = 1u << 5, kView2 = 1u << 6, kView3 = 1u << 7, kView4 = 1u << 8, kView5 = 1u << 9,
                   kViewNext = 1u << 10, kRecenter = 1u << 11;
}

struct Binding {
    Out out[kMaxOuts] = {};
};
// A control alone, and with the same hand's grip held ("left_grip+left_menu"). A control with a chord binding does
// that, not its plain one, while the grip is held at the moment it is pressed.
// A control with a "hold" binding is hold-or-tap (vr/hold_tap.h) while `holdEnabled`: its plain outputs wait for the
// release (a tap), its hold outputs fire once after `holdMs`; a command it holds (Alt) is held at once.
struct Table {
    Binding plain[kCtlCount];
    Binding chord[kCtlCount];
    Binding hold[kCtlCount];
    bool holdEnabled = true;                       // [direct] hold_enabled
    uint32_t holdMs = holdtap::kDefaultHoldMs;     // [direct] hold_ms
};

inline Binding Bind(Out a, Out b = {}, Out c = {}) { Binding r; r.out[0] = a; r.out[1] = b; r.out[2] = c; return r; }
inline Out OutPadBtn(uint16_t bit) { return {kOutPad, bit}; }
inline Out OutAction(uint32_t bit) { return {kOutAction, bit}; }
inline Out OutCmd(uint32_t cmd) { return {kOutCommand, cmd}; }
inline Out OutSkillPanel() { return {kOutSkillPanel, 0}; }

// An output that would shoot: the game's attack (action:attack), the pad's A (the attack on the pad) and the analog RT. [hands] bow_draw_only
// drops these - and only these - from a control pressed away from the string; a pad B / X / Y / RB (a skill slot), a key command, any other
// action stays as bound.
inline bool IsShot(const Out& o) {
    switch (o.kind) {
    case kOutAction: return (o.value & act::kAttack) != 0;
    case kOutPad: return (o.value & pad::kA) != 0;
    case kOutRT: return true;
    default: return false;
    }
}

// The default layout in the world (no menu): the game's own controller scheme where it has one - the right trigger
// is the attack, A the pick-up / interact, B / X / Y the skill slots (LT held: the second bank), the right stick
// click the weapon swap, the menu button the inventory. The grips are not a pad button here: the belt's potions
// and a two-handed grip read them. Chords on the grip: the map, the recentre, and RB (the fourth skill slot).
// stickMap ([direct] left_stick_map, default on): a quick click of the left stick also shows / hides the map. Alt
// (show items) stays what the click does while it is held, so the click's own line is Alt at once plus the map as a tap.
inline Table DefaultTable(bool stickMap = true) {
    Table t;
    t.plain[kRTrigger] = Bind(OutAction(act::kAttack));
    t.plain[kRA] = Bind(OutAction(act::kInteract));
    t.plain[kRB] = Bind(OutPadBtn(pad::kB));
    t.plain[kLX] = Bind(OutPadBtn(pad::kX));
    t.plain[kLY] = Bind(OutPadBtn(pad::kY));
    t.plain[kLTrigger] = Bind({kOutLT, 0});
    t.plain[kRStick] = Bind(OutAction(act::kSwap));
    t.plain[kLStick] = stickMap ? Bind(OutCmd(0x25), OutAction(act::kMap))   // Show items (Alt), held; the map on a tap
                                : Bind(OutCmd(0x25));
    t.plain[kLMenu] = Bind(OutAction(act::kInventory));
    t.plain[kRMenu] = Bind(OutAction(act::kInventory));
    t.chord[kLMenu] = Bind(OutAction(act::kMap));
    t.chord[kLStick] = Bind(OutAction(act::kRecenter));
    t.chord[kRB] = Bind(OutPadBtn(pad::kRightBumper));
    // a long press of a stick click: the right one opens the skill panel, the left one the inventory (a tap is the swap
    // on the right, the map on the left; held at once, the left click is Alt)
    t.hold[kRStick] = Bind(OutSkillPanel());
    t.hold[kLStick] = Bind(OutAction(act::kInventory));
    return t;
}

// In a menu (a panel open, or no game: the title and character screens) the pad is the game's own controller layout,
// control for control: A B X Y, the sticks, the stick clicks, the grips as bumpers, the triggers, the menu as Start.
inline Table MenuTable() {
    Table t;
    t.plain[kRA] = Bind(OutPadBtn(pad::kA));
    t.plain[kRB] = Bind(OutPadBtn(pad::kB));
    t.plain[kLX] = Bind(OutPadBtn(pad::kX));
    t.plain[kLY] = Bind(OutPadBtn(pad::kY));
    t.plain[kLStick] = Bind(OutPadBtn(pad::kLeftStick));
    t.plain[kRStick] = Bind(OutPadBtn(pad::kRightStick));
    t.plain[kLGrip] = Bind(OutPadBtn(pad::kLeftBumper));
    t.plain[kRGrip] = Bind(OutPadBtn(pad::kRightBumper));
    t.plain[kLTrigger] = Bind({kOutLT, 0});
    t.plain[kRTrigger] = Bind({kOutRT, 0});
    t.plain[kLMenu] = Bind(OutPadBtn(pad::kStart));
    t.plain[kRMenu] = Bind(OutPadBtn(pad::kStart));
    // a long press of the left stick click closes the inventory (the command is the game's own toggle); the right one
    // stays the game's R3 at once (held: sort, in the inventory)
    t.hold[kLStick] = Bind(OutAction(act::kInventory));
    return t;
}

enum class Mode { World, Menu };

struct Outputs {
    Pad pad;
    uint32_t actions = 0;      // D2RVRAction bits
    uint64_t commands[2] = {}; // bit n = the game's command n
    bool skillPanel = false;   // a toggle of the skill panel is asked for (true while the request's press is on: act on its edge)
};

// The layout. One per session; Step once per headset frame with the controls AFTER the gate.
class Layout {
public:
    Layout() : world_(DefaultTable()), menu_(MenuTable()) {}
    void SetWorld(const Table& t) { world_ = t; }
    const Table& World() const { return world_; }
    void Reset() { for (bool& d : down_) d = false; Cancel(); }
    // The context changed under the controls (a menu, the focus, the session): a press under way is forgotten, no tap.
    void Cancel() {
        for (int& l : latch_) l = -1;
        for (int i = 0; i < kCtlCount; ++i) { ht_[i].Cancel(); htOn_[i] = false; suppress_[i] = false; }
    }

    // nowMs: a monotonic clock in ms (hold-or-tap). uiUp: a panel of the plugin's own is up - a control with a hold line
    // gives nothing, plain or hold, until it has been let go. noShot: the controls (a bit per Ctl, NoShotMask) whose attack-type
    // outputs (IsShot) are left out of what they give - plain, chord and hold lines alike; everything else they are bound to works.
    Outputs Step(Mode mode, const Controls& c, uint64_t nowMs = 0, bool uiUp = false, uint32_t noShot = 0) {
        Outputs r;
        // sticks: the unit disc, then XInput's range
        auto axis = [](float v) { return (int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f); };
        for (int h = 0; h < 2; ++h) {
            float x = c.stick[h][0], y = c.stick[h][1];
            if (!std::isfinite(x) || !std::isfinite(y)) x = y = 0.0f;
            const float len = std::sqrt(x * x + y * y);
            if (len > 1.0f) { x /= len; y /= len; }
            (h ? r.pad.rx : r.pad.lx) = axis(x);
            (h ? r.pad.ry : r.pad.ly) = axis(y);
        }
        const Table& t = mode == Mode::Menu ? menu_ : world_;
        if (mode != mode_) { mode_ = mode; Cancel(); }   // (a latched binding is the other table's)
        bool now[kCtlCount];
        for (int i = 0; i < kCtlCount; ++i) {
            const Ctl ctl = (Ctl)i;
            const float v = c.v[i];
            if (ctl == kLTrigger || ctl == kRTrigger) now[i] = v > (down_[i] ? kTriggerOff : kTriggerOn);
            else if (ctl == kLGrip || ctl == kRGrip) now[i] = v > (down_[i] ? kGripOff : kGripOn);
            else now[i] = v > 0.5f;
        }
        for (int i = 0; i < kCtlCount; ++i) {
            const Ctl ctl = (Ctl)i;
            const bool htCtl = world_.holdEnabled && HasOut(t.hold[i]);
            const bool drop = ((noShot >> i) & 1u) != 0;   // this control's shots are left out
            const bool edge = now[i] && !down_[i];
            if (edge) {   // which binding this press is: the chord if the same hand's grip is already held
                const int h = HandOf(ctl);
                const bool chord = ctl != GripOf(h) && now[GripOf(h)] && HasOut(t.chord[i]);
                latch_[i] = chord ? i + kCtlCount : i;
                htOn_[i] = htCtl && !chord;
            }
            if (!now[i] && down_[i]) latch_[i] = -1;
            down_[i] = now[i];
            if (htCtl) {   // hold-or-tap: a press that is the machine's (a chord is not)
                if (uiUp) { ht_[i].Cancel(); htOn_[i] = false; suppress_[i] = now[i]; continue; }
                if (suppress_[i]) { if (now[i]) continue; suppress_[i] = false; }
                if (htOn_[i]) {
                    const holdtap::Out m = ht_[i].Step(now[i], nowMs, world_.holdMs);
                    for (const Out& o : t.plain[i].out) if ((o.kind == kOutCommand ? m.immediate : m.tap) && !(drop && IsShot(o))) Apply(o, 1.0f, r);
                    if (m.hold) for (const Out& o : t.hold[i].out) if (!(drop && IsShot(o))) Apply(o, 1.0f, r);
                    continue;
                }
            } else if (htOn_[i]) {   // the hold line was taken away under a press (settings changed)
                ht_[i].Cancel();
                htOn_[i] = false;
            }
            // an analog control bound to a trigger of the pad keeps its travel even before it counts as pressed
            const int l = latch_[i];
            if (l < 0) {
                if (IsAnalog(ctl) && c.v[i] > 0.0f) {   // not pressed: only an analog pad trigger follows its value
                    const Binding& b = t.plain[i];
                    for (const Out& o : b.out) if ((o.kind == kOutLT || o.kind == kOutRT) && !(drop && IsShot(o))) AnalogTrigger(o, c.v[i], r);
                }
                continue;
            }
            const Binding& b = l >= kCtlCount ? t.chord[l - kCtlCount] : t.plain[l];
            for (const Out& o : b.out) if (!(drop && IsShot(o))) Apply(o, IsAnalog(ctl) ? c.v[i] : 1.0f, r);
        }
        return r;
    }

private:
    static bool HasOut(const Binding& b) {
        for (const Out& o : b.out) if (o.kind != kOutNone) return true;
        return false;
    }
    static void AnalogTrigger(const Out& o, float v, Outputs& r) {
        const uint8_t b = (uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
        if (o.kind == kOutLT) r.pad.lt = std::max(r.pad.lt, b);
        else r.pad.rt = std::max(r.pad.rt, b);
    }
    static void Apply(const Out& o, float analog, Outputs& r) {
        switch (o.kind) {
        case kOutPad: r.pad.buttons |= (uint16_t)o.value; break;
        case kOutLT: case kOutRT: AnalogTrigger(o, analog, r); break;
        case kOutAction: r.actions |= o.value; break;
        case kOutCommand: if (o.value < kCommands) r.commands[o.value / 64] |= 1ull << (o.value % 64); break;
        case kOutSkillPanel: r.skillPanel = true; break;
        default: break;
        }
    }

    Table world_, menu_;
    Mode mode_ = Mode::World;
    bool down_[kCtlCount] = {};
    int latch_[kCtlCount] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    holdtap::Machine ht_[kCtlCount];
    bool htOn_[kCtlCount] = {};        // the press in progress (or its tap) is hold-or-tap's
    bool suppress_[kCtlCount] = {};    // a panel came up under the press: nothing until it is let go
};

// -- the ini's [direct] table -------------------------------------------------------------------------------------------
// right_trigger = action:attack          one control, one to three outputs separated by commas
// left_grip+left_menu = action:map       with the same hand's grip held
// left_stick = cmd:0x25, pad:L3          a command by its number (hex or decimal) or by its name ("Show items (Alt)")
// right_b = none                         unbound
// Outputs: pad:A B X Y LB RB LT RT L3 R3 Start Back Up Down Left Right; action:attack interact inventory map swap
// view1 .. view5 viewnext recenter; cmd:<n or name>; none.

inline bool EqNoCase(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
    return !*a && !*b;
}

// An output from its text (one token, trimmed). `cmdByName` maps a command's name to its number, -1 if it has none.
inline bool ParseOut(const char* tok, int (*cmdByName)(const char*), Out* out) {
    struct Named { const char* name; uint32_t value; };
    static const Named pads[] = {{"a", pad::kA}, {"b", pad::kB}, {"x", pad::kX}, {"y", pad::kY}, {"lb", pad::kLeftBumper},
                                 {"rb", pad::kRightBumper}, {"l3", pad::kLeftStick}, {"r3", pad::kRightStick},
                                 {"start", pad::kStart}, {"back", pad::kBack}, {"up", pad::kUp}, {"down", pad::kDown},
                                 {"left", pad::kLeft}, {"right", pad::kRight}};
    static const Named acts[] = {{"attack", act::kAttack}, {"interact", act::kInteract}, {"inventory", act::kInventory},
                                 {"map", act::kMap}, {"swap", act::kSwap}, {"view1", act::kView1}, {"view2", act::kView2},
                                 {"view3", act::kView3}, {"view4", act::kView4}, {"view5", act::kView5},
                                 {"viewnext", act::kViewNext}, {"recenter", act::kRecenter}};
    if (EqNoCase(tok, "none")) { *out = {}; return true; }
    const char* colon = std::strchr(tok, ':');
    if (!colon) return false;
    char kind[16] = {};
    const size_t kl = (size_t)(colon - tok);
    if (kl == 0 || kl >= sizeof kind) return false;
    std::memcpy(kind, tok, kl);
    const char* arg = colon + 1;
    while (*arg == ' ') ++arg;
    if (EqNoCase(kind, "pad")) {
        if (EqNoCase(arg, "lt")) { *out = {kOutLT, 0}; return true; }
        if (EqNoCase(arg, "rt")) { *out = {kOutRT, 0}; return true; }
        for (const Named& n : pads) if (EqNoCase(arg, n.name)) { *out = {kOutPad, n.value}; return true; }
        return false;
    }
    if (EqNoCase(kind, "action")) {
        for (const Named& n : acts) if (EqNoCase(arg, n.name)) { *out = {kOutAction, n.value}; return true; }
        if (EqNoCase(arg, "skillpanel")) { *out = OutSkillPanel(); return true; }
        return false;
    }
    if (EqNoCase(kind, "cmd")) {
        char* end = nullptr;
        const long n = std::strtol(arg, &end, 0);
        if (end != arg && !*end) {
            if (n < 0 || n >= (long)kCommands) return false;
            *out = {kOutCommand, (uint32_t)n};
            return true;
        }
        const int c = cmdByName ? cmdByName(arg) : -1;
        if (c < 0 || c >= (int)kCommands) return false;
        *out = {kOutCommand, (uint32_t)c};
        return true;
    }
    return false;
}

// "pad:B, cmd:Show items (Alt)" -> a binding. False when the text names something that does not exist (nothing
// is changed then). Empty text and "none" are a valid, empty binding.
inline bool ParseBinding(const char* text, int (*cmdByName)(const char*), Binding* out) {
    Binding b;
    int n = 0;
    const char* p = text;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        char tok[96];
        size_t len = 0;
        while (*p && *p != ',') { if (len + 1 < sizeof tok) tok[len++] = *p; ++p; }
        while (len && (tok[len - 1] == ' ' || tok[len - 1] == '\t')) --len;
        tok[len] = 0;
        Out o;
        if (!ParseOut(tok, cmdByName, &o)) return false;
        if (o.kind == kOutNone) continue;
        if (n >= kMaxOuts) return false;
        b.out[n++] = o;
    }
    *out = b;
    return true;
}

// -- the head and the hands as D2RVR_Shared wants them ---------------------------------------------------------------
// shared/d2r_vr_shared.h: the head's turn (yaw positive = turned left), pitch (up positive), roll (toward the right
// shoulder positive), and each hand in the frame of the head's TURN alone - x right, y up, z back, no pitch or roll.
namespace math {
struct V3 { float x, y, z; };
// q v q*, q = x y z w
inline V3 Rotate(const float q[4], V3 v) {
    const float tx = 2 * (q[1] * v.z - q[2] * v.y), ty = 2 * (q[2] * v.x - q[0] * v.z), tz = 2 * (q[0] * v.y - q[1] * v.x);
    return {v.x + q[3] * tx + (q[1] * tz - q[2] * ty), v.y + q[3] * ty + (q[2] * tx - q[0] * tz), v.z + q[3] * tz + (q[0] * ty - q[1] * tx)};
}
struct HeadAngles { float yawDeg, pitchDeg, rollDeg, yawRad; };
inline HeadAngles Angles(const float headRot[4]) {
    const float r2d = 57.2957795f;
    const V3 f = Rotate(headRot, {0.0f, 0.0f, -1.0f}), r = Rotate(headRot, {1.0f, 0.0f, 0.0f}), u = Rotate(headRot, {0.0f, 1.0f, 0.0f});
    const float yaw = std::atan2(-f.x, -f.z);
    return {yaw * r2d, std::asin(std::clamp(f.y, -1.0f, 1.0f)) * r2d, std::atan2(-r.y, u.y) * r2d, yaw};
}
// A pose in the room into the head's turn frame: the place relative to the head, turned by -yaw; the orientation
// with the head's turn taken off.
inline void IntoHeading(float yawRad, const float headPos[3], const float pos[3], const float rot[4], float outPos[3], float outRot[4]) {
    const float c = std::cos(yawRad), sn = std::sin(yawRad), qs = std::sin(-yawRad * 0.5f), qc = std::cos(-yawRad * 0.5f);
    const float dx = pos[0] - headPos[0], dy = pos[1] - headPos[1], dz = pos[2] - headPos[2];
    outPos[0] = dx * c - dz * sn;
    outPos[1] = dy;
    outPos[2] = dx * sn + dz * c;
    const float x = rot[0], y = rot[1], z = rot[2], w = rot[3];
    outRot[0] = qc * x + qs * z;
    outRot[1] = qc * y + qs * w;
    outRot[2] = qc * z - qs * x;
    outRot[3] = qc * w - qs * y;
}
}  // namespace math

}  // namespace layout
}  // namespace directinput
