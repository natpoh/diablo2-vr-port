// knock_props.cpp - see knock_props.h.
//
// The scan of the renderer's registry, the write of a moving prop's matrix and what a prop stands on are after bearstonem/d2r-pcvr (MIT), vr/prop_scan.cpp and
// steamvr/props.h of feat/knockable-props (2026-10-22), in our words. The physics is the weight library's (vr/props_lib.h), on a thread of our own.
//
// The renderer's entt registry, D2R 3.3.93787 (vr/skeletons.cpp has the same layout): pools at registry +0 / +8 in 0x28-byte slots, the storage at slot +0x20; a storage keeps
// sparse pages at +0x08 / +0x10, packed entities at +0x20 / +0x28, its components contiguous at +0x38 / +0x40. Three storages matter, told apart by what they hold:
//   transforms, 0xD0 bytes: local matrix +0x00, world +0x40, the world before that +0x80 (the motion vectors' last frame), the parent entity +0xC0. A static level model is
//              never updated again: the renderer draws it from +0x40 every frame - that is where a moving prop's pose goes;
//   preset ids, 4 bytes: the level preset entity's "id" (the catalogue's);
//   bounds, 0x60 bytes: local min / max (vec4 each), then the world min / max - moved with the prop, so culling follows it.
// Every read is a guarded copy; the only writes are those matrices and bounds of props this module moved, each checked first (the same entity at that index, the same
// parent and preset place, the matrix last put there). Nothing is called in the game.

#include "knock_props.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "belt_glow.h"   // [debug] knock_props_glow: the game's own highlight on the props found
#include "d2r_vr_props.h"
#include "pose_share.h"   // [multiplayer] pose_share_held: a prop's name hashed as the others hash it (poseshare::NameHash)
#include "props_lib.h"
#include "props_scene.h"
#include "sigscan.h"
#include "view_gate.h"

