// melee_direct_client.h - Direct hit for a joined TCP/IP player ([gameplay] melee_direct_client, default off).
//
// Direct hit (melee_direct.h) calls the server's attack itself, so it only works where the server runs: single player and the
// host. A player who joined someone's TCP/IP game has no server in the process, and his touch goes the key path: the plugin
// presses the pad's attack, the game's pad cast asks its target lookups (answered with the touched monster), its click record
// decides between "attack the unit" and, with Stand Still and the monster beyond the game's own reach, "attack at a point"
// (0x100970 -> 0x102B70), and the funnel sends that. A touch the VR blade made a little beyond the game's reach then goes out
// as a blow at a point and lands nothing.
//
// This option sends, at the accepted touch, the game's own command for the touched monster straight into the same funnel every
// command of the player goes through - the walk, the run, the stick's direction (0xFABE0: the client's prediction, then the
// packet to the server; docs/move_recon.md section 4, docs/stray_walk.md). The command is 6, "left skill on a unit" (a1 = the
// unit's type, 1 a monster; a2 = its id) - the very packet the game's own click on a monster sends (0x102B70 for the plain
// left skill without Stand Still; 0x248DF0 builds the same one). The host's server then does everything it does for a click:
// the walk up when the monster is beyond the reach, the attack-rate gate, the animation, the skill's start checks, the roll and
// the damage. Nothing is written into the game, no server function is called, no packet is made up: the host needs nothing
// but the same plugin every player needs anyway.
//
// The left skill is the one the pad's attack sets (the hand-skill setter 0x118D50 makes the unit's left skill the A slot's
// binding): the command is only sent while the hero's left skill IS the A slot's skill and no melee skill is armed for the
// press ([gameplay] melee_skill_armed), so it attacks exactly as the press would. Otherwise (another skill was cast with
// another button since, a skill armed), and on any doubt, the touch goes the key path as before.
//
// The author's rules (2026-10-27), all in this same option. A touch that fails one still attacks: it goes the key path, the normal
// attack - never nothing.
//   1. The first touch on a monster is a direct hit.
//   2. Then the game's own pace: a touch on the SAME monster sooner than the hero's attack takes (his attack animation as the game
//      plays it on his client - his attack speed, measured from his own unit's mode) is the normal attack - poking fast wins nothing.
//   3. Once that time has passed, a touch on the same monster is a direct hit again only for a real swing: the blade's speed where it
//      touched (melee_direct_client_min_speed, m/s), the strike's force (melee_direct_client_min_force: the weight model's strike
//      score, N s - vr/strike_force.h) and the swing's length (melee_direct_client_min_length: how far the CONTROLLER itself travelled
//      in this swing, m - its own path, so a flick of the wrist sweeping a long blade tip does not pass it; not a poke).
// A TWO-HANDED weapon (the game's own flag of the item) needs both hands on it (the plugin's two-hand grip): in one hand, its touch is
// the normal attack - no direct hit, no cleave; with both, the length passes when either controller travelled far enough.
// Another monster is never "too soon" (rule 2 is per monster). Without the cleave box the game still runs one attack at a time.
//
// [gameplay] melee_direct_client_cleave (a box of its own, off by default; the author's explicit permission of 2026-10-27): with a
// TWO-HANDED weapon, a swing through a group hits every monster it touches. THIS GOES PAST THE GAME'S RULE OF ONE ATTACK AT A TIME.
// The first monster of a swing gets the command above (the hero's animation, the game's own attack). A touch on ANOTHER monster while
// that attack is still under way (sooner than the hero's attack time since that command) - an "extra hit" - would be dropped by the
// game, so, when the swing has the least speed AND force, it is asked of the HOST over D2RLoader's plugin network (channel kChannel):
// the host's plugin calls the server's basic attack for that player's hero on that monster, once (melee_direct.cpp, the same call as
// the host's own Direct hit; the roll, the damage, the wear are the game's, no animation of its own). Rule 2 holds for it too. A
// one-handed weapon's extra touch, or one too slow or too weak, is the normal attack (the game's one attack at a time decides). The
// host must run the plugin with the same boxes ticked; a host with it off ignores the requests, and with no connection to the host
// an extra hit is the plain command.
//
// This header is the pure part; the game's side is vr/melee_direct_client.cpp; the test is cleanroom/tests/melee_direct_client_test.cpp.
#pragma once

#include <cstdint>

