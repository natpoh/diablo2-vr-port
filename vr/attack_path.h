// attack_path.h - [gameplay] flat_attack_path: the hero moves with W A S D while he swings. EXPERIMENTAL prototype, off by default, changes how the
// game plays. The hero stays in the game's own attack mode with its whole animation (no cut, no click held back), only his path goes on, as
// Whirlwind's does; the blow, its roll, its damage and the attack rate stay the game's. Once the hero walked in an attack, who the blow is for is decided
// at the action frame: the monster clicked while it still stands before the camera, else the nearest hostile there; nobody there, the blow goes into the
// air. An attack with no walk in it is the game's own, at what was clicked. The game's own reach test and
// hit roll then decide. One rule: who hosts decides. In a TCP/IP game the host's tick holds for everyone with the mod, a joined player's own tick means nothing: his
// client moves its hero as it does here, says on the plugin's channel who stands before its camera, and the host's server does the rest. Pure: no game, no Windows, no clock; the unit test is cleanroom/tests/attack_path_test.cpp, the game's side vr/attack_path_wire.cpp.
// [gameplay] move_during_skills (2026-11-10, EXPERIMENTAL, off) is the same walk through the special moves (Zeal, Double Swing, Frenzy, Bash ...) on its own
// key; which key a swing goes by is the used skill's (KindOf below). Player texts say the hero MOVES during a special move; how it is done (his path goes on
// in the attack mode) stays in the code and docs/notes/move_during_skills.md.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace attackpath {

constexpr int kTickCode = 0x7A;              // our own code of a MODECHANGE event: the path's step of a frame, never reaches the game's handler
constexpr uint32_t kSkillMovesItself = 0x1001;   // a used skill's flags: 1 its own path (Whirlwind, Charge), 0x1000 Charge
constexpr int kBaseWalk = 6 << 8, kBaseRun = 9 << 8;   // CharStats' walk and run velocity, shifted as the path has them
constexpr float kPickReachCells = 12.0f;     // no farther than this is looked at; the game's own reach test then decides
constexpr size_t kCandidates = 8;

// The key: 0 off, 1 at a walk always, 2 at a walk or a run as the Run toggle says.
inline bool RunAllowed(int key) { return key >= 2; }

// The stick on the game's screen (x right, y up) -> the grid: (x, -y) turned by -45 degrees (docs/move_recon.md section 3).
inline void ScreenToGrid(float sx, float sy, float* gx, float* gy) {
    constexpr float k = 0.70710678f;
    *gx = (sx - sy) * k;
    *gy = (-sx - sy) * k;
}

// A velocity seen while walking or running, as a percentage of its base (the speed bonuses): the same bonuses hold in the attack.
inline int PercentOf(int velocity, bool run) {
    const int base = run ? kBaseRun : kBaseWalk;
    return velocity > 0 ? velocity * 100 / base : 100;
}
inline int VelocityFor(int percent, bool run) {
    const int p = std::clamp(percent, 25, 400);
    return (run ? kBaseRun : kBaseWalk) * p / 100;
}

inline bool PlainAttackMode(uint32_t mode) { return mode == 7 || mode == 8 || mode == 11; }