namespace knockprops {

namespace {

using propscene::Box;
using propscene::Vec;

constexpr uint64_t kRegistryRva = 0x2677170;
constexpr size_t kTransformSize = 0xD0, kBoundsSize = 0x60, kIdSize = 4, kMaxPools = 4096, kMaxEntities = 200000, kChunk = 512;
constexpr float kSupportScan = 6.0f;           // m round the hero: what the props may stand on
constexpr unsigned kMaxNear = 4096;
constexpr uint32_t kNoParent = 0xFFFFF;
constexpr double kStep = 1.0 / 120.0;          // s: the physics' fixed step
constexpr int kMaxCatchUp = 4;                 // steps at most in one wake of the thread (after a stall it does not run away)
constexpr uint64_t kScanMs = 500, kRediscoverMs = 1000, kKeepMs = 2000;
constexpr float kRecentre = 20.0f;             // m: the hero this far from the physics origin: a new world (once nothing moves)
constexpr float kFistKg = 2.0f;                // a hand (the weight library's)
constexpr float kDefaultSteelKg = 1.5f;        // a weapon whose steel the weight model does not tell

LogFn g_log = nullptr;
PulseFn g_pulse = nullptr;
std::wstring g_pluginDir;
std::atomic<bool> g_wanted{false}, g_trace{false}, g_glow{false};
std::atomic<float> g_glowH{0.6f};
bool g_glowing = false;   // the tick's: props handed to the glow
std::atomic<bool> g_grabWanted{false};   // [weight] grab_props
std::atomic<float> g_strength{1.0f}, g_haptic{0.35f};
propscene::Catalog g_catalog;
std::atomic<bool> g_catalogReady{false};
std::atomic<int> g_state{0};   // 0 off, 1 waiting (no library / catalogue / F4), 2 running
char g_status[200] = "off";

void Log(const char* s) { if (g_log) g_log(s); }
void LogF(const char* fmt, ...) {
    char b[700];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    Log(b);
}

double Now() {
    static const double inv = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return 1.0 / (double)f.QuadPart; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * inv;
}

// ---- guarded memory ----
bool CopyGuarded(void* dst, const void* src, size_t n) noexcept {
    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template <class T> bool Read(uintptr_t p, T& v) { return p > 0x10000 && CopyGuarded(&v, (const void*)p, sizeof v); }
bool ReadBytes(uintptr_t p, void* out, size_t n) { return p > 0x10000 && n && CopyGuarded(out, (const void*)p, n); }
bool WriteBytes(uintptr_t p, const void* in, size_t n) { return p > 0x10000 && n && CopyGuarded((void*)p, in, n); }
bool Same(float a, float b) { return std::fabs(a - b) <= 1e-3f + 1e-6f * std::fabs(a); }
bool Same(const float* a, const float* b, int n) { for (int i = 0; i < n; ++i) if (!Same(a[i], b[i])) return false; return true; }
bool FiniteN(const float* v, int n) { for (int i = 0; i < n; ++i) if (!std::isfinite(v[i])) return false; return true; }

struct Storage {
    uintptr_t object = 0, sparse = 0, sparseEnd = 0, packed = 0, payload = 0;
    size_t count = 0, elem = 0;
    bool Load(uintptr_t at) {
        uintptr_t h[9] = {};
        *this = {};
        if (!ReadBytes(at, h, sizeof h)) return false;
        object = at; sparse = h[1]; sparseEnd = h[2]; packed = h[4]; payload = h[7];
        count = h[5] >= h[4] && (h[5] - h[4]) % 4 == 0 ? (h[5] - h[4]) / 4 : 0;
        elem = count && h[8] > h[7] && (h[8] - h[7]) % count == 0 ? (h[8] - h[7]) / count : 0;
        return count && count <= kMaxEntities && elem && elem <= 0x1000;
    }
    bool At(uint32_t index, uint32_t entity) const {
        uint32_t back = 0;
        return index < count && Read(packed + uintptr_t(index) * 4, back) && back == entity;
    }
    bool Dense(uint32_t entity, uint32_t& index) const {   // the dense index of an entity, as the renderer's views find it
        const size_t page = (entity >> 8) & 0xFFF;
        uintptr_t pagePtr = 0;
        uint32_t v = 0;
        if (sparseEnd < sparse || page >= (sparseEnd - sparse) / 8 || !Read(sparse + page * 8, pagePtr) || !pagePtr || !Read(pagePtr + (entity & 255) * 4, v)) return false;
        v &= 0xFFFFF;
        if (v == 0xFFFFF || !At(v, entity)) return false;
        index = v;
        return true;
    }
    template <class T> bool Copy(std::vector<uint32_t>& ids, std::vector<T>& items) const {
        ids.resize(count);
        items.resize(count * elem / sizeof(T));
        return elem % sizeof(T) == 0 && ReadBytes(packed, ids.data(), count * 4) && ReadBytes(payload, items.data(), count * elem);
    }
};

std::vector<uintptr_t> Pools() {
    std::vector<uintptr_t> objects;
    const uintptr_t registry = d2rsig::Addr(kRegistryRva);
    uintptr_t begin = 0, end = 0;
    if (!registry || !Read(registry, begin) || !Read(registry + 8, end) || end <= begin || (end - begin) % 40 || (end - begin) / 40 > kMaxPools) return objects;
    std::vector<uint8_t> slots(end - begin);
    if (!ReadBytes(begin, slots.data(), slots.size())) return objects;
    objects.resize(slots.size() / 40);
    for (size_t i = 0; i < objects.size(); ++i) std::memcpy(&objects[i], slots.data() + i * 40 + 0x20, 8);
    return objects;
}

// The transform storage: the largest with 0xD0-byte components whose first matrices look like matrices.
bool Transforms(const std::vector<uintptr_t>& pools, Storage& out) {
    out = {};
    for (const uintptr_t object : pools) {
        Storage s;
        float m[32];
        if (!object || !s.Load(object) || s.elem != kTransformSize || s.count <= out.count || !ReadBytes(s.payload, m, sizeof m)) continue;
        if (m[15] == 1 && m[31] == 1 && m[3] == 0 && m[7] == 0 && m[11] == 0 && m[19] == 0 && m[23] == 0 && m[27] == 0) out = s;
    }
    return out.count > 0;
}

// ---- the scan (a worker thread) ----
struct Request {
    float hero[3] = {};    // render units
    float upm = 1;
    int idPool = -1, boundsPool = -1;
    bool discover = false;
};
struct Seen {                 // a prop near the hero, as the renderer has it now (render units)
    uint32_t entity = 0, parent = 0, transformIndex = 0, boundsIndex = 0;
    int kind = -1;
    float local[3] = {}, world[16] = {};
    Box localBounds, placed;  // its model's bounds (its own units) and in the world
    propscene::Support support;
};
struct Found {
    bool ok = false;
    const char* why = "";
    std::vector<Seen> props;
    uintptr_t transforms = 0, bounds = 0;
    int idPool = -1, boundsPool = -1;
    unsigned nearby = 0, matched = 0, unbounded = 0, unrigid = 0, sized = 0, beyond = 0;
    unsigned byId = 0, elsewhere = 0;   // of matched: prefabs' props by their id alone; ids the catalogue has at other places only (not counted)
    double ms = 0;
};

struct NearE {
    uint32_t entity = 0, index = 0, parent = 0, boundsIndex = 0;
    float local[3] = {}, world[16] = {};
    int kind = -1;
    bool reach = false, bounded = false;
    Box localBounds, worldBounds;
};

Found Work(const Request& r) {
    Found f;
    const auto started = std::chrono::steady_clock::now();
    const float u = r.upm;
    const float floorY = r.hero[1];
    const std::vector<uintptr_t> pools = Pools();
    Storage transforms;
    if (pools.empty()) { f.why = "the renderer's registry does not read"; return f; }
    if (!Transforms(pools, transforms)) { f.why = "no transform storage"; return f; }
    f.transforms = transforms.object;
    std::vector<uint32_t> ids(transforms.count);
    if (!ReadBytes(transforms.packed, ids.data(), ids.size() * 4)) { f.why = "the transforms moved while read"; return f; }
    std::vector<NearE> nearby;
    std::unordered_map<uint32_t, uint32_t> nearIndex;
    std::vector<float> block(kChunk * kTransformSize / 4);
    for (size_t at = 0; at < transforms.count && nearby.size() < kMaxNear; at += kChunk) {
        const size_t n = std::min(kChunk, transforms.count - at);
        if (!ReadBytes(transforms.payload + at * kTransformSize, block.data(), n * kTransformSize)) continue;
        for (size_t i = 0; i < n && nearby.size() < kMaxNear; ++i) {
            const float* m = block.data() + i * (kTransformSize / 4);
            const float dx = m[28] - r.hero[0], dy = m[29] - floorY, dz = m[30] - r.hero[2];
            const float h = std::hypot(dx, dz);
            if (!(h <= kSupportScan * u) || !(dy >= -propscene::kBelow * u && dy <= (propscene::kAbove + 0.5f) * u)) continue;
            NearE e;
            e.entity = ids[at + i];
            e.index = uint32_t(at + i);
            std::memcpy(&e.parent, m + 48, 4);
            std::memcpy(e.local, m + 12, 12);
            std::memcpy(e.world, m + 16, 64);
            e.reach = h <= propscene::kPropReach * u && dy <= propscene::kAbove * u;
            if (!FiniteN(e.world, 16)) continue;
            nearIndex[e.entity] = uint32_t(nearby.size());
            nearby.push_back(e);
        }
    }
    f.nearby = unsigned(nearby.size());
    // the preset ids: the 4-byte storage whose values the catalogue knows at these entities' local positions
    std::vector<uint32_t> packed, values;
    auto matchIds = [&](const Storage& s, bool apply) {
        unsigned matched = 0;
        if (apply) f.byId = f.elsewhere = 0;
        if (!s.Copy(packed, values)) return 0u;
        for (size_t i = 0; i < s.count; ++i) {
            const auto it = nearIndex.find(packed[i]);
            if (it == nearIndex.end()) continue;
            NearE& e = nearby[it->second];
            if ((e.parent & kNoParent) == kNoParent) continue;   // not part of a level preset
            int how = 0;
            const int kind = g_catalog.Find(values[i], e.local, &how);
            if (apply && how == 3) ++f.elsewhere;
            if (kind < 0) continue;
            ++matched;
            if (apply && how == 2) ++f.byId;
            if (apply) e.kind = kind;
        }
        return matched;
    };
    Storage idStorage;
    if (r.idPool >= 0 && size_t(r.idPool) < pools.size() && idStorage.Load(pools[size_t(r.idPool)]) && idStorage.elem == kIdSize) f.idPool = r.idPool;
    else if (r.discover) {
        unsigned best = 0;
        for (size_t i = 0; i < pools.size(); ++i) {
            Storage s;
            if (!pools[i] || !s.Load(pools[i]) || s.elem != kIdSize || s.count < 64) continue;
            const unsigned m = matchIds(s, false);
            if (m > best) { best = m; f.idPool = int(i); idStorage = s; }
        }
    }
    if (f.idPool >= 0) f.matched = matchIds(idStorage, true);
    // the bounds: the 0x60-byte storage whose world box is its local box under the entity's world matrix
    std::vector<float> boxes;
    auto matchBounds = [&](const Storage& s, bool apply, unsigned& checked) {
        unsigned agree = 0;
        checked = 0;
        if (!s.Copy(packed, boxes)) return 0u;
        for (size_t i = 0; i < s.count; ++i) {
            const auto it = nearIndex.find(packed[i]);
            if (it == nearIndex.end()) continue;
            NearE& e = nearby[it->second];
            const float* b = boxes.data() + i * (kBoundsSize / 4);
            if (!FiniteN(b, 24)) continue;
            const Box local{{b[0], b[1], b[2]}, {b[4], b[5], b[6]}}, world{{b[16], b[17], b[18]}, {b[20], b[21], b[22]}};
            const float scale = std::max({std::fabs(world.lo.x), std::fabs(world.hi.x), std::fabs(world.lo.z), std::fabs(world.hi.z), 1.0f});
            ++checked;
            if (!propscene::Near(propscene::WorldBounds(local, e.world), world, 0.02f + scale * 2e-5f)) continue;
            ++agree;
            if (apply) { e.bounded = true; e.boundsIndex = uint32_t(i); e.localBounds = local; e.worldBounds = world; }
        }
        return agree;
    };
    Storage boundsStorage;
    unsigned checked = 0;
    if (r.boundsPool >= 0 && size_t(r.boundsPool) < pools.size() && boundsStorage.Load(pools[size_t(r.boundsPool)]) && boundsStorage.elem == kBoundsSize) f.boundsPool = r.boundsPool;
    else if (r.discover && f.matched) {
        unsigned best = 0;
        for (size_t i = 0; i < pools.size(); ++i) {
            Storage s;
            if (!pools[i] || !s.Load(pools[i]) || s.elem != kBoundsSize) continue;
            const unsigned agree = matchBounds(s, false, checked);
            if (agree >= 3 && agree * 2 >= checked && agree > best) { best = agree; f.boundsPool = int(i); boundsStorage = s; }
        }
    }
    if (f.boundsPool < 0) { f.why = f.matched ? "no bounds storage" : "no catalogued props near"; f.ok = true; return f; }
    matchBounds(boundsStorage, true, checked);
    f.bounds = boundsStorage.object;
    // what the props stand on: the level's entities that are not props
    std::vector<Box> around;
    for (const NearE& e : nearby)
        if (e.bounded && e.kind < 0 && (e.parent & kNoParent) != kNoParent && propscene::Finite(e.worldBounds.lo) && propscene::Finite(e.worldBounds.hi) &&
            e.worldBounds.lo.x <= e.worldBounds.hi.x && e.worldBounds.lo.y <= e.worldBounds.hi.y && e.worldBounds.lo.z <= e.worldBounds.hi.z)
            around.push_back(e.worldBounds);
    std::vector<const NearE*> found;
    for (const NearE& e : nearby) {
        if (e.kind < 0 || !e.reach) continue;
        if (!e.bounded) { ++f.unbounded; continue; }
        propscene::Placement at;
        if (!propscene::Decompose(e.world, at)) { ++f.unrigid; continue; }
        const Vec size = e.localBounds.hi - e.localBounds.lo;
        const float x = size.x * at.scale.x / u, y = size.y * at.scale.y / u, z = size.z * at.scale.z / u;
        if (std::max({x, y, z}) > propscene::kMaxExtent || std::min({x, y, z}) < propscene::kMinExtent) { ++f.sized; continue; }
        found.push_back(&e);
    }
    auto distance = [&](const NearE* e) { return std::hypot(e->world[12] - r.hero[0], e->world[14] - r.hero[2]); };
    std::sort(found.begin(), found.end(), [&](const NearE* a, const NearE* b) { return distance(a) < distance(b); });
    if (found.size() > propscene::kMaxSeen) { f.beyond = unsigned(found.size() - propscene::kMaxSeen); found.resize(propscene::kMaxSeen); }
    std::vector<Box> placed;
    for (const NearE* e : found) placed.push_back(propscene::WorldBounds(e->localBounds, e->world));
    std::vector<propscene::Support> supports(found.size());
    for (size_t i = 0; i < found.size(); ++i) {
        bool onProp = false;
        for (size_t j = 0; j < found.size() && !onProp; ++j) onProp = j != i && propscene::RestsOn(placed[i], placed[j], u);
        if (onProp) { supports[i] = {false, placed[i].lo.y}; supports[i].onProp = true; }
        else supports[i] = propscene::FindSupport(placed[i], around, floorY, u);
    }
    propscene::JoinLedges(supports, u);
    for (size_t i = 0; i < found.size(); ++i) {
        const NearE* e = found[i];
        Seen s;
        s.entity = e->entity; s.parent = e->parent; s.transformIndex = e->index; s.boundsIndex = e->boundsIndex; s.kind = e->kind;
        std::memcpy(s.local, e->local, 12);
        std::memcpy(s.world, e->world, 64);
        s.localBounds = e->localBounds;
        s.placed = placed[i];
        s.support = supports[i];
        f.props.push_back(s);
    }
    f.ok = true;
    f.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return f;
}

// One job at a time on a worker of its own: the reads of the registry never run on the game's threads.
class Scanner {
    std::mutex m_;
    std::condition_variable cv_;
    std::thread t_;
    bool have_ = false, busy_ = false, done_ = false;
    Request req_;
    Found result_;
    bool fresh_ = false;

public:
    void Start() {
        std::lock_guard<std::mutex> l(m_);
        if (t_.joinable()) return;
        t_ = std::thread([this] {
            for (;;) {
                Request r;
                {
                    std::unique_lock<std::mutex> l(m_);
                    cv_.wait(l, [this] { return have_; });
                    r = req_;
                    have_ = false;
                    busy_ = true;
                }
                Found f = Work(r);
                std::lock_guard<std::mutex> l(m_);
                result_ = std::move(f);
                fresh_ = true;
                busy_ = false;
            }
        });
        t_.detach();
    }
    bool Submit(const Request& r) {
        std::lock_guard<std::mutex> l(m_);
        if (have_ || busy_ || fresh_) return false;
        req_ = r;
        have_ = true;
        cv_.notify_one();
        return true;
    }
    bool Take(Found& out) {
        std::lock_guard<std::mutex> l(m_);
        if (!fresh_) return false;
        out = std::move(result_);
        fresh_ = false;
        return true;
    }
};
Scanner g_scanner;

// ---- between the scan, vrcam's tick, the physics thread and the write pass ----
struct Tracked {                // a prop in the world, and what the write pass knows of it
    uint32_t entity = 0, parent = 0, transformIndex = 0, boundsIndex = 0;
    float local[3] = {}, world[16] = {};
    Box bounds;
    propscene::Vec scale{1, 1, 1};
    int kind = -1;               // the catalogue's kind (the trace's name)
    bool hasBounds = false, settle = false, moved = false;
    uint64_t writtenAt = 0;
};

std::mutex g_inMutex;            // vrcam's tick <-> the physics thread
struct In {
    uint64_t world = 0;          // a new number: the physics thread clears its world (a new area, a new origin)
    float floorY = 0;            // physics frame
    uint64_t propsSerial = 0;
    std::vector<D2rpProp> props;
    std::vector<D2rpBox> statics;
    D2rpMover movers[2] = {};
    bool run = false;
    // [weight] grab_props: the hands as the hook noted them (physics frame: the origin below, the tick's), straight to the thread - no tick between
    float origin[3] = {};
    D2rpHand hands[D2RP_HANDS] = {};
    bool handsOn = false;
} g_in;

std::mutex g_outMutex;           // the physics thread -> the write pass and vrcam's tick
struct Snap {
    double t = 0;
    std::vector<D2rpPose> poses;
};
struct Out {
    uint64_t world = 0;
    Snap prev, cur;
    std::vector<D2rpKnock> knocks;
    unsigned steps = 0, moving = 0, late = 0;
    double stepUs = 0, worstUs = 0;
    // [weight] grab_props: each hand after the newest step, where it and its prop were the step before (the write pass draws between the two), what it did since it was read
    struct HandShot {
        bool ok = false;
        D2rpHandOut out{};
        float prevPos[3] = {}, prevP[3] = {};
        int events = 0;
        float amp = 0, sec = 0, hz = 0;
    } hands[D2RP_HANDS];
    float origin[3] = {};      // the physics frame's origin these were stepped in
    bool handsOn = false;
} g_out;

std::mutex g_trackMutex;         // vrcam's tick <-> the write pass
std::vector<Tracked> g_tracked;
std::atomic<uintptr_t> g_transformsObject{0}, g_boundsObject{0};
std::atomic<bool> g_active{false};    // the write pass has something to do: a prop moving or settling (the world-matrix function is called thousands of times a frame)
std::atomic<bool> g_settling{false};
std::atomic<bool> g_passing{false};
std::atomic<double> g_passAt{0.0};
// [multiplayer] pose_share_held: where each prop stood when first found this session (render units, under g_trackMutex) - the level's own place of it, the same on every
// PC, so another player's word on a prop finds ours; and his props: what his pose says of each (g_remoteWant, the update thread's), and the props of ours put there
// (g_remotePut, by entity, under g_trackMutex), kept where he left them until the area loads again
std::unordered_map<uint32_t, propscene::Vec> g_home;
std::mutex g_remoteMutex;
std::vector<RemoteProp> g_remoteWant;
struct RemotePut { propscene::Vec p; propscene::Quat q; bool dirty = true; bool settle = false; };
std::unordered_map<uint32_t, RemotePut> g_remotePut;
std::atomic<bool> g_remoteDirty{false};
std::atomic<unsigned> g_remoteWanted{0}, g_remoteMatched{0};
constexpr float kRemoteHomeM = 0.5f;   // his prop and ours are the same when they stood this near (metres) and have the same name
float g_origin[3] = {};          // m: the physics frame's origin in the render world (under g_trackMutex for the pass; the tick's own copy)
float g_upm = 1.0f;
uint64_t g_world = 0;            // the tick's: the world number the thread was told

// The tick's own
uint64_t g_scanAt = 0, g_discoverAt = 0;
int g_idPool = -1, g_boundsPool = -1;
uint64_t g_session = 0;
bool g_haveOrigin = false;
std::atomic<unsigned> g_writes{0}, g_settles{0}, g_lost{0}, g_passes{0}, g_knocks{0};
uint64_t g_reportAt = 0;
const char* g_lastWhy = "";
double g_lastScanMs = 0;
unsigned g_lastSeen = 0, g_lastMatched = 0, g_lastById = 0, g_lastElsewhere = 0, g_lastNearby = 0;

// The blades hit by touch draws (render metres), each hand
std::mutex g_bladeMutex;
struct Blade {
    bool on = false;
    int kind = 0;
    float a[3] = {}, b[3] = {}, radius = 0;
    double time = 0;
    float vel[3] = {}, angVel[3] = {};
};
Blade g_blades[2];

// ---- the physics thread ----
std::atomic<bool> g_threadUp{false};
void PhysicsThread() {
    // a core of its own: the last logical processor (the game's busiest threads start low), at the game's priority - never above it
    DWORD_PTR proc = 0, sys = 0;
    int ideal = -1;
    if (GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) && proc)
        for (int i = 63; i >= 0; --i)
            if (proc & (DWORD_PTR(1) << i)) { ideal = i; break; }
    if (ideal >= 0) SetThreadIdealProcessor(GetCurrentThread(), (DWORD)ideal);
    SetThreadDescription(GetCurrentThread(), L"D2R VR props physics");
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LogF("knock props: the physics thread runs at a fixed %.0f Hz (%.2f ms steps), its ideal processor %d of this machine's%s", 1.0 / kStep, kStep * 1000.0, ideal,
         timer ? "" : " (no high-resolution timer: Sleep(1))");
    D2rpParams params{};
    params.size = sizeof params;
    params.gravity = 9.81f;
    params.substeps = 4;
    params.maxMoving = 12;
    params.restSpeed = 0.03f;
    params.restTime = 0.6f;
    params.moveLimit = 12.0f;
    params.strength = 1.0f;
    params.propMass = 1.0f;
    void* world = nullptr;
    float strengthMade = -1.0f;
    uint64_t worldNo = 0, propsSerial = 0;
    std::vector<D2rpProp> props;
    std::vector<D2rpBox> statics;
    D2rpPose poses[D2RP_MAX_PROPS];
    D2rpKnock knocks[32];
    double next = Now();
    for (;;) {
        const double now = Now();
        if (now < next) {
            if (timer) {
                LARGE_INTEGER due;
                due.QuadPart = -(LONGLONG)((next - now) * 1e7);
                if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, 20);
                else Sleep(1);
            } else {
                Sleep(1);
            }
            continue;
        }
        bool run = false, clear = false, newProps = false, newStatics = false;
        float floorY = 0;
        D2rpMover movers[2];
        D2rpHand hands[D2RP_HANDS];
        float origin[3] = {};
        bool handsOn = false;
        {
            std::lock_guard<std::mutex> l(g_inMutex);
            run = g_in.run;
            if (run) {
                if (g_in.world != worldNo) { worldNo = g_in.world; clear = true; floorY = g_in.floorY; }
                if (g_in.propsSerial != propsSerial || clear) {
                    propsSerial = g_in.propsSerial;
                    props = g_in.props;
                    newProps = true;
                    if (statics.size() != g_in.statics.size() || !std::equal(statics.begin(), statics.end(), g_in.statics.begin(),
                                                                              [](const D2rpBox& a, const D2rpBox& b) { return std::memcmp(&a, &b, sizeof a) == 0; }))
                        newStatics = true;
                    statics = g_in.statics;
                }
                movers[0] = g_in.movers[0];
                movers[1] = g_in.movers[1];
            }
            handsOn = run && g_in.handsOn && propslib::HasHands();
            if (handsOn) {
                hands[0] = g_in.hands[0];
                hands[1] = g_in.hands[1];
                std::memcpy(origin, g_in.origin, sizeof origin);
            }
        }
        const float strength = g_strength.load();
        if (!run || !propslib::Loaded()) {
            if (world) {
                propslib::Destroy(world);
                world = nullptr;
                std::lock_guard<std::mutex> l(g_outMutex);
                g_out.prev = g_out.cur = Snap{};
                for (Out::HandShot& h : g_out.hands) { if (h.ok && h.out.state >= D2RP_HAND_TAKING) h.events |= D2RP_HEV_LOST; h.ok = false; h.out = D2rpHandOut{}; }
                g_out.handsOn = false;
            }
            next = Now() + 0.1;
            continue;
        }
        if (world && strength != strengthMade) { propslib::Destroy(world); world = nullptr; clear = true; }
        if (!world) {
            params.strength = strength;
            world = propslib::Create(&params);
            strengthMade = strength;
            if (!world) { next = Now() + 1.0; continue; }
            clear = true;
            newProps = newStatics = true;
        }
        if (clear) {
            propslib::Clear(world, floorY);
            std::lock_guard<std::mutex> l(g_outMutex);
            g_out.world = worldNo;
            g_out.prev = g_out.cur = Snap{};
        }
        if (newStatics || clear) propslib::SetStatics(world, statics.data(), (int32_t)statics.size());
        if (newProps || clear) propslib::SetProps(world, props.data(), (int32_t)props.size());
        propslib::SetMovers(world, movers, 2);
        if (propslib::HasHands()) propslib::SetHands(world, hands, handsOn ? D2RP_HANDS : 0);
        int steps = 0;
        unsigned late = 0;
        while (next <= Now() && steps < kMaxCatchUp) {
            const double t0 = Now();
            const int moving = propslib::Step(world, next, (float)kStep);
            const double us = (Now() - t0) * 1e6;
            next += kStep;
            ++steps;
            const int np = propslib::Poses(world, poses, D2RP_MAX_PROPS);
            const int nk = propslib::Knocks(world, knocks, 32);
            D2rpHandOut handOut[D2RP_HANDS] = {};
            int handOk[D2RP_HANDS] = {};
            if (propslib::HasHands())
                for (int h = 0; h < D2RP_HANDS; ++h) handOk[h] = propslib::HandAt(world, h, &handOut[h]);
            std::lock_guard<std::mutex> l(g_outMutex);
            for (int h = 0; h < D2RP_HANDS; ++h) {   // [weight] grab_props: the newest, the one before it, the events and the strongest pulse gathered
                Out::HandShot& hs = g_out.hands[h];
                const bool had = hs.ok && hs.out.state >= D2RP_HAND_TAKING;
                std::memcpy(hs.prevPos, had ? hs.out.pos : handOut[h].pos, sizeof hs.prevPos);
                std::memcpy(hs.prevP, had && hs.out.id == handOut[h].id ? hs.out.p : handOut[h].p, sizeof hs.prevP);
                hs.events |= handOut[h].events;
                if (handOut[h].hapticAmp > hs.amp) { hs.amp = handOut[h].hapticAmp; hs.sec = handOut[h].hapticSec; hs.hz = handOut[h].hapticHz; }
                hs.out = handOut[h];
                hs.ok = handOk[h] > 0 || handOut[h].events != 0;
            }
            std::memcpy(g_out.origin, origin, sizeof g_out.origin);
            g_out.handsOn = handsOn;
            g_out.prev = std::move(g_out.cur);
            g_out.cur.t = next;
            g_out.cur.poses.assign(poses, poses + std::max(0, np));
            for (int i = 0; i < nk; ++i) g_out.knocks.push_back(knocks[i]);
            if (g_out.knocks.size() > 64) g_out.knocks.erase(g_out.knocks.begin(), g_out.knocks.end() - 64);
            ++g_out.steps;
            g_out.moving = (unsigned)std::max(0, moving);
            g_out.stepUs += us;
            g_out.worstUs = std::max(g_out.worstUs, us);
        }
        if (next <= Now()) { ++late; next = Now() + kStep; }   // behind by more than kMaxCatchUp steps: start afresh from now, not a run of catch-up steps
        if (late) { std::lock_guard<std::mutex> l(g_outMutex); g_out.late += late; }
    }
}

// ---- the write pass (the renderer's thread, inside the world-matrix function) ----
propscene::Quat Blend(propscene::Quat a, propscene::Quat b, float t) {
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) b = {-b.x, -b.y, -b.z, -b.w};
    propscene::Quat q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 1e-6f ? propscene::Quat{q.x / n, q.y / n, q.z / n, q.w / n} : a;
}