namespace D2RL {
struct PluginContext;
}

namespace meleedirectclient {

// The funnel's commands (the D2 packet ids, docs/stray_walk.md): 6 the left skill on a unit, 7 the same with Stand Still,
// 0xD / 0xE the right skill's.
constexpr int kCmdLeftOnUnit = 6;
constexpr int kUnitMonster = 1;

// The game's two globals the role is read from (move_attack.h RoleOf has the same reading): gnGameType 0 = single player;
// geGameType LAUNCH_SINGLE_PLAYER 0, LAUNCH_BNET_PLAYER 1, LAUNCH_TCPIP_PLAYER 2.
constexpr uint32_t kLaunchSingle = 0, kLaunchBnet = 1;
// A joined TCP/IP player: no server of ours in this process (melee_touch never learned one) and a network game that is not online.
inline bool Joined(bool serverHere, bool read, uint32_t clientType, uint32_t launchType) {
    if (serverHere || !read || launchType == kLaunchBnet) return false;
    return clientType != 0 || launchType != kLaunchSingle;
}
// ... and so for this long without a break: at a game's start a host's own server may not have been learned yet (melee_touch learns
// it from the server's first lookups of the hero, within a few frames) - the host is never taken for a joined player.
constexpr uint64_t kJoinSettleMs = 3000;
inline bool Settled(uint64_t joinedSince, uint64_t now) { return joinedSince && now >= joinedSince && now - joinedSince >= kJoinSettleMs; }

// ---- the hero's attack time, measured on his client unit ----
// The modes an attack or skill plays in (attack 1 / 2, cast, throw, kick, the skill modes, sequence; move_attack.h StartsAttack).
inline bool AttackMode(uint32_t mode) { return mode == 7 || mode == 8 || (mode >= 10 && mode <= 16) || mode == 18; }
constexpr uint64_t kDefaultAttackMs = 600;                 // until the first attack is measured
constexpr uint64_t kMinAttackMs = 150, kMaxAttackMs = 2000; // a reading outside is not an attack's length (a mode flicker, two queued attacks)
class AttackClock {
 public:
    // Every tick: the hero's client mode (`known` false: not read - nothing changes).
    void Sample(bool known, uint32_t mode, uint64_t now) {
        if (!known) return;
        const bool in = AttackMode(mode);
        if (in && !in_) startAt_ = now;
        if (!in && in_ && startAt_ && now > startAt_) {
            const uint64_t d = now - startAt_;
            if (d >= kMinAttackMs && d <= kMaxAttackMs) { ms_ = d; measured_ = true; }
        }
        in_ = in;
    }
    uint64_t Ms() const { return ms_; }
    bool Measured() const { return measured_; }
 private:
    bool in_ = false, measured_ = false;
    uint64_t startAt_ = 0, ms_ = kDefaultAttackMs;
};

// ---- per monster: when the last command went to it ----
class PerMonster {
 public:
    static constexpr int kSlots = 16;
    // Too soon for monster `id`: its last command less than `attackMs` ago. *since: how long ago (when too soon).
    bool TooSoon(uint32_t id, uint64_t now, uint64_t attackMs, uint64_t* since) const {
        for (const Slot& s : slots_)
            if (s.id == id && s.at && now >= s.at && now - s.at < attackMs) {
                if (since) *since = now - s.at;
                return true;
            }
        return false;
    }
    // Rule 1: a direct hit went to monster `id` before (while it is remembered: the last kSlots monsters).
    bool Known(uint32_t id) const {
        for (const Slot& s : slots_) if (s.id == id && s.at) return true;
        return false;
    }
    void Sent(uint32_t id, uint64_t now) {
        Slot* oldest = &slots_[0];
        for (Slot& s : slots_) {
            if (s.id == id) { s.at = now; return; }
            if (s.at < oldest->at) oldest = &s;
        }
        *oldest = {id, now};
    }
    void Clear() { for (Slot& s : slots_) s = {}; }
 private:
    struct Slot { uint32_t id = 0; uint64_t at = 0; };
    Slot slots_[kSlots];
};

// What a touch was, as measured: each -1 when not known (a rule that needs it then fails: the normal attack).
struct Touch {
    float speed = -1.0f;    // m/s: the blade where it touched (a strike judged by force: its arrival's closing speed)
    float force = -1.0f;    // N s: the weight model's strike score (vr/strike_force.h Score)
    float length = -1.0f;   // m: the controller's own path in this swing - the weapon hand's; a two-handed weapon: the longer of the two
    float hand[2] = {-1.0f, -1.0f};   // m: each controller's path (0 right, 1 left), for the log
};
struct Mins { float speed = 2.5f, force = 1.5f, length = 0.2f; };

struct Gate {
    bool wanted = false;      // [gameplay] melee_direct_client
    bool ready = false;       // the funnel and the reads are proven, the UI thread service is there, no fault
    bool f4 = true;           // viewgate::F4(): F4 only, like the rest of hit by touch
    bool joined = false;      // Joined(...)
    bool melee = true;        // a melee blow (not a thrown weapon's point-blank throw: allowDirect in melee_touch)
    bool armed = false;       // a melee skill is armed for the press ([gameplay] melee_skill_armed): the press reads it, the command would not
    bool leftIsSlot = false;  // the UI thread's last look: the hero's left skill is the A slot's skill (fresh enough)
    bool tooSoon = false;     // rule 2: PerMonster::TooSoon for this monster at the hero's attack time
    bool first = true;        // rule 1: no direct hit on this monster yet (PerMonster::Known)
    bool extra = false;       // the hero's attack on ANOTHER monster still under way (InAttack)
    bool cleave = false;      // [gameplay] melee_direct_client_cleave
    bool twoHanded = false;   // the weapon is two-handed (the game's own flag of the item)
    bool twoHandsOn = false;  // ... and held in both hands (the plugin's two-hand grip)
    Touch t;
    Mins mins;
};

enum class Route { Key, Command, Extra };

// Why a touch went the way it did (the log's verdict and counts). Every one but Sent / SentFirst / SentExtra is the normal attack.
enum class Why { Sent, SentFirst, SentExtra, NotF4, NotJoined, Armed, OtherSkill, Thrown, NotReady, Off, TooSoon, TooSlow, TooWeak, TooShort, OneHanded, NoCleave,
                 NotTwoHands, kCount };
inline Why WhyOf(const Gate& g) {
    if (!g.wanted) return Why::Off;
    if (!g.ready) return Why::NotReady;
    if (!g.f4) return Why::NotF4;
    if (!g.joined) return Why::NotJoined;
    if (!g.melee) return Why::Thrown;
    if (g.armed) return Why::Armed;
    if (!g.leftIsSlot) return Why::OtherSkill;
    if (g.twoHanded && !g.twoHandsOn) return Why::NotTwoHands;   // a two-handed weapon in one hand: no direct hit, no cleave
    if (g.tooSoon) return Why::TooSoon;                     // rule 2, before all: the same monster, sooner than the attack takes
    const bool slow = g.t.speed < 0.0f || g.t.speed < g.mins.speed;
    const bool weak = g.t.force < 0.0f || g.t.force < g.mins.force;
    if (g.extra) {                                          // the cleave: its box, a two-handed weapon, the least speed AND force
        if (!g.cleave) return Why::NoCleave;
        if (!g.twoHanded) return Why::OneHanded;
        if (slow) return Why::TooSlow;
        if (weak) return Why::TooWeak;
        return Why::SentExtra;
    }
    if (g.first) return Why::SentFirst;                     // rule 1
    if (slow) return Why::TooSlow;                          // rule 3: a real swing again
    if (weak) return Why::TooWeak;
    if (g.t.length < 0.0f || g.t.length < g.mins.length) return Why::TooShort;
    return Why::Sent;
}
inline Route Decide(const Gate& g) {
    const Why w = WhyOf(g);
    return w == Why::SentExtra ? Route::Extra : (w == Why::Sent || w == Why::SentFirst) ? Route::Command : Route::Key;
}
inline const char* WhyName(Why w) {
    switch (w) {
        case Why::Sent: return "direct";
        case Why::SentFirst: return "direct (the first touch on it)";
        case Why::SentExtra: return "extra hit (cleave): the server's attack asked of the host";
        case Why::TooSoon: return "normal: too soon on the same monster";
        case Why::TooSlow: return "normal: too slow";
        case Why::TooWeak: return "normal: too weak";
        case Why::TooShort: return "normal: too short a swing";
        case Why::OneHanded: return "normal: one-handed (the game's one attack at a time)";
        case Why::NoCleave: return "normal: another monster within the attack (no cleave box: the game's one attack at a time)";
        case Why::NotTwoHands: return "normal: a two-handed weapon in one hand";
        case Why::OtherSkill: return "normal: another skill on the left";
        case Why::Armed: return "normal: a skill is armed";
        case Why::Thrown: return "normal: a thrown weapon";
        default: return "normal";
    }
}

// The UI thread's look at the skills, kept this long: a cast with another button changes the left skill, and the next look
// (every kLookMs while the touch is live) tells. The send itself looks again on the UI thread, at the moment it sends.
constexpr uint64_t kLookMs = 100, kLookFreshMs = 400;
inline bool Fresh(uint64_t lookAt, uint64_t now) { return lookAt && now >= lookAt && now - lookAt <= kLookFreshMs; }

// What the UI thread found at the send.
struct Found {
    bool hero = false;          // the client's own player unit, its header proven (type 0, the hero's id)
    uint32_t heroMode = 0;
    bool monster = false;       // the touched monster in the client's units (type 1, its id)
    uint32_t monsterMode = 0;
    bool skills = false;        // the left skill and the A slot's skill were read
    int32_t left = -1, slot = -2;
};

enum class Verdict { Go, NoHero, HeroMode, NoMonster, MonsterDying, NoSkills, OtherSkill };

inline Verdict Check(const Found& f) {
    if (!f.hero) return Verdict::NoHero;
    // dead, dying, in a town portal's sequence (the modes melee_direct.h refuses): the game would refuse the command anyway
    if (f.heroMode == 0 || f.heroMode == 17 || f.heroMode == 5 || f.heroMode == 6) return Verdict::HeroMode;
    if (!f.monster) return Verdict::NoMonster;
    if (f.monsterMode == 0 || f.monsterMode == 12) return Verdict::MonsterDying;
    if (!f.skills) return Verdict::NoSkills;
    if (f.left != f.slot) return Verdict::OtherSkill;
    return Verdict::Go;
}

inline const char* Name(Verdict v) {
    switch (v) {
        case Verdict::Go: return "sent";
        case Verdict::NoHero: return "no hero in the client's units";
        case Verdict::HeroMode: return "hero's mode (dead, in a portal, in town)";
        case Verdict::NoMonster: return "the monster is not in the client's units";
        case Verdict::MonsterDying: return "monster dying or dead";
        case Verdict::NoSkills: return "the skills could not be read";
        case Verdict::OtherSkill: return "the left skill is not the attack slot's - the key path takes it";
    }
    return "?";
}

// ---- [gameplay] melee_direct_client_cleave: the extra hits ----

// An extra touch: the hero's attack from a direct hit on ANOTHER monster is still under way (less than the attack time ago). The first
// monster of a swing is never one. With the cleave box (and its rules) it is an extra hit; without, the normal attack - the game's one
// attack at a time.
inline bool InAttack(uint64_t lastAt, uint32_t lastMonster, uint32_t monster, uint64_t now, uint64_t attackMs) {
    return lastAt && monster != lastMonster && now >= lastAt && now - lastAt < attackMs;
}

// The plugin network's channel (D2RLoader adds the plugin's id and scope) and the one message: "strike monster M for hero H".
constexpr uint16_t kChannel = 0x4448;                   // "DH"
constexpr uint64_t kToken = 0x3143485643523244ull;      // "D2RVCHC1": must match exactly on the client and the host
constexpr uint16_t kMsgStrike = 1;
constexpr uint8_t kStrikeVersion = 1;
constexpr uint32_t kStrikeBytes = 9;                    // version, hero id, monster id (little endian)
inline uint32_t BuildStrike(uint32_t hero, uint32_t monster, uint8_t* b, uint32_t cap) {
    if (!b || cap < kStrikeBytes) return 0;
    b[0] = kStrikeVersion;
    for (int i = 0; i < 4; ++i) {
        b[1 + i] = static_cast<uint8_t>(hero >> (8 * i));
        b[5 + i] = static_cast<uint8_t>(monster >> (8 * i));
    }
    return kStrikeBytes;
}
inline bool ParseStrike(const void* data, uint32_t size, uint32_t* hero, uint32_t* monster) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || size != kStrikeBytes || b[0] != kStrikeVersion) return false;
    uint32_t h = 0, m = 0;
    for (int i = 0; i < 4; ++i) {
        h |= static_cast<uint32_t>(b[1 + i]) << (8 * i);
        m |= static_cast<uint32_t>(b[5 + i]) << (8 * i);
    }
    if (!m) return false;
    *hero = h;
    *monster = m;
    return true;
}

