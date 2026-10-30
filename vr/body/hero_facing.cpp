// vr/body/hero_facing.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M12); what it gives: vr/body/body.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// The hero's arms (skeletons.cpp): mode, head hiding and the controllers from
// BodyWalk (Game Link), handed over every 10 ms. Off whenever the VR camera is off.
// Where the hero's model faces, against where the camera looks. The renderer's
// model-to-world matrix is found by a background search (probe::FindHeroMatrices)
// and then read in place for as long as its translation stays on the hero.
// Several matrices sit on the hero; the one that TURNS when he turns wins.
std::atomic<uintptr_t> g_heroMatrix{0};
std::atomic<bool> g_facingBusy{false};
std::atomic<ULONGLONG> g_facingLastSearch{0};
float g_heroPosNow[3] = {};

// lastOk: when it last read as the hero. late: pairs in which it still changed AFTER the pose of
// the left pass (between that pose and the right pass's) - a copy written after the pose, a frame
// old whenever the left eye's pose reads it, and the pose read off one of those shakes in the left
// eye (frame log 2026-10-04: one copy fresh 125 of 125, the one the walk vote then switched to
// stale 102 of 109). Copies written before the pose never change in between. leftYaw: its yaw
// at the last left-pass pose.
// prevYaw / moved: its yaw at the last pose and when it last changed. An early copy can stop
// being written altogether (2026-10-04: frozen from one spot on while the late ones went on
// turning) - the game has moved the hero's early transform elsewhere; PickHeroMatrix drops it
// and asks for a new search (g_facingSearchNow).
// bad: turning pairs in which its yaw at the left pose was not the frame's yaw (as the most late
// copy, which the renderer writes this frame, has it at the right pose) - stale for the left eye,
// whether written after the pose (late) or only at the end of the pair (0.113-0.115 took one of
// those: it never changes between the poses, yet is a frame old in both). The pick wants none.
struct FacingCand { uintptr_t at; float yaw0; bool turned; int score; ULONGLONG lastOk; uint32_t late = 0; uint32_t bad = 0; float nowYaw = 1000.0f; float leftYaw = 1000.0f;
                    float prevYaw = 1000.0f; ULONGLONG moved = 0; };
std::atomic<bool> g_facingSearchNow{false};
SRWLOCK g_candLock = SRWLOCK_INIT;
std::vector<FacingCand> g_cands;

// The hero point (g_heroPosNow) is the camera's look-at, which follows the hero
// on a spring: running, it trails him by several units. Within 3 units the
// right matrix failed in a run, was dropped, and the body flickered to another.
bool ReadHeroMatrix(uintptr_t a, float* m) {
    return a && SafeRead(m, (void*)a, 64) && m[15] == 1.0f &&
           fabsf(m[12] - g_heroPosNow[0]) < 12.0f && fabsf(m[14] - g_heroPosNow[2]) < 12.0f && m[5] > 0.3f;
}
float MatYawDeg(const float* m) { return atan2f(m[8], m[10]) * 57.2957795f; }

// ---- Binding the doll: where the hero's facing comes from (docs/doll_binding.md, 0.135) ----
// [hands] facing_source=1: straight from the hero's TransformComponent in the renderer's ECS
// (skel::HeroTransform: the SkeletonInstance the pose hook knows as the hero -> its entity ->
// the component's local matrix at +0x00, which the game sets before the frame's poses - the
// "early" copy of plan_left_eye_shake.md). No memory search and no turn needed: bound on the
// first pose of a new model (weapon swap), a new area or after a load. 0 - or whenever the
// chain does not read - the memory search below. Either way the last good facing is held for
// [hands] facing_hold_ms while none reads, so the doll does not come unbound in the gap.
std::atomic<bool> g_facingStruct{false};        // g_heroMatrix is the structural one
std::atomic<bool> g_structOff{false};           // measured a frame old at the left pose: the search instead (F11 retries)
std::atomic<uint32_t> g_structEntity{0};
std::atomic<ULONGLONG> g_structFailSince{0};    // the structural chain has not read since (0 = it reads)
std::atomic<ULONGLONG> g_facingFastUntil{0};    // after an event: the search retried every second, not on the backoff
std::atomic<uint32_t> g_facingWaitMs{2000};      // the search's backoff (FacingThread)

bool StructWanted() { return g_set.facingSource.load() == 1 && !g_structOff.load(); }

// [debug] doll_trace: why the structural facing did not read the last time (every time, not only when the line above is told), and with "not on the look-at"
// where the matrix was against it
std::atomic<const char*> g_structWhyNow{nullptr};
std::atomic<float> g_structOffX{0.0f}, g_structOffZ{0.0f}, g_structW{1.0f}, g_structUp{1.0f};   // (ReadHeroMatrix wants w 1, up > 0.3, within 12 units)

// The structural facing, read now: the address of the hero's local matrix (comp + 0x00).
bool FacingStructural(uintptr_t* at, uint32_t* entity = nullptr) {
    static std::atomic<const char*> toldWhy{nullptr};
    static std::atomic<ULONGLONG> toldAt{0};
    auto fail = [&](const char* why) {
        g_structWhyNow.store(why);
        // the reason, when it changes (at most every 5 s)
        if (why != toldWhy.load() && GetTickCount64() - toldAt.load() > 5000) {
            toldWhy.store(why); toldAt.store(GetTickCount64());
            LogF("vrcam: structural facing does not read: %s", why);
        }
        return false;
    };
    skel::HeroXform x;
    const char* why = nullptr;
    if (!skel::HeroTransform(&x, &why)) return fail(why ? why : "?");
    if ((x.parent & 0xFFFFF) != 0xFFFFF) return fail("the hero's transform has a parent - its local matrix is not his world");
    float m[16];
    if (!ReadHeroMatrix(x.comp, m)) {
        float r[16];
        if (SafeRead(r, (void*)x.comp, 64)) { g_structOffX.store(r[12] - g_heroPosNow[0]); g_structOffZ.store(r[14] - g_heroPosNow[2]); g_structW.store(r[15]); g_structUp.store(r[5]); }
        else { g_structOffX.store(NAN); g_structOffZ.store(NAN); g_structW.store(NAN); g_structUp.store(NAN); }
        return fail("the hero's local matrix is not on the camera's look-at");
    }
    if (toldWhy.load()) { toldWhy.store(nullptr); toldAt.store(0); }
    *at = x.comp;
    if (entity) *entity = x.entity;
    return true;
}

// What unbound the facing last, and when: the hero's model rebuilt (a weapon swap), a new
// area, the first frames in a game area, F11. Kept until the facing is bound again.
SRWLOCK g_facingEvLock = SRWLOCK_INIT;
char g_facingEv[96] = "start";
ULONGLONG g_facingEvAt = 0;

