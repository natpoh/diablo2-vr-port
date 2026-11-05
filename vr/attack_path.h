// attack_path.h - [gameplay] flat_attack_path: the hero moves with W A S D while he swings. EXPERIMENTAL prototype, off by default, changes how the
// game plays. The hero stays in the game's own attack mode with its whole animation (no cut, no click held back), only his path goes on, as
// Whirlwind's does; the blow, its roll, its damage and the attack rate stay the game's. Once the hero walked in an attack, who the blow is for is decided
// at the action frame: the monster clicked while it still stands before the camera, else the nearest hostile there; nobody there, the blow goes into the
// air. An attack with no walk in it is the game's own, at what was clicked. The game's own reach test and
// hit roll then decide. One rule: who hosts decides. In a TCP/IP game the host's tick holds for everyone with the mod, a joined player's own tick means nothing: his
// client moves its hero as it does here, says on the plugin's channel who stands before its camera, and the host's server does the rest. Pure: no game, no Windows, no clock; the unit test is cleanroom/tests/attack_path_test.cpp, the game's side vr/attack_path_wire.cpp.
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

// Whether the prototype acts for our hero now.
struct Ask {
    int key = 0;
    bool flatView = false;       // the monitor's F2 / F3 or VR F1 / F2 / F3, our camera on
    bool menu = false;
    bool allowed = false;        // the server is in this process, or the host of this game said yes
    uint32_t mode = 0;           // the hero's mode on this side
    uint32_t skillFlags = 0;     // his used skill's flags
};
inline bool PlainAttackMode(uint32_t mode) { return mode == 7 || mode == 8 || mode == 11; }
inline bool Acts(const Ask& a) {
    return a.key > 0 && a.flatView && !a.menu && a.allowed && PlainAttackMode(a.mode) && !(a.skillFlags & kSkillMovesItself);
}
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
constexpr uint64_t kToken = 0x4150415448000001ull;          // the protocol's version: client and host connect only when it is the same
enum : uint16_t { kMsgHello = 1, kMsgFront = 2, kMsgAllow = 3 };
struct HelloMsg { uint32_t hero; };                         // client -> host, once a second with the tick on: "this is my hero"
struct AllowMsg { uint32_t key; };                          // host -> client, the answer: the host's key (0: no) | kAllowF4
// [gameplay] quick_blow on the host: the swing does not stop the joined heroes in VR first person either (vr/quick_blow.h). An older client
// refuses the answer (its key past 2) and stays as in the game.
constexpr uint32_t kAllowF4 = 0x10;
inline int AllowKey(uint32_t allow) { return static_cast<int>(allow & 0xF); }
inline bool AllowF4(uint32_t allow) { return (allow & kAllowF4) != 0; }
struct FrontMsg { uint32_t hero; uint32_t n; uint32_t ids[kCandidates]; };   // client -> host: who stands before its camera, the nearest first
inline bool ParseHello(const void* d, uint32_t size, HelloMsg* m) {
    if (!d || size != sizeof *m) return false;
    *m = *static_cast<const HelloMsg*>(d);
    return m->hero != 0;
}
inline bool ParseAllow(const void* d, uint32_t size, AllowMsg* m) {
    if (!d || size != sizeof *m) return false;
    *m = *static_cast<const AllowMsg*>(d);
    return AllowKey(m->key) <= 2 && (m->key & ~(kAllowF4 | 0xFu)) == 0;
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