// The host: each peer's hero, bound at its first request; a later request from that peer naming another hero is refused (a peer
// strikes only for its own hero). D2RLoader's peer -> player handle names no unit, so the hero id comes in the message.
class PeerHeroes {
 public:
    static constexpr int kSlots = 8;
    bool Accept(uint64_t peer, uint32_t hero) {
        if (!peer || !hero) return false;
        Slot* free = nullptr;
        for (Slot& s : slots_) {
            if (s.peer == peer) return s.hero == hero;
            if (s.hero == hero) return false;   // another peer's hero
            if (!s.peer && !free) free = &s;
        }
        if (!free) return false;
        *free = {peer, hero};
        return true;
    }
    void Forget(uint64_t peer) { for (Slot& s : slots_) if (s.peer == peer) s = {}; }
    void Clear() { for (Slot& s : slots_) s = {}; }
 private:
    struct Slot { uint64_t peer = 0; uint32_t hero = 0; };
    Slot slots_[kSlots];
};

// ---- the game's side (vr/melee_direct_client.cpp) -----------------------------------------------------------------------------

// D2RLoaderLoadPlugin.
void Init(const D2RL::PluginContext* ctx);
// [gameplay] melee_direct_client, from the ini (every read): 0 off (nothing is asked of the game, the key path as before), 1 by the
// rules (Settings' "Direct hit: By the rules"; the old tick box), 2 every accepted touch (Settings' "Fully on": no rules, no cleave).
void SetMode(int mode);
bool Wanted();
// Single player and the host with [gameplay] melee_direct 2 (by the rules): the same rules for a touch whose direct hit is the server's
// call (melee_direct.cpp). True: strike it (*extra: an extra hit of the cleave), noted as a direct hit; false: the normal attack.
bool HostRules(uint32_t monsterId, const Touch& t, bool twoHanded, bool twoHandsOn, bool armed, bool tell, uint64_t nowMs, bool* extra);
// [gameplay] melee_direct_client_min_speed (m/s), _min_force (N s), _min_length (m) - every read.
void SetMins(const Mins& m);
// The weight model must measure the strikes for the force rule (vrcam's weight thread, strike measuring on).
bool WantsForce();
// [gameplay] melee_direct_client_cleave (every read): as a joined player, the extra hits are asked of the host; as the host, the
// joined players' requests are taken (TakeRemote). The channel itself is registered at the plugin's load only (InitNet).
void SetCleave(bool on);
// D2RLoaderLoadPlugin: the plugin network's channel, registered now or never this run (D2RLoader asks for it at the load) - when
// [gameplay] melee_direct_client_cleave is on in the ini then. False: no channel (the extra hits are plain commands).
bool InitNet(const D2RL::PluginContext* ctx, bool wanted);
void ShutdownNet();
// The host, the timer's thread: a joined player's extra hit to strike (his hero, the monster). False: none waiting.
bool TakeRemote(uint32_t* hero, uint32_t* monster);
// The timer's thread, every tick of hit by touch: the proofs of the code (tried again until found), the role, and - while the
// option is on, F4 and a joined game - the UI thread's look at the skills every kLookMs. `serverHere`: melee_touch learned a
// server game of ours in this process (single player, host).
// `heroModeKnown` / `heroMode`: the hero's client unit's mode, read this tick (the attack's time is measured from it).
void Tick(uint64_t nowMs, bool serverHere, bool f4, bool heroModeKnown, uint32_t heroMode);
// The timer's thread, when a touch is accepted: the command (true: queued for the UI thread - the caller presses nothing) or
// the key path (false: the normal attack). `melee` false for a thrown weapon; `armed` a melee skill armed for the press; `t` the touch as
// measured; `twoHanded` the weapon. `tell`: the touch's verdict is logged (one short line; the caller limits how often - a log line costs a frame).
bool Send(uint32_t heroId, uint32_t monsterId, int hand, bool melee, bool armed, const Touch& t, bool twoHanded, bool twoHandsOn, bool tell, uint64_t nowMs);
// The timer's thread, when the view is not F4: the queued commands are dropped (none is sent late).
void Flush();
// The timer's thread: a queued command the UI thread found it should not send for the skills (the left skill is no longer
// the A slot's): the touch goes the key path after all. True with the monster and hand.
bool TakeFallback(uint32_t* monsterId, int* hand);

}   // namespace meleedirectclient