void Lose(size_t i) {
    g_remotePut.erase(g_tracked[i].entity);   // (the game took it back: another player's put of it with it)
    g_tracked.erase(g_tracked.begin() + ptrdiff_t(i));
    ++g_lost;
}

// [multiplayer] pose_share_held: another player's props, as his pose tells them (SetRemoteProps), found among ours - the same catalogue name, stood within kRemoteHomeM of
// where his stood - and put where he has them. Under g_trackMutex.
void MatchRemote() {
    std::vector<RemoteProp> want;
    {
        std::lock_guard<std::mutex> rl(g_remoteMutex);
        want = g_remoteWant;
    }
    const float tol = kRemoteHomeM * g_upm;
    unsigned matched = 0;
    for (const RemoteProp& r : want) {
        const Tracked* best = nullptr;
        float bestD = tol * tol;
        for (const Tracked& t : g_tracked) {
            if (t.kind < 0 || poseshare::NameHash(g_catalog.KindAt((unsigned)t.kind).name) != r.nameHash) continue;
            const auto h = g_home.find(t.entity);
            if (h == g_home.end()) continue;
            const float dx = h->second.x - r.home[0], dy = h->second.y - r.home[1], dz = h->second.z - r.home[2], d = dx * dx + dy * dy + dz * dz;
            if (d <= bestD) { bestD = d; best = &t; }
        }
        if (!best) continue;
        ++matched;
        const propscene::Vec p{r.p[0], r.p[1], r.p[2]};
        const propscene::Quat q{r.q[0], r.q[1], r.q[2], r.q[3]};
        RemotePut& put = g_remotePut[best->entity];
        const float pp[3] = {put.p.x, put.p.y, put.p.z}, pq[4] = {put.q.x, put.q.y, put.q.z, put.q.w};
        if (!put.dirty && Same(pp, r.p, 3) && Same(pq, r.q, 4)) continue;
        put.p = p;
        put.q = q;
        put.dirty = true;
    }
    g_remoteWanted.store((unsigned)want.size());
    g_remoteMatched.store(matched);
}

