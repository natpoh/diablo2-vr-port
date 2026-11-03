// knock_down.cpp - see knock_down.h.

#include "knock_down.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "death_ragdoll.h"     // [weight] death_ragdoll: a humanoid the game kills falls as our ragdoll and lies as it fell
#include "fall_lib.h"
#include "walk_home.h"        // a body home again goes back to the game's own animation
#include "grab_move.h"        // [weight] grab_moves_unit: a carried or thrown body moved by the game's own knockback
#include "grab_move_wire.h"
#include "head_shadow.h"
#include "held_bounds.h"
#include "knock_pose.h"
#include "knock_props.h"
#include "knock_trigger.h"
#include "melee_touch.h"
#include "monster_bodies.h"
#include "parry.h"
#include "skeletons.h"
#include "view_gate.h"

static_assert(dollpose::kKnockJoints == D2RF_JOINTS, "dollpose::Knock holds the knock-down's joints");

namespace knockdown {

namespace {

std::atomic<LogFn> g_log{nullptr};
template <class... A> void LogF(const char* fmt, A... a) {
    const LogFn fn = g_log.load();
    if (!fn) return;
    char b[900];
    snprintf(b, sizeof b, fmt, a...);
    fn(b);
}

constexpr uint64_t kInfoFreshMs = 300;    // a body's last read older than this: no knock-down from a push on it, no hold
constexpr uint64_t kGoneMs = 1000;        // a body down whose rig has not been read this long: let go of
constexpr float kWalkedOffM = 0.6f;       // the game walked the body this far from where it fell while it falls or lies: it gets up now
constexpr int kMaxInfo = 24;

std::atomic<bool> g_on{false}, g_throwOn{false}, g_impaleOn{false}, g_deathOn{false};
std::atomic<bool> g_deathFlatOn{false};   // [weight] death_ragdoll_flat: the same falls in the views with no hands of ours (VR F1-F3, the flat game), by the game's swing
std::mutex g_mutex;   // (the settings, the bodies read, the pending knocks - between the weight model's thread and the timer's)
Settings g_set;
knocktrigger::Tracker g_tracker;
struct Info {          // a body as the timer last read it (render metres)
    uint32_t id = 0;
    uint64_t atMs = 0;
    knocktrigger::Body body;
    float joint[D2RF_JOINTS][3] = {};
    uint32_t have = 0;
    float vel[3] = {};
    char model[48] = {};
    bool hostile = true;
    bool swap = false;   // its left and right handed to the library swapped (knockpose::Mirrored: its model drawn mirrored)
    int toes = -1, knees = -1;   // which way its toe bones and its knees say it faces against the library's frame (the log's word: never used)
    uintptr_t self = 0;          // [weight] death_ragdoll: its skeleton (the instance, where its model stood - world units, its bones): the one it dies in
    float root[3] = {};
    int bones = 0;
};
Info g_info[kMaxInfo];
int g_nInfo = 0;
float g_heroVel[3] = {};
struct Pending { uint32_t id; knocktrigger::Verdict v; };
Pending g_pending[8];
int g_nPending = 0;
struct StabIn { uint32_t id = 0; int side = 0; float speed = 0.0f; uint64_t atMs = 0; };   // [weight] impale_drag: a stab that went in (OnStabEntered), for Tick
StabIn g_stabIn[2];
struct Strongest { uint32_t id = 0; float gave = 0, need = 0, run = 0, swing = 0, needSwing = 0; int kind = 0; uint64_t atMs = 0; };
Strongest g_strongest;   // the strongest push of the last second that did nothing (the log: what a blow gave against what it needed)
deathragdoll::Blows g_blows;   // [weight] death_ragdoll: the hand's last strike on each body (the push a kill falls by)

std::mutex g_lib;      // (the library's calls - the timer's and the grab's threads - and all below)
struct DownBody {
    uint32_t id = 0;
    float root[3] = {};    // where its model stood when it was struck or taken (world units)
    uint64_t seenMs = 0, sinceMs = 0;
    bool hurried = false;
    int kind = 0;
    bool swap = false;     // (Info::swap when the library took it: every joint to and from the library goes through SwapSides)
    bool grabbed = false;  // a hand took it ([weight] grab_moves_unit moves only what the hands carry or throw)
    float place[3] = {};   // where its unit stood when last seen (its rig's root, world units) - the scans keep it in reach (FarPlaces, vr/scan_reach.h)
    bool placeOk = false;
    // [weight] death_ragdoll: the game killed it - its skeleton found by the instance it died in (its rig is the scan's no more), kept once it lies
    bool death = false, fellAgain = false;
    uintptr_t self = 0;
    float rootW[3] = {};
    int bones = 0;
    uint64_t diedMs = 0;
    float impulse[3] = {}, point[3] = {};   // the push it fell by (render world)
};
DownBody g_down[D2RF_MAX_DOWN];
bool g_any = false;
deathragdoll::Corpses g_corpses;   // (the timer's thread only: Tick, ResetLocked from it) the corpses kept as they lie
deathragdoll::Told g_told;         // (the timer's thread only) the deaths decided
constexpr int kMaxSelves = D2RF_MAX_DOWN + deathragdoll::kMaxCorpses;
struct HandHold { uint32_t id = 0; float palm[3] = {}; uint64_t atMs = 0; float speed = 0.0f; uint64_t sinceMs = 0; };
HandHold g_hold[2];
// [weight] impale_drag: a body on the blade of hand `side` (the library's hand slot of that side; a hand holding a weapon holds no body by its grip)
struct OnBlade { uint32_t id = 0; uint64_t sinceMs = 0, bladeAtMs = 0, slowSinceMs = 0; float tip[3] = {}; bool hadTip = false; float speed = 0.0f; };
OnBlade g_blade[2];
std::atomic<uint32_t> g_bladeIds[2];
constexpr float kImpaleMs = 2.0f;        // the point's speed into the body, the hero's run along the blade in, that impales (m/s)
constexpr float kStopMs = 0.6f;          // the hero this slow across the ground ...
constexpr uint64_t kStopForMs = 250;     // ... this long: he stopped - it slides off
constexpr float kPullBackMs = 1.2f;      // the tip going back along the blade this fast against the hero: pulled back
constexpr float kTipDownSin = 0.5f;      // the blade pointing down more than 30 deg: it slides off
constexpr uint64_t kBladeGoneMs = 250;   // the blade not drawn this long (another weapon, no hit by touch): let go
constexpr uint64_t kMaxOnBladeMs = 8000;
constexpr float kImpaleStrength = 20.0f;  // the blade's hold: 20 hands' pull (the hero's run carries the body, D2RF_HAND_MAX_FORCE x 20 = 6000 N)
std::atomic<uint32_t> g_downIds[D2RF_MAX_DOWN];
std::atomic<uintptr_t> g_downSelves[kMaxSelves];   // the SkeletonInstances drawn out of the game's pose now (what hangs on their bones goes with them: OnXform)
std::atomic<uintptr_t> g_deadSelves[kMaxSelves];   // [weight] death_ragdoll: ... those of them dead (falling or kept): their parts put on them once a frame (PutHangs)
std::atomic<uintptr_t> g_deadKnown[kMaxSelves];    // ... every dead body we have, drawn this tick or not (its parts kept while it is: deathragdoll::HangNext)
SRWLOCK g_hangLock = SRWLOCK_INIT;
deathragdoll::Hangs g_hangs;                        // (under g_hangLock) what hangs on the dead bodies' bones
std::atomic<int> g_nHangs{0};
std::atomic<unsigned> g_hangPut{0}, g_hangBack{0}, g_hangNoStore{0}, g_hangNoPose{0};   // (the 5 s line's)
// [weight] death_ragdoll: each kept corpse's bones as they came to rest, in the world (render units): drawn so from then on - not laid on the game's pose
// again each frame (the bones the joints do not fix followed the game's death animation under ours: Serg's corpse twitching, 0.196.1), never moved with the
// game's model (eased to its cell after the death: our corpse went with it). The skinning reads them (skel::SetFrozenPoseSource -> FrozenBonesOf).
struct FrozenBones { uintptr_t self = 0; int n = 0; float w[knockpose::kMaxBones][16]; };
SRWLOCK g_frozenLock = SRWLOCK_INIT;
FrozenBones g_frozen[deathragdoll::kMaxCorpses];   // (under g_frozenLock)
// [weight] death_ragdoll: the kept corpses' poses, their one source (KeptPosesForSkin). Before 0.199.6 a kept corpse went through the moving bodies' list
// with the living - 16 places shared with the swaying and the knocked down (one crowded out: Carry faded it toward the game's pose, back the next tick),
// faded at once by a tick missed (dollpose::Faded took its weight under 0.999: no frozen bones, the game's live pose under its joints), eased in again from
// the game's pose after any 150 ms gap (deathragdoll::Drawn) - each a twitch while the touch made poses (a weapon in hand), none with the weapon dropped (no
// poses: the game's corpse, still). Here: weight 1, frozen, kept while the corpse is.
constexpr uint64_t kKeptFreshMs = 2000;   // an entry not refreshed this long is not handed (its instance's pose pointers may be another's by then)
SRWLOCK g_keptLock = SRWLOCK_INIT;
dollpose::Pose g_kept[deathragdoll::kMaxCorpses];   // (under g_keptLock)
int g_nKept = 0;
std::atomic<int> g_nKeptNow{0};
std::atomic<int> g_tickBy{0};
std::atomic<unsigned> g_keptBy[3], g_keptCarried{0};
// ... and what drawing a kept corpse needs of its skeleton, kept from its last read (the timer's thread): a tick its skeleton is not among the bodies' (the
// nearest 16 of them, a scan missed) it is drawn from this - never eased to the game's pose and back for it (the corpse pulled toward the game's place)
struct CorpseSkel {
    uint32_t id = 0; uintptr_t self = 0; int bones = 0; int16_t joint[D2RF_JOINTS] = {}; int16_t parent[dollpose::kMaxBones] = {}; float m3[9] = {}, root[3] = {};
    uint32_t entity = 0, parts[skel::MonsterRig::kMaxParts] = {}; int nParts = 0;   // (its entities: the skinning tells corpses alike apart by them)
};
CorpseSkel g_corpseSkel[deathragdoll::kMaxCorpses];
CorpseSkel g_downSkel[D2RF_MAX_DOWN];   // ... and a FALLING dead body's (by its slot in g_down): drawn and frozen from it the ticks its skeleton is not read (0.197.4: a
                                        // body falling out of the bodies' 6 m was let go of after a second and drawn as the game's)
void SaveSkel(CorpseSkel& sk, uint32_t id, const skel::MonsterRig& g) {
    sk.id = id;
    sk.self = g.self;
    sk.bones = g.bones;
    std::memcpy(sk.joint, g.joint, sizeof sk.joint);
    std::memcpy(sk.parent, g.parent, sizeof(int16_t) * static_cast<size_t>(std::min(g.bones, dollpose::kMaxBones)));
    std::memcpy(sk.m3, g.m3, sizeof sk.m3);
    std::memcpy(sk.root, g.root, sizeof sk.root);
    sk.entity = g.entity;
    sk.nParts = std::min(g.nParts, skel::MonsterRig::kMaxParts);
    std::memcpy(sk.parts, g.parts, sizeof(uint32_t) * static_cast<size_t>(sk.nParts));
}
void LoadSkel(const CorpseSkel& sk, skel::MonsterRig& out) {
    out.self = sk.self;
    out.bones = sk.bones;
    std::memcpy(out.joint, sk.joint, sizeof out.joint);
    std::memcpy(out.parent, sk.parent, sizeof(int16_t) * static_cast<size_t>(std::min(sk.bones, dollpose::kMaxBones)));
    std::memcpy(out.m3, sk.m3, sizeof out.m3);
    std::memcpy(out.root, sk.root, sizeof out.root);
    out.entity = sk.entity;
    out.nParts = sk.nParts;
    std::memcpy(out.parts, sk.parts, sizeof(uint32_t) * static_cast<size_t>(sk.nParts));
    out.joints = true;
}
std::atomic<unsigned> g_frozenDrawn{0}, g_corpseUnread{0};
std::atomic<unsigned> g_hangByComponent{0}, g_hangChildren{0}, g_hangMoved{0};          // ... written through its own component; parts on parts; components no longer its
std::atomic<unsigned> g_carried{0}, g_carryMissed{0};
std::atomic<int> g_downPhase[D2RF_MAX_DOWN];
// Each model's facing for the library, from the game's own walking (knockpose::FacesGoing: the game walks a unit toward its front): the ticks the game moved
// one of it with the library's front, against it; the verdict once there are enough (0 not yet: its transform's sign till then); said once. Timer thread.
struct Facing { char model[48] = {}; int with = 0, against = 0, verdict = 0; float tallest = 0.0f; };   // (tallest: the most it has stood, m - its size for its weight)
Facing g_facing[64];
int g_nFacing = 0;
Facing* FacingOf(const char* model) {
    for (int i = 0; i < g_nFacing; ++i)
        if (!std::strcmp(g_facing[i].model, model)) return &g_facing[i];
    if (g_nFacing >= 64) return nullptr;
    Facing& f = g_facing[g_nFacing++];
    snprintf(f.model, sizeof f.model, "%s", model);
    return &f;
}
constexpr int kFacingTicks = 20;        // ticks the game moved it, all one way, to decide
constexpr float kFacingMinMs = 0.5f;    // the game moving it at least this fast across the ground

// the poses Tick last made, for LayBodies (the timer's thread makes and lays them; the lock for the rest)
struct Laid {
    uint32_t id = 0;
    uintptr_t self = 0;
    int bones = 0;
    dollpose::Knock knock;
    uint32_t entity[1 + skel::MonsterRig::kMaxParts] = {};   // its root and its parts (their bounds grown while it is drawn away: GrowBounds)
    int nEntity = 0;
};
// Each entity of a body drawn out of the game's pose, and the world box (world units) where it is drawn: LayBodies (the timer) sets them, the world-matrix
// hook (OnXform, the renderer's threads) grows each one's bounds to hold that box - and puts back the bounds it grew once its body is the game's again.
struct Grow { uint32_t entity = 0; float lo[3] = {}, hi[3] = {}; };
SRWLOCK g_growLock = SRWLOCK_INIT;
Grow g_grow[64];
int g_nGrow = 0;
std::atomic<int> g_nGrowNow{0};
struct GrownBounds { uint32_t entity = 0; float orig[24] = {}; };   // (under g_grownMutex) the bounds as the game had them before we grew them
std::mutex g_grownMutex;
GrownBounds g_grown[64];
int g_nGrown = 0;
std::atomic<int> g_nGrownNow{0};
std::atomic<unsigned> g_boundsGrew{0}, g_boundsHeld{0}, g_boundsRebuilt{0}, g_boundsMissed{0}, g_boundsPutBack{0};
std::atomic<unsigned> g_boundsGrewTimer{0}, g_boundsHookMet{0}, g_boundsNoWorld{0};
std::atomic<unsigned> g_boundsGrewDraw{0}, g_drawEyes{0}, g_drawShadow{0}, g_drawHeld{0}, g_drawRebuilt{0};   // (BeforeDraw: grown; collected for the eyes' / the shadows'; the box then)   // (the timer's grows; the hook meeting one of them; no matrix read)
constexpr float kBoundsMarginM = 0.3f;   // the drawn box grown by this all round (a hand, a swinging arm between two ticks)
D2rfPose g_posesNow[D2RF_MAX_DOWN];   // (under g_lib) the library's bodies as Tick last drew them, render world - the wrist that holds a weapon taken
int g_nPosesNow = 0;
std::mutex g_laidMutex;
Laid g_laid[D2RF_MAX_DOWN];
int g_nLaid = 0;
std::atomic<unsigned> g_laidBodies{0}, g_laidMissed{0};   // capsules laid on a drawn body / bodies whose pose did not read (since the last line)

// The library's world is left-handed - its clips' and its "forward = left to right across up" (Unity's) - and the game's render world is its mirror (Serg on
// the first build: «у нпс в анимации регдол перепутан перед и зад» - a body on its back got up as from its face, the legs walked with the knees bent back).
// Every point and way handed to the library is mirrored in x, and every one it hands back.
void Flip(float* p) { p[0] = -p[0]; }
void FlipJoints(float (*j)[3]) {
    for (int k = 0; k < D2RF_JOINTS; ++k) j[k][0] = -j[k][0];
}
Info* InfoOf(uint32_t id) {
    for (int i = 0; i < g_nInfo; ++i)
        if (g_info[i].id == id) return &g_info[i];
    return nullptr;
}

const char* KindName(int k) {
    return k == D2RF_SWEEP ? "a sweep at the legs" : k == D2RF_SHOVE ? "a shove" : k == D2RF_STAGGER ? "a blow (a stagger)" : "a blow";
}
const char* PhaseName(int p) {
    switch (p) {
        case D2RF_FALLING: return "falling";
        case D2RF_LYING: return "lying";
        case D2RF_GETTING_UP: return "getting up";
        case D2RF_BACK: return "back to the game's";
        case D2RF_HELD: return "held";
        case D2RF_STAGGERING: return "staggering";
        case D2RF_WALKING: return "walking home";
        default: return "-";
    }
}
const char* SideName(int side) { return side ? "left" : "right"; }

// (under g_lib) the slot of a body drawn out of the game's pose, made if new
DownBody* DownOf(uint32_t id, uint64_t nowMs, bool make) {
    for (int s = 0; s < D2RF_MAX_DOWN; ++s)
        if (g_down[s].id == id) return &g_down[s];
    if (!make) return nullptr;
    for (int s = 0; s < D2RF_MAX_DOWN; ++s) {
        if (g_down[s].id) continue;
        g_down[s] = DownBody{};
        g_down[s].id = id;
        g_down[s].seenMs = g_down[s].sinceMs = nowMs;
        g_downIds[s].store(id);
        g_downPhase[s].store(D2RF_NONE);
        return &g_down[s];
    }
    return nullptr;
}

// (under g_lib) every body let go of, the library's world gone
void ResetLocked() {
    falllib::Reset();
    for (auto& d : g_down) d = DownBody{};
    for (auto& d : g_downIds) d.store(0);
    for (auto& h : g_hold) h = HandHold{};
    for (auto& b : g_blade) b = OnBlade{};
    for (auto& b : g_bladeIds) b.store(0);
    g_any = false;
    g_corpses.Reset();   // (back in F4 the corpses are the game's: none taken again - Decide finds them dead already)
    g_told.Reset();
    AcquireSRWLockExclusive(&g_keptLock);
    g_nKept = 0;
    g_nKeptNow.store(0);
    ReleaseSRWLockExclusive(&g_keptLock);
    for (auto& k : g_corpseSkel) k = CorpseSkel{};
    AcquireSRWLockExclusive(&g_frozenLock);
    for (auto& f : g_frozen) { f.self = 0; f.n = 0; }
    ReleaseSRWLockExclusive(&g_frozenLock);
}

bool Busy(int phase) { return phase == D2RF_FALLING || phase == D2RF_LYING || phase == D2RF_GETTING_UP || phase == D2RF_HELD; }

}  // namespace
void GrowBoundsNow(const Grow* grow, int n);   // (below: the bounds of the bodies drawn away grown on the timer too)

void SetLog(LogFn fn) {
    g_log.store(fn);
    falllib::SetLog(fn);
}

std::atomic<float> g_formStrength{1.0f};   // [shapeshift] a grafted form's paws: the factor on Settings::strength (SetFormStrength)
void SetFormStrength(float factor) {
    if (!std::isfinite(factor) || !(factor > 0.0f)) factor = 1.0f;
    if (g_formStrength.exchange(factor) == factor) return;
    float base = 1.0f;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        base = g_set.strength;
    }
    {
        std::lock_guard<std::mutex> lock(g_lib);
        falllib::SetStrength(base * factor);
    }
    if (g_throwOn.load()) LogF("knock down: the hands' strength %.2f (%s)", base * factor, factor != 1.0f ? "a form's paws, [shapeshift] wolf/bear_strength_pct" : "a person's hands again");
}

