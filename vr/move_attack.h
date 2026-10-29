// move_attack.h - [gameplay] move_while_attacking: the hero walks on as soon as the blow or the shot is out, instead of standing
// out the rest of the attack's animation. EXPERIMENTAL, off by default, VR first person (F4) only, reach and ranged weapons only
// (spear, polearm, javelin, bow, crossbow). Pure: no game, no Windows, no clock; the unit test is cleanroom/tests/move_attack_test.cpp,
// the game's side vr/move_attack_wire.cpp.
//
// What stops the hero (read in 3.3.93787, docs/notes/move_while_attacking.md):
//   1. the server: a walk asked for while an attack plays goes through the attack-mode gate 0x42D840; with more than one frame of the
//      animation left it answers 4 ("not yet") and the walk (the stick's command 0x42, server 0x4AC070 -> 0x42CED0) is dropped, not
//      queued. The hero stands until the animation is over. That gate is the game's attack rate.
//   2. the client: the controller's stick walk 0x14AA10 sends nothing unless the hero's mode is stand / walk / run (1 2 3 5 6), and
//      nothing while a skill press is held (record +0x1974). So the walk comes back only after the hero is in his stand again - a frame
//      or two after the animation, more as a TCP/IP client.
//   The plugin's own Stand Still ([gameplay] attack_in_place) is not one of them: the stick's command carries no flags.
//
// What this option does where the game's server runs in this process - single player, or a TCP/IP game we host (the author, 2026-10-26:
// «да надо сделать чтобы хост с модом пропускал» - in a network game the host's setting applies to every hero in it):
//   - at the attack's action frame (the server's frame trigger 0x42A940, codes 1 / 2: the hit or the missile, which the game then makes
//     itself - roll, damage, mana, the arrow) it notes the attack and when its animation would end (event 1, read with 0x48B230);
//   - the client sends the game's own stick walk (0xFEFE0, the command the controller sends) while that attack's animation plays out,
//     with the stick pushed and no attack button held;
//   - the server's gate lets that walk in (answers 0 for the gate's 4) - only a walk or run, only out of an attack mode, only after
//     that attack's action frame - for every hero on our server with a spear / polearm / javelin / bow / crossbow (read from his unit
//     there), ours and the joined players';
//   - the rate stays the game's: our next attack or skill waits on our pad until the cut animation would have ended (a press made in that
//     time goes out then); a joined hero's next attack is held by our server until then (its general mode gate 0x42BFB0 says "not yet").
//   Our own client sends the early walk only with our server here (Role Single / Host) - never as a joined client.
// A joined TCP/IP client (someone else hosts): our client sends nothing extra, predicts nothing - the host's server decides. Online: nothing.
// Off: no hook does anything.
#pragma once

#include <cmath>
#include <cstdint>