void FacingEvent(const char* what) {
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_facingEvLock);
    if (!g_facingEvAt) { strncpy_s(g_facingEv, what, _TRUNCATE); g_facingEvAt = now; }   // the first since the last bind
    ReleaseSRWLockExclusive(&g_facingEvLock);
    g_facingFastUntil.store(now + 20000);
    if (g_structFailSince.load()) g_structFailSince.store(now);   // the structural one gets its 3 s from here
    // The search, when it is the source: at once, not after the backoff.
    if (!StructWanted()) { g_facingSearchNow.store(true); g_facingWaitMs.store(1000u); }
}

void PollFacingEvents() {
    static uint32_t heroGen = 0, biomeGen = 0;
    static bool world = false, first = true;
    const uint32_t hg = skel::HeroGen(), bg = g_biomeGen.load();
    const bool w = g_inWorld.load();
    if (first) { first = false; heroGen = hg; biomeGen = bg; world = w; AcquireSRWLockExclusive(&g_facingEvLock); g_facingEvAt = GetTickCount64(); ReleaseSRWLockExclusive(&g_facingEvLock); return; }
    if (w && !world) FacingEvent("the first frames in a game area (load)");
    if (bg != biomeGen) FacingEvent("an area change");
    if (hg != heroGen) FacingEvent("the hero's model rebuilt (weapon swap, new area or load)");
    {   // One hero skeleton is assumed: two that alternate would rebuild the binding all the time.
        static uint32_t gen0 = 0;
        static ULONGLONG t0 = 0;
        static bool told = false;
        const ULONGLONG now = GetTickCount64();
        if (now - t0 > 5000) { t0 = now; gen0 = hg; }
        else if (hg - gen0 > 10 && !told && w) {
            told = true;
            Log("vrcam: more than 10 hero skeletons in 5 s - two /character/player/ models alternate? (facing binding assumes one)");
        }
    }
    heroGen = hg; biomeGen = bg; world = w;
}

bool SearchAllowed();

// Bound / lost, to the log with the time since the event that unbound it.
void NoteFacing(bool ok, uintptr_t at, bool held) {
    static bool wasOk = false;
    static uintptr_t lastAt = 0;
    static ULONGLONG lostAt = 0;          // [debug] doll_trace: the window's start
    static bool unheld = false;           // ... and whether the doll had no facing at all in it
    if (!ok && wasOk) { lostAt = GetTickCount64(); unheld = false; }
    if (!ok && !held) unheld = true;
    if (ok && !wasOk && lostAt && g_set.dollTrace.load()) {
        LogF("vrcam: doll: facing bound again after %llu ms - %s", (unsigned long long)(GetTickCount64() - lostAt),
             unheld ? "with no facing for some of it (the body kept the game's facing)" : "the last facing held all through");
        lostAt = 0;
    }
    if (ok && (!wasOk || at != lastAt)) {
        char ev[96]; ULONGLONG evAt;
        AcquireSRWLockExclusive(&g_facingEvLock);
        memcpy(ev, g_facingEv, sizeof ev); evAt = g_facingEvAt; g_facingEvAt = 0;
        ReleaseSRWLockExclusive(&g_facingEvLock);
        char how[128];
        if (g_facingStruct.load()) snprintf(how, sizeof how, "structural (hero entity %X, TransformComponent local matrix)", g_structEntity.load());
        else snprintf(how, sizeof how, "memory search copy");
        if (evAt) LogF("vrcam: facing bound: %s %p, in %llu ms after %s", how, (void*)at, (unsigned long long)(GetTickCount64() - evAt), ev);
        else LogF("vrcam: facing bound: %s %p (moved, no event)", how, (void*)at);
    } else if (!ok && wasOk) {
        AcquireSRWLockExclusive(&g_facingEvLock);
        if (!g_facingEvAt) { strncpy_s(g_facingEv, "(no event seen)", _TRUNCATE); g_facingEvAt = GetTickCount64(); }
        const std::string ev = g_facingEv;
        ReleaseSRWLockExclusive(&g_facingEvLock);
        LogF("vrcam: facing lost (%s) - %s", ev.c_str(), held ? "holding the last facing ([hands] facing_hold_ms)" : "nothing to hold");
        if (g_set.dollTrace.load()) {   // [debug] doll_trace: which read failed, with its numbers, and what the doll has instead
            const bool structural = StructWanted();
            const char* why = structural ? g_structWhyNow.load() : nullptr;
            char off[200] = "";
            if (why && strstr(why, "look-at"))
                snprintf(off, sizeof off, " (the matrix %.1f / %.1f units off it in x / z, w %.2f, up %.2f - wanted: within 12, w 1, up over 0.3)", g_structOffX.load(),
                         g_structOffZ.load(), g_structW.load(), g_structUp.load());
            LogF("vrcam: doll: facing let go - %s%s; instead: %s (the hero's model rebuilt %u times so far)",
                 structural ? (why ? why : "the structural chain read, its matrix did not") : "the memory search's copy no longer reads", off,
                 held ? "the last facing, its turn put at the look-at now ([hands] facing_hold_ms)"
                      : SearchAllowed() ? "nothing yet - the memory search looks" : "nothing - the body keeps the game's facing until it reads",
                 skel::HeroGen());
        }
    }
    wasOk = ok;
    if (ok) lastAt = at;
}

// The last good model matrix, held while none reads: its turn, at the look-at as it is now.
SRWLOCK g_holdLock = SRWLOCK_INIT;
float g_holdM[16] = {};
std::atomic<ULONGLONG> g_holdAt{0};

void KeepFacing(const float* m) {
    AcquireSRWLockExclusive(&g_holdLock); memcpy(g_holdM, m, sizeof g_holdM); ReleaseSRWLockExclusive(&g_holdLock);
    g_holdAt.store(GetTickCount64());
}

bool HeldFacing(float* m) {
    const ULONGLONG at = g_holdAt.load();
    // [hands] doll_rebuild: held past facing_hold_ms while the game still poses the hero's rig (a rebuilt model's facing reads again when it is in his
    // SkeletonComponent - half a second, sometimes more), until a facing reads: the time is not fixed
    const bool posed = g_set.dollRebuild.load() && skel::HeroPosedLately(500);
    if (!at || ((float)(GetTickCount64() - at) > g_set.facingHoldMs.load() && !posed) || !g_inWorld.load()) return false;
    float look[3];
    if (!LookAtNow(look)) return false;
    AcquireSRWLockShared(&g_holdLock); memcpy(m, g_holdM, sizeof g_holdM); ReleaseSRWLockShared(&g_holdLock);
    m[12] = look[0]; m[13] = look[1]; m[14] = look[2];
    return true;
}