// ---- [gameplay] move_during_skills: which tick a swing goes by is decided by the USED SKILL (0x34BA40), not by the mode alone -------------
// (2026-11-10, Serg: past early Act 1 every fight is special moves, moving during the plain Attack alone is of little use.) Until 0.211 the
// modes 7 / 8 / 11 let in every skill that plays attack 1 / 2 or the throw (Bash, Stun, Berserk, Zeal ...) under flat_attack_path. Now:
//   Plain  - the Attack and the Throw (and the left hand's two, and a skill not read): [gameplay] flat_attack_path, VR F4 [gameplay] quick_blow;
//   Single - any other skill with one blow an animation in attack 1 / 2 or the throw (Bash, Stun, Concentrate, Berserk, Sacrifice, Vengeance,
//            Conversion, Power Strike, the javelin and bow skills ...): [gameplay] move_during_skills; in VR F4 quick_blow too (its tick has
//            promised Bash and the like since 0.204, and it is on by default);
//   Series - several blows in one attack: Zeal and Fury (attack 1 started again after each blow: their server do-function 12 / 13 calls
//            0x4914E0, which drops every pending event of the hero and queues the frames anew), Double Swing and Frenzy (the sequence mode 18,
//            [unit+0x40], one schedule for both hands): [gameplay] move_during_skills only.
//   None   - skills that move by themselves (kSkillMovesItself; Charge, Leap, Leap Attack, Whirlwind, Dragon Flight by id as well), every
//            other skill of mode 18 (Jab, Fend, Strafe, Double Throw, Dragon Talon ... - not looked at yet), casts (10), kicks (12), S1-S4.
// The skill's id: Skills.txt's row ([skill+0], 0x33CBC0), its first word (the game's own getter 0x33E080). Its server do-function: the
// row's word +0x4E (the dispatch 0x43ACB0 reads it the same way).
constexpr int32_t kSkillNotRead = -1, kSkillAttack = 0, kSkillThrow = 2, kSkillLeftThrow = 3, kSkillLeftSwing = 4;
constexpr int32_t kSkillZeal = 106, kSkillCharge = 107, kSkillLeap = 132, kSkillDoubleSwing = 133, kSkillLeapAttack = 143, kSkillFrenzy = 147,
                  kSkillWhirlwind = 151, kSkillFury = 248, kSkillDragonFlight = 275;
constexpr uint32_t kModeSequence = 18;
enum class Move : uint8_t { None = 0, Plain = 1, Single = 2, Series = 3 };
// The do-functions that take the blow's target from the skill itself ([skill+0x2C] type, [skill+0x30] id) and start the frames again after
// each blow (0x557C10 = 12, 0x5568E0 = 13 - Zeal / Fury; read in 3.3.94995). Every other melee do-function reads the path's unit (0x48FE20).
inline bool TargetInSkill(int doFunc) { return doFunc == 12 || doFunc == 13; }
inline bool PlainSkill(int32_t id) { return id < 0 || id == kSkillAttack || id == kSkillThrow || id == kSkillLeftThrow || id == kSkillLeftSwing; }
inline bool SequenceIn(int32_t id) { return id == kSkillDoubleSwing || id == kSkillFrenzy; }
inline bool TwoHands(int32_t id) { return SequenceIn(id); }
inline bool NeverIn(int32_t id) {
    return id == kSkillCharge || id == kSkillLeap || id == kSkillLeapAttack || id == kSkillWhirlwind || id == kSkillDragonFlight;
}
inline Move KindOf(uint32_t mode, int32_t skill, uint32_t flags, int doFunc) {
    if (flags & kSkillMovesItself) return Move::None;
    if (mode == kModeSequence) return SequenceIn(skill) ? Move::Series : Move::None;
    if (!PlainAttackMode(mode)) return Move::None;
    if (PlainSkill(skill)) return Move::Plain;
    if (NeverIn(skill)) return Move::None;
    return TargetInSkill(doFunc) || skill == kSkillZeal || skill == kSkillFury ? Move::Series : Move::Single;
}
// The modes our walk goes on in at all (whatever the skill): attack 1 / 2, the throw, the sequence.
inline bool WalkInMode(uint32_t mode) { return PlainAttackMode(mode) || mode == kModeSequence; }
// The key a kind goes by: the plain one (flat_attack_path; F4: quick_blow), the skills' one (move_during_skills). In VR F4 a single-blow skill
// walks with either (quick_blow has covered Bash and the like since 0.204).
inline int KeyFor(Move m, int plainKey, int skillsKey, bool f4) {
    switch (m) {
    case Move::Plain: return plainKey;
    case Move::Single: return f4 ? std::max(plainKey, skillsKey) : skillsKey;
    case Move::Series: return skillsKey;
    default: return 0;
    }
}
// The host's server takes a walk in a joined hero's attack of this kind (the joined client decides by the host's keys it was told; the host
// takes what any of its ticks covers): plain - flat_attack_path or quick_blow; single - quick_blow or move_during_skills; series - the latter.
inline bool HostTakes(Move m, bool plainOn, bool quickOn, bool skillsOn) {
    switch (m) {
    case Move::Plain: return plainOn || quickOn;
    case Move::Single: return quickOn || skillsOn;
    case Move::Series: return skillsOn;
    default: return false;
    }
}
// A run on the server: the key of that kind allows it (2), or quick_blow (the toggle's walk or run in VR F4).
inline bool ServerRun(Move m, int plainKey, int skillsKey, bool quickOn) {
    if (quickOn) return true;
    return RunAllowed(m == Move::Plain ? plainKey : skillsKey);
}
// A series' blow: the game started its frames again (a new end, our step event gone with the hero's events) - the same attack goes on.
inline bool Rescheduled(int32_t endBefore, int32_t endAfter) { return endAfter != 0 && endAfter != endBefore; }
// What a special move's blow is aimed at when no camera says who stands before the hero (VR F4; a joined hero whose client said nothing
// lately): the monster it was started on (the touch), while it is hostile.
enum class Aim : uint8_t { Front, Started };
inline Aim AimFor(bool own, bool f4, bool peerFrontFresh) { return own ? (f4 ? Aim::Started : Aim::Front) : (peerFrontFresh ? Aim::Front : Aim::Started); }