void Configure(const Settings& s) {
    const bool was = g_on.exchange(s.on), wasThrow = g_throwOn.exchange(s.throwOn), wasImpale = g_impaleOn.exchange(s.impaleOn), wasDeath = g_deathOn.exchange(s.deathOn);
    const bool wasDeathFlat = g_deathFlatOn.exchange(s.deathFlatOn);
    skel::SetWeaponTrail(s.deathFlatOn);   // (the game's swing kept for it, in every view)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_set = s;
    }
    {
        std::lock_guard<std::mutex> lock(g_lib);
        falllib::SetStrength(s.strength * g_formStrength.load());   // ([shapeshift] a form's paws: SetFormStrength)
        falllib::SetBalance(s.balance);
    }
    if (s.on != was) {
        if (s.on) {
            falllib::Probe();
            LogF("knock down: on ([weight] knock_down) - a blow swung at %.1f m/s or faster that gives the body %.0f cm/s or more (your run adds to the push, a "
                 "sweep at the legs needs half of it) knocks a humanoid down as drawn; a blow with %.0f %% of both, or a shove, staggers it a step back; it lies "
                 "%.1f s, gets up and walks back to where the game has it (F4; the unit stands as the game has it)", s.minSwingMs, s.needCms, s.staggerPct,
                 s.lieMs / 1000.0f);
        } else {
            LogF("knock down: off");
        }
    }
    static float told[3] = {s.minSwingMs, s.needCms, s.staggerPct};   // (the sliders moved while on: the log says what the next blows are judged by)
    if (s.on && was && (told[0] != s.minSwingMs || told[1] != s.needCms || told[2] != s.staggerPct))
        LogF("knock down: now a blow swung at %.1f m/s or faster that gives the body %.0f cm/s knocks down; %.0f %% of both staggers", s.minSwingMs, s.needCms,
             s.staggerPct);
    told[0] = s.minSwingMs;
    told[1] = s.needCms;
    told[2] = s.staggerPct;
    static int toldBalance = -1;
    if (s.throwOn && toldBalance != (s.balance ? 1 : 0)) {
        toldBalance = s.balance ? 1 : 0;
        LogF(s.balance ? "knock down: grab balance on ([weight] grab_balance) - a humanoid held standing and pulled steps after the pull and keeps its feet; only a pull "
                         "faster than its legs can step takes it off them" : "knock down: grab balance off - a humanoid held standing and pulled 30 cm out of its place falls");
    }
    if (s.throwOn != wasThrow) {
        if (s.throwOn) falllib::Probe();
        LogF(s.throwOn ? "knock down: grab and throw on ([weight] grab_throw) - a humanoid the hand's grip takes hangs from the hand, is lifted, thrown, gets up "
                         "and walks home (as drawn; F4)" : "knock down: grab and throw off");
    }
    if (s.impaleOn != wasImpale) {
        if (s.impaleOn) falllib::Probe();
        LogF(s.impaleOn ? "knock down: impale and drag on ([weight] impale_drag) - a stab that goes into a humanoid at %.1f m/s (the thrust and the run along the blade) "
                          "carries it on the blade as drawn; it slides off when you stop, pull back or tip the blade down (F4; needs [weight] stab_in)"
                        : "knock down: impale and drag off", kImpaleMs);
    }
    if (s.deathOn != wasDeath) {
        if (s.deathOn) falllib::Probe();
        LogF(s.deathOn ? "death ragdoll: on ([weight] death_ragdoll) - a humanoid monster the game kills falls as a ragdoll (by your blow, or away from you) instead "
                         "of its own death animation and lies as it fell while the game keeps its corpse; bosses and the ones that burst are the game's (F4; only "
                         "the drawing)" : "death ragdoll: off");
    }
    if (s.deathFlatOn != wasDeathFlat) {
        if (s.deathFlatOn) falllib::Probe();
        LogF(s.deathFlatOn ? "death ragdoll flat: on ([weight] death_ragdoll_flat) - in VR F1-F3 and the flat game a humanoid monster the game kills falls as a "
                             "ragdoll along the arc of your weapon as the game swings it (away from you for a spell or a missile) and lies as it fell; F4 keeps "
                             "death_ragdoll's own (only the drawing)" : "death ragdoll flat: off");
    }
}

bool Wanted() {
    return g_on.load(std::memory_order_relaxed) || g_throwOn.load(std::memory_order_relaxed) || g_impaleOn.load(std::memory_order_relaxed) ||
           g_deathOn.load(std::memory_order_relaxed) || g_deathFlatOn.load(std::memory_order_relaxed);
}
bool ThrowWanted() { return g_throwOn.load(std::memory_order_relaxed); }
bool DeathWanted() { return viewgate::F4() ? g_deathOn.load(std::memory_order_relaxed) : g_deathFlatOn.load(std::memory_order_relaxed); }
bool DeathFlatWanted() { return g_deathFlatOn.load(std::memory_order_relaxed); }

void OnStabEntered(uint32_t id, int side, float speed) {
    if (!g_impaleOn.load(std::memory_order_relaxed) || !id || side < 0 || side > 1) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_stabIn[side] = {id, side, speed, GetTickCount64()};
}

bool Impaled(uint32_t id) {
    if (!id) return false;
    for (auto& b : g_bladeIds)
        if (b.load(std::memory_order_relaxed) == id) return true;
    return false;
}

int FarPlaces(float (*out)[2], int max) {
    if (!out || max <= 0 || !Wanted()) return 0;
    std::lock_guard<std::mutex> lock(g_lib);
    int n = 0;
    for (const DownBody& d : g_down)
        if (d.id && d.placeOk && !d.death && n < max) {
            out[n][0] = d.place[0];
            out[n][1] = d.place[2];
            ++n;
        }
    return n;
}

bool IsDown(uint32_t id) {
    if (!id || !Wanted()) return false;
    for (auto& d : g_downIds)
        if (d.load(std::memory_order_relaxed) == id) return true;
    return false;
}

float RunAlong(const float way[3]) {
    if (!way || !g_on.load(std::memory_order_relaxed)) return 0.0f;
    const float l = std::sqrt(way[0] * way[0] + way[2] * way[2]);
    if (!(l > 1e-6f)) return 0.0f;
    std::lock_guard<std::mutex> lock(g_mutex);
    const float r = (g_heroVel[0] * way[0] + g_heroVel[2] * way[2]) / l;
    return std::isfinite(r) ? std::max(0.0f, r) : 0.0f;
}

// How near a knock-down a blow came: the lesser of its force and its swing, each over what it needed.
static float Near(float gave, float need, float swing, float needSwing) {
    return std::min(gave / std::max(need, 1e-3f), needSwing > 1e-3f ? swing / needSwing : 1.0f);
}

void NotePush(uint32_t id, const float impulse[3], const float point[3], float swingMs, float heftKg) {
    if (!id) return;
    if (g_deathOn.load(std::memory_order_relaxed)) {   // [weight] death_ragdoll: the last strike on it - if the game's hit kills it, it falls by this
        std::lock_guard<std::mutex> lock(g_mutex);
        g_blows.Note(id, impulse, point, swingMs, heftKg, static_cast<double>(GetTickCount64()) * 0.001);
    }
    if (!g_on.load(std::memory_order_relaxed)) return;
    for (int s = 0; s < D2RF_MAX_DOWN; ++s)   // a body falling, lying, getting up or held takes no blow; one staggering or walking home does
        if (g_downIds[s].load(std::memory_order_relaxed) == id && Busy(g_downPhase[s].load(std::memory_order_relaxed))) return;
    const uint64_t now = GetTickCount64();
    std::lock_guard<std::mutex> lock(g_mutex);
    const Info* info = InfoOf(id);
    if (!info || now - info->atMs > kInfoFreshMs) return;
    knocktrigger::Settings ts;
    ts.needMs = std::max(0.05f, g_set.needCms * 0.01f);
    ts.minSwingMs = std::max(0.0f, g_set.minSwingMs);
    ts.staggerShare = std::clamp(g_set.staggerPct * 0.01f, 0.0f, 0.99f);
    const knocktrigger::Verdict v = g_tracker.Note(id, impulse, point, swingMs, heftKg, static_cast<double>(now) * 0.001, info->body, g_heroVel, ts);
    if (v.knock) {
        for (int i = 0; i < g_nPending; ++i)
            if (g_pending[i].id == id) return;
        if (g_nPending < (int)std::size(g_pending)) g_pending[g_nPending++] = {id, v};
    } else if (v.gaveMs > 0.0f && (Near(v.gaveMs, v.needMs, v.swingMs, v.needSwingMs) > Near(g_strongest.gave, g_strongest.need, g_strongest.swing, g_strongest.needSwing) ||
                                   now - g_strongest.atMs > 1000)) {
        g_strongest = {id, v.gaveMs, v.needMs, v.runMs, v.swingMs, v.needSwingMs, v.kind, now};
    }
}