// May the whole-memory search run? Always with facing_source=0; with 1 only once the structural
// chain has not read for 3 s (another game build, or something this does not know).
bool SearchAllowed() {
    if (!StructWanted()) return true;
    const ULONGLONG since = g_structFailSince.load();
    return since && GetTickCount64() - since > 3000;
}

// Is the structural matrix this frame's at the left eye's pose? Its world matrix (+0x40) at
// the right pose is the frame's turn (worked out in the left pass, after its pose); the local
// one at the left pose must already have it. Counted in turning pairs, to the log every 10 s;
// a frame old in more than a quarter of 30+ pairs and the search takes over (left-eye shake).
void StructFreshness(uintptr_t at, const float* local) {
    if (!PairOfOneFrame()) return;   // (by turns there is no left pose of this frame to be fresh against)
    float w[16];
    if (!SafeRead(w, (void*)(at + 0x40), 64) || w[15] != 1.0f) return;
    static SRWLOCK lock = SRWLOCK_INIT;
    static float leftLocal = 1000.0f, leftWorld = 1000.0f, lastTruth = 1000.0f;
    static uint32_t fresh = 0, stale = 0, worldStale = 0, allFresh = 0, allStale = 0;
    static ULONGLONG lastLog = 0;
    AcquireSRWLockExclusive(&lock);
    if ((g_eye.load() & 1) == 0) { leftLocal = MatYawDeg(local); leftWorld = MatYawDeg(w); }
    else {
        const float truth = MatYawDeg(w);
        if (leftLocal < 999.0f && lastTruth < 999.0f && fabsf(WrapDeg(truth - lastTruth)) > 0.01f) {
            if (fabsf(WrapDeg(leftLocal - truth)) > 0.01f) { ++stale; ++allStale; } else { ++fresh; ++allFresh; }
            if (fabsf(WrapDeg(leftWorld - truth)) > 0.01f) ++worldStale;
        }
        lastTruth = truth;
        leftLocal = 1000.0f;
    }
    const ULONGLONG now = GetTickCount64();
    if (fresh + stale && now - lastLog > 10000) {
        lastLog = now;
        LogF("vrcam: structural facing at the left pose, turning pairs: this frame's %u, a frame old %u "
             "(its world matrix +0x40 a frame old there in %u - the late copy)", fresh, stale, worldStale);
        fresh = stale = worldStale = 0;
    }
    const bool off = allFresh + allStale >= 30 && allStale * 4 > allFresh + allStale;
    if (off) { allFresh = allStale = 0; }
    ReleaseSRWLockExclusive(&lock);
    if (off && !g_structOff.exchange(true)) {
        Log("vrcam: structural facing is a frame old at the left pose too often - back to the memory search "
            "(F11 tries the structural one again; [hands] facing_source=0 keeps the search)");
        g_facingStruct.store(false);
        g_facingSearchNow.store(true);
    }
}

// The whole-memory search is heavy: run back to back every 2 s while it found
// nothing (an area loading, the hero not drawn yet), it took the game from 90
// pairs a second to 58. So it runs at the lowest priority, and each empty
// search doubles the wait before the next, up to 30 s; a find resets it (g_facingWaitMs,
// above). For 20 s after an event (FacingEvent) the wait is a second at most.

DWORD WINAPI FacingThread(void*) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    float hero[3]; memcpy(hero, g_heroPosNow, sizeof hero);
    uintptr_t found[64];
    const int n = probe::FindHeroMatrices(hero, found, 64);
    std::vector<FacingCand> c;
    for (int i = 0; i < n; ++i) { float m[16]; if (ReadHeroMatrix(found[i], m)) c.push_back({found[i], MatYawDeg(m), false, 0, GetTickCount64()}); }
    // A copy found again keeps what was learnt of it (turned, the walk's votes, stale counts):
    // a search after an event no longer forgets the pick and waits for a turn to choose again.
    AcquireSRWLockExclusive(&g_candLock);
    for (FacingCand& nc : c)
        for (const FacingCand& oc : g_cands) if (oc.at == nc.at) { nc = oc; nc.lastOk = GetTickCount64(); break; }
    g_cands = c;
    ReleaseSRWLockExclusive(&g_candLock);
    g_facingWaitMs.store(c.empty() ? std::min(g_facingWaitMs.load() * 2u, 30000u) : 2000u);
    LogF("vrcam: %d matrices on the hero - the one that turns with him will be used%s", (int)c.size(),
         c.empty() ? " (none: the next search waits longer)" : "");
    g_facingBusy.store(false);
    return 0;
}

// Where the hero walks, sampled every 100 ms: the game turns him to his
// walking direction, so while he walks steadily the right matrix reads that
// yaw and a wrong one does not. False while he stands or turns.
bool WalkYaw(float* yawDeg) {
    static float last[3] = {}, lastYaw = 1000.0f;
    static ULONGLONG lastT = 0;
    static bool steady = false;
    const ULONGLONG now = GetTickCount64();
    if (now - lastT < 100) return false;
    lastT = now;
    const float dx = g_heroPosNow[0] - last[0], dz = g_heroPosNow[2] - last[2];
    memcpy(last, g_heroPosNow, sizeof last);
    const float d = sqrtf(dx*dx + dz*dz);
    if (d < 0.8f || d > 30.0f) { steady = false; lastYaw = 1000.0f; g_walkSteady.store(false); return false; }   // standing, or a teleport
    const float y = atan2f(dx, dz) * 57.2957795f;
    steady = lastYaw < 999.0f && fabsf(WrapDeg(y - lastYaw)) < 15.0f;
    g_walkTheta.store(atan2f(dz, dx));
    lastYaw = y;
    *yawDeg = y;
    g_walkSteady.store(steady);
    return steady;
}