// The skills' names for the log (the game's own names).
inline const char* SkillName(int32_t id) {
    switch (id) {
    case kSkillAttack: return "Attack";
    case kSkillThrow: return "Throw";
    case 14: return "Power Strike";
    case 19: return "Impale";
    case 24: return "Charged Strike";
    case 34: return "Lightning Strike";
    case 73: return "Poison Dagger";
    case 96: return "Sacrifice";
    case 97: return "Smite";
    case kSkillZeal: return "Zeal";
    case 111: return "Vengeance";
    case 116: return "Conversion";
    case 126: return "Bash";
    case kSkillDoubleSwing: return "Double Swing";
    case 139: return "Stun";
    case 144: return "Concentrate";
    case kSkillFrenzy: return "Frenzy";
    case 152: return "Berserk";
    case 232: return "Feral Rage";
    case 233: return "Maul";
    case 238: return "Rabies";
    case 239: return "Fire Claws";
    case 242: return "Hunger";
    case kSkillFury: return "Fury";
    case 254: return "Tiger Strike";
    case 259: return "Fists of Fire";
    case 260: return "Dragon Claw";
    case 265: return "Cobra Strike";
    case 269: return "Claws of Thunder";
    case 274: return "Blades of Ice";
    case 280: return "Phoenix Strike";
    default: return nullptr;
    }
}
inline const char* MoveName(Move m) {
    switch (m) {
    case Move::Plain: return "plain";
    case Move::Single: return "special move";
    case Move::Series: return "special move, several blows";
    default: return "not ours";
    }
}

// Whether the prototype acts for our hero now.
struct Ask {
    int key = 0;                 // the plain key (flat_attack_path; VR F4: quick_blow's 2)
    int skillsKey = 0;           // move_during_skills' (VR F4: 2)
    bool f4 = false;             // VR first person
    bool flatView = false;       // the monitor's F2 / F3 or VR F1 / F2 / F3, our camera on (VR F4: quick_blow or move_during_skills there)
    bool menu = false;
    bool allowed = false;        // the server is in this process, or the host of this game said yes
    uint32_t mode = 0;           // the hero's mode on this side
    uint32_t skillFlags = 0;     // his used skill's flags
    int32_t skill = kSkillAttack;   // his used skill's id
    int doFunc = 0;              // its server do-function
};
inline Move KindOf(const Ask& a) { return KindOf(a.mode, a.skill, a.skillFlags, a.doFunc); }
inline int KeyOf(const Ask& a) { return KeyFor(KindOf(a), a.key, a.skillsKey, a.f4); }
inline bool Acts(const Ask& a) { return KeyOf(a) > 0 && a.flatView && !a.menu && a.allowed; }
// The kinds our hero may walk in now (a bit per Move): what the server is told between attacks too.
inline uint32_t KindsAllowed(const Ask& a) {
    if (!a.flatView || a.menu || !a.allowed) return 0;
    uint32_t bits = 0;
    for (Move m : {Move::Plain, Move::Single, Move::Series})
        if (KeyFor(m, a.key, a.skillsKey, a.f4) > 0) bits |= 1u << static_cast<int>(m);
    return bits;
}
inline bool KindIn(uint32_t bits, Move m) { return m != Move::None && (bits >> static_cast<int>(m) & 1u) != 0; }
// What a blow takes its target from: a shot or a throw keeps what the attack was started on, a melee blow looks again.
inline bool Ranged(uint32_t mode, uint32_t weaponKind) { return mode == 11 || weaponKind == 2 || weaponKind == 3; }
// A melee blow after a walk: what the attack was started on (the monster clicked) goes first while it still stands before the camera, the rest keep
// their order. (No walk in the attack: the blow is the game's own, nothing is chosen again.)
inline void StartedFirst(uint32_t* ids, size_t n, uint32_t started) {
    for (size_t i = 1; i < n; ++i)
        if (ids[i] == started) {
            for (size_t k = i; k > 0; --k) ids[k] = ids[k - 1];
            ids[0] = started;
            return;
        }
}