namespace moveattack {

// -- the game's numbers ---------------------------------------------------------------------------------------------------------------
// Player modes (unit +0xC): 1 stand, 2 walk, 3 run, 5 / 6 the same in town; 7 / 8 attack 1 / 2, 11 throw.
constexpr uint32_t kModeWalk = 2, kModeRun = 3, kModeAttack1 = 7, kModeAttack2 = 8, kModeThrow = 11;
constexpr int kGateNotYet = 4;        // the attack-mode gate: "frames of the animation left", the walk is dropped
constexpr uint32_t kFrameMs = 40;     // the server's frame: 25 a second
// The weapon kinds of shared/d2r_vr_state.h (D2RVR_TYPE_*), as numbers so this header needs nothing else.
constexpr uint32_t kTypeBow = 2, kTypeCrossbow = 3, kTypePolearm = 10, kTypeSpear = 11, kTypeJavelin = 14;
// The pad's attack and skill buttons (XInput): A, B, X, Y, RB.
constexpr uint16_t kPadA = 0x1000, kAttackButtons = 0x1000 | 0x2000 | 0x4000 | 0x8000 | 0x0200;

// The weapons the option is for: what fights at reach or from afar, where the step back IS the weapon's advantage. Swords, axes, maces,
// daggers, claws, staves and bare hands keep the game's stand: for them standing to swing is the game's melee balance, and a hero who
// strikes while backing off from every monster would play another game.
inline bool WeaponQualifies(uint32_t type) {
    return type == kTypeBow || type == kTypeCrossbow || type == kTypePolearm || type == kTypeSpear || type == kTypeJavelin;
}
// The attack modes whose rest may be cut: the plain attacks and the throw. Not the cast (spells), not the kick, not the sequences (Zeal,
// Fend, Strafe - their later hits are part of the skill), not the special skill modes.
inline bool AttackMode(uint32_t mode) { return mode == kModeAttack1 || mode == kModeAttack2 || mode == kModeThrow; }
// What the stick's command asks the server for (0x42CED0's mode: 2 + run).
inline bool WalkMode(uint32_t mode) { return mode == kModeWalk || mode == kModeRun; }

// -- the attack, noted at its action frame (the server) -------------------------------------------------------------------------------
struct Fired {
    bool valid = false;
    uint32_t heroId = 0;      // the hero's unit id
    uint32_t weapon = 0;      // his weapon's kind on our server (D2RVR_TYPE_*), read at the trigger
    uint32_t mode = 0;        // his mode at the trigger
    int32_t atFrame = 0;      // the server's frame of the trigger
    int32_t endFrame = 0;     // when the animation ends (event 1), absolute
    uint64_t atMs = 0;        // our clock at the trigger
};
// The trigger codes that are the action itself (0x42A940 makes the attack's action for these two).
inline bool ActionCode(int code) { return code == 1 || code == 2; }
inline bool NoteWorth(int code, uint32_t mode) { return ActionCode(code) && AttackMode(mode); }
// The animation's frames left at the trigger, never below 0.
inline int32_t FramesLeft(int32_t endFrame, int32_t nowFrame) { return endFrame > nowFrame ? endFrame - nowFrame : 0; }

// -- the server's gate ------------------------------------------------------------------------------------------------------------------
struct GateAsk {
    int original = 0;         // the gate's own answer
    bool on = false;          // [gameplay] move_while_attacking
    bool f4 = false;          // VR first person
    bool player = false;      // the unit is a hero of this game on our server (type 0, its game) - ours or a joined player's
    bool own = false;         // ... and it is ours (then the view must be F4; a joined hero's view is his own: the host's setting decides)
    uint32_t mode = 0;        // his mode now
    uint32_t newMode = 0;     // the mode asked for
    bool target = false;      // a skill target came with it (not the stick's walk)
    int32_t nowFrame = 0;
    int32_t endFrame = 0;     // the animation's end now (event 1)
    uint32_t heroId = 0;      // the unit's id
    Fired fired;              // the last noted blow of this hero (Heroes::Find)
};
enum class GateWhy : uint8_t { Game, Off, NotHero, NotWalk, NotAttack, Weapon, NotFired, Let };
inline const char* GateWhyText(GateWhy w) {
    switch (w) {
    case GateWhy::Game: return "the game's own answer (not the animation's wait)";
    case GateWhy::Off: return "off, or not VR first person";
    case GateWhy::NotHero: return "not a hero of this game";
    case GateWhy::NotWalk: return "not the stick's walk or run";
    case GateWhy::NotAttack: return "not out of an attack or a throw";
    case GateWhy::Weapon: return "not a spear, polearm, javelin, bow or crossbow";
    case GateWhy::NotFired: return "the attack's blow or shot is not out yet";
    case GateWhy::Let: return "let in: the blow is out, the hero walks on";
    }
    return "";
}
// Let: the walk goes in now (the gate answers 0). Everything else: the gate's own answer stands.
inline GateWhy JudgeGate(const GateAsk& a) {
    if (a.original != kGateNotYet) return GateWhy::Game;   // 0 already free, 1 refused for its own reasons (stunned, frozen ...)
    if (!a.on || (a.own && !a.f4)) return GateWhy::Off;
    if (!a.player) return GateWhy::NotHero;
    if (!WalkMode(a.newMode) || a.target) return GateWhy::NotWalk;
    if (!AttackMode(a.mode)) return GateWhy::NotAttack;
    const Fired& f = a.fired;
    // the same attack: this hero, the same mode, the same end - an attack that began after the noted one has another end
    if (!f.valid || f.heroId != a.heroId || f.mode != a.mode || f.endFrame != a.endFrame || a.nowFrame < f.atFrame) return GateWhy::NotFired;
    if (!WeaponQualifies(f.weapon)) return GateWhy::Weapon;
    return GateWhy::Let;
}

// -- every hero on our server: his last blow, and the frame his next attack may begin (a joined hero's rate) -------------------------------
// Up to 8 heroes in a game; the one used longest ago gives his place to a new one. The server's thread writes, the client's reads (under a lock).
constexpr int kMaxHeroes = 8;
class Heroes {
public:
    void Note(const Fired& f) {
        Slot& s = SlotOf(f.heroId);
        s.fired = f;
        s.used = ++clock_;
    }
    Fired Find(uint32_t id) const {
        for (const Slot& s : slots_)
            if (s.used && s.fired.valid && s.fired.heroId == id) return s.fired;
        return Fired{};
    }
    // The walk cut this hero's attack: his next attack not before `frame` (the cut animation's end).
    void HoldUntil(uint32_t id, int32_t frame) {
        Slot& s = SlotOf(id);
        s.id = id;
        s.notBefore = frame;
        s.held = true;
        s.used = ++clock_;
    }
    // An attack asked for at `nowFrame`: must it wait? (Then the gate says no - the game's own answer for an attack that comes too early.)
    bool AttackWaits(uint32_t id, int32_t nowFrame) const {
        for (const Slot& s : slots_)
            if (s.used && s.held && s.id == id) return nowFrame < s.notBefore;
        return false;
    }
    void Reset() {
        for (Slot& s : slots_) s = Slot{};
        clock_ = 0;
    }

private:
    struct Slot {
        uint32_t id = 0;
        Fired fired;
        int32_t notBefore = 0;
        bool held = false;
        uint64_t used = 0;
    };
    Slot& SlotOf(uint32_t id) {
        Slot* oldest = &slots_[0];
        for (Slot& s : slots_) {
            if (s.used && s.id == id) return s;
            if (s.used < oldest->used) oldest = &s;
        }
        *oldest = Slot{};
        oldest->id = id;
        return *oldest;
    }
    Slot slots_[kMaxHeroes];
    uint64_t clock_ = 0;
};
// The modes a new attack or skill starts in (what a joined hero's hold keeps back): attack 1 / 2, cast, throw, kick, the skill modes, sequence.
inline bool StartsAttack(uint32_t mode) { return mode == 7 || mode == 8 || (mode >= 10 && mode <= 16) || mode == 18; }
// The general gate (0x42BFB0) for a joined hero: his new attack waits while the cut animation would still play. Our own hero waits on our pad
// instead (so no answer of our server ever disagrees with our client); with the game's answer already "no", nothing to add.
inline bool HoldAttack(bool on, bool player, bool own, int original, uint32_t newMode, bool waits) {
    return on && player && !own && original == 0 && StartsAttack(newMode) && waits;
}

// -- the role: who decides -------------------------------------------------------------------------------------------------------------------
// The game's own globals (both set before a game starts): gnGameType (client, 0x2A233F4) - 0 = single player, the game's own IsSinglePlayer()
// 0x8D4B0 is exactly "== 0"; geGameType (launcher, 0x32EF888) - LAUNCH_SINGLE_PLAYER 0, LAUNCH_BNET_PLAYER 1, LAUNCH_TCPIP_PLAYER 2. And whether
// the server in this process has seen our hero (its frame trigger noted his blow): that is single player or a game we host; a network game
// without it is someone else's.
enum class Role : uint8_t { Unknown, Single, Host, Joined, Online };
constexpr uint32_t kLaunchSingle = 0, kLaunchBnet = 1, kLaunchTcpIp = 2;
inline Role RoleOf(bool serverHere, bool read, uint32_t clientType, uint32_t launchType) {
    if (read && launchType == kLaunchBnet) return Role::Online;
    const bool network = read && (clientType != 0 || launchType != kLaunchSingle);
    if (serverHere) return network ? Role::Host : Role::Single;   // (types unread with our server here: called single player; the server is ours either way)
    if (network) return Role::Joined;
    return Role::Unknown;
}
inline const char* RoleText(Role r) {
    switch (r) {
    case Role::Unknown: return "not known yet";
    case Role::Single: return "single player - for our hero";
    case Role::Host: return "a TCP/IP game we host - our setting applies to every hero in it";
    case Role::Joined: return "a TCP/IP game someone else hosts - the host's setting applies, our client sends nothing extra";
    case Role::Online: return "an online game - nothing";
    }
    return "";
}
// Our client may send the early walk only with our server here: it lets that walk in. As a joined client the host's might not, and a walk it
// drops would leave our client's hero out of step with it.
inline bool ClientMaySend(Role r) { return r == Role::Single || r == Role::Host; }

// -- the client's walk ------------------------------------------------------------------------------------------------------------------
// What the controller's stick walk 0x14AA10 hands 0xFEFE0, from the controller's record: the stick on the game's grid (+0x195C / +0x1960),
// the Run toggle (0x8AFD0's high byte), stamina (+0x1951), a forced walk (+0x1952), the run threshold (+0x1F94 of the manager).
struct Walk {
    bool ok = false;
    float dir[2] = {0.0f, 0.0f};   // unit length (0x3B3010 asserts it)
    float mag = 0.0f;
    bool run = false;
};
inline Walk MakeWalk(float gx, float gy, bool runToggle, bool stamina, bool forcedWalk, float runThreshold) {
    Walk w;
    const float len = std::sqrt(gx * gx + gy * gy);
    if (!(len > 1e-4f)) return w;
    w.mag = len;
    if (runToggle && stamina && runThreshold > 0.0f && runThreshold >= len) w.mag = len / runThreshold;
    const float inv = 1.0f / len;
    w.dir[0] = gx * inv;
    w.dir[1] = gy * inv;
    w.run = runToggle && !forcedWalk && stamina;
    w.ok = true;
    return w;
}

struct NudgeAsk {
    bool on = false, f4 = false, weapon = false, menu = false;
    Role role = Role::Unknown;   // ClientMaySend
    uint32_t heroId = 0;
    uint32_t clientMode = 0;     // the hero's mode on the client
    bool skillHeld = false;      // the controller holds a skill press (+0x1974 != -1): the game repeats the attack, the hero stays
    bool stick = false;          // the stick is pushed (a walk to send)
    uint64_t nowMs = 0;
    Fired fired;
    uint32_t sentForThis = 0;    // walks already sent for this noted attack
    uint64_t lastSentMs = 0;
};
constexpr uint32_t kMaxSends = 3;        // the server took none of them (its gate said no for another reason): stop for this attack
constexpr uint64_t kSlackMs = 120;       // the note stays good this long past the animation's end by our clock
inline bool NoteFresh(const Fired& f, uint64_t nowMs) {
    if (!f.valid || nowMs < f.atMs) return false;
    const uint64_t life = (uint64_t)FramesLeft(f.endFrame, f.atFrame) * kFrameMs + kSlackMs;
    return nowMs - f.atMs <= life;
}
// Send the game's walk now (once a server frame at most).
inline bool Nudge(const NudgeAsk& a) {
    if (!a.on || !a.f4 || !a.weapon || a.menu || !a.stick || a.skillHeld || !ClientMaySend(a.role)) return false;
    if (!AttackMode(a.clientMode)) return false;   // walking already (the game's controller has it), or not an attack
    if (!NoteFresh(a.fired, a.nowMs) || a.fired.heroId != a.heroId) return false;
    if (a.sentForThis >= kMaxSends) return false;
    return a.lastSentMs == 0 || a.nowMs - a.lastSentMs >= kFrameMs;
}

// -- the rate: the next attack waits out the cut animation -------------------------------------------------------------------------------
// The hold is armed when the server lets a walk cut an attack: until the animation's end by the game's frames. In it the pad's attack and
// skill buttons of ours are taken away; one pressed in that time is pressed when the hold ends, for at least kReplayMs (the pad is polled
// in it), so no blow or shot is lost - it only comes when the game would have taken it.
constexpr uint64_t kReplayMs = 65;
struct PadOut {
    uint16_t strip = 0;   // take these buttons off the pad this poll
    uint16_t add = 0;     // and press these
};
class RateHold {
public:
    void Arm(uint64_t untilMs) {
        if (untilMs > until_) until_ = untilMs;
    }
    bool Holding(uint64_t nowMs) const { return nowMs < until_; }
    uint64_t Until() const { return until_; }
    // `pressed`: the attack and skill buttons of ours on the pad this poll.
    PadOut Step(uint64_t nowMs, uint16_t pressed) {
        PadOut o;
        pressed &= kAttackButtons;
        if (nowMs < until_) {
            o.strip = kAttackButtons;
            pending_ |= pressed;
            return o;
        }
        if (pending_) {
            if (!replaying_) { replaying_ = true; from_ = nowMs; }
            if (nowMs - from_ < kReplayMs) { o.add = pending_; return o; }
            pending_ = 0;
            replaying_ = false;
        }
        return o;
    }
    void Reset() { until_ = 0; pending_ = 0; replaying_ = false; }

private:
    uint64_t until_ = 0, from_ = 0;
    uint16_t pending_ = 0;
    bool replaying_ = false;
};

}  // namespace moveattack