void Pass() {
    std::unique_lock<std::mutex> lock(g_trackMutex, std::try_to_lock);
    if (!lock || g_tracked.empty()) return;
    Snap prev, cur;
    {
        std::lock_guard<std::mutex> l(g_outMutex);
        if (g_out.world != g_world) return;   // the thread has not taken this world yet
        prev = g_out.prev;
        cur = g_out.cur;
    }
    MatchRemote();
    bool due = !cur.poses.empty();
    for (const Tracked& t : g_tracked) due |= t.settle;
    for (const auto& kv : g_remotePut) due |= kv.second.dirty || kv.second.settle;
    if (!due) { g_remoteDirty.store(false); return; }
    Storage transforms, bounds;
    if (!transforms.Load(g_transformsObject.load()) || transforms.elem != kTransformSize) return;
    const bool haveBounds = bounds.Load(g_boundsObject.load()) && bounds.elem == kBoundsSize;
    const uint64_t nowMs = GetTickCount64();
    // one step behind the newest: between the last two steps, by the time since the newest
    const float alpha = (float)std::clamp((Now() - cur.t) / kStep, 0.0, 1.0);
    ++g_passes;
    const float u = g_upm;
    for (size_t i = g_tracked.size(); i-- > 0;) {
        Tracked& t = g_tracked[i];
        const auto rp = g_remotePut.find(t.entity);
        RemotePut* remote = rp == g_remotePut.end() ? nullptr : &rp->second;   // another player's: his pose puts it, not our physics
        const D2rpPose* now = nullptr;
        const D2rpPose* was = nullptr;
        if (!remote) {
            for (const D2rpPose& p : cur.poses) if (p.id == t.entity) { now = &p; break; }
            if (now) for (const D2rpPose& p : prev.poses) if (p.id == t.entity) { was = &p; break; }
            if (!now && !t.settle) continue;
        } else if (!remote->dirty && !remote->settle) {
            continue;
        }
        float m[kTransformSize / 4];
        uint32_t index = t.transformIndex;
        if (!transforms.At(index, t.entity) && !transforms.Dense(t.entity, index)) { Lose(i); continue; }
        t.transformIndex = index;
        const uintptr_t component = transforms.payload + uintptr_t(index) * kTransformSize;
        uint32_t parent = 0;
        if (!ReadBytes(component, m, sizeof m)) { Lose(i); continue; }
        std::memcpy(&parent, m + 48, 4);
        if (parent != t.parent || !Same(m + 12, t.local, 3) || !Same(m + 16, t.world, 16)) { Lose(i); continue; }   // the game took it back (an area reload)
        float out[32];
        const bool moving = remote ? remote->dirty : now != nullptr;
        if (remote && remote->dirty) {
            propscene::Placement at;
            at.p = remote->p;
            at.q = remote->q;
            at.scale = t.scale;
            propscene::Compose(at, out);
        } else if (now) {
            propscene::Vec p{now->p[0], now->p[1], now->p[2]};
            propscene::Quat q{now->q[0], now->q[1], now->q[2], now->q[3]};
            if (was && now->state == 1) {
                const propscene::Vec p0{was->p[0], was->p[1], was->p[2]};
                const propscene::Quat q0{was->q[0], was->q[1], was->q[2], was->q[3]};
                p = p0 + (p - p0) * alpha;
                q = Blend(q0, q, alpha);
            }
            propscene::Placement at;
            at.p = {(p.x + g_origin[0]) * u, (p.y + g_origin[1]) * u, (p.z + g_origin[2]) * u};
            at.q = q;
            at.scale = t.scale;
            propscene::Compose(at, out);
        } else {
            std::memcpy(out, t.world, 64);   // a settle: the last pose into the last frame's slot too (no motion vector left behind)
        }
        if (!FiniteN(out, 16)) continue;
        std::memcpy(out + 16, m + 16, 64);   // the renderer's last frame: what was drawn until now
        if (!WriteBytes(component + 0x40, out, sizeof out)) { Lose(i); continue; }
        if (moving) ++g_writes; else ++g_settles;
        if (moving && haveBounds && t.hasBounds) {
            uint32_t b = t.boundsIndex;
            float box[kBoundsSize / 4];
            const Box expected = propscene::WorldBounds(t.bounds, m + 16);
            const float scale = std::max({std::fabs(expected.lo.x), std::fabs(expected.hi.x), std::fabs(expected.lo.z), std::fabs(expected.hi.z), 1.0f});
            if ((bounds.At(b, t.entity) || bounds.Dense(t.entity, b)) && ReadBytes(bounds.payload + uintptr_t(b) * kBoundsSize, box, sizeof box) && FiniteN(box, 24) &&
                propscene::Near(expected, {{box[16], box[17], box[18]}, {box[20], box[21], box[22]}}, 0.02f + scale * 2e-5f)) {
                const Box next = propscene::WorldBounds(t.bounds, out);
                const float w[8] = {next.lo.x, next.lo.y, next.lo.z, box[19], next.hi.x, next.hi.y, next.hi.z, box[23]};
                t.boundsIndex = b;
                if (!WriteBytes(bounds.payload + uintptr_t(b) * kBoundsSize + 0x40, w, sizeof w)) t.hasBounds = false;
            } else {
                t.hasBounds = false;
            }
        }
        std::memcpy(t.world, out, 64);
        t.writtenAt = nowMs;
        t.moved = true;
        if (remote) {
            remote->settle = remote->dirty;   // one more pass after the last move: the last frame's slot set to the final pose
            remote->dirty = false;
            t.settle = false;
        } else {
            t.settle = now != nullptr;   // one more pass after the last move: the last frame's slot set to the final pose
        }
    }
    bool settling = false, remoteDue = false;
    for (const Tracked& t : g_tracked) settling |= t.settle;
    for (const auto& kv : g_remotePut) remoteDue |= kv.second.dirty || kv.second.settle;
    g_settling.store(settling);
    g_remoteDirty.store(remoteDue);
}