// The client's "busy" test before ANY command of the player (0xFFAC0, its only caller the command funnel 0xFABE0; read in 3.3.94995): in
// an attack mode (7, 8, 10-16, 18) it reads the used skill's Skills.txt row (0x97790, 0x2EC a row) and, unless the row's byte +0x27 has
// 0x80 (the mask at 0x1D996EC: bit 31 of the first flags word, "interrupt"), answers "busy" and the command is dropped - not predicted,
// not sent. The Attack, Bash, Double Swing ... may be interrupted; Zeal and Fury may not, so our walk in them never left the client
// (2026-11-11: "0 walks predicted" in a whole session of Zeal on the flat PC, the host's server took one). Only OUR walk of a move in a
// special move goes past that answer (sent from AfterCtrl while the hero acts); every other command - a click, another skill, the game's own
// walk keys - still waits for the series' end, as in the game.
inline bool WalkPastBusy(bool ourWalkInFlight, bool ourHero, bool acts, Move kind) {
    return ourWalkInFlight && ourHero && acts && (kind == Move::Single || kind == Move::Series);
}

// The server's next step: only with a walk asked for lately (a let-go key stops him within a few frames).
constexpr int kStepFrames = 3;
inline bool StepOn(int32_t nowFrame, int32_t lastAskFrame) { return nowFrame - lastAskFrame <= kStepFrames; }

// Who may act: never online; with the server here, always; joined, only with a host that said yes lately.
constexpr uint64_t kPeerFreshMs = 3000, kFrontFreshMs = 600;
inline bool Allowed(bool online, bool serverHere, bool hostSaidYes) { return !online && (serverHere || hostSaidYes); }
// [gameplay] quick_blow: our hero in VR first person walks through the swing with the server here and our tick, or joined with the host's.
inline bool F4Quick(bool f4, bool online, bool serverHere, bool ownQuick, bool hostSaidLately, bool hostF4) {
    if (!f4 || online) return false;
    return serverHere ? ownQuick : hostSaidLately && hostF4;
}
// The key our client goes by: who hosts decides. Ours with the server here; joined, the host's while it keeps saying so.
inline int ClientKey(bool online, bool serverHere, int ownKey, bool hostSaidLately, int hostKey) {
    if (online) return 0;
    if (serverHere) return ownKey;
    return hostSaidLately ? hostKey : 0;
}
inline bool Fresh(uint64_t nowMs, uint64_t atMs, uint64_t forMs) { return atMs && nowMs >= atMs && nowMs - atMs <= forMs; }
// How often the others' clients are told where a hero walking in his attack is (their game sends them nothing for a path in an attack).
constexpr int kSyncFrames = 5;
inline bool SyncNow(int32_t nowFrame, int32_t lastFrame) { return nowFrame - lastFrame >= kSyncFrames; }

