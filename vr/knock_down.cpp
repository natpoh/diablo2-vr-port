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

#include "fall_lib.h"
#include "head_shadow.h"
#include "knock_pose.h"
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

std::atomic<bool> g_on{false}, g_throwOn{false}, g_impaleOn{false};
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
};
Info g_info[kMaxInfo];
int g_nInfo = 0;
float g_heroVel[3] = {};
struct Pending { uint32_t id; knocktrigger::Verdict v; };
Pending g_pending[8];
int g_nPending = 0;
struct StabIn { uint32_t id = 0; int side = 0; float speed = 0.0f; uint64_t atMs = 0; };   // [weight] impale_drag: a stab that went in (OnStabEntered), for Tick
StabIn g_stabIn[2];
struct Strongest { uint32_t id = 0; float gave = 0, need = 0, run = 0; int kind = 0; uint64_t atMs = 0; };
Strongest g_strongest;   // the strongest push of the last second that did nothing (the log: what a blow gave against what it needed)

std::mutex g_lib;      // (the library's calls - the timer's and the grab's threads - and all below)
struct DownBody {
    uint32_t id = 0;
    float root[3] = {};    // where its model stood when it was struck or taken (world units)
    uint64_t seenMs = 0, sinceMs = 0;
    bool hurried = false;
    int kind = 0;
    bool swap = false;     // (Info::swap when the library took it: every joint to and from the library goes through SwapSides)
};
DownBody g_down[D2RF_MAX_DOWN];
bool g_any = false;
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
std::atomic<uint32_t> g_downIds[D2RF_MAX_DOWN];
std::atomic<uintptr_t> g_downSelves[D2RF_MAX_DOWN];   // the SkeletonInstances drawn out of the game's pose now (what hangs on their bones goes with them: OnXform)
std::atomic<unsigned> g_carried{0}, g_carryMissed{0};
std::atomic<int> g_downPhase[D2RF_MAX_DOWN];
// Each model's facing for the library, from the game's own walking (knockpose::FacesGoing: the game walks a unit toward its front): the ticks the game moved
// one of it with the library's front, against it; the verdict once there are enough (0 not yet: its transform's sign till then); said once. Timer thread.
struct Facing { char model[48] = {}; int with = 0, against = 0, verdict = 0; };
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
struct Laid { uint32_t id = 0; uintptr_t self = 0; int bones = 0; dollpose::Knock knock; };
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
}

bool Busy(int phase) { return phase == D2RF_FALLING || phase == D2RF_LYING || phase == D2RF_GETTING_UP || phase == D2RF_HELD; }

}  // namespace

void SetLog(LogFn fn) {
    g_log.store(fn);
    falllib::SetLog(fn);
}

void Configure(const Settings& s) {
    const bool was = g_on.exchange(s.on), wasThrow = g_throwOn.exchange(s.throwOn), wasImpale = g_impaleOn.exchange(s.impaleOn);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_set = s;
    }
    if (s.on != was) {
        if (s.on) {
            falllib::Probe();
            LogF("knock down: on ([weight] knock_down) - a blow over %.0f cm/s of the body's speed, a running hit, a sweep at the legs or a shove knocks a humanoid "
                 "down as drawn (a blow of %.0f %% of it staggers it), it lies %.1f s, gets up and walks back to where the game has it (F4; the unit stands as the "
                 "game has it)", s.needCms, s.staggerPct, s.lieMs / 1000.0f);
        } else {
            LogF("knock down: off");
        }
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
}

bool Wanted() { return g_on.load(std::memory_order_relaxed) || g_throwOn.load(std::memory_order_relaxed) || g_impaleOn.load(std::memory_order_relaxed); }
bool ThrowWanted() { return g_throwOn.load(std::memory_order_relaxed); }

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

bool IsDown(uint32_t id) {
    if (!id || !Wanted()) return false;
    for (auto& d : g_downIds)
        if (d.load(std::memory_order_relaxed) == id) return true;
    return false;
}

