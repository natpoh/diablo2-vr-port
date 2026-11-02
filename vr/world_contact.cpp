// world_contact.cpp - see world_contact.h.

#include "world_contact.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <array>
#include <unordered_map>
#include <vector>

#include "knock_props.h"
#include "melee_boxes.h"
#include "own_models.h"
#include "skeletons.h"
#include "world_walls.h"

namespace worldcontact {

namespace {

namespace hw = heft::walls;

constexpr uint64_t kScanMs = 500;          // a scan twice a second (the walls stand still; a hand reaches a metre of the 5 m read)
constexpr uint64_t kStaleMs = 1500;        // walls older than this are not used (the scan stopped: another area, the box unticked)
constexpr float kScanRadiusM = 5.0f;       // m round the hero: the pieces read
constexpr float kNearHeadM = 2.8f;         // m: the walls given to the hands and the weapon (from the eyes, across the ground and up)
constexpr float kHandRadiusM = 0.045f;     // a bare hand: a capsule from the controller 9 cm on along its pointing, this thick
constexpr float kHandLengthM = 0.09f;
constexpr float kSteadyUnits = 0.01f;      // render units: a piece that moved more than this since the last scan is not steady (worldwalls::Entity::steady)
constexpr uint64_t kReportMs = 30000;      // the status line
constexpr uint64_t kMarkMs = 600;          // [debug] world_contact_draw: a strike's ball stays this long

LogFn g_log = nullptr;
PulseFn g_pulse = nullptr;
std::wstring g_solidsPath;   // d2r_vr_props\solids.bin beside the plugin (Settings makes it)
std::atomic<bool> g_weapon{false}, g_hands{false}, g_draw{false};
char g_status[220] = "off";

void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char b[600];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

double NowUs() {
    static const double inv = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return 1e6 / (double)f.QuadPart; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * inv;
}

// ---- the scan (a worker) ----
// The solids catalogue (shared/world_solids.h), the worker's own: read at the first scan, again a minute after a miss (Settings may be making it)
worldsolids::Catalog g_solids;
bool g_solidsOk = false;
uint64_t g_solidsTriedAt = 0;
size_t g_solidsBytes = 0;
bool KnowsPreset(uint32_t id) { return g_solids.Knows(id); }
bool LoadSolids() {
    if (g_solidsOk) return true;
    const uint64_t now = GetTickCount64();
    if (g_solidsTriedAt && now - g_solidsTriedAt < 60000) return false;
    g_solidsTriedAt = now;
    HANDLE f = CreateFileW(g_solidsPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        LogF("world contact: no solids catalogue (d2r_vr_props\\solids.bin) -D2R VR Settings makes it at its start while a World contact box is ticked; nothing is solid until then");
        return false;
    }
    LARGE_INTEGER size{};
    std::vector<uint8_t> data;
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (256ll << 20);
    if (ok) {
        data.resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = ReadFile(f, data.data(), (DWORD)data.size(), &got, nullptr) && got == data.size();
    }
    CloseHandle(f);
    g_solidsOk = ok && g_solids.Parse(data.data(), data.size());
    g_solidsBytes = data.size();
    LogF(g_solidsOk ? "world contact: the solids catalogue read - %zu level pieces of %zu models (%zu KB)" : "world contact: the solids catalogue does not read (%zu pieces, %zu models, %zu KB) - "
                      "D2R VR Settings makes it again", g_solids.Rows(), g_solids.Models(), data.size() / 1024);
    return g_solidsOk;
}

struct Request { float hero[3] = {}; float upm = 1.0f; };
struct Result {
    bool ok = false;
    const char* why = "";
    std::vector<worldwalls::Box> walls;   // render metres
    worldwalls::Counts counts;
    unsigned own = 0;                     // pieces hanging on the hero or of our own models (the belt's bottles), left out
    unsigned units = 0, unitsKnown = 0;   // pieces of units (an NPC, a monster, a merc, an item), left out; the units' skeletons known
    float feet[3] = {};                   // render metres
    double ms = 0;
};

Result Work(const Request& r) {
    Result res;
    const auto t0 = std::chrono::steady_clock::now();
    if (!LoadSolids()) {
        res.why = "no solids catalogue (D2R VR Settings makes it)";
        return res;
    }
    std::vector<knockprops::LevelEntity> es;
    const char* why = "";
    skel::HeroXform hx;   // (the hero: all that hangs on him is left out - his weapon is never a wall of his own)
    const char* heroWhy = "";
    const uint32_t ownRoot = skel::HeroTransform(&hx, &heroWhy) ? hx.entity : 0u;
    static std::vector<uint32_t> units;   // (the units' skeletons: nor they nor what hangs on them are walls - an NPC standing still; the scan's worker only)
    const char* unitsWhy = "";
    if (!skel::UnitEntities(&units, &unitsWhy)) units.clear();
    res.unitsKnown = (unsigned)units.size();
    static std::vector<uint32_t> ours;   // (the models we draw as the game's own - the belt's bottles, what a hand holds: his own, never walls; the scan's worker only)
    ownmodels::Copy(&ours);
    if (!knockprops::LevelBoxes(r.hero, r.upm, kScanRadiusM, &KnowsPreset, &es, &why, ownRoot, &res.own, &units, &res.units, &ours)) {
        res.why = why;
        return res;
    }
    std::vector<worldwalls::Entity> in(es.size());
    for (size_t i = 0; i < es.size(); ++i) {
        in[i].id = es[i].id;
        std::memcpy(in[i].world, es[i].world, sizeof in[i].world);
        std::memcpy(in[i].lo, es[i].lo, sizeof in[i].lo);
        std::memcpy(in[i].hi, es[i].hi, sizeof in[i].hi);
        in[i].preset = es[i].preset;
        std::memcpy(in[i].local, es[i].local, sizeof in[i].local);
        // steady: where it stood a scan ago (its axes and place within kSteadyUnits)
        static std::unordered_map<uint32_t, std::array<float, 12>> lastPlace;   // (the scan's worker only)
        std::array<float, 12> now;
        for (int k = 0; k < 3; ++k) { for (int c = 0; c < 3; ++c) now[(size_t)(k * 3 + c)] = es[i].world[k * 4 + c]; now[(size_t)(9 + k)] = es[i].world[12 + k]; }
        const auto it = lastPlace.find(es[i].id);
        bool steady = it != lastPlace.end();
        for (size_t q = 0; steady && q < 12; ++q) steady = std::fabs(it->second[q] - now[q]) <= (q < 9 ? 1e-3f : kSteadyUnits);
        in[i].steady = steady;
        if (lastPlace.size() > 20000) lastPlace.clear();
        lastPlace[es[i].id] = now;
    }
    for (int k = 0; k < 3; ++k) res.feet[k] = r.hero[k] / r.upm;
    res.walls = worldwalls::Choose(in, g_solids, r.upm, res.feet, &res.counts, &knockprops::IsMovable);
    res.ok = true;
    res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

class Scanner {
    std::mutex m_;
    std::condition_variable cv_;
    bool started_ = false, have_ = false, busy_ = false, fresh_ = false;
    Request req_;
    Result result_;

public:
    void Start() {
        std::lock_guard<std::mutex> l(m_);
        if (started_) return;
        started_ = true;
        std::thread([this] {
            SetThreadDescription(GetCurrentThread(), L"D2R VR world contact scan");
            for (;;) {
                Request r;
                {
                    std::unique_lock<std::mutex> l(m_);
                    cv_.wait(l, [this] { return have_; });
                    r = req_;
                    have_ = false;
                    busy_ = true;
                }
                Result res = Work(r);
                std::lock_guard<std::mutex> l(m_);
                result_ = std::move(res);
                fresh_ = true;
                busy_ = false;
            }
        }).detach();
    }
    bool Submit(const Request& r) {
        std::lock_guard<std::mutex> l(m_);
        if (have_ || busy_ || fresh_) return false;
        req_ = r;
        have_ = true;
        cv_.notify_one();
        return true;
    }
    bool Take(Result& out) {
        std::lock_guard<std::mutex> l(m_);
        if (!fresh_) return false;
        out = std::move(result_);
        fresh_ = false;
        return true;
    }
};
Scanner g_scanner;

// ---- the latest walls (the tick writes, the hook reads) ----
SRWLOCK g_wallsLock = SRWLOCK_INIT;
std::vector<worldwalls::Box> g_walls;   // render metres
uint64_t g_wallsAt = 0;
uint64_t g_session = 0;

// The tick's own
uint64_t g_scanAt = 0, g_reportAt = 0;
unsigned g_scans = 0, g_failed = 0;
double g_scanMs = 0, g_scanWorst = 0;
worldwalls::Counts g_lastCounts;
unsigned g_lastOwn = 0;   // the last scan's pieces hanging on the hero (left out)
unsigned g_lastUnits = 0, g_lastUnitsKnown = 0;   // the last scan's pieces of units (left out), the units' skeletons it knew
const char* g_lastWhy = "";

// ---- the hands (the hook's thread) ----
struct HandState {
    hw::Memory mem;
    bool have = false;          // drawnA/B is the last pose's (render metres)
    float drawnA[3] = {}, drawnB[3] = {};
    float ctlA[3] = {};         // the controller's (render metres), for the approach
    uint64_t at = 0;
    uint32_t touching = 0;      // the wall it rests on (0 none), for a pulse at a new one
    uint64_t pulseAt = 0;
};
HandState g_hand[2];

// ---- what to say and draw (any thread) ----
SRWLOCK g_noteLock = SRWLOCK_INIT;
struct Drawn {
    bool on = false;
    float controller[3] = {}, drawn[3] = {};   // render metres
    uint32_t wall = 0;
    uint64_t at = 0;
};
Drawn g_drawnHand[2], g_drawnWeapon;
struct Strike { float p[3] = {}; uint64_t at = 0; };
Strike g_strikes[4];
int g_strikeAt = 0;
struct Cost { double us = 0, worst = 0; unsigned steps = 0, strikes = 0, returns = 0, handSteps = 0, handStops = 0, handReturns = 0; double handUs = 0; } g_cost;
uint64_t g_weaponWas = 0;   // the body the weapon rested on lately (kept kStrikeGapMs after the last step on it: a step off it and back is the same strike)
uint64_t g_weaponOnAt = 0;
constexpr uint64_t kStrikeGapMs = 250;
uint64_t g_weaponPulseAt = 0, g_weaponToldAt = 0, g_handToldAt = 0;

void NoteStrike(const float p[3]) {
    Strike& s = g_strikes[g_strikeAt];
    std::memcpy(s.p, p, sizeof s.p);
    s.at = GetTickCount64();
    g_strikeAt = (g_strikeAt + 1) % 4;
}

// A box of the render world into the hands' frame: its centre through the view, each axis as the view turns it (the view is a turn about the vertical and a
// shift - RenderToHead is affine).
bool ToHead(const monsterbodies::View& v, const worldwalls::Box& b, hw::Wall* out) {
    float c[3];
    if (!monsterbodies::RenderToHead(v, b.c, c)) return false;
    hw::Wall w;
    w.c = {c[0], c[1], c[2]};
    for (int i = 0; i < 3; ++i) {
        const float tip[3] = {b.c[0] + b.axis[i][0], b.c[1] + b.axis[i][1], b.c[2] + b.axis[i][2]};
        float t[3];
        if (!monsterbodies::RenderToHead(v, tip, t)) return false;
        const heft::V3 d {t[0] - c[0], t[1] - c[1], t[2] - c[2]};
        const double l = heft::Len(d);
        if (!(l > 1e-6)) return false;
        w.axis[i] = heft::Mul(d, 1.0 / l);
    }
    w.half = {b.half[0], b.half[1], b.half[2]};
    w.id = b.id;
    w.body = b.body;
    *out = w;
    return true;
}

}  // namespace

void Init(LogFn log, PulseFn pulse, const wchar_t* pluginDir) {
    g_log = log;
    g_pulse = pulse;
    g_solidsPath = std::wstring(pluginDir ? pluginDir : L"") + L"d2r_vr_props\\solids.bin";
}

void Configure(bool weapon, bool hands, bool draw) {
    const bool was = g_weapon.load() || g_hands.load();
    g_weapon.store(weapon);
    g_hands.store(hands);
    g_draw.store(draw);
    if (weapon || hands) g_scanner.Start();
    if ((weapon || hands) != was || weapon || hands)
        LogF("world contact: %s%s%s%s", weapon ? "the held weapon meets the level's walls and big props" : "", weapon && hands ? "; " : "",
             hands ? "the bare hands are held at them" : "", !weapon && !hands ? "off" : draw ? " ([debug] world_contact_draw: drawn)" : "");
    if (!weapon && !hands) snprintf(g_status, sizeof g_status, "off");
}
bool WeaponWanted() { return g_weapon.load(); }
bool HandsWanted() { return g_hands.load(); }
bool DrawWanted() { return g_draw.load(); }

void Tick(const Frame& f) {
    const bool on = (g_weapon.load() || g_hands.load()) && f.ok && f.upm > 0.0f;
    const uint64_t nowMs = GetTickCount64();
    if (!on) {
        AcquireSRWLockExclusive(&g_wallsLock);
        g_walls.clear();
        g_wallsAt = 0;
        ReleaseSRWLockExclusive(&g_wallsLock);
        if (g_weapon.load() || g_hands.load()) snprintf(g_status, sizeof g_status, "waiting: VR first person (F4) in a game");
        return;
    }
    if (f.session != g_session) {   // another game or hero: nothing of the last one's walls
        g_session = f.session;
        AcquireSRWLockExclusive(&g_wallsLock);
        g_walls.clear();
        g_wallsAt = 0;
        ReleaseSRWLockExclusive(&g_wallsLock);
    }
    Result res;
    if (g_scanner.Take(res)) {
        ++g_scans;
        g_scanMs += res.ms;
        g_scanWorst = std::max(g_scanWorst, res.ms);
        g_lastWhy = res.why;
        if (res.ok) {
            g_lastCounts = res.counts;
            g_lastOwn = res.own;
            g_lastUnits = res.units;
            g_lastUnitsKnown = res.unitsKnown;
            AcquireSRWLockExclusive(&g_wallsLock);
            g_walls = std::move(res.walls);
            g_wallsAt = nowMs;
            ReleaseSRWLockExclusive(&g_wallsLock);
            snprintf(g_status, sizeof g_status, "running: %u walls near of %u pieces (%u in the catalogue)", res.counts.kept, res.counts.seen, res.counts.matched);
        } else {
            ++g_failed;
            snprintf(g_status, sizeof g_status, "no walls: %s", res.why);
        }
    }
    if (nowMs - g_scanAt >= kScanMs) {
        Request r;
        for (int k = 0; k < 3; ++k) r.hero[k] = f.hero[k];
        r.upm = f.upm;
        if (g_scanner.Submit(r)) g_scanAt = nowMs;
    }
    if (!g_reportAt) g_reportAt = nowMs;
    if (nowMs - g_reportAt >= kReportMs) {   // (one line every half a minute: a log line costs a millisecond)
        g_reportAt = nowMs;
        Cost c;
        AcquireSRWLockExclusive(&g_noteLock);
        c = g_cost;
        g_cost = Cost{};
        ReleaseSRWLockExclusive(&g_noteLock);
        const worldwalls::Counts& k = g_lastCounts;
        using worldwalls::Why;
        LogF("world contact: half a minute - %u scans (%.1f ms each, worst %.1f%s%s); the last: %u pieces near, %u of them in the solids catalogue (%u prefabs' by their id alone, %u laid stretched: "
             "the renderer's box not the mesh's), %u laid by their own box (game objects not in it %u, no mesh %u), %u parts -> %u walls in %u bodies (%u of them pieces joined) "
             "(left out: the hero's own gear and our models on him (the belt's bottles) %u, units' (NPCs, monsters, mercs, items: %u skeletons) %u, movable props %u, foliage %u, not in the catalogue and big %u or moving %u, tiny %u; parts low %u, high %u, round the hero %u, not boxes %u, beyond the nearest %u); "
             "the weapon %u steps with walls (%.1f us each, worst %.0f), %u strikes, %u back into the hand; the bare hands %u steps (%.1f us each), %u held at a wall, %u back to the controller",
             g_scans, g_scans ? g_scanMs / g_scans : 0.0, g_scanWorst, g_failed ? ", failed: " : "", g_failed ? g_lastWhy : "", k.seen, k.matched, k.byId, k.stretched, k.objects,
             k.why[(int)Why::kUnknown], k.why[(int)Why::kNoParts], k.parts, k.kept, k.bodies, k.joined, g_lastOwn, g_lastUnitsKnown, g_lastUnits, k.why[(int)Why::kMovable], k.why[(int)Why::kFoliage], k.why[(int)Why::kBig], k.why[(int)Why::kMoving],
             k.why[(int)Why::kTiny], k.why[(int)Why::kLow], k.why[(int)Why::kHigh], k.why[(int)Why::kRound], k.why[(int)Why::kBad], k.beyond, c.steps, c.steps ? c.us / c.steps : 0.0,
             c.worst, c.strikes, c.returns, c.handSteps, c.handSteps ? c.handUs / c.handSteps : 0.0, c.handStops, c.handReturns);
        g_scans = g_failed = 0;
        g_scanMs = g_scanWorst = 0;
    }
}

int Walls(const monsterbodies::View& v, hw::Wall* out, int max) {
    if (!out || max <= 0) return 0;
    int n = 0;
    float eye[3];
    for (int k = 0; k < 3; ++k) eye[k] = v.eye[k] / std::max(v.upm, 1e-3f);
    AcquireSRWLockShared(&g_wallsLock);
    if (g_wallsAt && GetTickCount64() - g_wallsAt <= kStaleMs)
        for (const worldwalls::Box& b : g_walls) {
            if (n >= max) break;
            const float p[3] = {eye[0], std::clamp(eye[1], b.bottom, b.top), eye[2]};   // (the eyes' column against the box: the hands reach down from them)
            if (worldwalls::Outside(b, p, 0.0f) > kNearHeadM) continue;
            if (ToHead(v, b, &out[n])) ++n;
        }
    ReleaseSRWLockShared(&g_wallsLock);
    return n;
}

void BlockHands(skel::Input* in, const bool holds[2]) {
    if (!in || !g_hands.load() || !in->wallsOn || !in->grabWorldOk) {
        for (HandState& h : g_hand) { h.have = false; h.touching = 0; }
        AcquireSRWLockExclusive(&g_noteLock);
        g_drawnHand[0].on = g_drawnHand[1].on = false;
        ReleaseSRWLockExclusive(&g_noteLock);
        return;
    }
    const double t0 = NowUs();
    monsterbodies::View v;
    for (int k = 0; k < 3; ++k) { v.eye[k] = in->grabEye[k]; v.camR[k] = in->grabCamR[k]; v.camF[k] = in->grabCamF[k]; }
    v.upm = std::clamp(in->grabUpm, 1.0f, 12.0f);
    static hw::Wall walls[hw::kMaxWalls];   // (32 KB: not on the pose thread's stack - called from skel's Arms only, as weight.cpp's walls)
    const int n = Walls(v, walls, hw::kMaxWalls);
    const uint64_t nowMs = GetTickCount64();
    hw::Params p;
    unsigned stops = 0, returns = 0;
    for (int side = 0; side < 2; ++side) {
        HandState& h = g_hand[side];
        Drawn d;
        if (!(in->handsValid & (1u << side)) || (holds && holds[side]) || n == 0) {
            h.have = false;
            h.touching = 0;
            AcquireSRWLockExclusive(&g_noteLock);
            g_drawnHand[side] = d;
            ReleaseSRWLockExclusive(&g_noteLock);
            continue;
        }
        // the hand's capsule: from the controller on along its pointing (its -Z), in the hands' frame
        const float* q = in->rot[side];
        const heft::Q rot = heft::QNorm({q[0], q[1], q[2], q[3]});
        const heft::V3 a {in->hand[side][0], in->hand[side][1], in->hand[side][2]};
        const heft::V3 b = heft::Add(a, heft::Rotate(rot, {0, 0, -kHandLengthM}));
        heft::V3 wasA, wasB;
        bool was = false;
        if (h.have && nowMs - h.at < 200) {   // where it was drawn, from the render world into this pose's frame (the hero walked, the head turned)
            float ha[3], hb[3];
            was = monsterbodies::RenderToHead(v, h.drawnA, ha) && monsterbodies::RenderToHead(v, h.drawnB, hb);
            wasA = {ha[0], ha[1], ha[2]};
            wasB = {hb[0], hb[1], hb[2]};
        }
        hw::Report rep;
        const heft::V3 off = hw::Block(a, b, kHandRadiusM, was ? &wasA : nullptr, was ? &wasB : nullptr, walls, n, h.mem, p, &rep);
        returns += rep.returned;
        for (int k = 0; k < 3; ++k) in->hand[side][k] += (float)(k == 0 ? off.x : k == 1 ? off.y : off.z);
        // kept in the render world for the next pose
        const float da[3] = {(float)(a.x + off.x), (float)(a.y + off.y), (float)(a.z + off.z)}, db[3] = {(float)(b.x + off.x), (float)(b.y + off.y), (float)(b.z + off.z)};
        const float ca[3] = {(float)a.x, (float)a.y, (float)a.z};
        float ra[3], rb[3], rc[3];
        if (monsterbodies::HeadToRender(v, da, ra) && monsterbodies::HeadToRender(v, db, rb) && monsterbodies::HeadToRender(v, ca, rc)) {
            float speed = 0.0f;   // the controller's speed into the face (a pulse at a new touch)
            if (h.have && nowMs > h.at && rep.walls > 0) {
                const float dtS = std::max(0.004f, (float)(nowMs - h.at) * 0.001f);
                const float mv[3] = {rc[0] - h.ctlA[0], rc[1] - h.ctlA[1], rc[2] - h.ctlA[2]};
                float nr[3];   // the face's normal in the render world
                const float n0[3] = {0, 0, 0}, n1[3] = {(float)rep.normal.x, (float)rep.normal.y, (float)rep.normal.z};
                float r0[3];
                if (monsterbodies::HeadToRender(v, n0, r0) && monsterbodies::HeadToRender(v, n1, nr))
                    speed = -((mv[0]) * (nr[0] - r0[0]) + mv[1] * (nr[1] - r0[1]) + mv[2] * (nr[2] - r0[2])) / dtS;
            }
            std::memcpy(h.drawnA, ra, sizeof ra);
            std::memcpy(h.drawnB, rb, sizeof rb);
            std::memcpy(h.ctlA, rc, sizeof rc);
            h.have = true;
            h.at = nowMs;
            d.on = true;
            std::memcpy(d.controller, rc, sizeof rc);
            std::memcpy(d.drawn, ra, sizeof ra);
            d.wall = rep.walls > 0 ? rep.id : 0;
            d.at = nowMs;
            if (rep.walls > 0) {
                ++stops;
                if (rep.id != h.touching && speed > 0.4f && g_pulse && nowMs - h.pulseAt >= 150) {   // (PS VR2 feels 0.55+ / 45 ms: vr/haptics.h)
                    h.pulseAt = nowMs;
                    g_pulse(side, std::clamp(0.55f + 0.15f * (speed - 0.4f), 0.55f, 0.9f), 40);
                }
                if (rep.id != h.touching && nowMs - g_handToldAt >= 2000) {
                    g_handToldAt = nowMs;
                    float pr[3];
                    const float pp[3] = {(float)rep.point.x, (float)rep.point.y, (float)rep.point.z};
                    if (monsterbodies::HeadToRender(v, pp, pr)) NoteStrike(pr);
                    LogF("world contact: the %s hand met wall %u at %.1f m/s - held at its face (%d wall%s near; the controller %.0f cm past)", side == 0 ? "right" : "left", rep.id,
                         speed, n, n == 1 ? "" : "s", rep.gap * 100.0);
                }
            }
            if (rep.returned && nowMs - g_handToldAt >= 2000) {
                g_handToldAt = nowMs;
                LogF("world contact: the %s hand's controller went %.0f cm past wall %u's face - the hand back on the controller, through it", side == 0 ? "right" : "left",
                     rep.gap * 100.0, rep.id);
            }
            h.touching = rep.walls > 0 ? rep.id : 0;
        } else {
            h.have = false;
        }
        AcquireSRWLockExclusive(&g_noteLock);
        g_drawnHand[side] = d;
        ReleaseSRWLockExclusive(&g_noteLock);
    }
    const double us = NowUs() - t0;
    AcquireSRWLockExclusive(&g_noteLock);
    ++g_cost.handSteps;
    g_cost.handUs += us;
    g_cost.handStops += stops;
    g_cost.handReturns += returns;
    ReleaseSRWLockExclusive(&g_noteLock);
}

void NoteWeapon(const WeaponNote& n) {
    const uint64_t nowMs = GetTickCount64();
    AcquireSRWLockExclusive(&g_noteLock);
    ++g_cost.steps;
    g_cost.us += n.costUs;
    g_cost.worst = std::max(g_cost.worst, (double)n.costUs);
    g_cost.returns += n.returned;
    Drawn d;
    d.on = n.controllerOk;
    std::memcpy(d.controller, n.controller, sizeof d.controller);
    std::memcpy(d.drawn, n.drawn, sizeof d.drawn);
    d.wall = n.walls > 0 ? n.id : 0;
    d.at = nowMs;
    g_drawnWeapon = d;
    if (g_weaponWas && nowMs - g_weaponOnAt > kStrikeGapMs) g_weaponWas = 0;
    const bool strike = n.walls > 0 && n.id != g_weaponWas && n.approach >= 0.5f;   // (one strike a body: its boxes are one object)
    if (strike) {
        ++g_cost.strikes;
        NoteStrike(n.point);
    }
    if (n.walls > 0) { g_weaponWas = n.id; g_weaponOnAt = nowMs; }
    ReleaseSRWLockExclusive(&g_noteLock);
    if (strike) {
        if (g_pulse && nowMs - g_weaponPulseAt >= 150) {   // a knock on the wall in the hands that hold it (PS VR2 feels 0.7 / 45 ms; a gentler one is lost)
            g_weaponPulseAt = nowMs;
            const float amp = std::clamp(0.6f + 0.1f * (n.approach - 0.5f), 0.6f, 1.0f);
            for (int side = 0; side < 2; ++side)
                if (n.holds[side]) g_pulse(side, amp, 45);
        }
        if (nowMs - g_weaponToldAt >= 1000) {
            g_weaponToldAt = nowMs;
            LogF("world contact: the weapon struck wall %u at %.1f m/s (%.1f cm in, put back on its face)", n.id, n.approach, n.depthCm);
        }
    }
    if (n.returned && nowMs - g_weaponToldAt >= 1000) {
        g_weaponToldAt = nowMs;
        LogF("world contact: the hand's controller went far past the wall the weapon rested on - the weapon back in the hand, through the wall");
    }
}

int Marks(uint64_t nowMs, const float eyeWorld[3], float upm, float (*a)[4], float (*b)[4], int max) {
    if (!g_draw.load() || max <= 0 || !(g_weapon.load() || g_hands.load()) || !(upm > 0.0f)) return 0;
    Drawn hands[2], weapon;
    Strike strikes[4];
    AcquireSRWLockShared(&g_noteLock);
    hands[0] = g_drawnHand[0];
    hands[1] = g_drawnHand[1];
    weapon = g_drawnWeapon;
    std::memcpy(strikes, g_strikes, sizeof strikes);
    ReleaseSRWLockShared(&g_noteLock);
    int k = 0;
    auto put = [&](const float p[3], const float q[3], float radiusM, int colour, int style) {
        if (k >= max) return;
        for (int i = 0; i < 3; ++i) { a[k][i] = p[i] * upm - eyeWorld[i]; b[k][i] = q[i] * upm - eyeWorld[i]; }
        a[k][3] = radiusM * upm;
        b[k][3] = (float)meleeboxes::TouchKind(colour, style);
        ++k;
    };
    // the controller (a blue ball) and the hand or weapon as drawn (green), a red line between while they are apart
    auto pair = [&](const Drawn& d) {
        if (!d.on || nowMs - d.at > 300) return;
        put(d.controller, d.controller, 0.02f, meleeboxes::kCBlue, meleeboxes::kBall);
        put(d.drawn, d.drawn, 0.025f, meleeboxes::kCGreen, meleeboxes::kBall);
        const float dx = d.drawn[0] - d.controller[0], dy = d.drawn[1] - d.controller[1], dz = d.drawn[2] - d.controller[2];
        if (dx * dx + dy * dy + dz * dz > 1e-4f) put(d.controller, d.drawn, 0.006f, meleeboxes::kCRed, meleeboxes::kTube);
    };
    pair(hands[0]);
    pair(hands[1]);
    pair(weapon);
    for (const Strike& s : strikes)
        if (s.at && nowMs - s.at < kMarkMs) put(s.p, s.p, 0.04f, meleeboxes::kCAmber, meleeboxes::kBall);
    // the walls: the ones met red, the others near a hand or the weapon amber - their twelve edges as thin tubes
    std::vector<worldwalls::Box> nearWalls;
    AcquireSRWLockShared(&g_wallsLock);
    if (g_wallsAt && nowMs - g_wallsAt <= kStaleMs) nearWalls = g_walls;
    ReleaseSRWLockShared(&g_wallsLock);
    const Drawn* refs[3] = {&hands[0], &hands[1], &weapon};
    auto distance = [&](const worldwalls::Box& w) {
        float best = 1e9f;
        for (const Drawn* d : refs)
            if (d->on && nowMs - d->at <= 300) best = std::min(best, worldwalls::Outside(w, d->drawn, 0.0f));
        return best;
    };
    std::sort(nearWalls.begin(), nearWalls.end(), [&](const worldwalls::Box& x, const worldwalls::Box& y) { return distance(x) < distance(y); });
    int boxes = 0;
    for (const worldwalls::Box& w : nearWalls) {
        if (boxes >= 3 || distance(w) > 1.5f || k + 12 > max) break;
        ++boxes;
        const bool met = (hands[0].on && hands[0].wall == w.body) || (hands[1].on && hands[1].wall == w.body) || (weapon.on && weapon.wall == w.body);
        float corner[8][3];
        for (int c = 0; c < 8; ++c)
            for (int i = 0; i < 3; ++i) {
                corner[c][i] = w.c[i];
                for (int ax = 0; ax < 3; ++ax) corner[c][i] += ((c >> ax & 1) ? 1.0f : -1.0f) * w.half[ax] * w.axis[ax][i];
            }
        for (int c = 0; c < 8; ++c)
            for (int ax = 0; ax < 3; ++ax)
                if (!(c >> ax & 1)) put(corner[c], corner[c | (1 << ax)], 0.008f, met ? meleeboxes::kCRed : meleeboxes::kCAmber, meleeboxes::kTube);
    }
    return k;
}

const char* StatusText() { return g_status; }

}  // namespace worldcontact