bool TakeOver(int side, uint32_t id, const float point[3], const float palm[3], const float* weaponA, const float* weaponB) {
    if (!ThrowWanted() || side < 0 || side > 1 || !id || !point || !palm || !falllib::Loaded()) return false;
    const uint64_t now = GetTickCount64();
    Info info;
    float lieS = 1.5f;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const Info* f = InfoOf(id);
        if (!f || now - f->atMs > kInfoFreshMs) return false;
        info = *f;
        lieS = g_set.lieMs * 0.001f;
    }
    D2rfKnock k{};
    k.size = sizeof k;
    k.id = id;
    std::memcpy(k.joint, info.joint, sizeof k.joint);
    k.have = info.have;
    k.floorY = info.body.floorY;
    k.massKg = info.body.massKg;
    k.lieS = lieS;
    FlipJoints(k.joint);
    bool swap = info.swap;
    float at[3] = {point[0], point[1], point[2]};
    Flip(at);
    int r = 0, part = -1;
    bool both = false;
    {
        std::lock_guard<std::mutex> lock(g_lib);
        if (weaponA && weaponB) {   // its weapon: held by the wrist nearer it, as the body is drawn now (the library's, when it has the body; else the game's)
            const float (*j)[3] = info.joint;
            for (int i = 0; i < g_nPosesNow; ++i)
                if (g_posesNow[i].id == id) j = g_posesNow[i].joint;
            const float mid[3] = {(weaponA[0] + weaponB[0]) * 0.5f, (weaponA[1] + weaponB[1]) * 0.5f, (weaponA[2] + weaponB[2]) * 0.5f};
            float best = 1e30f;
            for (const int w : {D2RF_L_WRIST, D2RF_R_WRIST}) {
                if (!(info.have >> w & 1u) && j == info.joint) continue;
                const float d = (j[w][0] - mid[0]) * (j[w][0] - mid[0]) + (j[w][1] - mid[1]) * (j[w][1] - mid[1]) + (j[w][2] - mid[2]) * (j[w][2] - mid[2]);
                if (d < best) { best = d; part = w; }
            }
        }
        both = g_hold[1 - side].id == id;
        if (const DownBody* was = DownOf(id, now, false)) swap = was->swap;   // (one the library has: as it was handed over)
        if (swap) knockpose::SwapSides(k.joint, &k.have);
        r = falllib::Grab(k, side, at, knockpose::SwapJoint(part, swap));
        if (r == 1) {
            if (DownBody* d = DownOf(id, now, true)) {
                d->swap = swap;
                d->grabbed = true;
            }
            g_hold[side] = HandHold{};
            g_hold[side].id = id;
            std::memcpy(g_hold[side].palm, palm, sizeof g_hold[side].palm);
            g_hold[side].atMs = g_hold[side].sinceMs = now;
            g_any = true;
        }
    }
    if (r == 1)
        LogF("knock down: the %s hand took %s %u ('%s', %.0f kg)%s%s - it hangs from the hand: standing while its feet bear it, off its feet once lifted 12 cm or "
             "pulled 30 cm out of its place", SideName(side), info.hostile ? "monster" : "NPC", id, info.model, info.body.massKg,
             part >= 0 ? (part == D2RF_L_WRIST ? " by what it holds in its left hand" : " by what it holds in its right hand") : "",
             both ? " - the other hand holds it too (both hands have it; either lets go, the other keeps it)" : "");
    else
        LogF("knock down: the %s hand's %u not taken by the library (%d)", SideName(side), id, r);
    return r == 1;
}

void HandAt(int side, const float palm[3]) {
    if (side < 0 || side > 1 || !palm) return;
    std::lock_guard<std::mutex> lock(g_lib);
    HandHold& h = g_hold[side];
    if (!h.id) return;
    const uint64_t now = GetTickCount64();
    const float dt = static_cast<float>(now - h.atMs) * 0.001f;
    if (dt > 1e-3f) {   // the palm's speed, eased (the log tells it at the let-go)
        const float dx = palm[0] - h.palm[0], dy = palm[1] - h.palm[1], dz = palm[2] - h.palm[2];
        const float v = std::sqrt(dx * dx + dy * dy + dz * dz) / dt;
        h.speed += (v - h.speed) * std::min(1.0f, dt / 0.06f);
    }
    std::memcpy(h.palm, palm, sizeof h.palm);
    h.atMs = now;
    float at[3] = {palm[0], palm[1], palm[2]};
    Flip(at);
    if (falllib::Hand(h.id, side, at) <= 0) h = HandHold{};   // (the library let it go: died, gone)
}

void LetGo(int side) {
    if (side < 0 || side > 1) return;
    uint32_t id = 0;
    float speed = 0.0f, held = 0.0f;
    {
        std::lock_guard<std::mutex> lock(g_lib);
        HandHold& h = g_hold[side];
        if (!h.id) return;
        id = h.id;
        speed = h.speed;
        held = static_cast<float>(GetTickCount64() - h.sinceMs) * 0.001f;
        falllib::Release(id, side);
        h = HandHold{};
    }
    LogF("knock down: the %s hand let go of %u after %.1f s, the palm going %.1f m/s", SideName(side), id, held, speed);
}

bool HeldPart(int side, float a[3], float b[3], float* r) {
    if (side < 0 || side > 1) return false;
    std::lock_guard<std::mutex> lock(g_lib);
    if (!g_hold[side].id || falllib::Held(g_hold[side].id, side, a, b, r) <= 0) return false;
    Flip(a);
    Flip(b);
    return true;
}

uint32_t Holding(int side) {
    if (side < 0 || side > 1) return 0;
    std::lock_guard<std::mutex> lock(g_lib);
    return g_hold[side].id;
}

namespace {
// [weight] death_ragdoll: a corpse coming to rest - its bones as drawn now (the game's pose laid on its joints, atModel), kept in the world. False: not read.
bool FreezeBones(const skel::MonsterRig& g, const float (*atModel)[3]) {
    static float game[knockpose::kMaxBones][16], drawn[knockpose::kMaxBones][16];   // (the timer's thread)
    const int nb = skel::PoseNow(g.self, game, knockpose::kMaxBones);
    if (nb != g.bones || nb <= 0 || nb > dollpose::kMaxBones) return false;
    std::memcpy(drawn, game, sizeof(float) * 16 * static_cast<size_t>(nb));
    if (!knockpose::Apply(drawn, nb, g.parent, g.joint, atModel, 1.0f)) return false;
    AcquireSRWLockExclusive(&g_frozenLock);
    FrozenBones* slot = nullptr;
    for (auto& f : g_frozen)
        if (f.self == g.self) slot = &f;
    for (auto& f : g_frozen)
        if (!slot && !f.self) slot = &f;
    if (slot) {
        for (int b = 0; b < nb; ++b) knockpose::ModelToWorld(drawn[b], g.m3, g.root, slot->w[b]);
        slot->n = nb;
        slot->self = g.self;
    }
    ReleaseSRWLockExclusive(&g_frozenLock);
    return slot != nullptr;
}
// ... the frozen bones of corpses no longer kept let go of (keep: the selves of the corpses kept now)
void ForgetFrozen(const uintptr_t* keep, int nKeep) {
    AcquireSRWLockExclusive(&g_frozenLock);
    for (auto& f : g_frozen) {
        bool kept = false;
        for (int i = 0; i < nKeep && !kept; ++i) kept = f.self && keep[i] == f.self;
        if (!kept) { f.self = 0; f.n = 0; }
    }
    ReleaseSRWLockExclusive(&g_frozenLock);
}
}  // namespace

bool KeptPose(uintptr_t self, dollpose::Pose* out) {
    if (!self || !out || g_nKeptNow.load(std::memory_order_relaxed) == 0) return false;
    const uint64_t now = GetTickCount64();
    bool found = false;
    AcquireSRWLockShared(&g_keptLock);
    for (int k = 0; k < g_nKept && !found; ++k)
        if (g_kept[k].self == self && now - g_kept[k].tickMs <= kKeptFreshMs) {
            *out = g_kept[k];
            found = true;
        }
    ReleaseSRWLockShared(&g_keptLock);
    return found;
}

int KeptPosesForSkin(uintptr_t skeleton, uintptr_t worldPose, dollpose::Pose* out, int max, bool* exact) {
    if (exact) *exact = false;
    if (!out || max <= 0 || g_nKeptNow.load(std::memory_order_relaxed) == 0) return 0;
    const uint64_t now = GetTickCount64();
    int n = 0;
    AcquireSRWLockShared(&g_keptLock);
    for (int k = 0; k < g_nKept; ++k) {
        const dollpose::Pose& p = g_kept[k];
        if (now - p.tickMs > kKeptFreshMs) continue;
        if (worldPose && (p.poseCur == worldPose || p.posePrev == worldPose)) {   // the instance's own pose: it alone
            out[0] = p;
            n = 1;
            if (exact) *exact = true;
            break;
        }
        if (skeleton && p.skel == skeleton && n < max) out[n++] = p;
    }
    ReleaseSRWLockShared(&g_keptLock);
    return n;
}

bool IsKept(uintptr_t self) {
    if (!self || g_nKeptNow.load(std::memory_order_relaxed) == 0) return false;
    const uint64_t now = GetTickCount64();
    bool found = false;
    AcquireSRWLockShared(&g_keptLock);
    for (int k = 0; k < g_nKept && !found; ++k) found = g_kept[k].self == self && now - g_kept[k].tickMs <= kKeptFreshMs;
    ReleaseSRWLockShared(&g_keptLock);
    return found;
}

void NoteTickBy(int who) { g_tickBy.store(who, std::memory_order_relaxed); }