D2rpProp ToProp(const Seen& s, const float origin[3], float u, propscene::Vec* scaleOut) {
    D2rpProp p{};
    propscene::Placement at;
    propscene::Decompose(s.world, at);
    p.id = s.entity;
    const propscene::CatalogKind& k = g_catalog.KindAt((unsigned)std::max(0, s.kind));
    p.shape = (uint8_t)k.shape;
    p.material = (uint8_t)k.material;
    p.p[0] = at.p.x / u - origin[0];
    p.p[1] = at.p.y / u - origin[1];
    p.p[2] = at.p.z / u - origin[2];
    p.q[0] = at.q.x; p.q[1] = at.q.y; p.q[2] = at.q.z; p.q[3] = at.q.w;
    const Vec c = (s.localBounds.lo + s.localBounds.hi) * 0.5f, h = (s.localBounds.hi - s.localBounds.lo) * 0.5f;
    p.centre[0] = c.x * at.scale.x / u; p.centre[1] = c.y * at.scale.y / u; p.centre[2] = c.z * at.scale.z / u;
    p.half[0] = std::fabs(h.x * at.scale.x / u); p.half[1] = std::fabs(h.y * at.scale.y / u); p.half[2] = std::fabs(h.z * at.scale.z / u);
    if (scaleOut) *scaleOut = at.scale;
    return p;
}

}  // namespace