void NotePush(uint32_t id, const float impulse[3], const float point[3]) {
    if (!g_on.load(std::memory_order_relaxed) || !id) return;
    for (int s = 0; s < D2RF_MAX_DOWN; ++s)   // a body falling, lying, getting up or held takes no blow; one staggering or walking home does
        if (g_downIds[s].load(std::memory_order_relaxed) == id && Busy(g_downPhase[s].load(std::memory_order_relaxed))) return;
    const uint64_t now = GetTickCount64();
    std::lock_guard<std::mutex> lock(g_mutex);
    const Info* info = InfoOf(id);
    if (!info || now - info->atMs > kInfoFreshMs) return;
    knocktrigger::Settings ts;
    ts.needMs = std::max(0.05f, g_set.needCms * 0.01f);
    ts.staggerShare = std::clamp(g_set.staggerPct * 0.01f, 0.0f, 0.99f);
    const knocktrigger::Verdict v = g_tracker.Note(id, impulse, point, static_cast<double>(now) * 0.001, info->body, g_heroVel, ts);
    if (v.knock) {
        for (int i = 0; i < g_nPending; ++i)
            if (g_pending[i].id == id) return;
        if (g_nPending < (int)std::size(g_pending)) g_pending[g_nPending++] = {id, v};
    } else if (v.gaveMs > 0.0f && (v.gaveMs / std::max(v.needMs, 1e-3f) > g_strongest.gave / std::max(g_strongest.need, 1e-3f) || now - g_strongest.atMs > 1000)) {
        g_strongest = {id, v.gaveMs, v.needMs, v.runMs, v.kind, now};
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
            if (DownBody* d = DownOf(id, now, true)) d->swap = swap;
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

int Tick(const skel::MonsterRig* rigs, size_t nr, const Unit* units, int nUnits, const int* rigOf, uint64_t nowMs, float upm, const float heroRender[3],
         dollpose::Pose* made, int n, int cap) {
    static uint64_t lastMs = 0;
    static float heroWas[3] = {};
    static bool heroHad = false;
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
    if (!Wanted() || !viewgate::F4() || !(upm > 1e-3f) || !rigs || !units || !rigOf) {
        std::lock_guard<std::mutex> lock(g_lib);
        for (auto& s : g_downSelves) s.store(0);
        if (g_any) ResetLocked();   // switched off, another view: every body let go of (the game's own animation at once)
        heroHad = false;
        return n;
    }
    if (!falllib::Ensure()) return n;
    // the hero's speed across the ground (render metres a second)
    float heroVel[3] = {};
    if (heroRender && std::isfinite(heroRender[0] + heroRender[1] + heroRender[2])) {
        if (heroHad && dt > 1e-4f)
            for (int k = 0; k < 3; ++k) heroVel[k] = (heroRender[k] - heroWas[k]) / upm / dt;
        std::memcpy(heroWas, heroRender, sizeof heroWas);
        heroHad = true;
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
        if (g.doll <= 0 || !g.joints || g.bones <= 0 || g.bones > knockpose::kMaxBones) continue;   // a humanoid seen standing, not a corpse, its joints found
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
        ++nf;
    }
    Pending pending[8];
    int np = 0;
    StabIn stabs[2];
    Strongest strongest;
    float lieS = 1.5f;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (int i = 0; i < nf; ++i) {
            fresh[i].body.massKg = knocktrigger::MassOf(fresh[i].body.height, g_set.massScale);
            const Info* was = InfoOf(fresh[i].id);
            if (was && dt > 1e-4f)
                for (int k = 0; k < 3; ++k) fresh[i].vel[k] = std::clamp((fresh[i].joint[D2RF_PELVIS][k] - was->joint[D2RF_PELVIS][k]) / std::max(dt, 0.005f), -8.0f, 8.0f);
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
        LogF("knock down: the strongest push of the last second on %u - %s, %.2f m/s to the body (%.2f needed%s): nothing", strongest.id, KindName(strongest.kind),
             strongest.gave, strongest.need, strongest.run > 0.2f ? ", running" : "");
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
            LogF("knock down: %u ('%s', %.0f kg, %.2f m) %s by %s - %.2f m/s to the body (%.2f needed%s, %.1f N s); its toes point %s, its knees bend %s%s", k.id,
                 f->model, k.massKg, f->body.height, k.kind == D2RF_STAGGER ? "staggered" : "knocked down", KindName(k.kind), pending[p].v.gaveMs, pending[p].v.needMs,
                 pending[p].v.runMs > 0.2f ? ", running" : "", std::sqrt(k.impulse[0] * k.impulse[0] + k.impulse[1] * k.impulse[1] + k.impulse[2] * k.impulse[2]),
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
    if (!g_any) return n;
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
    D2rfPose poses[D2RF_MAX_DOWN];
    const int nPoses = falllib::Poses(poses, D2RF_MAX_DOWN);
    for (int i = 0; i < nPoses; ++i) {
        FlipJoints(poses[i].joint);
        if (const DownBody* d = DownOf(poses[i].id, nowMs, false); d && d->swap) knockpose::SwapSides(poses[i].joint, nullptr);   // (back to its own left and right)
    }
    std::memcpy(g_posesNow, poses, sizeof(D2rfPose) * static_cast<size_t>(std::max(0, nPoses)));
    g_nPosesNow = std::max(0, nPoses);
    uintptr_t selves[D2RF_MAX_DOWN] = {};
    int nSelves = 0;
    static uint64_t toldAt = 0;
    const bool tell = nowMs - toldAt >= 1000;
    char said[700];
    int at = 0;
    said[0] = 0;
    for (int i = 0; i < nPoses; ++i) {
        const D2rfPose& p = poses[i];
        DownBody* d = DownOf(p.id, nowMs, false);
        const skel::MonsterRig* g = nullptr;
        int mode = -1;
        for (int m = 0; m < nUnits && !g; ++m)
            if (units[m].id == p.id && rigOf[m] >= 0 && static_cast<size_t>(rigOf[m]) < nr && rigs[rigOf[m]].joints) {
                g = &rigs[rigOf[m]];
                mode = units[m].mode;
            }
        if (!d) { falllib::LetGo(p.id, 0); continue; }
        for (int s = 0; s < D2RF_MAX_DOWN; ++s)
            if (g_down[s].id == p.id) g_downPhase[s].store(p.phase);
        if (!g) {   // its rig not read (out of reach, a scan missed): let go of after a moment
            if (nowMs - d->seenMs > kGoneMs) {
                falllib::LetGo(p.id, 0);
                LogF("knock down: %u let go of - not seen for %.1f s", p.id, (nowMs - d->seenMs) / 1000.0f);
            }
            continue;
        }
        d->seenMs = nowMs;
        if (mode == 0 || mode == 12) {   // the game killed it: its own death from here (the corpse is the game's)
            falllib::LetGo(p.id, 0);
            LogF("knock down: %u died %s - the game's own death from here", p.id, PhaseName(p.phase));
            continue;
        }
        const float off = std::sqrt((g->root[0] - d->root[0]) * (g->root[0] - d->root[0]) + (g->root[2] - d->root[2]) * (g->root[2] - d->root[2])) / upm;
        const bool attacks = parry::MonsterAttacking(mode) || mode == 7;
        const bool down = p.phase == D2RF_FALLING || p.phase == D2RF_LYING || p.phase == D2RF_GETTING_UP;
        if (!d->hurried && down && (attacks || off > kWalkedOffM)) {   // it lies while the game has it fight or walk off: up now, quicker (then home)
            d->hurried = true;
            falllib::LetGo(p.id, 1);
            LogF("knock down: %u gets up now - the game %s (mode %d)", p.id, attacks ? "has it attack" : "walked it off", mode);
        }
        if (n >= cap) continue;
        dollpose::Pose pose;
        pose.self = g->self;
        pose.id = p.id;
        pose.bones = g->bones;
        pose.tickMs = nowMs;
        skel::WorldPoses(g->self, &pose.poseCur, &pose.posePrev, &pose.skel);
        pose.knock.weight = std::clamp(p.weight, 0.0f, 1.0f);
        std::memcpy(pose.knock.joint, g->joint, sizeof pose.knock.joint);
        std::memcpy(pose.knock.parent, g->parent, sizeof(int16_t) * static_cast<size_t>(std::min(g->bones, dollpose::kMaxBones)));
        std::memcpy(pose.knock.m3, g->m3, sizeof pose.knock.m3);
        std::memcpy(pose.knock.root, g->root, sizeof pose.knock.root);
        if (nSelves < D2RF_MAX_DOWN) selves[nSelves++] = g->self;
        bool ok = true;
        for (int j = 0; j < D2RF_JOINTS && ok; ++j) ok = knockpose::ToModel(g->m3, g->root, upm, p.joint[j], pose.knock.at[j]);
        if (!ok) continue;
        int slot = n;
        for (int s = 0; s < n; ++s)
            if (made[s].self == g->self) slot = s;   // (out of the game's pose: its sway turns give way)
        made[slot] = pose;
        if (slot == n) ++n;
        if (nLaid < D2RF_MAX_DOWN) laid[nLaid++] = {pose.id, pose.self, pose.bones, pose.knock};
        if (tell && at < (int)sizeof said - 140)
            at += snprintf(said + at, sizeof said - at, "%s%u %s %.1f s%s (hips %.2f m over its floor, %.2f m from home, weight %.2f)", at ? "; " : "", p.id,
                           PhaseName(p.phase), p.t, p.phase == D2RF_GETTING_UP ? (p.fromBack ? " from the back" : " from the face") : "",
                           p.joint[D2RF_PELVIS][1] - g->root[1] / upm, p.off, p.weight);
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
    for (int s = 0; s < D2RF_MAX_DOWN; ++s) g_downSelves[s].store(s < nSelves ? selves[s] : 0);
    lib.unlock();
    if (tell && at) {
        toldAt = nowMs;
        const unsigned carried = g_carried.exchange(0), missed = g_carryMissed.exchange(0);
        const unsigned laidN = g_laidBodies.exchange(0), laidMissed = g_laidMissed.exchange(0);
        LogF("knock down: %d out of the game's pose - %s; what hangs on their bones (weapons, shields) carried %u times since the last line%s; their bodies (the "
             "weapon's, the grab's) laid on them as drawn: %u capsules%s", nPoses, said, carried,
             missed ? (", " + std::to_string(missed) + " not (the pose not found)").c_str() : "", laidN,
             laidMissed ? (", " + std::to_string(laidMissed) + " times a pose did not read (left where the game has them)").c_str() : "");
    }
    return n;
}

void LayBodies(monsterbodies::Snapshot* s, float upm) {
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
void OnXform(uint32_t entity, uintptr_t component, uintptr_t out) {
    if (!Wanted() || out < 0x10000) return;
    bool any = false;
    for (auto& s : g_downSelves) any = any || s.load(std::memory_order_relaxed) != 0;
    if (!any) return;
    uintptr_t self = 0;
    int32_t bone = -1;
    if (!headshadow::AttachmentOf(entity, component, &self, &bone)) return;
    bool ours = false;
    for (auto& s : g_downSelves) ours = ours || s.load(std::memory_order_relaxed) == self;
    if (!ours) return;
    static thread_local dollpose::Pose pose;
    static thread_local float game[knockpose::kMaxBones][16], drawn[knockpose::kMaxBones][16];
    if (!meleetouch::MonsterPose(self, &pose) || !(pose.knock.weight > 0.0f)) { g_carryMissed.fetch_add(1, std::memory_order_relaxed); return; }
    const int n = skel::PoseNow(self, game, knockpose::kMaxBones);
    if (n != pose.bones || bone < 0 || bone >= n) { g_carryMissed.fetch_add(1, std::memory_order_relaxed); return; }
    std::memcpy(drawn, game, sizeof(float) * 16 * static_cast<size_t>(n));
    if (!knockpose::Apply(drawn, n, pose.knock.parent, pose.knock.joint, pose.knock.at, pose.knock.weight)) { g_carryMissed.fetch_add(1, std::memory_order_relaxed); return; }
    float m[16], o[16];
    if (ReadM(out, m) <= 0 || !knockpose::Carry(m, game[bone], drawn[bone], pose.knock.m3, pose.knock.root, o)) return;
    if (WriteM(out, o) <= 0) return;
    WriteM(component + 0x40, o);   // (the transform's own world matrix, as potion_hide's writes do)
    g_carried.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace knockdown