bool FrozenBonesOf(uintptr_t self, const float m3[9], const float root[3], float (*mats)[16], int n) {
    if (!self || !mats || !m3 || !root || n <= 0) return false;
    bool ok = false;
    AcquireSRWLockShared(&g_frozenLock);
    for (const auto& f : g_frozen)
        if (f.self == self && f.n == n) {
            ok = true;
            for (int b = 0; b < n && ok; ++b) ok = knockpose::WorldToModel(f.w[b], m3, root, mats[b]);
            break;
        }
    ReleaseSRWLockShared(&g_frozenLock);
    if (ok) g_frozenDrawn.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

int Tick(const skel::MonsterRig* rigs, size_t nr, const Unit* units, int nUnits, const int* rigOf, uint64_t nowMs, float upm, const float heroRender[3],
         dollpose::Pose* made, int n, int cap, const Dead* dead, int nDead) {
    static uint64_t lastMs = 0;
    static knocktrigger::HeroRun heroRun;
    static Laid laid[D2RF_MAX_DOWN];   // (the timer's thread only) handed to LayBodies on every way out
    int nLaid = 0;
    struct Publish {
        const Laid* l;
        const int& n;
        ~Publish() {
            std::lock_guard<std::mutex> lock(g_laidMutex);
            for (int i = 0; i < n; ++i) g_laid[i] = l[i];
            g_nLaid = n;
        }
    } publish{laid, nLaid};
    const float dt = lastMs ? std::min(0.1f, static_cast<float>(nowMs - lastMs) * 0.001f) : 0.0f;
    lastMs = nowMs;
    // F4: everything of the knock-down; the other views ([weight] death_ragdoll_flat): the deaths alone (nothing pushes, holds or impales there). A view changed:
    // every body let go of first - what one view drew is not carried into the other.
    const bool f4 = viewgate::F4(), flatDeaths = !f4 && g_deathFlatOn.load(std::memory_order_relaxed);
    static int lastView = -1;
    if (lastView >= 0 && lastView != (f4 ? 1 : 0)) {
        std::lock_guard<std::mutex> lock(g_lib);
        if (g_corpses.Count()) LogF("death ragdoll: %d kept corpses released - the view changed (F4 and the other views draw their own): the game's own from here", g_corpses.Count());
        if (g_any || g_corpses.Count()) ResetLocked();
    }
    lastView = f4 ? 1 : 0;
    if (!Wanted() || !(f4 || flatDeaths) || !(upm > 1e-3f) || !rigs || !units || !rigOf) {
        std::lock_guard<std::mutex> lock(g_lib);
        for (auto& s : g_downSelves) s.store(0);
        for (auto& s : g_deadSelves) s.store(0);
        for (auto& s : g_deadKnown) s.store(0);
        if (g_any) ResetLocked();   // switched off, another view: every body let go of (the game's own animation at once)
        heroRun.Feed(nullptr, 0.0f);
        return n;
    }
    if (!falllib::Ensure()) return n;
    // the hero's speed across the ground (render metres a second), eased; a jump (a teleport, a co-op client's correction) is no run
    float heroVel[3] = {};
    {
        float at[3];
        const bool known = heroRender && std::isfinite(heroRender[0] + heroRender[1] + heroRender[2]);
        if (known)
            for (int k = 0; k < 3; ++k) at[k] = heroRender[k] / upm;
        std::memcpy(heroVel, heroRun.Feed(known ? at : nullptr, dt), sizeof heroVel);
    }
    // each humanoid of the scan as drawn now: its joints, floor, height and mass (for the weight model's pushes and the hands) - from the game's own pose
    static Info fresh[kMaxInfo];
    int nf = 0;
    static float mats[knockpose::kMaxBones][16];
    for (int m = 0; m < nUnits && nf < kMaxInfo; ++m) {
        const int r = rigOf[m];
        if (r < 0 || static_cast<size_t>(r) >= nr) continue;
        const skel::MonsterRig& g = rigs[r];
        {   // each model once (and again when its verdict changes): can it be knocked down, and its joints' bones - or why not
            static char told[64][56];
            static int nTold = 0;
            char key[56];
            snprintf(key, sizeof key, "%.40s|%d%d", g.model, g.doll > 0 ? 1 : 0, g.joints ? 1 : 0);
            bool seen = false;
            for (int i = 0; i < nTold && !seen; ++i) seen = !std::strcmp(told[i], key);
            if (!seen && g.model[0] && g.n > 0) {
                if (nTold < 64) std::memcpy(told[nTold++], key, sizeof key);
                if (g.doll > 0 && g.joints) LogF("knock down: '%s' can be knocked down and held - its joints' bones %s", g.model, g.jointsSaid);
                else LogF("knock down: '%s' cannot be knocked down - %s", g.model,
                          g.doll < 0 ? "not a humanoid by its bones" : g.doll == 0 ? (g.still ? "its pose held still (a corpse)" : "not seen standing yet")
                                                                                     : g.jointsSaid[0] ? g.jointsSaid : "its joints not found");
            }
        }
        if (g.doll <= 0 || !g.joints || g.bones <= 0 || g.bones > bonecap::Max()) continue;   // a humanoid seen standing, not a corpse, its joints found
        const int count = skel::PoseNow(g.self, mats, knockpose::kMaxBones);
        if (count != g.bones) continue;
        float jm[D2RF_JOINTS][3];
        uint32_t have = 0;
        if (!knockpose::JointsOf(g.joint, mats, count, jm, &have, g.parent)) continue;
        Info& f = fresh[nf];
        f = Info{};
        f.id = units[m].id;
        f.hostile = units[m].hostile;
        f.atMs = nowMs;
        f.have = have;
        f.self = g.self;
        std::memcpy(f.root, g.root, sizeof f.root);
        f.bones = g.bones;
        snprintf(f.model, sizeof f.model, "%s", g.model);
        for (int j = 0; j < D2RF_JOINTS; ++j) knockpose::ToWorld(g.m3, g.root, upm, jm[j], f.joint[j]);
        {
            float lib[D2RF_JOINTS][3];
            std::memcpy(lib, f.joint, sizeof lib);
            FlipJoints(lib);
            f.swap = knockpose::Mirrored(g.m3);   // (its model drawn mirrored - never by its toes: the rogues' rig went back to front, 0.192.16)
            f.toes = knockpose::ToesForward(lib, have);
            f.knees = knockpose::KneesForward(lib, have);
            static char toldFacing[64][56];   // each model once (and again if its transform's sign changes): how it is handed over, and what its bones say
            static int nToldFacing = 0;
            char key[56];
            snprintf(key, sizeof key, "%.40s|%d", g.model, f.swap ? 1 : 0);
            bool seen = false;
            for (int i = 0; i < nToldFacing && !seen; ++i) seen = !std::strcmp(toldFacing[i], key);
            if (!seen) {
                if (nToldFacing < 64) std::memcpy(toldFacing[nToldFacing++], key, sizeof key);
                LogF("knock down: '%s' faces the library's way %s - its model is %s; its toe bones say %s, its knees %s (the bones' word: not used)", g.model,
                     f.swap ? "with its left and right swapped" : "as it is", f.swap ? "drawn MIRRORED (its transform's determinant below 0)" : "not drawn mirrored",
                     f.toes < 0 ? "nothing" : f.toes ? "forward" : "backward", f.knees < 0 ? "nothing" : f.knees ? "forward" : "backward");
            }
        }
        f.body.floorY = g.root[1] / upm;
        f.body.height = std::max(0.3f, (f.joint[D2RF_HEAD][1] - f.body.floorY) * 1.1f);
        if (Facing* fc = FacingOf(g.model)) {   // its size the tallest its model has stood: a crouch, an attack, a fall weigh nothing less (the brute came to 6 kg)
            fc->tallest = std::max(fc->tallest, f.body.height);
            f.body.height = fc->tallest;
        }
        ++nf;
    }
    Pending pending[8];
    int np = 0;
    StabIn stabs[2];
    Strongest strongest;
    float lieS = 1.5f;
    // [weight] death_ragdoll: the deaths the scan reports, each decided once (vr/death_ragdoll.h) - ours start from the unit's last read alive (a tick ago)
    const bool deathOn = f4 ? g_deathOn.load(std::memory_order_relaxed) : flatDeaths;
    struct Death { Info info; deathragdoll::Push push; int mode; };
    Death deaths[8];
    int nDeaths = 0;
    uint32_t deadIds[deathragdoll::kMaxDeadListed];
    int nDeadIds = 0;
    for (int i = 0; deathOn && dead && i < nDead && nDeadIds < deathragdoll::kMaxDeadListed; ++i) deadIds[nDeadIds++] = dead[i].id;
    static unsigned leftBoss = 0, leftNotSeen = 0, leftNotHostile = 0, leftFull = 0;   // (the 5 s line's)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (int i = 0; i < nf; ++i) {
            fresh[i].body.massKg = knocktrigger::MassOf(fresh[i].body.height, g_set.massScale);
            const Info* was = InfoOf(fresh[i].id);
            if (was && dt > 1e-4f)
                for (int k = 0; k < 3; ++k) fresh[i].vel[k] = std::clamp((fresh[i].joint[D2RF_PELVIS][k] - was->joint[D2RF_PELVIS][k]) / std::max(dt, 0.005f), -8.0f, 8.0f);
        }
        if (deathOn) {
            g_told.Seen(deadIds, nDeadIds, nowMs);
            float heroM[3];
            const bool heroKnown = heroRender && std::isfinite(heroRender[0] + heroRender[1] + heroRender[2]);
            for (int k = 0; k < 3 && heroKnown; ++k) heroM[k] = heroRender[k] / upm;
            bool arcRead = false;   // ([weight] death_ragdoll_flat: the game's swing, read once a tick when a kill needs it)
            deathragdoll::Arc arc;
            for (int i = 0; i < nDead && dead; ++i) {
                const Dead& u = dead[i];
                if (!u.id || g_told.Has(u.id)) continue;
                g_told.Add(u.id, nowMs);
                const Info* was = InfoOf(u.id);
                const char* why = nullptr;
                const deathragdoll::Start s = deathragdoll::Decide(u.rowOk, u.rowFlags, was != nullptr, was ? nowMs - was->atMs : ~0ull, was && was->hostile, &why);
                if (s == deathragdoll::Start::LeftToGame) {
                    ++leftBoss;
                    if (was) LogF("death ragdoll: %u ('%s') died - left to the game: %s (its MonStats flags 0x%08X%s)", u.id, was->model, why ? why : "-", u.rowFlags,
                                  u.rowOk ? "" : ", the row not read");
                    continue;
                }
                if (s == deathragdoll::Start::NotSeenAlive) { ++leftNotSeen; continue; }
                if (s == deathragdoll::Start::NotHostile) { ++leftNotHostile; continue; }
                if (nDeaths >= (int)std::size(deaths)) { ++leftFull; continue; }
                Death& d = deaths[nDeaths++];
                d.info = *was;
                d.mode = u.mode;
                if (f4) {
                    d.push = deathragdoll::KillingPush(g_blows.Last(u.id, static_cast<double>(nowMs) * 0.001), was->joint[D2RF_CHEST], was->body.massKg,
                                                       heroKnown ? heroM : nullptr);
                } else {   // [weight] death_ragdoll_flat: along the arc of the game's own swing of the hero's weapon (skel::WeaponTrail), else away from him
                    if (!arcRead) {
                        arcRead = true;
                        skel::WeaponTrailSample w[48];
                        const int nw = skel::WeaponTrail(w, 48);
                        deathragdoll::ArcSample a[48];
                        for (int k = 0; k < nw; ++k) {
                            a[k].t = w[k].t;
                            for (int c = 0; c < 3; ++c) {
                                a[k].root[c] = w[k].root[c] / upm;
                                a[k].p[0][c] = w[k].p[0][c] / upm;
                                a[k].p[1][c] = w[k].p[1][c] / upm;
                            }
                            a[k].ok[0] = w[k].ok[0];
                            a[k].ok[1] = w[k].ok[1];
                        }
                        arc = deathragdoll::ArcAt(a, nw, skel::WeaponTrailNow());
                    }
                    d.push = deathragdoll::ArcPush(arc, was->joint[D2RF_CHEST], was->body.massKg, heroKnown ? heroM : nullptr);
                }
            }
        }
        std::memcpy(g_info, fresh, sizeof(Info) * static_cast<size_t>(nf));
        g_nInfo = nf;
        std::memcpy(g_heroVel, heroVel, sizeof g_heroVel);
        np = g_nPending;
        std::memcpy(pending, g_pending, sizeof(Pending) * static_cast<size_t>(np));
        g_nPending = 0;
        for (int s = 0; s < 2; ++s) { stabs[s] = g_stabIn[s]; g_stabIn[s] = StabIn{}; }
        if (g_strongest.atMs && nowMs - g_strongest.atMs >= 1000) { strongest = g_strongest; g_strongest = Strongest{}; }
        lieS = g_set.lieMs * 0.001f;
    }
    for (int i = 0; i < nf; ++i) {   // each model's facing for the library, from the game walking its units (never one we draw out of the game's pose)
        Info& f = fresh[i];
        Facing* fc = FacingOf(f.model);
        if (!fc) continue;
        const float speed = std::sqrt(f.vel[0] * f.vel[0] + f.vel[2] * f.vel[2]);
        if (!fc->verdict && speed >= kFacingMinMs && !IsDown(f.id)) {
            float lib[D2RF_JOINTS][3], way[3] = {-f.vel[0], 0.0f, f.vel[2]};
            std::memcpy(lib, f.joint, sizeof lib);
            FlipJoints(lib);
            const int s = knockpose::FacesGoing(lib, way);
            if (s > 0) ++fc->with;
            if (s < 0) ++fc->against;
            const int n = fc->with + fc->against;
            if (n >= kFacingTicks && (fc->with * 5 <= n || fc->against * 5 <= n)) {
                fc->verdict = fc->with > fc->against ? 1 : -1;
                LogF("knock down: '%s' - the game walked it %d times toward the library's front, %d against it: %s (its transform %s; its toe bones say %s, its "
                     "knees %s)", f.model, fc->with, fc->against, fc->verdict > 0 ? "handed over as it is" : "its left and right handed over SWAPPED",
                     f.swap ? "mirrored" : "not mirrored", f.toes < 0 ? "nothing" : f.toes ? "forward" : "backward", f.knees < 0 ? "nothing" : f.knees ? "forward" : "backward");
            }
        }
        if (fc->verdict) f.swap = fc->verdict < 0;   // (the game's own word, once a model; before it: its transform's sign)
    }
    if (strongest.id)   // (once a second at most, while blows land and do nothing: what the strongest gave against what it needed)
        LogF("knock down: the strongest push of the last second on %u - %s swung at %.1f m/s (%.1f needed), %.2f m/s to the body (%.2f needed%s): nothing",
             strongest.id, KindName(strongest.kind), strongest.swing, strongest.needSwing, strongest.gave, strongest.need, strongest.run > 0.2f ? ", running" : "");
    std::unique_lock<std::mutex> lib(g_lib);
    // the knock-downs and staggers the pushes made
    for (int p = 0; p < np; ++p) {
        const Info* f = nullptr;
        for (int i = 0; i < nf && !f; ++i)
            if (fresh[i].id == pending[p].id) f = &fresh[i];
        if (!f) continue;
        D2rfKnock k{};
        k.size = sizeof k;
        k.id = f->id;
        std::memcpy(k.joint, f->joint, sizeof k.joint);
        k.have = f->have;
        std::memcpy(k.vel, f->vel, sizeof k.vel);
        k.vel[1] = 0.0f;
        k.floorY = f->body.floorY;
        k.massKg = f->body.massKg;
        std::memcpy(k.impulse, pending[p].v.impulse, sizeof k.impulse);
        std::memcpy(k.point, pending[p].v.point, sizeof k.point);
        k.kind = pending[p].v.kind;
        k.lieS = lieS;
        FlipJoints(k.joint);
        Flip(k.vel);
        Flip(k.impulse);
        Flip(k.point);
        const int toes = knockpose::ToesForward(k.joint, k.have), knees = knockpose::KneesForward(k.joint, k.have);
        if (f->swap) knockpose::SwapSides(k.joint, &k.have);
        const int r = falllib::KnockDown(k);
        if (r == 1) {
            if (DownBody* d = DownOf(k.id, nowMs, true)) {
                d->swap = f->swap;
                d->kind = k.kind;
                d->hurried = false;
                for (int m = 0; m < nUnits; ++m)
                    if (units[m].id == k.id && rigOf[m] >= 0 && static_cast<size_t>(rigOf[m]) < nr) std::memcpy(d->root, rigs[rigOf[m]].root, sizeof d->root);
            }
            g_any = true;
            const knocktrigger::Verdict& v = pending[p].v;
            LogF("knock down: %u ('%s', %.0f kg, %.2f m) %s by %s - swung at %.1f m/s (%.1f needed), %.2f m/s to the body (%.2f needed%s, %.1f N s); its toes point "
                 "%s, its knees bend %s%s", k.id, f->model, k.massKg, f->body.height, k.kind == D2RF_STAGGER ? "staggered" : "knocked down",
                 v.shove ? "a shove (a stagger)" : KindName(k.kind), v.swingMs, v.needSwingMs, v.gaveMs, v.needMs, v.runMs > 0.2f ? ", running" : "",
                 std::sqrt(k.impulse[0] * k.impulse[0] + k.impulse[1] * k.impulse[1] + k.impulse[2] * k.impulse[2]),
                 toes < 0 ? "- (no toe bones)" : toes ? "forward" : "backward", knees < 0 ? "- (too straight to tell)" : knees ? "forward" : "backward",
                 f->swap ? " - its model is drawn mirrored: its left and right handed over swapped" : " (the bones' word only: handed over as it is)");
        } else if (r < 0) {
            LogF("knock down: the library refused %u (%d)", k.id, r);
        }
    }
    {   // [weight] impale_drag: a stab that went in fast enough - the body taken at the blade's tip by the library's hand of the weapon's side
        meleetouch::OwnBlade blades[2];
        const size_t nb = g_impaleOn.load(std::memory_order_relaxed) ? meleetouch::OwnBlades(blades, 150) : 0;
        for (int s = 0; s < 2; ++s) {
            const StabIn& st = stabs[s];
            if (!st.id || nowMs - st.atMs > 300 || g_blade[s].id || g_hold[s].id) continue;
            const meleetouch::OwnBlade* bl = nullptr;
            for (size_t b = 0; b < nb && !bl; ++b)
                if (blades[b].hand == s) bl = &blades[b];
            const Info* f = nullptr;
            for (int i = 0; i < nf && !f; ++i)
                if (fresh[i].id == st.id) f = &fresh[i];
            if (!bl || !f) continue;
            float ax[3], len = 0.0f;
            for (int k = 0; k < 3; ++k) { ax[k] = bl->tip[k] - bl->butt[k]; len += ax[k] * ax[k]; }
            len = std::sqrt(len);
            if (!(len > 0.05f)) continue;
            float run = 0.0f;
            for (int k = 0; k < 3; ++k) run += heroVel[k] * ax[k] / len;
            const float into = st.speed + std::max(0.0f, run);
            if (into < kImpaleMs) {
                LogF("knock down: the blade went into %u at %.1f m/s (the thrust %.1f, the run along it %.1f) - under %.1f: not impaled", st.id, into, st.speed, run, kImpaleMs);
                continue;
            }
            D2rfKnock k{};
            k.size = sizeof k;
            k.id = f->id;
            std::memcpy(k.joint, f->joint, sizeof k.joint);
            k.have = f->have;
            k.floorY = f->body.floorY;
            k.massKg = f->body.massKg;
            k.lieS = lieS;
            FlipJoints(k.joint);
            bool swap = f->swap;
            if (const DownBody* was = DownOf(k.id, nowMs, false)) swap = was->swap;
            if (swap) knockpose::SwapSides(k.joint, &k.have);
            float at[3] = {bl->tip[0], bl->tip[1], bl->tip[2]};
            Flip(at);
            if (falllib::Grab(k, s, at) != 1) continue;
            falllib::SetHandStrength(k.id, s, kImpaleStrength);   // (the blade carries it by the hero's whole running weight, not an arm's pull)
            if (DownBody* d = DownOf(k.id, nowMs, true)) d->swap = swap;
            g_any = true;
            OnBlade& o = g_blade[s];
            o = OnBlade{};
            o.id = k.id;
            o.sinceMs = o.bladeAtMs = nowMs;
            std::memcpy(o.tip, bl->tip, sizeof o.tip);
            o.hadTip = true;
            o.speed = into;
            g_bladeIds[s].store(k.id);
            LogF("knock down: %u ('%s', %.0f kg) impaled on the %s hand's blade at %.1f m/s (the thrust %.1f, the run along it %.1f) - it goes with the blade until "
                 "you stop, pull back or tip it down", k.id, f->model, k.massKg, SideName(s), into, st.speed, run);
        }
        for (int s = 0; s < 2; ++s) {   // each body on a blade: at the tip as the blade is drawn now - or slid off
            OnBlade& o = g_blade[s];
            if (!o.id) continue;
            const meleetouch::OwnBlade* bl = nullptr;
            for (size_t b = 0; b < nb && !bl; ++b)
                if (blades[b].hand == s) bl = &blades[b];
            const char* why = nullptr;
            float ax[3] = {}, len = 0.0f, back = 0.0f;
            if (bl) {
                for (int k = 0; k < 3; ++k) { ax[k] = bl->tip[k] - bl->butt[k]; len += ax[k] * ax[k]; }
                len = std::sqrt(std::max(len, 1e-8f));
                if (o.hadTip && dt > 1e-4f)
                    for (int k = 0; k < 3; ++k) back += ((bl->tip[k] - o.tip[k]) / dt - heroVel[k]) * ax[k] / len;
                o.bladeAtMs = nowMs;
            }
            const float heroSpeed = std::sqrt(heroVel[0] * heroVel[0] + heroVel[2] * heroVel[2]);
            if (heroSpeed < kStopMs) { if (!o.slowSinceMs) o.slowSinceMs = nowMs; }
            else o.slowSinceMs = 0;
            if (!bl && nowMs - o.bladeAtMs > kBladeGoneMs) why = "the blade is gone";
            else if (bl && back < -kPullBackMs) why = "pulled back";
            else if (bl && ax[1] / len < -kTipDownSin) why = "the blade tipped down";
            else if (o.slowSinceMs && nowMs - o.slowSinceMs >= kStopForMs && nowMs - o.sinceMs > 400) why = "you stopped";
            else if (nowMs - o.sinceMs > kMaxOnBladeMs) why = "held too long";
            if (why) {
                falllib::Release(o.id, s);
                LogF("knock down: %u slid off the %s hand's blade after %.1f s - %s", o.id, SideName(s), (nowMs - o.sinceMs) / 1000.0f, why);
                o = OnBlade{};
                g_bladeIds[s].store(0);
                continue;
            }
            if (!bl) continue;
            std::memcpy(o.tip, bl->tip, sizeof o.tip);
            o.hadTip = true;
            float at[3] = {bl->tip[0], bl->tip[1], bl->tip[2]};
            Flip(at);
            falllib::Hand(o.id, s, at);
        }
    }
    static unsigned fell = 0, kept = 0, gone = 0;   // [weight] death_ragdoll: the 5 s line's
    for (int i = 0; i < nDeaths; ++i) {   // [weight] death_ragdoll: the game killed it - it falls from the pose it died in, by the blow that killed it
        const Info& f = deaths[i].info;
        const deathragdoll::Push& push = deaths[i].push;
        DownBody* was = DownOf(f.id, nowMs, false);
        int r = 1;
        if (!was) {   // (one the library has already - down, held, staggering - stays its own; a body on its feet falls anew in the poses' loop)
            D2rfKnock k{};
            k.size = sizeof k;
            k.id = f.id;
            std::memcpy(k.joint, f.joint, sizeof k.joint);
            k.have = f.have;
            deathragdoll::DeathVel(f.vel, k.vel);   // (its walk only: a pelvis read off two rigs is no motion)
            k.floorY = f.body.floorY;
            k.massKg = f.body.massKg;
            std::memcpy(k.impulse, push.impulse, sizeof k.impulse);
            std::memcpy(k.point, push.point, sizeof k.point);
            k.kind = D2RF_BLOW;
            k.lieS = deathragdoll::kLieS;
            FlipJoints(k.joint);
            Flip(k.vel);
            Flip(k.impulse);
            Flip(k.point);
            if (f.swap) knockpose::SwapSides(k.joint, &k.have);
            r = falllib::KnockDown(k);
        }
        DownBody* d = r == 1 ? DownOf(f.id, nowMs, true) : nullptr;
        if (!d) {
            ++leftFull;
            LogF("death ragdoll: %u ('%s') killed - the knock-down library did not take it (%d: %s) - the game's own death", f.id, f.model, r,
                 r == 0 ? "8 bodies are down already, or its joints make no body" : r == 1 ? "no slot" : "an error");
            continue;
        }
        if (!was) {
            d->swap = f.swap;
            d->kind = D2RF_BLOW;
            std::memcpy(d->root, f.root, sizeof d->root);
        }
        d->death = true;
        d->self = f.self;
        for (size_t r = 0; r < nr; ++r)   // (its skeleton as read at its death: drawn from it the ticks it is not among the bodies')
            if (rigs[r].self == f.self && rigs[r].bones == f.bones && rigs[r].joints) SaveSkel(g_downSkel[d - g_down], f.id, rigs[r]);
        std::memcpy(d->rootW, f.root, sizeof d->rootW);
        d->bones = f.bones;
        d->diedMs = d->seenMs = nowMs;
        std::memcpy(d->impulse, push.impulse, sizeof d->impulse);
        std::memcpy(d->point, push.point, sizeof d->point);
        g_any = true;
        ++fell;
        char by[96];
        if (push.byBlow) snprintf(by, sizeof by, f4 ? "by your blow (swung at %.1f m/s)" : "along your weapon's arc (the game's swing, %.1f m/s)", push.swingMs);
        else snprintf(by, sizeof by, "away from you (no blow of your hand on it a moment before)");
        LogF("death ragdoll: %u ('%s', %.0f kg, %.2f m) killed (mode %d) - falls as a ragdoll from the pose it died in, pushed %.1f m/s %s%s%s", f.id, f.model,
             f.body.massKg, f.body.height, deaths[i].mode, push.gaveMs, by, was ? " - it was " : "", was ? PhaseName(g_downPhase[was - g_down].load()) : "");
    }
    if (!g_any && g_corpses.Count() == 0) {
        for (auto& s : g_deadSelves) s.store(0);   // (no dead body drawn: what hung on one gets the game's matrix back - PutHangs)
        for (auto& s : g_deadKnown) s.store(0);
        return n;
    }
    for (const DownBody& d : g_down) {   // home: where the game has each body now
        if (!d.id) continue;
        for (int i = 0; i < nf; ++i)
            if (fresh[i].id == d.id) {
                float home[D2RF_JOINTS][3];
                std::memcpy(home, fresh[i].joint, sizeof home);
                FlipJoints(home);
                if (d.swap) knockpose::SwapSides(home, nullptr);
                falllib::Home(d.id, home);
            }
    }
    falllib::Step(dt);
    grabmove::Tick(nowMs);   // [weight] grab_moves_unit: a knockback asked is looked at once it has run
    D2rfPose poses[D2RF_MAX_DOWN];
    const int nPoses = falllib::Poses(poses, D2RF_MAX_DOWN);
    for (int i = 0; i < nPoses; ++i) {
        FlipJoints(poses[i].joint);
        if (const DownBody* d = DownOf(poses[i].id, nowMs, false); d && d->swap) knockpose::SwapSides(poses[i].joint, nullptr);   // (back to its own left and right)
    }
    std::memcpy(g_posesNow, poses, sizeof(D2rfPose) * static_cast<size_t>(std::max(0, nPoses)));
    g_nPosesNow = std::max(0, nPoses);
    uintptr_t selves[kMaxSelves] = {};
    int nSelves = 0;
    uintptr_t deadSelves[kMaxSelves] = {};   // [weight] death_ragdoll: the dead among them (their parts put on them: PutHangs)
    int nDeadSelves = 0;
    static uint64_t toldAt = 0;
    const bool tell = nowMs - toldAt >= 1000;
    char said[700];
    int at = 0;
    said[0] = 0;
    // A pose for the skinning: rig g laid on the knock-down's joints at atModel (the model's frame) by weight - replacing one for the same skeleton (out of the
    // game's pose: its sway turns give way); lay: its bodies laid on it too (LayBodies - a living body's; the dead are no bodies of the scan).
    const auto build = [&](const skel::MonsterRig* g, uint32_t id, float weight, const float (*atModel)[3], dollpose::Pose& pose) {
        pose = dollpose::Pose{};
        pose.self = g->self;
        pose.id = id;
        pose.bones = g->bones;
        pose.tickMs = nowMs;
        pose.entity = g->entity;   // (the skinning tells bodies alike apart by the entity drawn: dollpose::ByEntity)
        pose.nParts = std::min(g->nParts, dollpose::Pose::kParts);
        for (int k = 0; k < pose.nParts; ++k) pose.parts[k] = g->parts[k];
        skel::WorldPoses(g->self, &pose.poseCur, &pose.posePrev, &pose.skel);
        pose.knock.weight = std::clamp(weight, 0.0f, 1.0f);
        std::memcpy(pose.knock.joint, g->joint, sizeof pose.knock.joint);
        std::memcpy(pose.knock.parent, g->parent, sizeof(int16_t) * static_cast<size_t>(std::min(g->bones, dollpose::kMaxBones)));
        std::memcpy(pose.knock.m3, g->m3, sizeof pose.knock.m3);
        std::memcpy(pose.knock.root, g->root, sizeof pose.knock.root);
        std::memcpy(pose.knock.at, atModel, sizeof pose.knock.at);
    };
    const auto emit = [&](const skel::MonsterRig* g, uint32_t id, float weight, const float (*atModel)[3], bool lay) {
        if (n >= cap) return;
        dollpose::Pose pose;
        build(g, id, weight, atModel, pose);
        if (nSelves < kMaxSelves) selves[nSelves++] = g->self;
        int slot = n;
        for (int s = 0; s < n; ++s)
            if (made[s].self == g->self) slot = s;
        made[slot] = pose;
        if (slot == n) ++n;
        if (lay && nLaid < D2RF_MAX_DOWN) {
            Laid& l = laid[nLaid++];
            l = Laid{};
            l.id = pose.id;
            l.self = pose.self;
            l.bones = pose.bones;
            l.knock = pose.knock;
            l.entity[l.nEntity++] = g->entity;
            for (int k = 0; k < g->nParts && l.nEntity < (int)std::size(l.entity); ++k) l.entity[l.nEntity++] = g->parts[k];
        }
    };
    const auto markFrozen = [&](uintptr_t self) {   // [weight] death_ragdoll: the pose made for a kept corpse is drawn from its frozen bones (FrozenBonesOf)
        for (int s = 0; s < n; ++s)
            if (made[s].self == self) made[s].knock.frozen = true;
    };
    // [weight] death_ragdoll: the skeleton a dead body died in, as read now - the instance, standing where it stood (the dead are not the scan's units)
    const auto deadRig = [&](uintptr_t self, const float root[3], int bones) -> const skel::MonsterRig* {
        for (size_t r = 0; r < nr; ++r)
            if (rigs[r].joints && deathragdoll::SameSkeleton(self, root, bones, rigs[r].self, rigs[r].root, rigs[r].bones, upm)) return &rigs[r];
        return nullptr;
    };
    const auto listedDead = [&](uint32_t id) {
        for (int i = 0; i < nDeadIds; ++i)
            if (deadIds[i] == id) return true;
        return false;
    };
    for (int i = 0; i < nPoses; ++i) {
        const D2rfPose& p = poses[i];
        DownBody* d = DownOf(p.id, nowMs, false);
        const skel::MonsterRig* g = nullptr;
        int mode = -1;
        bool hostile = false;
        for (int m = 0; m < nUnits && !g; ++m)
            if (units[m].id == p.id && rigOf[m] >= 0 && static_cast<size_t>(rigOf[m]) < nr && rigs[rigOf[m]].joints) {
                g = &rigs[rigOf[m]];
                mode = units[m].mode;
                hostile = units[m].hostile;
            }
        if (!d) { falllib::LetGo(p.id, 0); continue; }
        for (int s = 0; s < D2RF_MAX_DOWN; ++s)
            if (g_down[s].id == p.id) g_downPhase[s].store(p.phase);
        if (d->death) {   // [weight] death_ragdoll: the game killed it - it falls, and once it lies still it is kept as it lies (the library's slot freed)
            const skel::MonsterRig* dg = deadRig(d->self, d->rootW, d->bones);
            CorpseSkel& dsk = g_downSkel[d - g_down];
            static skel::MonsterRig unreadDown;   // (the timer's thread)
            if (dg) {
                SaveSkel(dsk, p.id, *dg);
            } else if (deathOn && listedDead(p.id) && dsk.id == p.id && dsk.self == d->self) {   // its unit still the game's dead, only not among the bodies read
                LoadSkel(dsk, unreadDown);
                dg = &unreadDown;
                g_corpseUnread.fetch_add(1, std::memory_order_relaxed);
            }
            if (!dg || !deathOn || !listedDead(p.id)) {
                if (!deathOn || nowMs - d->seenMs > kGoneMs) {
                    falllib::LetGo(p.id, 0);
                    ++gone;
                    LogF("death ragdoll: %u let go of while %s - %s", p.id, PhaseName(p.phase),
                         !deathOn ? "switched off" : dg ? "the game took its corpse away (its unit is not among the game's dead any more)" : "its skeleton never read since it died");
                }
                continue;
            }
            d->seenMs = nowMs;
            std::memcpy(d->rootW, dg->root, sizeof d->rootW);   // (the game may ease its model to the cell: the same instance, found where it is now)
            const deathragdoll::Next next = deathragdoll::OnPhase(p.phase);
            if (next == deathragdoll::Next::Fell && !d->fellAgain) {   // it died on its feet (staggering, walking home): it falls anew from where it is drawn
                d->fellAgain = true;
                D2rfKnock k{};
                k.size = sizeof k;
                k.id = p.id;
                std::memcpy(k.joint, p.joint, sizeof k.joint);
                k.have = (1u << D2RF_JOINTS) - 1u;
                k.floorY = dg->root[1] / upm;
                k.massKg = 50.0f;   // (the library keeps the weight it had)
                std::memcpy(k.impulse, d->impulse, sizeof k.impulse);
                std::memcpy(k.point, d->point, sizeof k.point);
                k.kind = D2RF_BLOW;
                k.lieS = deathragdoll::kLieS;
                FlipJoints(k.joint);
                Flip(k.impulse);
                Flip(k.point);
                if (d->swap) knockpose::SwapSides(k.joint, &k.have);
                const int r = falllib::KnockDown(k);
                LogF("death ragdoll: %u died on its feet (%s) - %s", p.id, PhaseName(p.phase), r == 1 ? "falls from where it is drawn" : "the library did not fell it");
            }
            float atModel[D2RF_JOINTS][3];
            bool ok = true;
            for (int j = 0; j < D2RF_JOINTS && ok; ++j) ok = knockpose::ToModel(dg->m3, dg->root, upm, p.joint[j], atModel[j]);
            if (!ok) continue;
            emit(dg, p.id, p.weight, atModel, true);
            if (nDeadSelves < kMaxSelves) deadSelves[nDeadSelves++] = dg->self;
            if (next == deathragdoll::Next::Keep) {
                deathragdoll::Corpse c;
                c.id = p.id;
                c.self = dg->self;
                std::memcpy(c.root, dg->root, sizeof c.root);
                c.bones = dg->bones;
                std::memcpy(c.at, p.joint, sizeof c.at);   // (the world: the game moving its model moves nothing of ours)
                c.seenMs = c.drawnMs = nowMs;
                if (FreezeBones(*dg, atModel) && p.weight >= 0.999f) markFrozen(dg->self);
                const float dx = p.joint[D2RF_PELVIS][0] - dg->root[0] / upm, dz = p.joint[D2RF_PELVIS][2] - dg->root[2] / upm;
                c.offM = std::sqrt(dx * dx + dz * dz);
                const uint32_t out = g_corpses.Keep(c);
                for (int k = 0; k < deathragdoll::kMaxCorpses; ++k) {   // its skeleton kept for the ticks it is not read (the slot of the corpse it is, by its place in the table)
                    const deathragdoll::Corpse* kc = g_corpses.At(k);
                    if (!kc || kc->id != p.id) continue;
                    SaveSkel(g_corpseSkel[k], p.id, *dg);
                }
                falllib::LetGo(p.id, 0);
                ++kept;
                char room[120] = "";
                if (out) snprintf(room, sizeof room, " (%d kept: %u, kept longest ago, made room - drawn as the game has it)", deathragdoll::kMaxCorpses, out);
                LogF("death ragdoll: %u frozen where it lies, %.1f s after it died (its hips %.2f m from the game's corpse) - its bones as they came to rest, held "
                     "until the game takes the corpse away; no physics, nothing of the game's moved%s",
                     p.id, (nowMs - d->diedMs) / 1000.0f, c.offM, room);
            }
            if (tell && at < (int)sizeof said - 140)
                at += snprintf(said + at, sizeof said - at, "%s%u dead, %s %.1f s (hips %.2f m over its floor, weight %.2f)", at ? "; " : "", p.id, PhaseName(p.phase),
                               p.t, p.joint[D2RF_PELVIS][1] - dg->root[1] / upm, p.weight);
            continue;
        }
        if (!g) {   // its rig not read (out of reach, a scan missed): let go of after a moment
            if (nowMs - d->seenMs > kGoneMs) {
                falllib::LetGo(p.id, 0);
                LogF("knock down: %u let go of - not seen for %.1f s", p.id, (nowMs - d->seenMs) / 1000.0f);
            }
            continue;
        }
        d->seenMs = nowMs;
        std::memcpy(d->place, g->root, sizeof d->place);
        d->placeOk = true;
        if (mode == 0 || mode == 12) {   // the game killed it: its own death from here (the corpse is the game's)
            falllib::LetGo(p.id, 0);
            LogF("knock down: %u died %s - the game's own death from here", p.id, PhaseName(p.phase));
            continue;
        }
        if (d->grabbed && grabmove::Wanted()) {   // [weight] grab_moves_unit: the unit put by the game's knockback on the cell under the drawn body (its pelvis)
            for (int i = 0; i < nf && (grabmove::PhaseMoves(p.phase) || p.phase == walkhome::kWalking); ++i)   // (up again too: the unit brought under it before it is let go)
                if (fresh[i].id == p.id) {
                    float dx = 0.0f, dy = 0.0f;
                    if (grabmove::CellsApart(p.joint[D2RF_PELVIS], fresh[i].joint[D2RF_PELVIS], upm, &dx, &dy)) grabmove::Note(p.id, dx, dy, hostile, nowMs);
                    break;
                }
            std::memcpy(d->root, g->root, sizeof d->root);   // (the game moved it there for us: not a walk off that gets a lying body up)
        }
        const float off = std::sqrt((g->root[0] - d->root[0]) * (g->root[0] - d->root[0]) + (g->root[2] - d->root[2]) * (g->root[2] - d->root[2])) / upm;
        const bool attacks = parry::MonsterAttacking(mode) || mode == 7;
        const bool down = p.phase == D2RF_FALLING || p.phase == D2RF_LYING || p.phase == D2RF_GETTING_UP;
        if (!d->hurried && down && (attacks || off > kWalkedOffM)) {   // it lies while the game has it fight or walk off: up now, quicker (then home)
            d->hurried = true;
            falllib::LetGo(p.id, 1);
            LogF("knock down: %u gets up now - the game %s (mode %d)", p.id, attacks ? "has it attack" : "walked it off", mode);
        }
        if (walkhome::BackNow(p.phase, p.off)) {   // up again and the game has it where it is drawn: let go wholly, the game's own from this cell (vr/walk_home.h)
            float dx = 0.0f, dy = 0.0f;
            float gamePelvis[3] = {g->root[0] / upm, p.joint[D2RF_PELVIS][1], g->root[2] / upm};   // (the game's cell: its root; the drawn pelvis's height, no shear)
            grabmove::CellsApart(p.joint[D2RF_PELVIS], gamePelvis, upm, &dx, &dy);
            falllib::LetGo(p.id, 0);
            LogF("knock down: %u released %.1f s after it got up - drawn on cell %.0f,%.0f, the game has it on %.0f,%.0f (%.2f m apart): the game's own animation, AI "
                 "and walk from this cell%s", p.id, p.t, (g->root[0] + dx * grabmove::kUnitsPerCell) / grabmove::kUnitsPerCell,
                 (g->root[2] + dy * grabmove::kUnitsPerCell) / grabmove::kUnitsPerCell, g->root[0] / grabmove::kUnitsPerCell, g->root[2] / grabmove::kUnitsPerCell,
                 p.off, attacks ? " (it attacks)" : "");
            continue;
        }
        if (n >= cap) continue;
        float atModel[D2RF_JOINTS][3];
        bool ok = true;
        for (int j = 0; j < D2RF_JOINTS && ok; ++j) ok = knockpose::ToModel(g->m3, g->root, upm, p.joint[j], atModel[j]);
        if (!ok) continue;
        emit(g, p.id, p.weight, atModel, true);
        if (tell && at < (int)sizeof said - 140)
            at += snprintf(said + at, sizeof said - at, "%s%u %s %.1f s%s (hips %.2f m over its floor, %.2f m from home, weight %.2f)", at ? "; " : "", p.id,
                           PhaseName(p.phase), p.t, p.phase == D2RF_GETTING_UP ? (p.fromBack ? " from the back" : " from the face") : "",
                           p.joint[D2RF_PELVIS][1] - g->root[1] / upm, p.off, p.weight);
    }
    // [weight] death_ragdoll: the corpses kept as they lie - each drawn so while its skeleton is read (out of the bodies' reach the skinning eases it back to
    // the game's; back in reach ours eases in again); one the scan has not had dead for a second is gone (the game took the corpse, or it is far)
    int corpsesDrawn = 0;
    if (!deathOn) {
        if (g_corpses.Count()) {
            LogF("death ragdoll: %d kept corpses released - switched off: the game's own from here", g_corpses.Count());
            g_corpses.Reset();
        }
    } else {
        uint32_t releasedIds[deathragdoll::kMaxCorpses];
        const int released = g_corpses.Forget(deadIds, nDeadIds, nowMs, releasedIds, deathragdoll::kMaxCorpses);
        gone += static_cast<unsigned>(released);
        for (int i = 0; i < released; ++i)
            LogF("death ragdoll: %u released - its unit is not among the game's dead any more (the corpse taken away, raised, exploded, or the area left)", releasedIds[i]);
        static skel::MonsterRig unread;   // (the timer's thread) a kept corpse's skeleton as last read, for a tick it is not among the bodies'
        uintptr_t keep[deathragdoll::kMaxCorpses] = {};
        int nKeep = 0;
        static dollpose::Pose keptNew[deathragdoll::kMaxCorpses], keptWas[deathragdoll::kMaxCorpses];   // (the timer's thread) the kept table made anew
        int nKept = 0, nWas = 0;
        AcquireSRWLockShared(&g_keptLock);
        nWas = g_nKept;
        for (int k = 0; k < nWas; ++k) keptWas[k] = g_kept[k];
        ReleaseSRWLockShared(&g_keptLock);
        const auto carryKept = [&](const deathragdoll::Corpse& c) {   // not refreshed this tick: its last entry, while fresh enough
            for (int k = 0; k < nWas && nKept < deathragdoll::kMaxCorpses; ++k)
                if (keptWas[k].id == c.id && keptWas[k].self == c.self && nowMs - keptWas[k].tickMs <= kKeptFreshMs) {
                    keptNew[nKept++] = keptWas[k];
                    g_keptCarried.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
        };
        for (int i = 0; i < deathragdoll::kMaxCorpses; ++i) {
            deathragdoll::Corpse* c = g_corpses.At(i);
            CorpseSkel& sk = g_corpseSkel[i];
            if (!c) { sk = CorpseSkel{}; continue; }
            if (sk.id != c->id) sk = CorpseSkel{};   // (another corpse in this slot now)
            keep[nKeep++] = c->self;
            if (c->drawnMs == nowMs) {   // (kept this tick: drawn above, from its fall's last pose; the table has it from the next)
                carryKept(*c);
                continue;
            }
            const skel::MonsterRig* g = deadRig(c->self, c->root, c->bones);
            if (g) {   // read: where its model is now, for the next tick it is not
                std::memcpy(c->root, g->root, sizeof c->root);
                std::memcpy(sk.m3, g->m3, sizeof sk.m3);
                std::memcpy(sk.root, g->root, sizeof sk.root);
            } else if (sk.id == c->id && sk.self == c->self && listedDead(c->id)) {
                LoadSkel(sk, unread);   // (its unit still the game's dead: its skeleton is there, only not among the nearest read)
                g = &unread;
                g_corpseUnread.fetch_add(1, std::memory_order_relaxed);
            }
            if (!g) { carryKept(*c); continue; }
            float atModel[D2RF_JOINTS][3];
            bool ok = true;
            for (int j = 0; j < D2RF_JOINTS && ok; ++j) ok = knockpose::ToModel(g->m3, g->root, upm, c->at[j], atModel[j]);
            if (!ok) { carryKept(*c); continue; }
            deathragdoll::Drawn(*c, nowMs);
            if (nSelves < kMaxSelves) selves[nSelves++] = g->self;   // (drawn out of the game's pose: what hangs on its bones goes with them)
            if (nDeadSelves < kMaxSelves) deadSelves[nDeadSelves++] = g->self;
            if (nKept < deathragdoll::kMaxCorpses) {   // its pose to the kept table, whole - not to the moving bodies' list (made), never eased or faded
                dollpose::Pose& kp = keptNew[nKept++];
                build(g, c->id, 1.0f, atModel, kp);
                kp.knock.kept = true;
                kp.knock.frozen = true;   // (FrozenBonesOf finds none for it: the skinning lays it on its joints instead)
                g_keptBy[std::clamp(g_tickBy.load(std::memory_order_relaxed), 0, 2)].fetch_add(1, std::memory_order_relaxed);
            }
            ++corpsesDrawn;
        }
        AcquireSRWLockExclusive(&g_keptLock);
        for (int k = 0; k < nKept; ++k) g_kept[k] = keptNew[k];
        g_nKept = nKept;
        g_nKeptNow.store(nKept, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_keptLock);
        ForgetFrozen(keep, nKeep);
        static uint64_t deathToldAt = 0;
        if (nowMs - deathToldAt >= 5000) {
            deathToldAt = nowMs;
            if (fell + kept + gone + leftBoss + leftNotSeen + leftNotHostile + leftFull || g_corpses.Count())
                LogF("death ragdoll - the last 5 s: %u fell as ragdolls, %u came to rest and are kept, %u corpses gone; %d kept now, %d of them drawn so (the rest "
                     "out of reach: drawn as the game has them); left to the game: %u bosses or bursting, %u not humanoids read alive a moment before, %u not "
                     "hostile, %u with no room in the library", fell, kept, gone, g_corpses.Count(), corpsesDrawn, leftBoss, leftNotSeen, leftNotHostile, leftFull);
            fell = kept = gone = leftBoss = leftNotSeen = leftNotHostile = leftFull = 0;
            const unsigned hangPut = g_hangPut.exchange(0), hangBack = g_hangBack.exchange(0), hangNoStore = g_hangNoStore.exchange(0), hangNoPose = g_hangNoPose.exchange(0);
            if (!f4) {   // [weight] death_ragdoll_flat: the game's swing kept (the pose hook's weapon bones), what the kills' arc is read from
                skel::WeaponTrailSample w[48];
                const int nw = skel::WeaponTrail(w, 48);
                LogF("death ragdoll flat - the last 5 s: the hero's weapon bones noted on %u of his poses (%d kept now%s)", skel::WeaponTrailNoted(), nw,
                     nw > 0 && skel::WeaponTrailNow() - w[nw - 1].t > 0.5 ? ", the newest older than half a second: his pose not read lately" : "");
            }
            const unsigned frozenDrawn = g_frozenDrawn.exchange(0), corpseUnread = g_corpseUnread.exchange(0);
            if (g_corpses.Count())
                LogF("death ragdoll - the last 5 s: the kept corpses drawn from their bones as they came to rest %u times (skinnings), %u ticks drawn from their "
                     "skeleton as last read (not among the bodies read then)", frozenDrawn, corpseUnread);
            const unsigned byTouch = g_keptBy[0].exchange(0), byGrab = g_keptBy[1].exchange(0), byDead = g_keptBy[2].exchange(0), carried = g_keptCarried.exchange(0);
            if (g_corpses.Count() || byTouch + byGrab + byDead + carried)
                LogF("death ragdoll - kept corpses, their one source (the kept table), the last 5 s: %d in it now; refreshed %u times by the touch's tick, %u by the "
                     "grab's, %u by the dead's tick, %u corpse-ticks not refreshed (their last entry handed on)", g_nKeptNow.load(), byTouch, byGrab, byDead, carried);
            const unsigned hangByComponent = g_hangByComponent.exchange(0), hangChildren = g_hangChildren.exchange(0), hangMoved = g_hangMoved.exchange(0);
            if (hangPut + hangBack + hangNoStore + hangNoPose || g_nHangs.load())
                LogF("death ragdoll - the last 5 s: what hangs on the bodies (%d parts known, the dead's and those drawn away) put on the dead as drawn %u times "
                     "(%u of them through the part's own component - the storage refused it; %u parts hung on parts), the game's matrix put back %u; not put - "
                     "not written %u (%u components no longer the part's), no pose %u", g_nHangs.load(), hangPut, hangByComponent, hangChildren, hangBack, hangNoStore,
                     hangMoved, hangNoPose);
        }
    }
    // the bodies the library no longer has: free their slots (and a hand that held one)
    for (int s = 0; s < D2RF_MAX_DOWN; ++s) {
        if (!g_down[s].id) continue;
        bool there = false;
        for (int i = 0; i < nPoses; ++i) there = there || poses[i].id == g_down[s].id;
        if (!there) {
            for (auto& h : g_hold)
                if (h.id == g_down[s].id) h = HandHold{};
            for (int b = 0; b < 2; ++b)
                if (g_blade[b].id == g_down[s].id) { g_blade[b] = OnBlade{}; g_bladeIds[b].store(0); }
            g_down[s] = DownBody{};
            g_downIds[s].store(0);
            g_downPhase[s].store(D2RF_NONE);
        }
    }
    g_any = nPoses > 0;
    for (int s = 0; s < kMaxSelves; ++s) g_downSelves[s].store(s < nSelves ? selves[s] : 0);
    for (int s = 0; s < kMaxSelves; ++s) g_deadSelves[s].store(s < nDeadSelves ? deadSelves[s] : 0);
    {   // [weight] death_ragdoll: every dead body we have - falling in the library or kept as it lies - drawn this tick or not
        uintptr_t known[kMaxSelves] = {};
        int nk = 0;
        for (const DownBody& d : g_down)
            if (d.id && d.death && d.self && nk < kMaxSelves) known[nk++] = d.self;
        for (int i = 0; i < deathragdoll::kMaxCorpses && nk < kMaxSelves; ++i)
            if (const deathragdoll::Corpse* c = g_corpses.At(i)) known[nk++] = c->self;
        for (int s = 0; s < kMaxSelves; ++s) g_deadKnown[s].store(s < nk ? known[s] : 0);
    }
    lib.unlock();
    if (tell && at) {
        toldAt = nowMs;
        const unsigned carried = g_carried.exchange(0), missed = g_carryMissed.exchange(0);
        const unsigned laidN = g_laidBodies.exchange(0), laidMissed = g_laidMissed.exchange(0);
        const unsigned grew = g_boundsGrew.exchange(0), held = g_boundsHeld.exchange(0), rebuilt = g_boundsRebuilt.exchange(0), bmissed = g_boundsMissed.exchange(0),
                       putBack = g_boundsPutBack.exchange(0), grewTimer = g_boundsGrewTimer.exchange(0), hookMet = g_boundsHookMet.exchange(0),
                       noWorld = g_boundsNoWorld.exchange(0), grewDraw = g_boundsGrewDraw.exchange(0), drawEyes = g_drawEyes.exchange(0),
                       drawShadow = g_drawShadow.exchange(0), drawHeld = g_drawHeld.exchange(0), drawRebuilt = g_drawRebuilt.exchange(0);
        const int from = knockprops::EntityBoundsSource();
        int entities = 0;
        {
            AcquireSRWLockShared(&g_growLock);
            entities = g_nGrow;
            ReleaseSRWLockShared(&g_growLock);
        }
        LogF("knock down: kept in view - %d entities of the bodies drawn away; their bounds grown by the world-matrix hook %u times (it met them %u), by the timer "
             "%u (no matrix read %u) - %s; before each: the game's box held the drawn body %u times, made again without it %u; no bounds read %u; put back %u",
             entities, grew, hookMet, grewTimer, noWorld, from == 1 ? "the props' bounds storage" : from == 2 ? "the bounds storage found by a unit's box" : "NO bounds storage found yet",
             held, rebuilt, bmissed, putBack);
        LogF("knock down: kept in view at the draw collect - the bodies' entities collected for the eyes' passes %u times, the shadows' %u (a pass that "
             "leaves one out before its collect is not counted); their bounds grown there %u times, just before the game's - the box then held the drawn body "
             "%u times, made again without it %u", drawEyes, drawShadow, grewDraw, drawHeld, drawRebuilt);
        LogF("knock down: %d out of the game's pose - %s; what hangs on their bones (weapons, shields) carried %u times since the last line%s; their bodies (the "
             "weapon's, the grab's) laid on them as drawn: %u capsules%s", nPoses, said, carried,
             missed ? (", " + std::to_string(missed) + " not (the pose not found)").c_str() : "", laidN,
             laidMissed ? (", " + std::to_string(laidMissed) + " times a pose did not read (left where the game has them)").c_str() : "");
    }
    return n;
}