void Init(LogFn log, PulseFn pulse, const wchar_t* pluginDir) {
    g_log = log;
    g_pulse = pulse;
    g_pluginDir = pluginDir ? pluginDir : L"";
    propslib::SetLog(log);
    std::wstring dir = g_pluginDir;
    if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
    const std::wstring file = dir + L"d2r_vr_props\\props.bin";
    std::vector<uint8_t> data;
    HANDLE f = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (64ll << 20)) {
            data.resize((size_t)size.QuadPart);
            DWORD got = 0;
            if (!ReadFile(f, data.data(), (DWORD)data.size(), &got, nullptr) || got != data.size()) data.clear();
        }
        CloseHandle(f);
    }
    if (g_catalog.Parse(data.data(), data.size())) {
        g_catalogReady.store(true);
        LogF("knock props: the catalogue - %zu knockable level props of %u kinds (d2r_vr_props\\props.bin, made by D2R VR Settings from your game)", g_catalog.Size(), g_catalog.KindCount());
    } else {
        LogF("knock props: no catalogue (%s): the level's props stand where they are until D2R VR Settings makes it from your game (Weapon Weight > Knock props)",
             data.empty() ? "d2r_vr_props\\props.bin is missing" : "d2r_vr_props\\props.bin does not read");
    }
}

void Configure(bool on, float strengthPct, float haptic) {
    const bool was = g_wanted.exchange(on);
    g_strength.store(std::clamp(strengthPct, 5.0f, 1000.0f) / 100.0f);
    g_haptic.store(std::clamp(haptic, 0.0f, 1.0f));
    if (was != on) Log(on ? "knock props: on ([weight] knock_props) - the level's small props are knocked about by the weapon and the fists (drawn only, F4)"
                          : "knock props: off ([weight] knock_props=0) - the props stand where the game placed them");
}

bool Wanted() { return g_wanted.load(); }

void ConfigureGrab(bool on) {
    const bool was = g_grabWanted.exchange(on);
    if (was != on) Log(on ? "grab props: on ([weight] grab_props) - an empty hand's grip takes the level's small props (with knock_props; drawn only, F4)"
                          : "grab props: off ([weight] grab_props=0)");
}

bool GrabReady() { return g_grabWanted.load() && g_wanted.load() && g_state.load() == 2 && propslib::HasHands(); }

// The hands' velocities from their last poses (the hook's pace), a little smoothed, as the blades'
struct HandTrack {
    bool on = false;
    double time = 0;
    float pos[3] = {}, rot[4] = {0, 0, 0, 1}, vel[3] = {}, angVel[3] = {};
};
HandTrack g_handTrack[2];   // (the skeleton hook's thread only)

