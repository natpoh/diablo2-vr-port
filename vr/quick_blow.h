// quick_blow.h - [gameplay] quick_blow: VR first person (F4), the game's own attack with no wait and no stop. EXPERIMENTAL, on by default since 0.204,
// changes how the game plays (Serg, 2026-11-05: «весь функционал под отдельной галочкой»; «чтобы эффекта замирания не было при каждом ударе
// это важно для вр»). Three parts under one tick:
//   1. the blow on the animation's first frame: the server's scheduler of a mode's frame events 0x491240 (called by the attack modes' start
//      0x42A8B0) reads the AnimData record at [unit+0x70] and queues each event of the frames as a MODECHANGE event on its game frame; for a
//      plain attack (modes 7 / 8) of a hero it is handed, for that call only, a copy of the record whose events are moved so the first action
//      lands on the ATTACK'S START FRAME (ShiftEvents) - the first frame the scheduler reads. The scheduler does not start at frame 0: it starts
//      at 0x34B760(unit), the game's start frame of an attack (modes 7 / 8 / 15 / 16) from its table [weapon class][hero class] at 0x1D00E20:
//      the Amazon and the Sorceress start at frame 1 bare-handed and at 2 with weapon classes 2 - 6, every other class at 0. It reads the
//      frames from there on (that frame included, queued for the next server frame); a flag below it is never read. Moved to frame 0
//      (0.204 - 0.206.6) or 1 (0.206.7) the Amazon's sword blow (AMA11HS / AMA21HS, start frame 2) was below her start and never fired: no
//      trigger, no roll - [debug] quick_blow_probe 0.206.7: "MODECHANGE pending for 0" = no event queued at all. The Barbarian starts at 0, so
//      his blows landed. The blow now lands one server frame (~40 ms) after the start for every class. The animation's
//      length ([unit+0x64], not the record) and its end event stay: the attack rate, IAS and the gates are the game's;
//   2. the swing does not stop the hero: vr/attack_path.h's walk in the attack (the hero's path goes on in his attack mode, as Whirlwind's) in
//      F4 too, at a walk or a run as the toggle says; the blow goes to the monster the attack was started on (the one the blade touched);
//   3. the touch is the game's own attack (the key path), never [gameplay] melee_direct's call - but By the rules' extra blows of a two-handed
//      cleave ([gameplay] melee_direct_client_cleave) stay direct: the game has no one-swing-many-monsters of its own.
// Kept the game's: the press, UseSkill and Start (skills, mana, LOS), the hit roll, block, damage, the animation's length and the attack rate.
// Not for bows and crossbows (the shot keeps its draw), throws (mode 11), kicks, the sequences (Zeal, Fury: their own events, [unit+0x40]).
// Who hosts decides: in a TCP/IP game we host, the joined heroes get 1 and 2 as well (vr/host_rules.h); joined, the host's tick holds.
// Pure: no game, no Windows, no clock; the test is cleanroom/tests/quick_blow_test.cpp, the game's side vr/move_attack_wire.cpp (the
// scheduler's hook), vr/attack_path_wire.cpp (the walk), vr/melee_touch.cpp (the route). Read in 3.3.93787: docs/DIRECT_HIT_DAMAGE.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace quickblow {

// AnimData's record (160 bytes, the loader 0x3E3810): the COF's name [8], frames a direction (+8), speed (+0xC), an event byte a frame (+0x10).
constexpr size_t kRecordBytes = 160, kEventsAt = 0x10, kMaxFrames = 144, kFramesAt = 8;
// The event codes the scheduler queues: 1 the attack, 2 the missile (0x42A940 makes the action for both), 3 and 4 the game's others.
inline bool ActionEvent(uint8_t code) { return code == 1 || code == 2; }

// ShiftEvents: nothing moved (no action event in the record's frames, or the start frame past them).
constexpr int kNoAction = -1000;
// The record's events moved so the first action lands on `start` - the attack's start frame, the game's 0x34B760(unit), the first frame its
// scheduler reads: out[i] = in[i + shift], shift = the action's frame - start, emptied where nothing comes. The events keep their order and
// distances (a dual-wield row's second blow too). An action BELOW the start frame (a row another tick cut too early, e.g. quick strike's frame 1
// for the Amazon's start 2) is moved up to it: below it the game never reads it. Returns the shift (> 0 moved earlier, < 0 moved later, 0
// already on the start frame) or kNoAction. `out` gets the whole record (name, frames, speed) either way.
inline int ShiftEvents(const uint8_t* in, uint8_t* out, uint32_t start) {
    std::memcpy(out, in, kRecordBytes);
    uint32_t frames = 0;
    std::memcpy(&frames, in + kFramesAt, sizeof frames);
    const size_t n = frames > 0 && frames <= kMaxFrames ? frames : kMaxFrames;
    if (start >= n) return kNoAction;
    const uint8_t* ev = in + kEventsAt;
    size_t first = n;
    for (size_t i = 0; i < n; ++i)
        if (ActionEvent(ev[i])) { first = i; break; }
    if (first == n) return kNoAction;
    const int shift = static_cast<int>(first) - static_cast<int>(start);
    if (shift == 0) return 0;
    uint8_t* o = out + kEventsAt;
    for (int i = 0; i < static_cast<int>(kMaxFrames); ++i) {
        const int j = i + shift;
        o[i] = j >= 0 && j < static_cast<int>(kMaxFrames) ? ev[j] : 0;
    }
    return shift;
}