// The plugin's channel between a joined client and the host (D2RLoader copies each message; at most 224 bytes).
constexpr uint16_t kChannel = 0x4150;                       // "AP"
// 2: [gameplay] move_during_skills' key in the answer (an older client and host no longer connect: the plugin's DLL must match anyway).
constexpr uint64_t kToken = 0x4150415448000002ull;          // the protocol's version: client and host connect only when it is the same
enum : uint16_t { kMsgHello = 1, kMsgFront = 2, kMsgAllow = 3 };
struct HelloMsg { uint32_t hero; };                         // client -> host, once a second with the tick on: "this is my hero"
struct AllowMsg { uint32_t key; };                          // host -> client, the answer: the host's key (0: no) | kAllowF4 | skills' key << 8
// [gameplay] quick_blow on the host: the swing does not stop the joined heroes in VR first person either (vr/quick_blow.h).
constexpr uint32_t kAllowF4 = 0x10;
// [gameplay] move_during_skills on the host: its key (0 off, 1 walk, 2 walk or run) in bits 8-11.
constexpr int kAllowSkillsShift = 8;
inline int AllowKey(uint32_t allow) { return static_cast<int>(allow & 0xF); }
inline bool AllowF4(uint32_t allow) { return (allow & kAllowF4) != 0; }
inline int AllowSkills(uint32_t allow) { return static_cast<int>(allow >> kAllowSkillsShift & 0xF); }
inline uint32_t MakeAllow(int key, bool f4, int skillsKey) {
    return static_cast<uint32_t>(std::clamp(key, 0, 2)) | (f4 ? kAllowF4 : 0u) | static_cast<uint32_t>(std::clamp(skillsKey, 0, 2)) << kAllowSkillsShift;
}
struct FrontMsg { uint32_t hero; uint32_t n; uint32_t ids[kCandidates]; };   // client -> host: who stands before its camera, the nearest first
inline bool ParseHello(const void* d, uint32_t size, HelloMsg* m) {
    if (!d || size != sizeof *m) return false;
    *m = *static_cast<const HelloMsg*>(d);
    return m->hero != 0;
}
inline bool ParseAllow(const void* d, uint32_t size, AllowMsg* m) {
    if (!d || size != sizeof *m) return false;
    *m = *static_cast<const AllowMsg*>(d);
    return AllowKey(m->key) <= 2 && AllowSkills(m->key) <= 2 && (m->key & ~(kAllowF4 | 0xFu | 0xFu << kAllowSkillsShift)) == 0;
}
inline bool ParseFront(const void* d, uint32_t size, FrontMsg* m) {
    if (!d || size != sizeof *m) return false;
    *m = *static_cast<const FrontMsg*>(d);
    return m->hero != 0 && m->n <= kCandidates;
}
inline bool SameFront(const uint32_t* a, size_t an, const uint32_t* b, size_t bn) {
    if (an != bn) return false;
    for (size_t i = 0; i < an; ++i) if (a[i] != b[i]) return false;
    return true;
}

struct Candidate { uint32_t id = 0; float x = 0.0f, z = 0.0f; };

// The ids before (fx, fz) from (hx, hz), the nearest first, at most `cap`. All in the same units; forward of unit length.
inline size_t PickFront(float hx, float hz, float fx, float fz, float cosHalf, float reach, const Candidate* in, size_t n, uint32_t* out, size_t cap) {
    struct R { uint32_t id; float d2; } kept[kCandidates];
    size_t m = 0;
    const float reach2 = reach * reach;
    for (size_t i = 0; i < n; ++i) {
        const float dx = in[i].x - hx, dz = in[i].z - hz, d2 = dx * dx + dz * dz;
        if (d2 > reach2) continue;
        if (d2 > 1e-6f && (dx * fx + dz * fz) < cosHalf * std::sqrt(d2)) continue;   // (on top of him: he counts as before him)
        R r{in[i].id, d2};
        size_t at = m;
        while (at > 0 && kept[at - 1].d2 > r.d2) --at;
        if (at >= kCandidates) continue;
        const size_t last = std::min(m, kCandidates - 1);
        for (size_t k = last; k > at; --k) kept[k] = kept[k - 1];
        kept[at] = r;
        if (m < kCandidates) ++m;
    }
    const size_t count = std::min(m, cap);
    for (size_t i = 0; i < count; ++i) out[i] = kept[i].id;
    return count;
}

}  // namespace attackpath