// The candidate to read: once copies have been seen stale at the left pose (bad,
// see FacingCand), the least stale one the walk does not vote against - the one
// read now kept while it is among those; then the one that agrees with
// the walking direction; before any walk, the first that has turned since it was
// found; then the first not at yaw 0 (a fixed one reads exactly 0), else the first.
uintptr_t PickHeroMatrix() {
    if (g_facingReset.exchange(false)) {
        AcquireSRWLockExclusive(&g_candLock); g_cands.clear(); ReleaseSRWLockExclusive(&g_candLock);
        g_heroMatrix.store(0);
        g_facingStruct.store(false);
        g_facingLastSearch.store(0);
        g_structOff.store(false);   // the structural one gets another chance
        FacingEvent("F11");
        Log("vrcam: F11 - looking for the hero's facing again");
    }
    float walk = 0.0f;
    const bool voteNow = WalkYaw(&walk);   // true at most once per 100 ms (also the stick's walk direction, g_walkTheta)
    // Structural first ([hands] facing_source=1): no candidates, no votes.
    if (StructWanted()) {
        uintptr_t sat = 0; uint32_t ent = 0;
        if (FacingStructural(&sat, &ent)) {
            g_structFailSince.store(0);
            g_structEntity.store(ent);
            g_facingStruct.store(true);
            g_heroMatrix.store(sat);
            return sat;
        }
        if (!g_structFailSince.load()) g_structFailSince.store(GetTickCount64());
        if (!SearchAllowed()) { g_facingStruct.store(false); return 0; }   // a short gap: held, no search yet
    }
    g_facingStruct.store(false);
    uintptr_t pick = 0, nonZero = 0, first = 0, best = 0, fresh = 0;
    int bestScore = 2;   // a few agreeing steps before the walk decides
    uint32_t maxBad = 0, minBad = UINT32_MAX;
    bool currentLeast = false;
    const uintptr_t current = g_heroMatrix.load();
    AcquireSRWLockExclusive(&g_candLock);
    for (size_t i = 0; i < g_cands.size();) {
        float m[16];
        FacingCand& c = g_cands[i];
        // A miss is skipped, not fatal: a matrix gone for good (a new area, death, a
        // teleport) is dropped after 1.5 s of misses, one that just failed a read stays.
        if (!ReadHeroMatrix(c.at, m)) {
            if (GetTickCount64() - c.lastOk > 1500) g_cands.erase(g_cands.begin() + i); else ++i;
            continue;
        }
        c.lastOk = GetTickCount64();
        const float yaw = MatYawDeg(m);
        if (fabsf(WrapDeg(yaw - c.yaw0)) > 3.0f) c.turned = true;
        if (voteNow) c.score = std::clamp(c.score + (fabsf(WrapDeg(yaw - walk)) < 35.0f ? 1 : -1), -20, 20);
        if (c.score <= -6) { g_cands.erase(g_cands.begin() + i); continue; }   // walked against it often enough
        if (c.score > bestScore) { bestScore = c.score; best = c.at; }
        if (!first) first = c.at;
        if (!nonZero && fabsf(yaw) > 0.5f) nonZero = c.at;
        if (!pick && c.turned) pick = c.at;
        ++i;
    }
    // Frozen: not moved for 1.5 s while another copy moved in the last half second.
    ULONGLONG newest = 0;
    for (const FacingCand& c : g_cands) newest = std::max(newest, c.moved);
    const ULONGLONG nowT = GetTickCount64();
    const bool turningNow = newest && nowT - newest < 500;
    // Only one seen moving before can freeze: a copy never seen to move (not the hero's turn at
    // all, or not sampled yet) is the walk vote's to judge (0.114 dropped 32 of 35 that way).
    auto frozen = [&](const FacingCand& c) { return turningNow && c.moved && newest - c.moved > 1500; };
    bool currentFrozen = false;
    for (size_t i = 0; i < g_cands.size();) {
        const FacingCand& c = g_cands[i];
        if (frozen(c)) {
            if (c.at == current) currentFrozen = true;
            if (best == c.at) best = 0;
            if (pick == c.at) pick = 0;
            g_cands.erase(g_cands.begin() + i);
            continue;
        }
        maxBad = std::max(maxBad, c.bad);
        if (c.score >= 0) {
            if (c.bad < minBad) { minBad = c.bad; fresh = c.at; currentLeast = false; }
            if (c.bad == minBad && c.at == current) currentLeast = true;
        }
        ++i;
    }
    if (first && std::none_of(g_cands.begin(), g_cands.end(), [&](const FacingCand& c) { return c.at == first; })) first = g_cands.empty() ? 0 : g_cands[0].at;
    if (nonZero && std::none_of(g_cands.begin(), g_cands.end(), [&](const FacingCand& c) { return c.at == nonZero; })) nonZero = 0;
    // Without a turn: the first of three copies 0x40 apart is a TransformComponent's local matrix
    // (local, world, the world before - docs/doll_binding.md), the copy set before the poses.
    uintptr_t head = 0;
    {
        auto has = [&](uintptr_t a) { return std::any_of(g_cands.begin(), g_cands.end(), [&](const FacingCand& c) { return c.at == a; }); };
        for (const FacingCand& c : g_cands)
            if (c.score >= 0 && has(c.at + 0x40) && has(c.at + 0x80) && !has(c.at - 0x40)) { head = c.at; break; }
    }
    const size_t left = g_cands.size();
    ReleaseSRWLockExclusive(&g_candLock);
    if (currentFrozen) {   // its successor is somewhere the last search did not see: look again
        g_facingSearchNow.store(true);
        LogF("vrcam: facing copy %p stopped moving while the hero turns - looking for his matrices again", (void*)current);
    }
    // All gone - a new area, death, a teleport: look again now, not 2 s after the last search.
    static size_t hadLeft = 0;
    if (left == 0 && hadLeft > 0) { g_facingLastSearch.store(0); g_facingWaitMs.store(2000u); Log("vrcam: the hero's matrices are gone (new area?) - looking again"); }
    hadLeft = left;
    // A few stale poses seen before it decides; the copy read now stays while it is among the least stale.
    if (maxBad < 3 || minBad * 4 > maxBad) fresh = 0;
    else if (currentLeast) fresh = current;
    const uintptr_t a = fresh ? fresh : best ? best : head ? head : pick ? pick : nonZero ? nonZero : first;
    if (a != g_heroMatrix.load()) {
        g_heroMatrix.store(a);
        LogF("vrcam: facing from %p (%s, %d candidates, stale at the left pose: this %u, most %u)", (void*)a,
             fresh ? "this frame's at the left pose" : best ? "agrees with the walk" : head ? "first of a 0x40 group - a transform's local matrix" :
             pick ? "turns, walk not checked yet" : nonZero ? "turned, not yet seen turning" : "not seen turning yet", (int)left,
             fresh ? minBad : 0u, maxBad);
    }
    return a;
}

// [debug] matrix_writer - who writes the hero's model matrix, and when (left-eye shake:
// on the first pass of a pair it is written after the pose is worked out). Hardware write
// breakpoints on its yaw word (A, m[8]) and its x (B, m[12]) for 3 s, each hit tagged with
// the pass and the eye; with the frame log on, the times line up with its P and V rows.
uint64_t MatrixWatchTag() {
    return ((uint64_t)flog::Pass() << 32) | (g_pairNow.load() ? 0x100u : 0u) | (uint64_t)(g_eye.load() & 1);
}
void StartMatrixWatch() {
    const uintptr_t at = g_heroMatrix.load();
    if (!at) { Log("vrcam: matrix_writer - no hero matrix found yet (walk a step), nothing watched"); return; }
    wchar_t path[MAX_PATH];
    wcscpy_s(path, g_iniPath);
    if (wchar_t* slash = wcsrchr(path, L'\\')) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"d2r_vr_matrix_writers.csv");
    if (ww::Start(at + 0x20, at + 0x30, 3000, path, MatrixWatchTag, Log))
        LogF("vrcam: matrix_writer - watching the hero matrix %p for 3 s (flags: 100 = in a pair, 1 = right eye)", (void*)at);
}