void LayBodies(monsterbodies::Snapshot* s, float upm) {
    struct PublishGrow {   // (on every way out: the entities and their boxes for the world-matrix hook - none when no body is drawn away)
        Grow g[64];
        int n = 0;
        ~PublishGrow() {
            AcquireSRWLockExclusive(&g_growLock);
            for (int i = 0; i < n; ++i) g_grow[i] = g[i];
            g_nGrow = n;
            g_nGrowNow.store(n);
            ReleaseSRWLockExclusive(&g_growLock);
            if (n > 0 || g_nGrownNow.load(std::memory_order_relaxed) > 0) GrowBoundsNow(g, n);   // (and on the timer: the hook may never see them)
        }
    } grow;
    if (!s || s->n <= 0 || !(upm > 1e-3f)) return;
    static Laid laid[D2RF_MAX_DOWN];   // (the timer's thread only)
    int nl = 0;
    {
        std::lock_guard<std::mutex> lock(g_laidMutex);
        nl = g_nLaid;
        for (int i = 0; i < nl; ++i) laid[i] = g_laid[i];
    }
    static float game[knockpose::kMaxBones][16], drawn[knockpose::kMaxBones][16];
    for (int l = 0; l < nl; ++l) {
        const Laid& L = laid[l];
        if (!(L.knock.weight > 0.0f)) continue;
        bool any = false;
        for (int i = 0; i < s->n && !any; ++i) any = s->b[i].id == L.id && s->b[i].rig;
        if (!any) continue;
        const int n = skel::PoseNow(L.self, game, knockpose::kMaxBones);
        if (n != L.bones || n <= 0) { g_laidMissed.fetch_add(1, std::memory_order_relaxed); continue; }
        std::memcpy(drawn, game, sizeof(float) * 16 * static_cast<size_t>(n));
        if (!knockpose::Apply(drawn, n, L.knock.parent, L.knock.joint, L.knock.at, L.knock.weight)) { g_laidMissed.fetch_add(1, std::memory_order_relaxed); continue; }
        // a bone's place in the world (world units): its matrix's origin through the model's
        const auto placeOf = [&](const float m[16], float out[3]) {
            for (int j = 0; j < 3; ++j) out[j] = m[12] * L.knock.m3[j] + m[13] * L.knock.m3[3 + j] + m[14] * L.knock.m3[6 + j] + L.knock.root[j];
        };
        const auto d2 = [](const float a[3], const float b[3]) { return (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]); };
        for (int i = 0; i < s->n; ++i) {
            monsterbodies::Body& b = s->b[i];
            if (b.id != L.id || !b.rig) continue;
            float pa[3], pb[3];
            for (int k = 0; k < 3; ++k) { pa[k] = b.bottom[k] * upm; pb[k] = b.top[k] * upm; }
            int bone = b.bone;
            if (bone < 0 || bone >= n) {   // a rigid item it holds (on no bone of its skeleton): with the hand nearer it - unless the world-matrix hook has
                                           // already carried it there (OnXform writes the item's own world matrix, the rig may have read it so)
                const float mid[3] = {(pa[0] + pb[0]) * 0.5f, (pa[1] + pb[1]) * 0.5f, (pa[2] + pb[2]) * 0.5f};
                bone = -1;
                float best = 1e30f, bestDrawn = 1e30f;
                for (const int w : {L.knock.joint[D2RF_L_WRIST], L.knock.joint[D2RF_R_WRIST]}) {
                    if (w < 0 || w >= n) continue;
                    float g[3], dr[3];
                    placeOf(game[w], g);
                    placeOf(drawn[w], dr);
                    if (d2(mid, g) < best) { best = d2(mid, g); bestDrawn = d2(mid, dr); bone = w; }
                }
                if (bone < 0 || bestDrawn < best) continue;
            }
            float qa[3], qb[3];
            if (!knockpose::CarryPoint(pa, game[bone], drawn[bone], L.knock.m3, L.knock.root, qa) ||
                !knockpose::CarryPoint(pb, game[bone], drawn[bone], L.knock.m3, L.knock.root, qb))
                continue;
            for (int k = 0; k < 3; ++k) { b.bottom[k] = qa[k] / upm; b.top[k] = qb[k] / upm; }
            g_laidBodies.fetch_add(1, std::memory_order_relaxed);
        }
        // where it is drawn (world units): its capsules laid, all round - each of its entities' bounds to hold it (OnXform)
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (int i = 0; i < s->n; ++i) {
            const monsterbodies::Body& b = s->b[i];
            if (b.id != L.id || !b.rig) continue;
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], (std::min(b.bottom[k], b.top[k]) - b.radius - kBoundsMarginM) * upm);
                hi[k] = std::max(hi[k], (std::max(b.bottom[k], b.top[k]) + b.radius + kBoundsMarginM) * upm);
            }
        }
        if (!(lo[0] <= hi[0])) continue;
        for (int e = 0; e < L.nEntity && grow.n < (int)std::size(grow.g); ++e) {
            if (!L.entity[e]) continue;
            Grow& g = grow.g[grow.n++];
            g.entity = L.entity[e];
            std::memcpy(g.lo, lo, sizeof lo);
            std::memcpy(g.hi, hi, sizeof hi);
        }
    }
}

