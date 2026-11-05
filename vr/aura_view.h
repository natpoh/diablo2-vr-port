// aura_view.h - [render] aura_centre: the Paladin's aura rings (Thorns, Might, Holy Fire, Holy Freeze, Conviction, Fanaticism ...) drawn under the hero in VR,
// not beside him. Serg's report of 2026-11-04 (screenshot: the Thorns ring of yellow spikes on the ground a step away from the hero, by a fallen monster).
//
// WHY THEY SIT OFF THE FEET (read from the game's HD files, 2026-11-04, tools/casc_extract): the aura overlays' particle effects
// (data/hd/vfx/particles/overlays/paladin/<aura>/*.particles; the overlay data/hd/overlays/paladin/aura_thorns_front.json names FX_thorns_overlay.particles) read
// PopcornFX's view.direction - the camera's direction - in their scripts: 21 of the 24 effects there do (all the auras but Defiance and Prayer, and the Resist Cold cast
// flash). Each particle is pushed TOWARD THE CAMERA when it is born: its local point = -view.direction * k (Holy Freeze: k = 3 units), and keeps that place. On the flat
// screen the push runs along the camera's own line of sight, so it moves nothing on the screen: it only makes the ring draw over the hero's feet. In VR the camera is
// another one (the head in F4, the chase camera in F2), and the push becomes a SIDEWAYS move of the whole ring, a few dozen cm off the feet, and it shifts with the view.
//
// THE FIX (the same as bearstonem/d2r-pcvr a4191f8 / 56da117, MIT, 2026-10-21, tested in a headset there): the game's own view.direction function for scripts
// (0xC04BB0: (out stream, view-index stream, scene context)) is run as ever, then, for the lanes whose effect is one of those files, its answer is replaced by the
// vertical part of the GAME camera's own direction (its view matrix, as the game built it this frame), scaled by the share of the game's upward push kept
// ([render] aura_lift_pct, 0 by default): (0, forward.y * share, 0). No sideways part - centred on the feet in every view - and, at 0, no lift either: the ring at its
// root, the unit's feet, where the flat game shows it (Upright says why). d2r-pcvr kept 0.475 of the lift by hand after its headset tests; here the default comes from
// what the game shows, and aura_lift_pct (and [render] move_own_auras' sliders, vr/own_effects.h AuraShift) are the player's taste on top.
//
// What is read of the game (D2R 3.3.93787 under D2RLoader 1.3.1, the layouts d2r-pcvr read and checked against the same build; every layout is guarded and anything that
// does not read as expected leaves the lane as the game made it):
//   out stream   {u64 data, u32 count, u32 stride}: count float3 (stride 12)
//   context      {u64 worker, scene, particles, indices; u32 count, unused; i32 instance; u32 pad} (48 bytes): one instance id for the batch (instance >= 0) or one per lane
//                (u32 indices[lane])
//   instance id  the scene's segmented pool at scene + 0x230 {u64 pages, reserved; u32 count, capacity}: page = pages[id / 512], instance = page[id % 512]; the instance's
//                vtable is the game's (+0x1DA1118), its definition at +0x40 (header[8]), its transform at +0x48
//   definition   vtable +0x1DA0FF0; +0x48 the compiled asset (vtable +0x1DA67F0) whose +0x10 is a string object (vtable +0x1D9D258) with the path's characters at +0x20
// This header is the pure part (the test: cleanroom/tests/aura_view_test.cpp); the hook is vr/aura_view.cpp.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace auraview {