// The game's scheduler 0x491240 as read in 3.3.93787 (for the tests and the probe's word): from the start frame S (pos = S << 8), each server
// frame pos += speed and every animation frame pos passed - S itself on the first - is read; an action (1 / 2) is queued for that server frame.
// It stops before the frame pos + speed reaches the length (frames << 8). Returns the server frame offset (1 = the next) of the first action
// queued, 0 none.
inline int FirstActionAt(const uint8_t* record, uint32_t start, int32_t speed) {
    uint32_t frames = 0;
    std::memcpy(&frames, record + kFramesAt, sizeof frames);
    if (speed <= 0) return 0;
    const int64_t length = static_cast<int64_t>(frames) << 8;
    int64_t pos = static_cast<int64_t>(start) << 8;
    int64_t f = start;
    for (int at = 1; pos + speed < length; ++at) {
        pos += speed;
        for (; f <= (pos >> 8); ++f)
            if (f < static_cast<int64_t>(kMaxFrames) && ActionEvent(record[kEventsAt + f])) return at;
    }
    return 0;
}

// The plain attacks (attack 1 / 2): Bash, Stun, Berserk, Concentrate ... use them too (their action is the skill's). Not the throw, the kick,
// the cast, the sequence.
inline bool QuickMode(uint32_t mode) { return mode == 7 || mode == 8; }
// Every weapon but the bow and the crossbow (the weapon kinds of shared/d2r_vr_state.h: 2, 3); bare hands too.
inline bool QuickWeapon(uint32_t kind) { return kind != 2 && kind != 3; }

struct Ask {
    bool on = false;          // our [gameplay] quick_blow (a game whose server runs here: ours decides for every hero in it)
    bool player = false;      // the unit is a hero of this game on our server
    bool own = false;         // ... ours: then only in VR first person
    bool f4 = false;
    uint32_t mode = 0;        // his mode (the attack's: 0x491130 set it before the scheduler runs)
    bool sequence = false;    // [unit+0x40]: the events come from a sequence, not from the record
    uint32_t weapon = 0;      // his weapon's kind on our server
    bool record = false;      // [unit+0x70] read
    bool start = false;       // the attack's start frame read (the game's 0x34B760, found through the scheduler's own call)
};
enum class Why : uint8_t { Off, NotHero, View, Mode, Sequence, Weapon, NoRecord, NoStart, Shift };
inline const char* WhyText(Why w) {
    switch (w) {
    case Why::Off: return "off";
    case Why::NotHero: return "not a hero of this game";
    case Why::View: return "our hero not in VR first person";
    case Why::Mode: return "not a plain attack (attack 1 / 2)";
    case Why::Sequence: return "a sequence (Zeal, Fury ...)";
    case Why::Weapon: return "a bow or crossbow";
    case Why::NoRecord: return "no AnimData record";
    case Why::NoStart: return "the attack's start frame not known (the scheduler not as read)";
    case Why::Shift: return "the blow moved to the attack's start frame";
    }
    return "";
}
// Shift: hand the scheduler the moved copy (if ShiftEvents finds something to move). Everything else: the game's own record.
inline Why Judge(const Ask& a) {
    if (!a.on) return Why::Off;
    if (!a.player) return Why::NotHero;
    if (a.own && !a.f4) return Why::View;
    if (!QuickMode(a.mode)) return Why::Mode;
    if (a.sequence) return Why::Sequence;
    if (!QuickWeapon(a.weapon)) return Why::Weapon;
    if (!a.record) return Why::NoRecord;
    if (!a.start) return Why::NoStart;
    return Why::Shift;
}