// Camera forward and eye point in the hero's model space, from model matrix m.
bool YawFromMatrix(const float* m, float* out, float* eyeModel, bool* eyeOk, float rows[3][3]) {
    *eyeOk = false;
    AcquireSRWLockShared(&g_stickLock); StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    if (!f.valid) return false;
    // The camera's forward as it will be drawn, not as last built: the skeleton
    // runs before its pass rebuilds the view, so on the first pass of a pair the
    // stored forward was the last pair's and the body shook in the left eye on
    // every turn. Turned on by what the yaw has moved since that build; a yaw
    // up turns this forward the other way (pose-order log: cam = const - tgt).
    {
        const float d = WrapDeg(TargetYaw() - f.yawAtBuild) * 0.0174532925f;
        if (d != 0.0f && std::isfinite(d)) {
            const float c = cosf(d), sn = sinf(d), x = f.camF[0], z = f.camF[2];
            f.camF[0] = x * c - z * sn; f.camF[2] = z * c + x * sn;
        }
    }
    // world -> model for a row-vector rotation: dot with each (normalised) row
    for (int r = 0; r < 3; ++r) {
        const float l = sqrtf(m[r*4]*m[r*4] + m[r*4+1]*m[r*4+1] + m[r*4+2]*m[r*4+2]);
        for (int c = 0; c < 3; ++c) rows[r][c] = m[r*4+c] / l;
    }
    const float cx = f.camF[0]*rows[0][0] + f.camF[1]*rows[0][1] + f.camF[2]*rows[0][2];
    const float cz = f.camF[0]*rows[2][0] + f.camF[1]*rows[2][1] + f.camF[2]*rows[2][2];
    *out = atan2f(cx, cz);
    // The eye point in model space: (world - translation), dotted with each row
    // over its squared length (the rows carry the model's scale). The eye from
    // the look-at as it is now (g_eyeFromLook); the last rebuild's if unreadable.
    if (g_eyeOk.load()) {
        float eye[3] = {g_eyeWorld[0].load(), g_eyeWorld[1].load(), g_eyeWorld[2].load()}, look[3];
        if (LookAtNow(look))
            for (int i = 0; i < 3; ++i) eye[i] = look[i] + g_eyeFromLook[i].load();
        const float d[3] = {eye[0] - m[12], eye[1] - m[13], eye[2] - m[14]};
        for (int r = 0; r < 3; ++r) {
            const float l2 = m[r*4]*m[r*4] + m[r*4+1]*m[r*4+1] + m[r*4+2]*m[r*4+2];
            eyeModel[r] = (d[0]*m[r*4] + d[1]*m[r*4+1] + d[2]*m[r*4+2]) / l2;
        }
        *eyeOk = std::isfinite(eyeModel[0] + eyeModel[1] + eyeModel[2]) && eyeModel[1] > 1.0f;
    }
    return true;
}

// For the skeleton hook, on every pose (skel::SetFresh): the matrix picked on the
// timer, read now. While the game turns the hero to the stick it changes every
// frame, and the 10 ms copy left the body part-turned and shaking.

constexpr uint64_t RVA_DRAW_COUNTER = 0x33ED6D8;   // ++ after every PrismBlit (PrismEndDraw 0x6580DF; read by 0x658230)

// Which of eye, camera look-at and the hero's matrices is a frame old on which
// pass: while the hero walks, every 3 s, the next 8 hero poses go to the log.
// Of one pair's two passes (frame time > 0, then 0) a value that differs
// between them was stale on the first.
void DiagPose(float yawInModel) {
    static ULONGLONG next = 0;
    static int left = 0;
    static float lastLook[3] = {}, lastCamYaw = 0.0f;
    const ULONGLONG now = GetTickCount64();
    float look[3] = {};
    if (!LookAtNow(look)) return;
    AcquireSRWLockShared(&g_stickLock); const StickFrame sf = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    const float camYaw = atan2f(sf.camF[0], sf.camF[2]) * 57.2957795f;
    if (left == 0) {
        const float dx = look[0] - lastLook[0], dz = look[2] - lastLook[2], dy = WrapDeg(camYaw - lastCamYaw);
        memcpy(lastLook, look, sizeof look); lastCamYaw = camYaw;
        if (now < next || (dx*dx + dz*dz < 1e-6f && fabsf(dy) < 0.05f)) return;   // standing and not turning: nothing to see
        left = 16; next = now + 3000;
        Log("pose-order: pass dt vr eye(x z) look(x z) camYaw target bodyInModel | hero matrices (x z yaw)...");
    }
    --left;
    uint32_t pass = 0; float dt = -1.0f;
    SafeRead(&pass, (void*)d2rsig::Addr(RVA_DRAW_COUNTER), 4);
    SafeRead(&dt, (void*)d2rsig::Addr(RVA_FRAME_TIME), 4);
    char b[600];
    int n = snprintf(b, sizeof b, "pose-order: %u %.4f vr %u eye %.3f %.3f look %.3f %.3f cam %.2f tgt %.2f body %.2f |", pass, dt, g_viewBuilds.load(),
                     g_eyeWorld[0].load(), g_eyeWorld[2].load(), look[0], look[2], camYaw, TargetYaw(), yawInModel * 57.2957795f);
    // the matrix in use and its neighbours 0xD0 either side (the second slot seen in the log)
    const uintptr_t at = g_heroMatrix.load();
    for (const intptr_t off : {(intptr_t)-0xD0, (intptr_t)0, (intptr_t)0xD0}) {
        float m[16];
        if (at && SafeRead(m, (void*)(at + off), 64) && m[15] == 1.0f)
            n += snprintf(b + n, sizeof b - n, " %s%03X %.3f %.3f %.1f", off ? "" : "*",
                          (unsigned)((at + off) & 0xFFF), m[12], m[14], atan2f(m[8], m[10]) * 57.2957795f);
    }
    Log(b);
}