struct Vec { float x = 0.0f, y = 0.0f, z = 0.0f; };
inline bool Finite(const Vec& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

constexpr size_t kPathCap = 160;

// data/hd/vfx/particles/overlays/paladin/<...>.particles - any case, either slash; nothing that climbs out ("..").
inline bool Asset(const char* path) {
    static const char kPre[] = "data/hd/vfx/particles/overlays/paladin/";
    static const char kSuf[] = ".particles";
    if (!path) return false;
    const size_t n = std::strlen(path), pre = sizeof kPre - 1, suf = sizeof kSuf - 1;
    if (n <= pre + suf || n >= kPathCap) return false;
    auto low = [](char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c == '\\' ? '/' : c; };
    for (size_t i = 0; i < pre; ++i) if (low(path[i]) != kPre[i]) return false;
    for (size_t i = 0; i < suf; ++i) if (low(path[n - suf + i]) != kSuf[i]) return false;
    return std::strstr(path, "..") == nullptr;
}

// The direction the aura effects get: the vertical part of the game camera's own direction (world, unit length, looking down), times `lift` - the share of the game's
// upward push kept ([render] aura_lift_pct / 100). False (nothing replaced) when it is not one: not finite, not of unit length, not looking down.
// THE DEFAULT IS 0, FROM WHAT THE GAME SHOWS (Serg 2026-11-04: "to move it into its place"): on the flat screen the whole push runs along the line of sight, its upward part
// too, so the game shows the ring exactly at the effect's root - the unit's feet. In VR any push left is seen: d2r-pcvr's headset test found the full lift (1.5 units, ~29 cm
// for k = 3) floating the ring above the floor. So the ring's own place is no push at all. A thousandth of the direction is kept even at 0 (kMinLift) so the effect's other
// uses of it never see a zero vector (d2r-pcvr ran these effects at 0.475 of it - not of unit length - in its headset tests); the push left is k / 1000, under a millimetre.
constexpr float kMinLift = 0.001f;
inline float ClampLiftPct(float pct) { return !std::isfinite(pct) ? 0.0f : pct < 0.0f ? 0.0f : pct > 100.0f ? 100.0f : pct; }
inline bool Upright(const Vec& gameForward, float lift, Vec* out) {
    if (!Finite(gameForward) || !std::isfinite(lift)) return false;
    const float l2 = gameForward.x * gameForward.x + gameForward.y * gameForward.y + gameForward.z * gameForward.z;
    if (std::fabs(l2 - 1.0f) > 0.01f || gameForward.y >= 0.0f) return false;
    const float share = lift < kMinLift ? kMinLift : lift > 1.0f ? 1.0f : lift;
    *out = Vec{0.0f, gameForward.y * share, 0.0f};
    return true;
}

// The game camera's direction from its view matrix (row-major, v * M: column 2 is its back axis in the world) - as vrcam reads it (vr/view/camera.cpp).
inline Vec ForwardOfView(const float g[16]) { return Vec{-g[2], -g[6], -g[10]}; }

// How old the game camera's direction may be (ms) before nothing is replaced: it is set every VR frame of the world (vrcam's view function); a menu, the flat game, the
// view from above as the game makes it (F1 without 3D) set none, and the effects keep the game's own push there.
constexpr uint64_t kFreshMs = 250;
inline bool Fresh(uint64_t now, uint64_t at) { return at != 0 && now >= at && now - at <= kFreshMs; }

// -- the game's objects, through rd(address, out, size) -> bool (guarded in the game, a fake memory in the test)
struct Stream { uint64_t data = 0; uint32_t count = 0, stride = 0; };
struct Context { uint64_t worker = 0, scene = 0, particles = 0, indices = 0; uint32_t count = 0, unused = 0; int32_t instance = -1; uint32_t padding = 0; };
static_assert(sizeof(Context) == 48, "the scene context is 48 bytes");
constexpr uint32_t kMaxLanes = 1024;
constexpr uint64_t kInstanceVtbl = 0x1DA1118, kDefinitionVtbl = 0x1DA0FF0, kCompiledVtbl = 0x1DA67F0, kStringVtbl = 0x1D9D258;
constexpr size_t kScenePool = 0x230, kStringChars = 0x20;
constexpr int32_t kMaxInstanceId = 131072;

// An output stream the hook may write: float3 lanes, a sane count.
inline bool StreamOk(const Stream& s) { return s.data >= 0x10000 && s.count > 0 && s.count <= kMaxLanes && s.stride == 12; }

// The instance id of one lane: the batch's own, or the lane's from the index list. -1 none.
template <class Rd>
inline int32_t LaneInstance(Rd&& rd, const Context& c, uint32_t lane) {
    int32_t id = c.instance;
    if (id < 0) {
        if (lane >= c.count || c.indices < 0x10000 || !rd(c.indices + 4ull * lane, &id, sizeof id)) return -1;
    }
    return id >= 0 && id < kMaxInstanceId ? id : -1;
}

// The effect definition of an instance id (0 = not read, or not the game's objects).
template <class Rd>
inline uint64_t DefinitionOf(Rd&& rd, const Context& c, int32_t id, uint64_t base) {
    if (id < 0 || id >= kMaxInstanceId || c.scene < 0x10000) return 0;
    struct Pool { uint64_t pages, reserved; uint32_t count, capacity; } pool{};
    if (!rd(c.scene + kScenePool, &pool, sizeof pool) || pool.pages < 0x10000 || pool.count == 0 || pool.count > 256 || (uint32_t)id / 512 >= pool.count) return 0;
    uint64_t page = 0, inst = 0;
    if (!rd(pool.pages + 8ull * ((uint32_t)id / 512), &page, sizeof page) || page < 0x10000 || !rd(page + 8ull * ((uint32_t)id % 512), &inst, sizeof inst) || inst < 0x10000)
        return 0;
    uint64_t header[10] = {};
    if (!rd(inst, header, sizeof header) || header[0] != base + kInstanceVtbl || header[8] < 0x10000 || header[9] < 0x10000) return 0;
    return header[8];
}

// A definition's particle file path (kPathCap characters at most, zero-terminated). False when it does not read as the game's.
template <class Rd>
inline bool DefinitionPath(Rd&& rd, uint64_t def, uint64_t base, char (&path)[kPathCap]) {
    path[0] = 0;
    uint64_t header[10] = {}, compiled[3] = {}, type = 0;
    if (def < 0x10000 || !rd(def, header, sizeof header) || header[0] != base + kDefinitionVtbl || header[9] < 0x10000) return false;
    if (!rd(header[9], compiled, sizeof compiled) || compiled[0] != base + kCompiledVtbl || compiled[2] < 0x10000) return false;
    if (!rd(compiled[2], &type, sizeof type) || type != base + kStringVtbl || !rd(compiled[2] + kStringChars, path, sizeof path)) { path[0] = 0; return false; }
    if (!std::memchr(path, 0, sizeof path)) { path[0] = 0; return false; }
    return true;
}

// A small cache of what each definition is (an aura of the list or not), so a batch reads a path once a second at most. One per worker thread in the game.
class Kinds {
public:
    static constexpr size_t kSlots = 32;
    static constexpr uint64_t kKeepMs = 1000;
    // -1 not known (the caller reads the path and Puts it), else 1 an aura's, 0 not
    int Get(uint64_t def, uint64_t now) const {
        const E& e = e_[Slot(def)];
        return e.def == def && now >= e.at && now - e.at <= kKeepMs ? (e.aura ? 1 : 0) : -1;
    }
    void Put(uint64_t def, uint64_t now, bool aura) { e_[Slot(def)] = E{def, now, aura}; }
private:
    struct E { uint64_t def = 0, at = 0; bool aura = false; };
    static size_t Slot(uint64_t def) { return (size_t)((def >> 6) % kSlots); }
    E e_[kSlots]{};
};

// One batch after the game's function: every lane whose effect is one of the auras gets `value`. kinds: the caller's cache; isAura(def) -> bool reads the path when the
// cache does not know. Returns the lanes written; *firstPath (optional) is set to the definition of the first lane written.
template <class Rd, class Wr, class IsAura>
inline uint32_t CorrectBatch(Rd&& rd, Wr&& wr, const Stream& out, const Context& c, uint64_t base, const Vec& value, Kinds* kinds, uint64_t now, IsAura&& isAura,
                             uint64_t* firstDef = nullptr) {
    if (!StreamOk(out)) return 0;
    uint32_t written = 0;
    int32_t lastId = -2;
    bool lastAura = false;
    uint64_t lastDef = 0;
    for (uint32_t i = 0; i < out.count; ++i) {
        const int32_t id = LaneInstance(rd, c, i);
        if (id < 0) continue;
        if (id != lastId) {
            lastId = id;
            lastAura = false;
            lastDef = DefinitionOf(rd, c, id, base);
            if (lastDef) {
                int k = kinds->Get(lastDef, now);
                if (k < 0) { k = isAura(lastDef) ? 1 : 0; kinds->Put(lastDef, now, k == 1); }
                lastAura = k == 1;
            }
        }
        if (!lastAura) continue;
        if (!wr(out.data + 12ull * i, &value, sizeof value)) continue;
        if (!written && firstDef) *firstDef = lastDef;
        ++written;
    }
    return written;
}

}  // namespace auraview

// The game's side (vr/aura_view.cpp).
namespace D2RL {
struct PluginContext;
}
namespace auraview {

void Init(const D2RL::PluginContext* ctx);
// [render] aura_centre, aura_lift_pct (0..100: the share of the game's upward push kept; 0 = the ring at the feet): live.
void Configure(bool on, float liftPct);
// vrcam's view function, every VR frame of the world: the game camera's own view matrix (row-major, as the game built it).
void SetGameView(const float gameView[16]);
// Puts the hook in when it is wanted and the game's code is where 3.3.93787 has it (tried again now and then); cheap to call every frame.
void Install();

}  // namespace auraview