void NoteHand(int side, const GrabHand& gh) {
    if (side < 0 || side > 1) return;
    HandTrack& t = g_handTrack[side];
    const double now = Now();
    if (!gh.tracked || !GrabReady()) {
        t.on = false;
        std::lock_guard<std::mutex> l(g_inMutex);
        g_in.hands[side] = D2rpHand{};
        g_in.handsOn = GrabReady() && (g_in.hands[0].tracked || g_in.hands[1].tracked);
        return;
    }
    const double dt = now - t.time;
    if (t.on && dt > 1e-4 && dt < 0.1) {
        const float k = 0.5f;
        float dq[4];   // the turn since the last pose: rot * conj(was), as a rotation vector
        const float* a = gh.rot;
        const float b[4] = {-t.rot[0], -t.rot[1], -t.rot[2], t.rot[3]};
        dq[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        dq[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        dq[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        dq[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
        if (dq[3] < 0) for (float& c : dq) c = -c;
        const float sn = std::sqrt(dq[0] * dq[0] + dq[1] * dq[1] + dq[2] * dq[2]);
        const float ang = sn > 1e-7f ? 2.0f * std::atan2(sn, dq[3]) / sn : 2.0f;
        for (int i = 0; i < 3; ++i) {
            t.vel[i] += ((gh.pos[i] - t.pos[i]) / (float)dt - t.vel[i]) * k;
            t.angVel[i] += (dq[i] * ang / (float)dt - t.angVel[i]) * k;
        }
    } else {
        std::memset(t.vel, 0, sizeof t.vel);
        std::memset(t.angVel, 0, sizeof t.angVel);
    }
    t.on = true;
    t.time = now;
    std::memcpy(t.pos, gh.pos, sizeof t.pos);
    std::memcpy(t.rot, gh.rot, sizeof t.rot);
    std::lock_guard<std::mutex> l(g_inMutex);
    D2rpHand& h = g_in.hands[side];
    h = D2rpHand{};
    h.tracked = 1;
    h.mayTake = gh.mayTake ? 1 : 0;
    h.grip = gh.grip;
    h.grabRadius = gh.grabRadius;
    h.breakDistance = gh.breakDistance;
    h.strength = gh.strength;
    for (int i = 0; i < 3; ++i) { h.pos[i] = gh.pos[i] - g_in.origin[i]; h.vel[i] = t.vel[i]; h.angVel[i] = t.angVel[i]; }
    std::memcpy(h.rot, gh.rot, sizeof h.rot);
    h.time = now;
    g_in.handsOn = true;
}

void NoHands() {
    g_handTrack[0].on = g_handTrack[1].on = false;
    std::lock_guard<std::mutex> l(g_inMutex);
    g_in.hands[0] = g_in.hands[1] = D2rpHand{};
    g_in.handsOn = false;
}

bool TakeHand(int side, D2rpHandOut* out) {
    if (side < 0 || side > 1 || !out) return false;
    std::lock_guard<std::mutex> l(g_outMutex);
    Out::HandShot& hs = g_out.hands[side];
    if (!hs.ok && !hs.events) return false;
    *out = hs.out;
    out->events = hs.events;
    out->hapticAmp = hs.amp;
    out->hapticSec = hs.sec;
    out->hapticHz = hs.hz;
    hs.events = 0;
    hs.amp = hs.sec = hs.hz = 0;
    // drawn as the write pass draws the props: between the last two steps, by the time since the newest
    const float alpha = (float)std::clamp((Now() - g_out.cur.t) / kStep, 0.0, 1.0);
    for (int i = 0; i < 3; ++i) {
        if (out->state >= D2RP_HAND_TAKING) {
            out->pos[i] = hs.prevPos[i] + (out->pos[i] - hs.prevPos[i]) * alpha;
            const float dp = out->p[i] - hs.prevP[i];
            out->p[i] = hs.prevP[i] + dp * alpha;
            out->point[i] -= dp * (1.0f - alpha);
        }
        out->pos[i] += g_out.origin[i];
        out->p[i] += g_out.origin[i];
        out->point[i] += g_out.origin[i];
    }
    return true;
}

const char* PropName(uint32_t id) {
    std::lock_guard<std::mutex> tl(g_trackMutex);
    for (const Tracked& t : g_tracked)
        if (t.entity == id && t.kind >= 0) return g_catalog.KindAt((unsigned)t.kind).name;
    return "prop";
}

void SetTrace(bool on) { g_trace.store(on); }
void SetGlow(bool on, float h) {
    const bool was = g_glow.exchange(on);
    g_glowH.store(std::isfinite(h) ? std::clamp(h, -0.9f, 4.0f) : 0.6f);
    if (was != on) Log(on ? "knock props: glow on ([debug] knock_props_glow) - the props found are lit with the game's highlight" : "knock props: glow off");
}

const char* StatusText() { return g_status; }

bool PropNow(uint32_t id, PropShot* out) {
    if (!out || !g_wanted.load()) return false;
    std::lock_guard<std::mutex> tl(g_trackMutex);
    for (const Tracked& t : g_tracked) {
        if (t.entity != id || t.kind < 0) continue;
        const auto h = g_home.find(t.entity);
        if (h == g_home.end()) return false;
        std::memcpy(out->name, g_catalog.KindAt((unsigned)t.kind).name, sizeof out->name);
        out->name[sizeof out->name - 1] = 0;
        out->home[0] = h->second.x; out->home[1] = h->second.y; out->home[2] = h->second.z;
        std::memcpy(out->world, t.world, sizeof out->world);
        out->moving = t.moved && GetTickCount64() - t.writtenAt < 150;
        return true;
    }
    return false;
}

void SetRemoteProps(const RemoteProp* r, int n) {
    {
        std::lock_guard<std::mutex> rl(g_remoteMutex);
        if (r && n > 0) g_remoteWant.assign(r, r + n);
        else g_remoteWant.clear();
    }
    if (n > 0 && g_haveOrigin) { g_remoteDirty.store(true); g_active.store(true); }
}

unsigned RemotePlaced(unsigned* wanted) {
    if (wanted) *wanted = g_remoteWanted.load();
    return g_remoteMatched.load();
}

void NoteBlade(int side, bool on, int kind, const float a[3], const float b[3], float radius) {
    if (side < 0 || side > 1 || !g_wanted.load()) return;
    const double time = Now();
    std::lock_guard<std::mutex> l(g_bladeMutex);
    Blade& bl = g_blades[side];
    if (!on || !a || !b) { bl.on = false; return; }
    const double dt = time - bl.time;
    if (bl.on && dt > 1e-4 && dt < 0.1) {
        // the velocities from the last pose: of its end a, and the turn of its axis
        const Vec da{a[0] - bl.a[0], a[1] - bl.a[1], a[2] - bl.a[2]};
        const Vec d0 = propscene::Norm({bl.b[0] - bl.a[0], bl.b[1] - bl.a[1], bl.b[2] - bl.a[2]}), d1 = propscene::Norm({b[0] - a[0], b[1] - a[1], b[2] - a[2]});
        const Vec ax = propscene::Cross(d0, d1);
        const float s = propscene::Len(ax), c = propscene::Dot(d0, d1);
        const Vec w = s > 1e-6f ? ax * (std::atan2(s, c) / s / (float)dt) : Vec{};
        const float k = 0.5f;   // a little smoothing: the poses come at the frame's pace, not evenly
        bl.vel[0] += (da.x / (float)dt - bl.vel[0]) * k; bl.vel[1] += (da.y / (float)dt - bl.vel[1]) * k; bl.vel[2] += (da.z / (float)dt - bl.vel[2]) * k;
        bl.angVel[0] += (w.x - bl.angVel[0]) * k; bl.angVel[1] += (w.y - bl.angVel[1]) * k; bl.angVel[2] += (w.z - bl.angVel[2]) * k;
    } else {
        std::memset(bl.vel, 0, sizeof bl.vel);
        std::memset(bl.angVel, 0, sizeof bl.angVel);
    }
    bl.on = true;
    bl.kind = kind;
    std::memcpy(bl.a, a, sizeof bl.a);
    std::memcpy(bl.b, b, sizeof bl.b);
    bl.radius = radius;
    bl.time = time;
}

void Transformed() {
    if (!g_active.load(std::memory_order_relaxed)) return;
    const double now = Now();
    if (now - g_passAt.load(std::memory_order_relaxed) < 0.005 || g_passing.exchange(true, std::memory_order_acquire)) return;   // once a frame (both eyes of a pair)
    if (now - g_passAt.load(std::memory_order_relaxed) >= 0.005) {
        g_passAt.store(now);
        Pass();
    }
    g_passing.store(false, std::memory_order_release);
}

void Tick(const Frame& fr) {
    const uint64_t nowMs = GetTickCount64();
    const bool on = g_wanted.load() && fr.ok && viewgate::F4();
    if (!on) {
        if (g_glowing) { beltglow::SetProps(nullptr, 0, 0.0f); g_glowing = false; }
        if (g_state.exchange(g_wanted.load() ? 1 : 0) == 2) {
            std::lock_guard<std::mutex> l(g_inMutex);
            g_in.run = false;
            g_in.handsOn = false;
        }
        snprintf(g_status, sizeof g_status, "%s", g_wanted.load() ? "waiting for VR first person (F4) in a game area" : "off");
        return;
    }
    if (!g_catalogReady.load()) { snprintf(g_status, sizeof g_status, "no catalogue: D2R VR Settings makes d2r_vr_props\\props.bin from your game"); g_state.store(1); return; }
    if (!propslib::Ensure()) { snprintf(g_status, sizeof g_status, "no weight library 1.3 (d2r_vr_weight.dll with the props world)"); g_state.store(1); return; }
    if (!g_threadUp.exchange(true)) {
        g_scanner.Start();
        try {
            std::thread(PhysicsThread).detach();
        } catch (...) {
            g_threadUp.store(false);
            return;
        }
    }
    g_state.store(2);
    const float u = std::clamp(fr.upm, 1.0f, 12.0f);
    const float heroM[3] = {fr.hero[0] / u, fr.hero[1] / u, fr.hero[2] / u};
    // a new world: another game or hero (at once), the hero far from the origin (an area, a waypoint) or another scale - once nothing moves there
    unsigned moving = 0;
    {
        std::lock_guard<std::mutex> l(g_outMutex);
        moving = g_out.moving;
    }
    const bool farAway = !g_haveOrigin || std::hypot(heroM[0] - g_origin[0], heroM[2] - g_origin[2]) > kRecentre || std::fabs(u - g_upm) > 0.01f * g_upm;
    if (!g_haveOrigin || fr.session != g_session || (farAway && moving == 0)) {
        std::lock_guard<std::mutex> tl(g_trackMutex);
        g_origin[0] = std::round(heroM[0]);
        g_origin[1] = heroM[1];
        g_origin[2] = std::round(heroM[2]);
        g_upm = u;
        if (fr.session != g_session) { g_home.clear(); g_remotePut.clear(); }   // (another game: the props' homes and the other players' puts go with it)
        g_session = fr.session;
        g_haveOrigin = true;
        g_tracked.clear();
        g_active.store(false);
        g_settling.store(false);
        std::lock_guard<std::mutex> l(g_inMutex);
        std::memcpy(g_in.origin, g_origin, sizeof g_in.origin);
        g_in.world = ++g_world;
        g_in.floorY = 0.0f;
        g_in.props.clear();
        g_in.statics.clear();
        ++g_in.propsSerial;
    }
    // the movers: hit by touch's blades in the physics frame
    {
        float steel = fr.steelKg > 0.0f ? fr.steelKg : kDefaultSteelKg;
        std::lock_guard<std::mutex> bl(g_bladeMutex);
        std::lock_guard<std::mutex> l(g_inMutex);
        g_in.run = true;
        for (int s = 0; s < 2; ++s) {
            D2rpMover& m = g_in.movers[s];
            const Blade& b = g_blades[s];
            m = D2rpMover{};
            if (!b.on || Now() - b.time > 0.15) continue;
            m.on = 1;
            m.kind = b.kind;
            m.hands = b.kind == 0 && s == 0 ? std::clamp(fr.hands, 1, 2) : 1;
            for (int i = 0; i < 3; ++i) { m.a[i] = b.a[i] - g_origin[i]; m.b[i] = b.b[i] - g_origin[i]; m.vel[i] = b.vel[i]; m.angVel[i] = b.angVel[i]; }
            m.radius = b.radius;
            m.mass = b.kind == 1 ? kFistKg : steel + kFistKg * (float)m.hands;
            m.time = b.time;
        }
    }
    // the scan: every half second; its results to the world
    if (nowMs - g_scanAt >= kScanMs) {
        Request r;
        std::memcpy(r.hero, fr.hero, sizeof r.hero);
        r.upm = u;
        r.idPool = g_idPool;
        r.boundsPool = g_boundsPool;
        r.discover = (g_idPool < 0 || g_boundsPool < 0) && (!g_discoverAt || nowMs - g_discoverAt >= kRediscoverMs);
        if (g_scanner.Submit(r)) {
            g_scanAt = nowMs;
            if (r.discover) g_discoverAt = nowMs;
        }
    }
    Found f;
    if (g_scanner.Take(f)) {
        if (f.idPool >= 0) g_idPool = f.idPool;
        if (f.boundsPool >= 0) g_boundsPool = f.boundsPool;
        g_lastWhy = f.why;
        g_lastScanMs = f.ms;
        g_lastSeen = (unsigned)f.props.size();
        g_lastMatched = f.matched;
        g_lastById = f.byId;
        g_lastElsewhere = f.elsewhere;
        g_lastNearby = f.nearby;
        if (f.ok) {
            std::vector<D2rpProp> props;
            std::vector<D2rpBox> statics;
            std::vector<Tracked> next;
            std::lock_guard<std::mutex> tl(g_trackMutex);
            for (const Seen& s : f.props) {
                propscene::Vec scale;
                D2rpProp p = ToProp(s, g_origin, u, &scale);
                if (!std::isfinite(p.p[0] + p.p[1] + p.p[2])) continue;
                props.push_back(p);
                // a prop the world moved lately keeps what the pass knows of it (the scan may have read it a frame old)
                auto it = std::find_if(g_tracked.begin(), g_tracked.end(), [&](const Tracked& o) { return o.entity == s.entity; });
                if (it != g_tracked.end() && it->moved && nowMs - it->writtenAt < kKeepMs) { next.push_back(*it); continue; }
                Tracked t;
                t.entity = s.entity; t.parent = s.parent; t.transformIndex = s.transformIndex; t.boundsIndex = s.boundsIndex;
                std::memcpy(t.local, s.local, 12);
                std::memcpy(t.world, s.world, 64);
                t.bounds = s.localBounds;
                t.scale = scale;
                t.hasBounds = true;
                t.kind = s.kind;
                g_home.emplace(s.entity, propscene::Vec{s.world[12], s.world[13], s.world[14]});   // (where it stood when first found: its home this session)
                next.push_back(t);
                // what it stands on, a 2 cm plate (the floor and another prop need nothing)
                const propscene::Support& sp = s.support;
                if (!sp.floor && !sp.onProp && sp.x1 > sp.x0 && sp.z1 > sp.z0) {
                    D2rpBox bx{};
                    bx.lo[0] = sp.x0 / u - g_origin[0]; bx.lo[1] = sp.y / u - g_origin[1] - 0.02f; bx.lo[2] = sp.z0 / u - g_origin[2];
                    bx.hi[0] = sp.x1 / u - g_origin[0]; bx.hi[1] = sp.y / u - g_origin[1]; bx.hi[2] = sp.z1 / u - g_origin[2];
                    bx.friction = 0.6f;
                    bx.bounce = 0.1f;
                    if (bx.hi[1] > 0.005f && std::none_of(statics.begin(), statics.end(), [&](const D2rpBox& o) { return std::memcmp(&o, &bx, sizeof bx) == 0; }) &&
                        statics.size() < D2RP_MAX_STATICS)
                        statics.push_back(bx);
                }
            }
            // moved props the scan no longer lists (lost from its reach while moving) stay until written back
            for (const Tracked& t : g_tracked)
                if (t.moved && nowMs - t.writtenAt < kKeepMs && std::none_of(next.begin(), next.end(), [&](const Tracked& o) { return o.entity == t.entity; })) next.push_back(t);
            if (g_trace.load()) {   // [debug] knock_props_trace: what the scan found and what it stands on
                unsigned onFloor = 0, onProp = 0, onFurniture = 0, onLedge = 0;
                for (const Seen& s : f.props) {
                    if (s.support.floor) ++onFloor;
                    else if (s.support.onProp) ++onProp;
                    else if (s.support.guessed) ++onLedge;
                    else ++onFurniture;
                }
                LogF("knock props: scan - %u props in reach (%u of the catalogue's near, %u of them prefabs' by their id alone, %u ids known only at other places; "
                     "%u entities read; %.1f ms): on the floor %u, on another prop %u, on furniture %u, on a guessed ledge %u; %zu surfaces; left out: no bounds %u, "
                     "not rigid %u, size %u, beyond the nearest %u",
                     (unsigned)f.props.size(), f.matched, f.byId, f.elsewhere, f.nearby, f.ms, onFloor, onProp, onFurniture, onLedge, statics.size(), f.unbounded, f.unrigid,
                     f.sized, f.beyond);
            }
            g_tracked = std::move(next);
            if (f.transforms) g_transformsObject.store(f.transforms);
            if (f.bounds) g_boundsObject.store(f.bounds);
            std::lock_guard<std::mutex> l(g_inMutex);
            g_in.props = std::move(props);
            g_in.statics = std::move(statics);
            ++g_in.propsSerial;
        }
    }
    // the knocks: a pulse in the hand that struck, the log
    std::vector<D2rpKnock> knocks;
    unsigned steps = 0, late = 0;
    double stepUs = 0, worstUs = 0;
    {
        std::lock_guard<std::mutex> l(g_outMutex);
        knocks.swap(g_out.knocks);
        if (nowMs - g_reportAt >= 10000) {
            steps = g_out.steps; late = g_out.late; stepUs = g_out.stepUs; worstUs = g_out.worstUs; moving = g_out.moving;
            g_out.steps = g_out.late = 0;
            g_out.stepUs = g_out.worstUs = 0;
        }
    }
    static uint64_t pulsedAt[2] = {0, 0};
    for (const D2rpKnock& k : knocks) {
        ++g_knocks;
        if (g_trace.load()) {
            int kind = -1;
            {
                std::lock_guard<std::mutex> tl(g_trackMutex);
                for (const Tracked& t : g_tracked) if (t.entity == k.id) { kind = t.kind; break; }
            }
            LogF("knock props: knocked a %s (entity %08X) by %s at %.1f m/s - %.3f N s into it", kind >= 0 ? g_catalog.KindAt((unsigned)kind).name : "prop", k.id,
                 k.mover == 0 ? "the right hand" : k.mover == 1 ? "the left hand" : "another moving thing", k.speed, k.impulse);
        }
        if (k.mover >= 0 && k.mover < 2 && g_pulse && g_haptic.load() > 0.0f && nowMs - pulsedAt[k.mover] >= 80) {
            pulsedAt[k.mover] = nowMs;
            g_pulse(k.mover, g_haptic.load() * std::clamp(0.4f + k.impulse * 2.0f, 0.4f, 1.0f), 30);
        }
    }
    {
        std::lock_guard<std::mutex> l(g_outMutex);
        moving = g_out.moving;
    }
    g_active.store(moving > 0 || g_settling.load() || g_remoteDirty.load());
    if (g_glow.load()) {   // [debug] knock_props_glow: the props in the world, lit
        uint32_t ents[beltglow::kMaxProps];
        int n = 0;
        {
            std::lock_guard<std::mutex> tl(g_trackMutex);
            for (const Tracked& t : g_tracked) if (n < beltglow::kMaxProps) ents[n++] = t.entity;
        }
        beltglow::SetProps(ents, n, g_glowH.load());
        g_glowing = true;
    } else if (g_glowing) {
        beltglow::SetProps(nullptr, 0, 0.0f);
        g_glowing = false;
    }
    snprintf(g_status, sizeof g_status, "running: %u props in reach, %u moving", g_lastSeen, moving);
    if (nowMs - g_reportAt >= 10000) {
        g_reportAt = nowMs;
        LogF("knock props: ten seconds - %u props in reach (%u catalogued near of %u entities, %u prefabs' by id, %u ids at other places; scan %.1f ms%s%s), physics %u steps at %.0f Hz (%.0f us each, worst %.0f us, %u times behind), "
             "%u moving now; knocks %u; writes %u, settles %u, lost %u, passes %u",
             g_lastSeen, g_lastMatched, g_lastNearby, g_lastById, g_lastElsewhere, g_lastScanMs, *g_lastWhy ? ", " : "", g_lastWhy, steps, 1.0 / kStep, steps ? stepUs / steps : 0.0, worstUs, late, moving,
             g_knocks.exchange(0), g_writes.exchange(0), g_settles.exchange(0), g_lost.exchange(0), g_passes.exchange(0));
    }
}

bool PresetPieces(const float (*places)[3], int n, std::vector<PresetPiece>* out) {
    out->clear();
    const std::vector<uintptr_t> pools = Pools();
    Storage transforms;
    if (pools.empty() || !Transforms(pools, transforms)) return false;
    std::vector<float> block(kChunk * kTransformSize / 4);
    for (size_t at = 0; at < transforms.count; at += kChunk) {
        const size_t count = std::min(kChunk, transforms.count - at);
        if (!ReadBytes(transforms.payload + at * kTransformSize, block.data(), count * kTransformSize)) continue;
        for (size_t i = 0; i < count; ++i) {
            const float* m = block.data() + i * (kTransformSize / 4);
            uint32_t parent = 0;
            std::memcpy(&parent, m + 48, 4);
            if ((parent & kNoParent) == kNoParent || !FiniteN(m + 12, 3) || !FiniteN(m + 28, 3)) continue;   // (not part of a level preset)
            for (int k = 0; k < n; ++k) {
                if (std::fabs(m[12] - places[k][0]) > 1e-3f || std::fabs(m[13] - places[k][1]) > 1e-3f || std::fabs(m[14] - places[k][2]) > 1e-3f) continue;
                PresetPiece p{parent, {m[12], m[13], m[14]}, {m[28], m[29], m[30]}};
                out->push_back(p);
                break;
            }
        }
    }
    return true;
}

}  // namespace knockprops
