// parry_wire.cpp - [weight] parry_*: the pulse, the knock and the log of a parry (vr/parry_wire.h; the decisions are vr/parry.h's).
// Reading only, as far as the game goes: the monsters' modes come with the bodies hit by touch's scan reads (unit_scan.h), the hero's mode from
// vrcam (HeroAnimMode); the pulse is the controllers', the knock moves only the drawn weapon (weight::Knock). A clash sounds ([weight] clash_sound): the
// game's own block sound through the game's own calls (vr/clash_sound.h), as loud as the two met hard - their closing speed, the monster's weapon's
// against the hand's (vr/clash_loud.h: under 0.5 m/s neither heard nor felt; the pulse by the same rule). A spark has no way to be drawn yet.

#include "parry_wire.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <mutex>

#include "clash_loud.h"
#include "clash_sound.h"
#include "d2r_vr_state.h"
#include "haptics.h"
#include "view_gate.h"
#include "weight.h"

namespace parrywire {
namespace {

std::atomic<LogFn> g_log{nullptr};
std::mutex g_mutex;   // everything below (melee_touch's and shield_smite's calls, vrcam's timer)
Settings g_set;
parry::Clashes g_clashes;      // a hand's weapon (key id * 4 + hand) or the shield (id * 4 + 2 + hand) against a monster's weapon
parry::WeaponMotion g_motion;  // each monster weapon capsule's velocity (key: its id and bone)
parry::WeaponMotion g_own;     // the hands' blades' and the shield's own velocity (key: kOwnKey + hand, + 2 for the shield): a clash's speed is the two's
constexpr uint32_t kOwnKey = 0xB1ADE000u;
parry::BlockWatch g_block;
constexpr float kInf = std::numeric_limits<float>::infinity();
float g_nearGap = kInf;   // the nearest the weapon or the shield came to an attacking monster's weapon lately (the block shown's strength)
uint64_t g_nearAt = 0;
uint32_t g_nearId = 0;
bool g_nearShield = false;
int g_claws = 0, g_weaponHand = -1, g_shieldHand = -1;   // the last pose's: claws in hand, the weapon's hand; the shield's hand
uint64_t g_bladesAt = 0, g_shieldAt = 0;

void LogF(const char* fmt, ...) {
    const LogFn log = g_log.load();
    if (!log) return;
    char b[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    log(b);
}

haptics::Request Pulse(int hand, const parry::Feel& f) {   // hand: 0 right, 1 left; lifted for a weak motor (the PS VR2's)
    const haptics::Request r = haptics::ForMotor(haptics::CurrentMotor(), f.amplitude, f.ms);
    if (r.amplitude > 0.0f) haptics::Pulse(haptics::FromSkel(hand), r.amplitude, static_cast<int>(r.durationMs));
    return r;
}

void NoteNear(float gap, uint32_t id, bool shield, uint64_t nowMs) {   // (g_mutex held)
    if (nowMs - g_nearAt > 250 || gap < g_nearGap) {
        g_nearGap = gap;
        g_nearAt = nowMs;
        g_nearId = id;
        g_nearShield = shield;
    }
}

stroke::Vec V(const float p[3]) { return {p[0], p[1], p[2]}; }

// The monsters' weapons in the snapshot, nearest first per monster to `c`: for each monster with a weapon, its nearest weapon capsule's gap and that
// capsule's velocity (fed by the snapshot's own tick: the same snapshot asked again is the same velocity). Calls fn(id, attacking, gap, vel, mode). (g_mutex held.)
template <class Fn>
void EachWeapon(const parry::Capsule& c, const monsterbodies::Snapshot& snap, Fn&& fn) {
    uint32_t ids[monsterbodies::kMax];
    float gaps[monsterbodies::kMax];
    stroke::Vec vels[monsterbodies::kMax];
    int modes[monsterbodies::kMax];
    int n = 0;
    for (int i = 0; i < snap.n; ++i) {
        const monsterbodies::Body& w = snap.b[i];
        if (w.held != 1) continue;
        const parry::Capsule wc{V(w.bottom), V(w.top), w.radius};
        const stroke::Vec centre = (wc.a + wc.b) * 0.5f;
        const uint32_t key = w.id * 31u + static_cast<uint32_t>(w.bone + 1);
        const stroke::Vec vel = g_motion.Feed(key, centre, snap.tickMs);
        const float gap = parry::Gap(c, wc);
        int k = 0;
        while (k < n && ids[k] != w.id) ++k;
        if (k == n) {
            if (n >= monsterbodies::kMax) continue;
            ids[n] = w.id;
            gaps[n] = kInf;
            modes[n] = w.mode;
            ++n;
        }
        if (gap < gaps[k]) { gaps[k] = gap; vels[k] = vel; }
    }
    for (int k = 0; k < n; ++k) fn(ids[k], parry::MonsterAttacking(modes[k]), gaps[k], vels[k], modes[k]);
}

// The clash's sound and pulse, by how fast the two met (vr/clash_loud.h): under the threshold neither. Returns the pulse given (amplitude 0: none).
haptics::Request Clang(const char* what, int hand, float closing, const clashloud::Clash& k, bool* sounded) {
    *sounded = k.heard && clashsound::Play(closing, k.gameVolume, what);
    return k.heard ? Pulse(hand, parry::Feel{k.pulse, static_cast<uint32_t>(k.pulseMs)}) : haptics::Request{};
}

void TellClash(const char* what, int hand, uint32_t id, int mode, float blowSpeed, float closing, const clashloud::Clash& k, bool sounded, const haptics::Request& r, float knockCm) {
    char sound[96];
    if (!k.heard) snprintf(sound, sizeof sound, "too gentle to be heard or felt (under %.1f m/s)", clashloud::kQuietMps);
    else snprintf(sound, sizeof sound, "the game's block sound %u/255 (%.0f %%)%s", k.gameVolume, k.volume * 100.0f, sounded ? "" : " not played ([weight] clash_sound off?)");
    LogF("parry: %c hand's %s met monster %u's weapon in its attack (mode %d, its blow %.1f m/s, the two met at %.1f m/s) - a clash: %s, pulse %.2f for %u ms%s%s", hand ? 'L' : 'R', what, id,
         mode, blowSpeed, closing, sound, r.amplitude, r.durationMs, knockCm > 0.0f ? "" : ", not knocked (",
         knockCm > 0.0f ? "" : "the weapon is not the weight model's, or the blow hardly moves)");
    if (knockCm > 0.0f) LogF("parry: ... the weapon knocked %.0f cm along the blow ([weight] parry_feel)", knockCm);
}

}  // namespace

void SetLog(LogFn fn) { g_log.store(fn); }

void Configure(const Settings& s) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const Settings was = g_set;
    g_set = s;
    if (s.feel != was.feel || s.blockShow != was.blockShow || s.clawBlock != was.clawBlock || s.shieldSmite != was.shieldSmite || s.countsHit != was.countsHit ||
        s.shields != was.shields)
        LogF("parry: the feel %s, the game's block shown %s, the claws' Weapon Block %s, the shield into the blow casts Smite %s, a blow on the weapon counts %s%s",
             s.feel ? "on" : "off", s.blockShow ? "on" : "off", s.clawBlock ? "on" : "off", s.shieldSmite ? "on" : "off", s.countsHit ? "on" : "off",
             s.shields ? "" : " - the monsters' weapons are not there to meet ([weight] monster_bodies and monster_shields): only the block shown works");
    if (!s.feel) g_clashes.Reset();
}
Settings Get() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_set;
}
bool ShieldSmiteWanted() {
    const Settings s = Get();
    return s.shieldSmite && s.shields && viewgate::F4();
}
bool CountsHitWanted() {
    const Settings s = Get();
    return s.countsHit && s.shields && viewgate::F4();
}