// The hero's model matrix is written by the renderer while the poses are
// worked out on other threads: on the first pass of a pair it is sometimes
// this frame's and sometimes the last's (pose-order log, two runs, both
// ways). Whichever, it is made this frame's: the translation is the camera's
// look-at (exactly the hero's position, and already this frame's), and a
// matrix whose translation is not on it yet is a frame old, so its yaw gets
// the last step it turned by.
void HeroMatrixNow(uintptr_t at, float* m) {
    static SRWLOCK lock = SRWLOCK_INIT;
    static uintptr_t lastAt = 0;
    static float lastT[3] = {}, lastYaw = 0.0f, step = 0.0f;
    float look[3];
    if (!LookAtNow(look)) return;
    const float yaw = atan2f(m[8], m[10]);
    const float off = fabsf(m[12] - look[0]) + fabsf(m[14] - look[2]);
    AcquireSRWLockExclusive(&lock);
    if (at != lastAt) { lastAt = at; step = 0.0f; memcpy(lastT, m + 12, sizeof lastT); lastYaw = yaw; }
    else if (m[12] != lastT[0] || m[14] != lastT[2] || yaw != lastYaw) {   // a new write: how far it turned
        const float d = WrapDeg((yaw - lastYaw) * 57.2957795f) * 0.0174532925f;
        step = fabsf(d) < 0.5f ? d : 0.0f;
        memcpy(lastT, m + 12, sizeof lastT); lastYaw = yaw;
    }
    const float turn = off > 1e-3f && off < 2.0f ? step : 0.0f;   // not on the look-at yet: last frame's
    ReleaseSRWLockExclusive(&lock);
    if (turn != 0.0f) {   // rows turned about the world's vertical (row-vector, axes in the rows)
        const float c = cosf(turn), sn = sinf(turn);
        for (int r = 0; r < 3; ++r) {
            const float x = m[r*4], z = m[r*4 + 2];
            m[r*4] = x * c + z * sn; m[r*4 + 2] = -x * sn + z * c;
        }
    }
    if (off < 2.0f) { m[12] = look[0]; m[13] = look[1]; m[14] = look[2]; }
}

// The matrix to read at this pose: the structural one resolved now (its array moves when it
// grows - the 10 ms copy of the address could point at the freed one), else the picked copy.
uintptr_t FacingAtNow(bool* structural) {
    *structural = false;
    if (StructWanted()) {
        uintptr_t sat = 0;
        if (FacingStructural(&sat)) { *structural = true; return sat; }
        if (!SearchAllowed()) return 0;   // the gap: the hook keeps the held values (Input)
    }
    return g_facingStruct.load() ? 0 : g_heroMatrix.load();
}

// [openxr] roomscale_body (vr/room_walk_wire.h BodyTarget): the floor under your neck less the hero's place now, in his model units - worked
// out by FreshYawInModel at this pose from the matrix it reads, handed to the skeleton hook by FreshRoomShift right after (the same
// thread, the same pose), so the drawn body stands under the neck whatever the unit does under it this frame. [2]: up, model units
// ([body] under_camera, below: the body down to the camera's ground or up with it).
thread_local bool t_roomShiftOk = false;
thread_local float t_roomShift[3] = {};
bool FreshRoomShift(float shift[3]) {
    const bool ok = t_roomShiftOk;
    t_roomShiftOk = false;
    if (ok) { shift[0] = t_roomShift[0]; shift[1] = t_roomShift[1]; shift[2] = t_roomShift[2]; }
    return ok;
}

// [body] under_camera (vr/under_camera.h), VR F4: our camera stands on the game camera's look-at, the body is drawn by the hero's model
// matrix - and a Leap's arc lifts that matrix 12 - 13.7 units over the look-at (the author's log, 2026-10-26), while a pad's camera spring
// leaves the look-at behind him across the floor. At each pose the drawn body's origin is put where the camera stands on (undercam::Target:
// the look-at, up by the lift this pose asks the view for - the skeleton poses before its pass rebuilds the view), in his model units,
// through the room shift above (PoseBody moves every drawn bone by it), and the eyes are taken against the look-at up by the last view's
// lift and across the floor against his own place (undercam::EyesOrigin: the skeleton adds the move up only at the end of the pose). With
// [body] leap_camera_up the camera rises with the game's arc (UnderLiftForView, read by JumpLift at the view), else (the default) it stays
// and the body is pulled down to it. Taken only from his own TransformComponent (the structural facing): it is this frame's at the pose,
// where a copy from the memory search can be a frame old - a frame's step in the move would shake the body. One log line per leap or
// trail, moved or not and why (UnderNote).
SRWLOCK g_underLock = SRWLOCK_INIT;
undercam::Trail g_underTrail;
std::atomic<float> g_underLift{0.0f};          // the pose's: the lift the camera is to rise by at the next view (world units) - the body is drawn under it
std::atomic<ULONGLONG> g_underLiftAt{0};
std::atomic<float> g_viewLift{0.0f};           // the view's: the lift the last view built rose by - in the eye the pose puts together
void UnderNote(float gap, float lift, bool moved, const char* why) {
    undercam::TrailEnd e;
    AcquireSRWLockExclusive(&g_underLock);
    const bool ended = undercam::Step(g_underTrail, gap, lift, moved, why, GetTickCount64(), &e);
    ReleaseSRWLockExclusive(&g_underLock);
    static std::atomic<ULONGLONG> told{0};
    if (!ended || GetTickCount64() - told.load() < 1000) return;   // one line a leap (a line can cost a millisecond, more on a slow disk)
    told.store(GetTickCount64());
    const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();   // world units per metre, as TrueScale
    char did[200];
    if (e.moved && !e.left) snprintf(did, sizeof did, "the drawn body kept under the camera all through%s",
                                     g_set.leapCameraUp.load() ? ", the camera up with the arc ([body] leap_camera_up)" : ", the body kept down with the camera ([body] leap_camera_up=0)");
    else if (e.moved) snprintf(did, sizeof did, "the drawn body kept under the camera in %d poses, left in %d: %s", e.moved, e.left, e.why ? e.why : "nothing to move");
    else snprintf(did, sizeof did, "NOT moved: %s", e.why ? e.why : "nothing to move");
    LogF("under camera: a leap or a trail of %llu ms - the hero's model up to %.1f units (%.2f m) over the camera's ground, %.1f units (%.2f m) from it across "
         "the floor; %s", (unsigned long long)e.ms, e.lift, e.lift / upm, e.gap, e.gap / upm, did);
}
bool UnderCameraView() { return viewgate::F4() && !ThirdPerson(); }
// JumpLift, at a view: the game's own lift of the model the camera rises by - [body] under_camera with leap_camera_up, F4, from a pose of
// the last quarter second; 0 otherwise. Kept as the view's (g_viewLift): the eye the next pose puts together carries it.
float UnderLiftForView() {
    float up = 0.0f;
    if (g_set.underCamera.load() && g_set.leapCameraUp.load() && UnderCameraView() && GetTickCount64() - g_underLiftAt.load() < 250) up = g_underLift.load();
    if (!std::isfinite(up)) up = 0.0f;
    g_viewLift.store(up);
    return up;
}
// At the pose, with his matrix `r` read: the move in model units (x, y, z) to `out` and `m` (what the eyes are taken against) put at the new
// origin. False: nothing moved - off, not F4, the search's copy (structural false), apart past undercam::kMaxGap; the log is told either way.
bool UnderCamera(bool structural, const float* r, float* m, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    float look[3], gap = 0.0f, lift = 0.0f;
    if (!LookAtNow(look) || !undercam::Measure(r, look, &gap, &lift)) { g_underLift.store(0.0f); return false; }
    const char* why = !g_set.underCamera.load() ? "off ([body] under_camera=0)"
                    : !UnderCameraView() ? "not VR first person (F4)"
                    : !structural ? "the facing is the memory search's copy (it can be a frame old at the pose: [hands] facing_source)" : nullptr;
    const float ask = why || !g_set.leapCameraUp.load() ? 0.0f : undercam::Lift(lift);
    g_underLift.store(ask);   // the view this pose is drawn with rises by this
    g_underLiftAt.store(GetTickCount64());
    bool moved = false;
    if (!why) {
        float to[3];
        undercam::Target(look, ask, to);
        moved = undercam::MoveInModel(r, to, out);
        if (!moved) why = "the hero's matrix has no turn in it";
        else if (m) { float at[3]; undercam::EyesOrigin(r, look, g_viewLift.load(), at); m[12] = at[0]; m[13] = at[1]; m[14] = at[2]; }
    }
    UnderNote(gap, lift, moved, why);
    return moved;
}
// ... and when his matrix did not read as the hero's because the look-at trailed him past ReadHeroMatrix's 12 units (the structural facing
// then says "not on the camera's look-at" and the last facing is held at the look-at): his TransformComponent read here for the move alone.
bool UnderCameraFar(float out[3]) {
    if (!StructWanted()) return false;
    skel::HeroXform x;
    float r[16];
    if (!skel::HeroTransform(&x) || (x.parent & 0xFFFFF) != 0xFFFFF || !SafeRead(r, (void*)x.comp, 64) || r[15] != 1.0f || !(r[5] > 0.3f)) return false;
    return UnderCamera(true, r, nullptr, out);
}