// -- [gameplay] quick_blow_faster + quick_blow_min_ms: strike faster than the animation (Serg, 2026-11-05: «ползунок минимального ожидания и
// галочку, бить быстрее анимации, чтобы удары проходили") ------------------------------------------------------------------------------
// The game runs one attack at a time: a new attack asked for during one goes through the server's attack-mode gate 0x42D840, which reads the
// frames left of the animation (event 1): at most 1 - in now (0); 2-5 - "not yet" (4), the caller keeps it; more - refused (1). With this tick
// a quick attack whose blow is already out (its frame trigger noted it) lets the next attack in (0) once `minMs` passed since that blow: the
// rest of the animation is cut, the new one starts and its own blow lands on its first frame. Goes past the game's attack rate (IAS): the
// least wait is the only pace. Not before the blow (a blow is never lost), never for the gate's other answers.
constexpr uint32_t kFrameMs = 40;   // the server's frame
constexpr int kGateIn = 0, kGateRefused = 1, kGateNotYet = 4;
inline int32_t MinFrames(uint32_t ms) { return static_cast<int32_t>((ms + kFrameMs - 1) / kFrameMs); }
struct FasterAsk {
    bool on = false;          // [gameplay] quick_blow and quick_blow_faster
    int original = 0;         // the gate's own answer
    bool player = false, own = false, f4 = false;
    uint32_t mode = 0;        // the hero's mode now
    uint32_t newMode = 0;     // the mode asked for
    bool target = false;      // a skill target came with it (an attack on a unit; not the stick's walk)
    bool quickAttack = false; // the attack he is in had its blow moved (or on frame 0 already) by the tick
    bool blowOut = false;     // ... and its blow is out (noted at the frame trigger, this attack's end)
    int32_t nowFrame = 0, blowFrame = 0, endFrame = 0;
    uint32_t minMs = 0;       // [gameplay] quick_blow_min_ms
};
enum class FastWhy : uint8_t { Game, Off, NotHero, View, NotAttack, NotQuick, NotOut, TooSoon, Let };
inline const char* FastWhyText(FastWhy w) {
    switch (w) {
    case FastWhy::Game: return "the game's own answer";
    case FastWhy::Off: return "off";
    case FastWhy::NotHero: return "not a hero of this game";
    case FastWhy::View: return "our hero not in VR first person";
    case FastWhy::NotAttack: return "not a plain attack on a unit out of a plain attack";
    case FastWhy::NotQuick: return "the attack he is in is not a quick one";
    case FastWhy::NotOut: return "its blow is not out yet";
    case FastWhy::TooSoon: return "the least wait since the blow has not passed";
    case FastWhy::Let: return "let in: the next attack cuts the animation";
    }
    return "";
}
inline FastWhy JudgeFaster(const FasterAsk& a) {
    const int32_t left = a.endFrame - a.nowFrame;
    // only the gate's answers about the animation's frames: 4 with 2-5 left, 1 with more (its 1 for other reasons stays)
    if (!((a.original == kGateNotYet && left >= 2 && left <= 5) || (a.original == kGateRefused && left > 5))) return FastWhy::Game;
    if (!a.on) return FastWhy::Off;
    if (!a.player) return FastWhy::NotHero;
    if (a.own && !a.f4) return FastWhy::View;
    if (!a.target || !QuickMode(a.newMode) || !QuickMode(a.mode)) return FastWhy::NotAttack;
    if (!a.quickAttack) return FastWhy::NotQuick;
    if (!a.blowOut || a.nowFrame < a.blowFrame) return FastWhy::NotOut;
    if (a.nowFrame - a.blowFrame < MinFrames(a.minMs)) return FastWhy::TooSoon;
    return FastWhy::Let;
}
constexpr uint32_t kMinMsLo = 0, kMinMsHi = 1000, kMinMsDefault = 200;
inline uint32_t ClampMinMs(int ms) { return ms < static_cast<int>(kMinMsLo) ? kMinMsLo : ms > static_cast<int>(kMinMsHi) ? kMinMsHi : static_cast<uint32_t>(ms); }

// -- the quick attacks of the heroes on our server (the scheduler notes, the trigger and the gate ask): the server's thread only ----------
constexpr int kMaxHeroes = 8;
class QuickNotes {
public:
    void Note(uint32_t hero, int32_t endFrame) {
        Slot* s = &slots_[0];
        for (Slot& x : slots_) {
            if (x.hero == hero) { s = &x; break; }
            if (x.used < s->used) s = &x;
        }
        s->hero = hero;
        s->end = endFrame;
        s->used = ++clock_;
    }
    // The attack ending at `endFrame` is this hero's last quick one.
    bool Is(uint32_t hero, int32_t endFrame) const {
        for (const Slot& x : slots_) if (x.used && x.hero == hero) return x.end == endFrame;
        return false;
    }

private:
    struct Slot { uint32_t hero = 0; int32_t end = 0; uint64_t used = 0; };
    Slot slots_[kMaxHeroes];
    uint64_t clock_ = 0;
};

// A touch Direct hit planned: does it go direct (vr/melee_touch.cpp Approve)? `scope`: [gameplay] melee_direct_scope (shared/direct_hit_mode.h,
// Serg 2026-11-06) - 0 "Two-handed extras": only By the rules' extra blow of a two-handed cleave, quick blow or not; 1 "By the rules": every touch
// the rules pass, quick blow or not; -1 (missing, an ini of before): what 0.204 did - with quick blow the game's key but the cleave's extras.
// Always (byRules false): every touch.
inline bool TouchDirect(bool quick, bool byRules, bool extra, int scope = -1) {
    if (!byRules) return true;
    if (scope == 0) return extra;
    if (scope == 1) return true;
    return !quick || extra;
}

}  // namespace quickblow