namespace {
int ReadM(uintptr_t at, float* m) noexcept {
    __try {
        std::memcpy(m, reinterpret_cast<const void*>(at), sizeof(float) * 16);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
int WriteM(uintptr_t at, const float* m) noexcept {
    __try {
        std::memcpy(reinterpret_cast<void*>(at), m, sizeof(float) * 16);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
}  // namespace

// The world-matrix hook (the renderer's thread, after the game made `out`): an entity hanging on a bone of a body drawn out of the game's pose - its weapon,
// its shield - carried with that bone as drawn (Serg on the first build: «если это нпс с оружием оружие должно оставаться у него в руках сейчас оружие
// остается висеть в воздухе»). The game made it from the bone as the game has it: out x X^-1 x B^-1 x B' x X, B the bone's matrix in the game's pose, B' in
// ours (the same copy the skinning is handed, laid on the library's joints), X the model's place in the world.
// The bounds of an entity of a body drawn away from its place grown to hold where it is drawn (its world matrix just made: `out`); one the game has again put
// back as it was. Serg, 0.192.33: «почему-то модельки нпс пропадают когда я их поднимаю» - the renderer leaves out what its box says is out of view, and
// the box was the game place's (vr/held_bounds.h).
// Two ways in, as the game may make the boxes: the world-matrix hook (each entity the moment its matrix is made: `out`) and the touch's timer (LayBodies,
// every 10 ms: the matrix the renderer has) - 0.192.34 grew by the hook alone and nothing was ever grown (the hook may not make a unit's matrices): its
// walking home flickered and vanished. The ids matched by their index (the hook's and the storages' may differ in the version bits).
bool SameEntity(uint32_t a, uint32_t b) { return a && b && (a & 0xFFFFF) == (b & 0xFFFFF); }
// Under g_grownMutex: entity (the storages' own id) grown to hold want (null: its own bounds put back), m its world matrix now.
void GrowOneLocked(uint32_t entity, const float m[16], const Grow* want, std::atomic<unsigned>& grewCount, std::atomic<unsigned>* heldC = &g_boundsHeld,
                   std::atomic<unsigned>* rebuiltC = &g_boundsRebuilt) {
    int at = -1;
    for (int i = 0; i < g_nGrown && at < 0; ++i)
        if (SameEntity(g_grown[i].entity, entity)) at = i;
    if (!want && at < 0) return;
    float box[24];
    if (!knockprops::EntityBounds(entity, m, box)) { if (want) g_boundsMissed.fetch_add(1, std::memory_order_relaxed); return; }
    if (!want) {   // its body is the game's again: its own bounds back (its world box under its matrix now)
        float back[24];
        std::memcpy(back, g_grown[at].orig, sizeof back);
        heldbounds::WorldOf(back, back + 4, m, back + 16, back + 20);
        if (knockprops::PutEntityBounds(entity, back)) g_boundsPutBack.fetch_add(1, std::memory_order_relaxed);
        g_grown[at] = g_grown[--g_nGrown];
        g_nGrownNow.store(g_nGrown);
        return;
    }
    if (at < 0) {   // (first touch: its bounds as the game has them, to grow from and put back)
        if (g_nGrown >= (int)std::size(g_grown)) g_grown[0] = g_grown[--g_nGrown];
        at = g_nGrown++;
        g_grown[at].entity = entity;
        std::memcpy(g_grown[at].orig, box, sizeof box);
        g_nGrownNow.store(g_nGrown);
    }
    (heldbounds::Holds(box, want->lo, want->hi) ? *heldC : *rebuiltC).fetch_add(1, std::memory_order_relaxed);
    float grown[24];
    if (heldbounds::Grown(g_grown[at].orig, m, want->lo, want->hi, grown) && knockprops::PutEntityBounds(entity, grown)) grewCount.fetch_add(1, std::memory_order_relaxed);
}
// The hook's way in (the renderer's threads).
void GrowBounds(uint32_t entity, uintptr_t out) {
    if (g_nGrowNow.load(std::memory_order_relaxed) == 0 && g_nGrownNow.load(std::memory_order_relaxed) == 0) return;
    Grow want;
    bool wanted = false;
    AcquireSRWLockShared(&g_growLock);
    for (int i = 0; i < g_nGrow && !wanted; ++i)
        if (SameEntity(g_grow[i].entity, entity)) { want = g_grow[i]; wanted = true; }
    ReleaseSRWLockShared(&g_growLock);
    if (!wanted && g_nGrownNow.load(std::memory_order_relaxed) == 0) return;
    float m[16];
    if (ReadM(out, m) <= 0) return;
    std::lock_guard<std::mutex> lock(g_grownMutex);
    if (wanted) {
        g_boundsHookMet.fetch_add(1, std::memory_order_relaxed);
        GrowOneLocked(want.entity, m, &want, g_boundsGrew);
        return;
    }
    for (int i = 0; i < g_nGrown; ++i)
        if (SameEntity(g_grown[i].entity, entity)) { GrowOneLocked(g_grown[i].entity, m, nullptr, g_boundsGrew); return; }
}
// The timer's way in (LayBodies): every entity of the bodies drawn away grown by the matrix the renderer has, and those no longer drawn so put back.
void GrowBoundsNow(const Grow* grow, int n) {
    std::lock_guard<std::mutex> lock(g_grownMutex);
    for (int i = 0; i < n; ++i) {
        float m[16];
        if (!knockprops::EntityWorld(grow[i].entity, m)) { g_boundsNoWorld.fetch_add(1, std::memory_order_relaxed); continue; }
        GrowOneLocked(grow[i].entity, m, &grow[i], g_boundsGrewTimer);
    }
    for (int i = g_nGrown; i-- > 0;) {
        bool still = false;
        for (int k = 0; k < n && !still; ++k) still = SameEntity(grow[k].entity, g_grown[i].entity);
        float m[16];
        if (!still && knockprops::EntityWorld(g_grown[i].entity, m)) GrowOneLocked(g_grown[i].entity, m, nullptr, g_boundsGrewTimer);
    }
}

// The draw collect's way in (the render workers, just before the game's ModelDrawCollect for this entity and pass).
void BeforeDraw(uint32_t entity, bool eyes) {
    if (g_nGrowNow.load(std::memory_order_relaxed) == 0) return;
    Grow want;
    bool wanted = false;
    AcquireSRWLockShared(&g_growLock);
    for (int i = 0; i < g_nGrow && !wanted; ++i)
        if (SameEntity(g_grow[i].entity, entity)) { want = g_grow[i]; wanted = true; }
    ReleaseSRWLockShared(&g_growLock);
    if (!wanted) return;
    (eyes ? g_drawEyes : g_drawShadow).fetch_add(1, std::memory_order_relaxed);
    float m[16];
    if (!knockprops::EntityWorld(want.entity, m)) return;
    std::lock_guard<std::mutex> lock(g_grownMutex);
    GrowOneLocked(want.entity, m, &want, g_boundsGrewDraw, &g_drawHeld, &g_drawRebuilt);
}

namespace {
bool In(const std::atomic<uintptr_t>* set, uintptr_t self) {
    if (!self) return false;
    for (int s = 0; s < kMaxSelves; ++s)
        if (set[s].load(std::memory_order_relaxed) == self) return true;
    return false;
}
bool DeadSelf(uintptr_t self) { return In(g_deadSelves, self); }    // a dead body drawn by us this tick
bool DeadKnown(uintptr_t self) { return In(g_deadKnown, self); }    // ... a dead body we have, drawn or not
bool DownSelf(uintptr_t self) { return In(g_downSelves, self); }    // any body drawn out of the game's pose (its parts noted before it dies)

// A body's bones as drawn: a kept corpse's frozen ones (FrozenBonesOf), else the game's pose laid on the pose's joints.
bool DrawnBones(uintptr_t self, const dollpose::Pose& pose, float (*game)[16], float (*drawn)[16], int n) {
    if (pose.knock.frozen && pose.knock.weight >= 0.999f && FrozenBonesOf(self, pose.knock.m3, pose.knock.root, drawn, n)) return true;
    std::memcpy(drawn, game, sizeof(float) * 16 * static_cast<size_t>(n));
    return knockpose::Apply(drawn, n, pose.knock.parent, pose.knock.joint, pose.knock.at, pose.knock.weight);
}

int ReadU32(uintptr_t at, uint32_t* v) noexcept {
    __try {
        std::memcpy(v, reinterpret_cast<const void*>(at), sizeof *v);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// A part's world matrix written: through the props' transform storage (its entity's own slot), else through its component as the hook handed it - if
// that still holds this part (its parent and its local matrix as noted: a storage that moved its components would hold another one there). 1 the storage,
// 2 its component, 0 not written.
int WriteHang(const deathragdoll::Hang& h, const float w[16]) {
    if (knockprops::PutEntityWorld(h.entity, w)) return 1;
    float local[16], old[16];
    uint32_t parentField = 0;
    if (!h.component || ReadM(h.component, local) <= 0 || ReadU32(h.component + 0xC0, &parentField) <= 0) return 0;
    if (!deathragdoll::SameComponent(h, local, parentField)) { g_hangMoved.fetch_add(1, std::memory_order_relaxed); return 0; }
    if (ReadM(h.component + 0x40, old) > 0) WriteM(h.component + 0x80, old);   // (the one drawn until now: the last frame's slot - no motion vector left behind)
    return WriteM(h.component + 0x40, w) > 0 ? 2 : 0;
}

// [weight] death_ragdoll, once a frame from the world-matrix hook (any entity's): each part hung on a dead body put on its bone as drawn now - the game makes
// its matrix no more once the death animation is over (deathragdoll::Hangs) - and each part hung on such a part after it (its local matrix times the parent's
// world); a body not drawn by us now gets the game's matrices back (its parts kept while the body is ours), a body gone gets them back and is forgotten.
void PutHangs() {
    if (g_nHangs.load(std::memory_order_relaxed) == 0) return;
    static std::atomic<int64_t> passAt{0};
    static std::atomic<bool> passing{false};
    static uint32_t passNo = 0;
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    if (t.QuadPart - passAt.load(std::memory_order_relaxed) < f.QuadPart / 200 || passing.exchange(true, std::memory_order_acquire)) return;   // (once a frame: 5 ms)
    passAt.store(t.QuadPart, std::memory_order_relaxed);
    ++passNo;
    static dollpose::Pose pose;   // (one pass at a time: `passing`)
    static float game[knockpose::kMaxBones][16], drawn[knockpose::kMaxBones][16];
    uintptr_t posed = 0;   // the body `drawn` holds now (0: none read)
    int n = 0;
    AcquireSRWLockExclusive(&g_hangLock);
    const auto put = [&](deathragdoll::Hang& h, const float w[16]) {
        const int how = WriteHang(h, w);
        if (!how) { g_hangNoStore.fetch_add(1, std::memory_order_relaxed); return; }
        if (how == 2) g_hangByComponent.fetch_add(1, std::memory_order_relaxed);
        h.ours = true;
        h.pass = passNo;
        std::memcpy(h.now, w, sizeof h.now);
        g_hangPut.fetch_add(1, std::memory_order_relaxed);
    };
    const auto back = [&](deathragdoll::Hang& h) {
        if (WriteHang(h, h.game)) g_hangBack.fetch_add(1, std::memory_order_relaxed);
        h.ours = false;
    };
    // the parts on the bones first (level 0), then the parts on parts, each after the one it hangs on (a few levels down)
    bool done[deathragdoll::Hangs::kMax] = {};
    for (int level = 0; level < 4; ++level)
        for (int i = 0; i < deathragdoll::Hangs::kMax; ++i) {
            deathragdoll::Hang* h = g_hangs.At(i);
            if (!h || done[i] || (level == 0) != (h->bone >= 0)) continue;
            const bool known = DeadKnown(h->self);
            if (!known && DownSelf(h->self)) { done[i] = true; continue; }   // (a living body drawn away: the hook carries its parts each time the game makes them)
            bool poseNow = false;
            const deathragdoll::Hang* on = nullptr;
            if (known && DeadSelf(h->self)) {
                if (h->bone >= 0) {
                    if (posed != h->self) {
                        posed = h->self;
                        n = 0;
                        if (meleetouch::MonsterPose(h->self, &pose) && pose.knock.weight > 0.0f) {
                            const int got = skel::PoseNow(h->self, game, knockpose::kMaxBones);
                            if (got == pose.bones && got > 0 && DrawnBones(h->self, pose, game, drawn, got)) n = got;
                        }
                    }
                    poseNow = n > 0 && h->bone < n;
                } else {
                    on = g_hangs.Of(h->onEntity);
                    if (on && on->pass != passNo && on->ours && level < 3) continue;   // (its parent not put yet this pass: the next level)
                    poseNow = on && on->pass == passNo;
                }
                if (!poseNow) g_hangNoPose.fetch_add(1, std::memory_order_relaxed);
            }
            done[i] = true;
            const deathragdoll::HangStep step = deathragdoll::HangNext(known, poseNow, h->ours);
            if (step.act == deathragdoll::HangDo::Carry) {
                float o[16];
                if (h->bone >= 0) {
                    if (knockpose::Carry(h->game, h->boneGame, drawn[h->bone], pose.knock.m3, pose.knock.root, o)) put(*h, o);
                } else {
                    deathragdoll::ChildWorld(h->local, on->now, o);
                    put(*h, o);
                    g_hangChildren.fetch_add(1, std::memory_order_relaxed);
                }
            } else if (step.act == deathragdoll::HangDo::PutBack) {
                back(*h);
            }
            if (step.drop && !DownSelf(h->self)) g_hangs.Drop(*h);   // (a living body drawn away keeps its parts noted: it may die)
        }
    g_nHangs.store(g_hangs.Count(), std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&g_hangLock);
    passing.store(false, std::memory_order_release);
}
}  // namespace

void OnXform(uint32_t entity, uintptr_t component, uintptr_t out) {
    if (!Wanted() || out < 0x10000) return;
    GrowBounds(entity, out);
    PutHangs();   // [weight] death_ragdoll
    bool any = false;
    for (auto& s : g_downSelves) any = any || s.load(std::memory_order_relaxed) != 0;
    if (!any) return;
    float m[16], local[16];
    uint32_t parentField = 0;
    if (g_nHangs.load(std::memory_order_relaxed) > 0 && component > 0x10000 && ReadU32(component + 0xC0, &parentField) > 0 && (parentField & 0xFFFFFu) != 0xFFFFFu) {
        // [weight] death_ragdoll: a part hung on a part we keep (a forearm piece on the upper arm's) - noted to go with it (no bone of its own)
        AcquireSRWLockExclusive(&g_hangLock);
        if (const deathragdoll::Hang* on = g_hangs.OfSlot(parentField); on && on->entity != entity && ReadM(out, m) > 0 && ReadM(component, local) > 0) {
            const uintptr_t onSelf = on->self;
            const uint32_t onEntity = on->entity;
            const float none[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            g_hangs.Note(entity, onSelf, -1, onEntity, m, none, local, parentField, component);   // (the game just made it: its own matrix is in it)
            g_nHangs.store(g_hangs.Count(), std::memory_order_relaxed);
        }
        ReleaseSRWLockExclusive(&g_hangLock);
    }
    uintptr_t self = 0;
    int32_t bone = -1;
    if (!headshadow::AttachmentOf(entity, component, &self, &bone)) return;
    bool ours = false;
    for (auto& s : g_downSelves) ours = ours || s.load(std::memory_order_relaxed) == self;
    if (!ours) return;
    static thread_local dollpose::Pose pose;
    static thread_local float game[knockpose::kMaxBones][16], drawn[knockpose::kMaxBones][16];
    const int n = skel::PoseNow(self, game, knockpose::kMaxBones);
    float o[16];
    if (n <= 0 || bone < 0 || bone >= n || ReadM(out, m) <= 0) { g_carryMissed.fetch_add(1, std::memory_order_relaxed); return; }
    // [weight] death_ragdoll: every part of a body drawn away noted as the game just made it - the game may make it no more once the body dies (a zombie held
    // when it was killed: its parts' matrices were made only now and then) - for PutHangs; carried: ours is in it now
    const auto note = [&](bool carried) {
        if (ReadM(component, local) <= 0 || ReadU32(component + 0xC0, &parentField) <= 0) return;
        AcquireSRWLockExclusive(&g_hangLock);
        if (deathragdoll::Hang* h = g_hangs.Note(entity, self, bone, 0, m, game[bone], local, parentField, component)) h->ours = carried;
        g_nHangs.store(g_hangs.Count(), std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_hangLock);
    };
    if (!meleetouch::MonsterPose(self, &pose) || !(pose.knock.weight > 0.0f) || n != pose.bones) { note(false); g_carryMissed.fetch_add(1, std::memory_order_relaxed); return; }
    if (!DrawnBones(self, pose, game, drawn, n)) { note(false); g_carryMissed.fetch_add(1, std::memory_order_relaxed); return; }
    if (!knockpose::Carry(m, game[bone], drawn[bone], pose.knock.m3, pose.knock.root, o) || WriteM(out, o) <= 0) { note(false); return; }
    WriteM(component + 0x40, o);   // (the transform's own world matrix, as potion_hide's writes do)
    note(true);
    g_carried.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace knockdown