bool FreshYawInModel(float* yawInModel, float* eyeModel, bool* eyeOk) {
    float m[16], rows[3][3], yaw = 0.0f;
    t_roomShiftOk = false;
    if (!g_enabled.load()) return false;
    bool structural = false;
    const uintptr_t at = FacingAtNow(&structural);
    if (!ReadHeroMatrix(at, m)) {
        flog::Line("P,%u,%.4f,%d,%p,,,,,,,,,,,nomatrix", flog::Pass(), flog::Dt(), g_eye.load() & 1, (void*)at);
        if (float u[3]; UnderCameraFar(u)) {   // [body] under_camera: trailed past 12 units - the held facing's eyes are at the look-at
            t_roomShift[0] = u[0]; t_roomShift[1] = u[2]; t_roomShift[2] = u[1];   // (x, z, then up)
            t_roomShiftOk = true;
        }
        return false;
    }
    // A frame-old matrix on the first pass of a pair was the left eye's shake: the
    // copy read was one the renderer writes AFTER the poses (or at the pair's end).
    // PickHeroMatrix now keeps to a copy that is this frame's at the left pose
    // (FacingCand::bad); HeroMatrixNow's step is left for the moments it is not.
    float raw[16]; memcpy(raw, m, sizeof raw);
    if (structural) StructFreshness(at, raw);
    if (const float sc = sqrtf(raw[0]*raw[0] + raw[1]*raw[1] + raw[2]*raw[2]); sc > 0.2f && sc < 5.0f) g_heroScale.store(sc);
    HeroMatrixNow(at, m);
    const float matrixOverLook = raw[13] - m[13];   // (the jump diagnostic below: as HeroMatrixNow left it)
    t_roomShift[2] = 0.0f;
    float under[3] = {};
    const bool underOn = UnderCamera(structural, raw, m, under);   // [body] under_camera: the body under the camera, the eyes against its new origin
    if (float tx = 0.0f, tz = 0.0f, look[3]; ThirdPerson() == false && roomwalkwire::BodyTarget(&tx, &tz) && LookAtNow(look)) {
        // world -> model for a direction: dotted with each row over its squared length (the rows carry the model's scale)
        const float dx = tx - look[0], dz = tz - look[2];
        const float lx = m[0] * m[0] + m[1] * m[1] + m[2] * m[2], lz = m[8] * m[8] + m[9] * m[9] + m[10] * m[10];
        if (lx > 1e-8f && lz > 1e-8f) {
            t_roomShift[0] = (dx * m[0] + dz * m[2]) / lx;
            t_roomShift[1] = (dx * m[8] + dz * m[10]) / lz;
            t_roomShiftOk = std::isfinite(t_roomShift[0]) && std::isfinite(t_roomShift[1]);
        }
    }
    if (underOn) {   // [body] under_camera: on top of the room's (that one is counted from the look-at; the body stood at his place)
        if (!t_roomShiftOk) t_roomShift[0] = t_roomShift[1] = 0.0f;
        t_roomShift[0] += under[0]; t_roomShift[1] += under[2]; t_roomShift[2] = under[1];   // (x, z, then up)
        t_roomShiftOk = true;
    }
    {   // Diagnostics for the jump: does the game lift the unit itself (matrix
        // above the look-at), or only the animation (the hips, skel::HeroEye)?
        static ULONGLONG told = 0;
        float eye = 0.0f, lift = 0.0f, rawLift = 0.0f;
        const float unitLift = matrixOverLook;
        if (skel::HeroEye(&eye, &lift, &rawLift) && (rawLift > 0.6f || fabsf(unitLift) > 0.3f) && GetTickCount64() - told > 500) {
            told = GetTickCount64();
            LogF("vrcam: jump - hips %.2f above standing (camera up %.2f units), model matrix %.2f above the look-at",
                 rawLift, lift * g_heroScale.load(), unitLift);
        }
    }
    if (!YawFromMatrix(m, &yaw, eyeModel, eyeOk, rows)) {
        flog::Line("P,%u,%.4f,%d,%p,%.3f,%.3f,%.3f,,,,,,,,noview", flog::Pass(), flog::Dt(), g_eye.load() & 1, (void*)at,
                   raw[12], raw[14], atan2f(raw[8], raw[10]) * 57.2957795f);
        return false;
    }
    const LONGLONG logicAt = g_logicFacingAt.load();
    flog::Line("P,%u,%.4f,%d,%p,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%s,%.3f,%.2f", flog::Pass(), flog::Dt(), g_eye.load() & 1, (void*)at,
               raw[12], raw[14], atan2f(raw[8], raw[10]) * 57.2957795f, m[12], m[14], atan2f(m[8], m[10]) * 57.2957795f,
               (g_set.facingSign.load() * yaw) * 57.2957795f, eyeModel[0], eyeModel[1], eyeModel[2], *eyeOk ? "ok" : "noeye",
               [] { float d = -999.0f; return AskHeroFacingNow(&d) ? d : -999.0f; }(), logicAt ? (QpcUs() - logicAt) / 1000.0 : -1.0);
    {   // every copy of the hero's matrix at this pose: late (written after the pose) and bad (not
        // yet this frame's at the left pose - FacingCand)
        const bool logIt = flog::g_on.load();
        const int eye = g_eye.load() & 1;
        const bool pair = PairOfOneFrame();
        char b[700]; int k = logIt ? snprintf(b, sizeof b, "C,%u,%d", flog::Pass(), eye) : 0;
        static float lastTruth = 1000.0f;   // this frame's yaw as the right pass found it, last pair
        AcquireSRWLockExclusive(&g_candLock);
        float truth = 1000.0f; uint32_t truthLate = 2;   // the most late copy: written this frame by the renderer
        for (FacingCand& c : g_cands) {
            float cm[16];
            c.nowYaw = 1000.0f;
            if (!SafeRead(cm, (void*)c.at, 64) || cm[15] != 1.0f) continue;
            const float y = c.nowYaw = MatYawDeg(cm);
            if (c.prevYaw < 999.0f && fabsf(WrapDeg(y - c.prevYaw)) > 0.01f) c.moved = GetTickCount64();
            c.prevYaw = y;
            if (pair && eye == 1 && c.leftYaw < 999.0f && fabsf(WrapDeg(y - c.leftYaw)) > 0.01f) c.late++;
            if (pair && eye == 1 && c.late > truthLate) { truthLate = c.late; truth = y; }
            if (logIt && k < (int)sizeof b - 24)
                k += snprintf(b + k, sizeof b - k, ",%llX:%.2f", (unsigned long long)(c.at & 0xFFFFF), y);
        }
        for (FacingCand& c : g_cands) {
            if (pair && eye == 0) { c.leftYaw = c.nowYaw; continue; }
            // While he turns (this frame's yaw is new), a copy that did not have it at the left pose is bad.
            if (pair && truth < 999.0f && lastTruth < 999.0f && fabsf(WrapDeg(truth - lastTruth)) > 0.01f &&
                c.leftYaw < 999.0f && fabsf(WrapDeg(c.leftYaw - truth)) > 0.01f) c.bad++;
            c.leftYaw = 1000.0f;
        }
        if (pair && eye == 1 && truth < 999.0f) lastTruth = truth;
        ReleaseSRWLockExclusive(&g_candLock);
        if (logIt) flog::Line("%s", b);
    }
    if (g_set.poseOrderLog.load()) DiagPose(g_set.facingSign.load() * yaw);
    *yawInModel =g_set.facingSign.load() * yaw + g_set.bodyYawDeg.load() * 0.0174532925f;
    return true;
}