bool Attacking(const monsterbodies::Snapshot& snap, uint32_t id) {
    for (int i = 0; i < snap.n; ++i)
        if (snap.b[i].id == id && parry::MonsterAttacking(snap.b[i].mode)) return true;
    return false;
}

void Blades(const Blade* blades, int n, const monsterbodies::Snapshot& snap, uint64_t nowMs) {
    if (!viewgate::F4()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    int claws = 0;
    for (int i = 0; i < n; ++i) claws += blades[i].kind == D2RVR_TYPE_CLAW ? 1 : 0;
    g_claws = claws;
    g_weaponHand = n > 0 ? blades[0].hand : -1;
    g_bladesAt = nowMs;
    if (!g_set.shields) return;
    for (int i = 0; i < n; ++i) {
        const Blade& b = blades[i];
        const stroke::Vec own = g_own.Feed(kOwnKey + static_cast<uint32_t>(b.hand & 1), (b.c.a + b.c.b) * 0.5f, nowMs);
        EachWeapon(b.c, snap, [&](uint32_t id, bool attacking, float gap, stroke::Vec vel, int mode) {
            if (attacking) NoteNear(gap, id, false, nowMs);
            if (!g_clashes.Step(id * 4u + static_cast<uint32_t>(b.hand & 1), attacking, gap, nowMs) || !g_set.feel) return;
            const float speed = stroke::Length(vel), closing = stroke::Length(vel - own);
            const clashloud::Clash loud = clashloud::Of(closing);
            bool sounded = false;
            const haptics::Request r = Clang("a parry with the weapon", b.hand, closing, loud, &sounded);
            const parry::Knock k = parry::KnockOf(vel);
            float knockCm = 0.0f;
            if (k.ok && b.weighted) {
                const float dir[3] = {k.dir.x, k.dir.y, k.dir.z};
                weight::Knock(dir, k.metres, parry::kKnockMs);
                knockCm = k.metres * 100.0f;
            }
            TellClash("weapon", b.hand, id, mode, speed, closing, loud, sounded, r, knockCm);
        });
    }
}

void Shield(const parry::Capsule& c, int hand, const monsterbodies::Snapshot& snap, uint64_t nowMs) {
    if (!viewgate::F4()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_shieldHand = hand & 1;
    g_shieldAt = nowMs;
    if (!g_set.shields) return;
    const stroke::Vec own = g_own.Feed(kOwnKey + 2u + static_cast<uint32_t>(hand & 1), (c.a + c.b) * 0.5f, nowMs);
    EachWeapon(c, snap, [&](uint32_t id, bool attacking, float gap, stroke::Vec vel, int mode) {
        if (attacking) NoteNear(gap, id, true, nowMs);
        if (!g_clashes.Step(id * 4u + 2u + static_cast<uint32_t>(hand & 1), attacking, gap, nowMs) || !g_set.feel) return;
        const float closing = stroke::Length(vel - own);
        const clashloud::Clash loud = clashloud::Of(closing);
        bool sounded = false;
        const haptics::Request r = Clang("a parry with the shield", hand, closing, loud, &sounded);
        TellClash("shield", hand, id, mode, stroke::Length(vel), closing, loud, sounded, r, -1.0f);
    });
}

void Tick(int heroMode, int heroClass, uint64_t nowMs) {
    if (!viewgate::F4()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_block.Step(heroMode)) return;
    // the Assassin's Weapon Block: claws in both hands (the skill needs two) and no shield - its own tick
    const bool claws = heroClass == 6 && g_claws >= 2 && nowMs - g_bladesAt < 500;
    if (!(claws ? g_set.clawBlock : g_set.blockShow)) return;
    const bool isNear = nowMs - g_nearAt <= 250 && std::isfinite(g_nearGap);
    const float strength = parry::BlockStrength(isNear ? g_nearGap : std::numeric_limits<float>::quiet_NaN());
    const parry::Feel f = parry::ClashFeel(strength);
    haptics::Request r;
    const char* by = "the weapon";
    if (claws) {
        r = Pulse(0, f);
        Pulse(1, f);
        by = "both claws";
    } else if (g_shieldHand >= 0 && nowMs - g_shieldAt < 500) {
        r = Pulse(g_shieldHand, f);
        by = "the shield";
    } else {
        r = Pulse(g_weaponHand >= 0 ? g_weaponHand : 0, f);
    }
    if (isNear)
        LogF("parry: the game blocked (%s) - shown as a parry with %s: pulse %.2f for %u ms (the %s %.0f cm from monster %u's weapon in its attack)", claws ? "the claws' Weapon Block" : "its block",
             by, r.amplitude, r.durationMs, g_nearShield ? "shield" : "weapon", std::max(g_nearGap, 0.0f) * 100.0f, g_nearId);
    else
        LogF("parry: the game blocked (%s) - shown as a parry with %s: pulse %.2f for %u ms (no attacking monster's weapon seen isNear)", claws ? "the claws' Weapon Block" : "its block", by,
             r.amplitude, r.durationMs);
}

}  // namespace parrywire