// Camera forward in the hero's model space as a yaw (rad) about its up axis,
// 0 = the model faces where we look. False while the matrix is not known.
bool CameraYawInModel(float* out, float* eyeModel, bool* eyeOk) {
    *eyeOk = false;
    PollFacingEvents();
    const uintptr_t a = PickHeroMatrix();
    // The copy read has stopped moving while the others turn (PickHeroMatrix), or an event asked
    // for it (FacingEvent): search again now, the game has moved the hero's copies somewhere new.
    if (g_facingSearchNow.load() && SearchAllowed() && !g_facingBusy.load() && d2rcam::InWorld()) {
        g_facingSearchNow.store(false);
        g_facingBusy.store(true);
        g_facingLastSearch.store(GetTickCount64());
        if (HANDLE h = CreateThread(nullptr, 0, FacingThread, nullptr, 0, nullptr)) CloseHandle(h); else g_facingBusy.store(false);
    }
    float m[16];
    const bool ok = ReadHeroMatrix(a, m);
    bool held = false;
    if (ok) KeepFacing(m);
    else {
        // lost (moved, reallocated) or never found: look again, 2 s after the last search, longer after
        // empty ones (a second at most for 20 s after an event) - unless the structural one is the source
        uint32_t wait = g_facingWaitMs.load();
        if (GetTickCount64() < g_facingFastUntil.load()) wait = std::min(wait, 1000u);
        if (SearchAllowed() && !g_facingBusy.load() && GetTickCount64() - g_facingLastSearch.load() > wait && d2rcam::InWorld()) {
            g_facingBusy.store(true);
            g_facingLastSearch.store(GetTickCount64());
            if (HANDLE h = CreateThread(nullptr, 0, FacingThread, nullptr, 0, nullptr)) CloseHandle(h); else g_facingBusy.store(false);
        }
        // Not dropped: the last good facing, at the hero's place now, for [hands] facing_hold_ms.
        held = HeldFacing(m);
    }
    NoteFacing(ok, a, held);
    if (!ok && !held) return false;
    float rows[3][3];
    if (!YawFromMatrix(m, out, eyeModel, eyeOk, rows)) return false;
    if (g_set.bowLatency.load(std::memory_order_relaxed)) { g_latYaw.store(atan2f(rows[2][0], rows[2][2]) * 57.2957795f); g_latYawS.store(throwborn::NowS()); }
    AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    // Facing diagnostics, four times a second while on: the camera's yaw, the
    // model matrix's facing in the world, the yaw handed to the body, and the
    // animation's own root/anim/pelvis yaw in model space.
    static ULONGLONG lastDiag = 0;
    if (GetTickCount64() - lastDiag >= 250) {
        lastDiag = GetTickCount64();
        float py[3]; skel::PoseYaws(py);
        // the source, and with the search the structural yaw beside it (a live comparison)
        char src[96];
        if (!ok) snprintf(src, sizeof src, "held");
        else if (g_facingStruct.load()) snprintf(src, sizeof src, "structural");
        else {
            uintptr_t sat = 0; float sm[16];
            if (FacingStructural(&sat) && ReadHeroMatrix(sat, sm)) snprintf(src, sizeof src, "search, structural %.1f", MatYawDeg(sm));
            else snprintf(src, sizeof src, "search, no structural");
        }
        LogF("facing: cam %.1f | model %.1f (mat %p, %s) | cam-in-model %.1f | pose root %.1f anim %.1f pelvis %.1f | hero %.1f %.1f",
             atan2f(f.camF[0], f.camF[2]) * 57.2957795f, atan2f(rows[2][0], rows[2][2]) * 57.2957795f, (void*)a, src,
             *out * 57.2957795f, py[0], py[1], py[2], g_heroPosNow[0], g_heroPosNow[2]);
    }
    return true;
}

}  // namespace vrc
