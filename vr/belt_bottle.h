// belt_bottle.h - what becomes of a belt potion in the hand in VR first person (F4), past the drink: [hands] belt_empty_bottle (the emptied bottle stays in the hand and can be
// thrown away) and [hands] belt_drop_ground (a full one let go of away from the belt falls, and where it lands the game drops it). Pure: cleanroom/tests/belt_bottle_test.cpp.
//
// The engine side (vr/vrcam.cpp, engitem::BeltFrame) draws the game's own potion models; vr/bottle_fall.h makes them fall. This file decides:
//   - the size of each kind of bottle (the game's own models, model units: the glass's radius and the top's height - read off the installed 3.3.93787 .model files'
//     meshes, glassShape / solidShape) and so its body in metres and its weight;
//   - a full bottle let go of: back on the belt (within belt_drop_back_cm of its place, or the drop off) or onto the ground;
//   - the game's word on it, by the item's own id in the belt's bottom row (gamestate::BeltIds): a drink or a drop is done when that id has left the column (the next potion
//     of the kind falling into its place has another id), refused when it is still there after a moment;
//   - how long an emptied bottle lies before it goes, and which of the lying ones goes first when there are too many.
// Matrices: the game's row-major 4x4 (rows = the model's x, y, z axes scaled, then the place), as engitem lays them.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "bottle_fall.h"

namespace beltbottle {

// ---- the bottles' sizes ----
struct Size { float radius, height; };   // model units (the model stands on its base at y = 0)
// By the item code's three letters; a code not known: a flask of the common size.
inline Size SizeOf(const char* code) {
    struct Row { char c[4]; Size s; };
    static const Row kRows[] = {
        {"hp1", {0.205f, 1.0f}}, {"hp2", {0.30f, 1.0f}}, {"hp3", {0.265f, 1.0f}}, {"hp4", {0.254f, 1.0f}}, {"hp5", {0.258f, 1.0f}},
        {"mp1", {0.205f, 1.0f}}, {"mp2", {0.30f, 1.0f}}, {"mp3", {0.265f, 1.0f}}, {"mp4", {0.254f, 1.0f}}, {"mp5", {0.262f, 1.0f}},
        {"rvs", {0.268f, 1.0f}}, {"rvl", {0.434f, 1.0f}}, {"yps", {0.20f, 0.844f}}, {"wms", {0.14f, 0.934f}}, {"vps", {0.14f, 0.934f}},
    };
    if (code)
        for (const Row& r : kRows)
            if (code[0] == r.c[0] && code[1] == r.c[1] && code[2] == r.c[2]) return r.s;
    return {0.25f, 1.0f};
}

// Its body in metres: the model's size times the belt's scale (model units -> world units) over the world units a metre; the mass of glass, and of the potion in it
// while full (water's density, the bottle three quarters full; the glass a fifth of a full bottle's weight).
inline bottlefall::Shape ShapeOf(const Size& s, float scale, float unitsPerMetre, bool empty) {
    bottlefall::Shape b;
    if (!(scale > 0.0f) || !(unitsPerMetre > 0.0f)) return b;
    b.radius = std::clamp((double)s.radius * scale / unitsPerMetre, 0.005, 0.5);
    b.height = std::clamp((double)s.height * scale / unitsPerMetre, 0.02, 1.0);
    const double litres = 3.14159265358979 * b.radius * b.radius * b.height * 1000.0;
    const double full = litres * 0.75 * 1.0;   // kg of potion
    b.mass = std::clamp(empty ? full * 0.2 : full * 1.2, 0.02, 5.0);
    return b;
}

// ---- matrices <-> the falling body ----
// A unit turn (x y z w) whose +x, +y, +z are the given orthonormal axes.
inline void FromAxes(const double x[3], const double y[3], const double z[3], double q[4]) {
    const double tr = x[0] + y[1] + z[2];
    if (tr > 0) {
        const double s = std::sqrt(tr + 1.0) * 2;
        q[3] = 0.25 * s; q[0] = (y[2] - z[1]) / s; q[1] = (z[0] - x[2]) / s; q[2] = (x[1] - y[0]) / s;
    } else if (x[0] > y[1] && x[0] > z[2]) {
        const double s = std::sqrt(1.0 + x[0] - y[1] - z[2]) * 2;
        q[3] = (y[2] - z[1]) / s; q[0] = 0.25 * s; q[1] = (y[0] + x[1]) / s; q[2] = (z[0] + x[2]) / s;
    } else if (y[1] > z[2]) {
        const double s = std::sqrt(1.0 + y[1] - x[0] - z[2]) * 2;
        q[3] = (z[0] - x[2]) / s; q[0] = (y[0] + x[1]) / s; q[1] = 0.25 * s; q[2] = (z[1] + y[2]) / s;
    } else {
        const double s = std::sqrt(1.0 + z[2] - x[0] - y[1]) * 2;
        q[3] = (x[1] - y[0]) / s; q[0] = (z[0] + x[2]) / s; q[1] = (z[1] + y[2]) / s; q[2] = 0.25 * s;
    }
    const double l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; ++i) q[i] = l > 1e-12 ? q[i] / l : (i == 3 ? 1.0 : 0.0);
}

// A model matrix (world units) -> the base's place in metres from `origin` (world units) and its turn. False for a matrix with no size.
inline bool FromMatrix(const float m[16], const double origin[3], float unitsPerMetre, double base[3], double q[4]) {
    double ax[3][3];
    for (int r = 0; r < 3; ++r) {
        const double l = std::sqrt((double)m[r * 4] * m[r * 4] + (double)m[r * 4 + 1] * m[r * 4 + 1] + (double)m[r * 4 + 2] * m[r * 4 + 2]);
        if (!(l > 1e-9) || !(unitsPerMetre > 0.0f)) return false;
        for (int i = 0; i < 3; ++i) ax[r][i] = m[r * 4 + i] / l;
    }
    FromAxes(ax[0], ax[1], ax[2], q);
    for (int i = 0; i < 3; ++i) base[i] = ((double)m[12 + i] - origin[i]) / unitsPerMetre;
    return true;
}

// The falling body -> the model matrix to lay: its axes times `scale` (model units -> world units), its base at origin + base x unitsPerMetre.
inline void ToMatrix(const bottlefall::Sim& s, const double origin[3], float unitsPerMetre, float scale, float m[16]) {
    const double e[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int r = 0; r < 3; ++r) {
        double a[3];
        bottlefall::detail::Rot(s.body.q, e[r], a);
        for (int i = 0; i < 3; ++i) m[r * 4 + i] = (float)(a[i] * scale);
        m[r * 4 + 3] = 0.0f;
    }
    double b[3];
    bottlefall::Base(s, b);
    for (int i = 0; i < 3; ++i) m[12 + i] = (float)(origin[i] + b[i] * unitsPerMetre);
    m[15] = 1.0f;
}

// ---- a full bottle let go of ----
enum class LetGo { Back, Falls };
// toSlotM: the hand's distance to the bottle's place on the belt, metres; backM: within it, it goes back ([hands] belt_drop_back_cm).
inline LetGo OnLetGo(bool dropOn, float toSlotM, float backM) {
    return dropOn && std::isfinite(toSlotM) && toSlotM > backM ? LetGo::Falls : LetGo::Back;
}
// ... with the throw to the mercenary too: a potion he is given flies away from the belt even with the drop off (a miss comes back).
inline LetGo OnLetGo(bool dropOn, bool mercOn, bool forMerc, float toSlotM, float backM) {
    return OnLetGo(dropOn || (mercOn && forMerc), toSlotM, backM);
}

// ---- a full bottle thrown to the mercenary ([hands] belt_throw_merc) ----
// The potions a hireling is given (the game's Shift + belt key: he drinks it): healing, rejuvenation, antidote, thawing - the ones that do him good. (The four
// after bearstonem/d2r-pcvr's "heal your mercenary" toss, MIT; ideas only.)
inline bool ForMerc(const char* code) { return code && (code[0] == 'h' || code[0] == 'r' || code[0] == 'y' || code[0] == 'w'); }

// The mercenary as the throw sees him: a capsule (bottom, top, radius) in the bottle's frame (metres, y up), and his feet.
struct Merc { bool seen = false; double bottom[3] = {}, top[3] = {}; double radius = 0; };
// The bottle's centre in his body (the capsule widened by the bottle's own radius): it reached him in the air.
inline bool HitsMerc(const Merc& m, const double p[3], double bottleRadius) {
    if (!m.seen) return false;
    double ab[3], ap[3];
    for (int i = 0; i < 3; ++i) { ab[i] = m.top[i] - m.bottom[i]; ap[i] = p[i] - m.bottom[i]; }
    const double l2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    double t = l2 > 1e-12 ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / l2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    double d2 = 0;
    for (int i = 0; i < 3; ++i) { const double d = p[i] - (m.bottom[i] + ab[i] * t); d2 += d * d; }
    const double r = m.radius + bottleRadius;
    return d2 <= r * r;
}
// Where it lands, across the ground (x z), within reachM of his feet (his capsule's bottom, across).
inline bool LandsByMerc(const Merc& m, const double p[3], double reachM) {
    if (!m.seen || !(reachM > 0)) return false;
    const double dx = p[0] - m.bottom[0], dz = p[2] - m.bottom[2];
    return dx * dx + dz * dz <= reachM * reachM;
}

// What becomes of a full bottle in the air: given to the merc (it reached him, or landed by his feet), dropped on the ground by the game (belt_drop_ground),
// or back on the belt (a miss with the drop off, or a potion he is not given).
enum class Target { Flying, Merc, Ground, Back };
inline Target OnFlight(bool mercOn, bool forMerc, bool dropOn, bool hitsMerc, bool touchedFloor, bool landsByMerc) {
    if (mercOn && forMerc && (hitsMerc || (touchedFloor && landsByMerc))) return Target::Merc;
    if (!touchedFloor) return Target::Flying;
    return dropOn ? Target::Ground : Target::Back;
}

// ---- the game's word, by the item's id in its column ----
constexpr double kAnswerS = 1.5;   // s: the game has not taken the item by then - it refused (a drink, a drop)

// A full bottle on its way to the ground: Falling until it first touches the floor (the game's drop is asked for then), Asked until the item leaves the belt (Done: the
// game dropped it - ours goes, the game's lies there) or kAnswerS pass (Refused: it is still in the belt - ours goes, the belt shows it again). An item that left the belt
// while ours was still falling (drunk with a key, moved by the player) is Gone: ours goes, nothing is asked.
enum class Drop { Falling, Asked, Done, Refused, Gone };
inline Drop DropNext(Drop d, bool touchedFirst, bool stillInBelt, double sinceAskS) {
    switch (d) {
        case Drop::Falling: return !stillInBelt ? Drop::Gone : touchedFirst ? Drop::Asked : Drop::Falling;
        case Drop::Asked: return !stillInBelt ? Drop::Done : sinceAskS > kAnswerS ? Drop::Refused : Drop::Asked;
        default: return d;
    }
}
inline bool DropEnded(Drop d) { return d == Drop::Done || d == Drop::Refused || d == Drop::Gone; }

// The drink at the mouth with the empty bottle on: the hand holds the empty one at once; the game took the item (its id left the column) - it stays; still there after
// kAnswerS - the drink was refused: the empty one goes and the full one is back on the belt.
enum class Drink { Waiting, Drunk, Refused };
inline Drink DrinkNext(Drink d, bool stillInBelt, double sinceS) {
    if (d != Drink::Waiting) return d;
    return !stillInBelt ? Drink::Drunk : sinceS > kAnswerS ? Drink::Refused : Drink::Waiting;
}

// ---- the emptied bottle's look: the model's meshes that go ----
// The game's potion models (the installed 3.3.93787 .model files, read with a Granny reader): most are two meshes - the glass ("glassShape", "polySurface1Shape",
// "polySurfaceShape3/7") and everything that is not glass in one ("solidShape"; "outerShape" for the greater mana potion, "metal_ropeShape" for mana_potion2): the cork,
// the neck's rope or metal, the label. The light healing potion has the liquid as a mesh of its own ("liquidShape") with 38 bubbles (10 triangles each). In the
// others the potion is the glass's own colour, the same mesh as the glass: it cannot be taken out by hiding a mesh. healing_potion2 (hp3) is one mesh: nothing goes.
// So the emptied bottle keeps its glass; its cork (with the rope / metal / label round the neck), the liquid mesh and the bubbles go.
inline bool EmptyHides(const char* name, uint32_t triangles) {
    if (!name) return false;
    char low[64];
    size_t i = 0;
    for (; name[i] && i < sizeof low - 1; ++i) low[i] = (char)((name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i]);
    low[i] = 0;
    if (std::strstr(low, "glass")) return false;
    if (std::strstr(low, "solid") || std::strstr(low, "liquid") || std::strstr(low, "outer") || std::strstr(low, "metal") || std::strstr(low, "rope") ||
        std::strstr(low, "cork"))
        return true;
    return triangles > 0 && triangles <= 12;   // a bubble in the potion
}

// Where the game keeps it (D2R 3.3.93787, D2Render model_instance.cpp, read off the image - side notes mesh-hide-recon.md): a ModelInstance draws the render items at
// +0x40, which ModelInstance::RebuildRenderItems (0xF3FDB0) makes from its list of visible mesh indices at +0x2A0 (data, count at +0x2A8; size_t each), indices into
// its own meshes at +0x310 (data, count at +0x318: MeshInstance*, whose +0 is the shared Mesh: its name a char* at +0x160, its triangles a uint32 at +0x158). The list
// is the instance's own - the potions on the ground are other instances. The game fills it again (0xF3FFF0) on a layer, LOD or quality change: the hide is checked
// every frame and done again then.
constexpr size_t kInstResource = 0x20, kResourceState = 0xC8, kInstVisible = 0x2A0, kInstMeshes = 0x310, kMeshName = 0x160, kMeshTriangles = 0x158;
constexpr uint8_t kLoaded = 4;
constexpr int kMaxMeshes = 64;

struct Parts {
    bool ready = false;                 // the model is loaded and its meshes read
    int meshes = 0;                     // +0x318
    uint64_t hidden = 0;                // bit i: mesh i goes
    int visibleHidden = 0;              // of the visible list, entries that are hidden meshes (0: the hide holds)
    char names[160] = {};               // "name(triangles) ..." for the log
    const char* why = "";
};

// `read(address, out, bytes)` true when the bytes read (the caller's guarded copy).
template <class Read>
bool ReadParts(Read&& read, uintptr_t inst, Parts* out) {
    *out = Parts{};
    uintptr_t res = 0, data = 0, vis = 0;
    uint8_t state = 0;
    uint64_t count = 0, nVis = 0;
    if (inst < 0x10000 || !read(inst + kInstResource, &res, sizeof res) || res < 0x10000 || !read(res + kResourceState, &state, 1)) { out->why = "the model instance does not read"; return false; }
    if (state != kLoaded) { out->why = "the model is not loaded yet"; return false; }
    if (!read(inst + kInstMeshes, &data, sizeof data) || !read(inst + kInstMeshes + 8, &count, sizeof count) || data < 0x10000 || count < 1 || count > (uint64_t)kMaxMeshes) {
        out->why = "its meshes do not read";
        return false;
    }
    if (!read(inst + kInstVisible, &vis, sizeof vis) || !read(inst + kInstVisible + 8, &nVis, sizeof nVis) || vis < 0x10000 || nVis > (uint64_t)kMaxMeshes) {
        out->why = "its visible list does not read";
        return false;
    }
    out->meshes = (int)count;
    size_t w = 0;
    for (uint64_t i = 0; i < count; ++i) {
        uintptr_t mi = 0, mesh = 0, np = 0;
        uint32_t tris = 0;
        char name[48] = {};
        if (!read(data + i * 8, &mi, sizeof mi) || mi < 0x10000 || !read(mi, &mesh, sizeof mesh) || mesh < 0x10000) { out->why = "a mesh does not read"; return false; }
        read(mesh + kMeshTriangles, &tris, sizeof tris);
        if (read(mesh + kMeshName, &np, sizeof np) && np >= 0x10000)
            for (size_t k = 0; k < sizeof name - 1; ++k)
                if (!read(np + k, &name[k], 1) || !name[k]) { name[k] = 0; break; }
        if (EmptyHides(name, tris)) out->hidden |= 1ull << i;
        if (w < sizeof out->names - 1 && !(tris > 0 && tris <= 12)) {   // the bubbles not one by one
            const int n = std::snprintf(out->names + w, sizeof out->names - w, "%s%s(%u)%s", w ? " " : "", name[0] ? name : "?", tris, (out->hidden >> i & 1) ? "-" : "");
            if (n > 0) w = std::min(sizeof out->names - 1, w + (size_t)n);
        }
    }
    for (uint64_t k = 0; k < nVis; ++k) {
        uint64_t idx = 0;
        if (!read(vis + k * 8, &idx, sizeof idx)) { out->why = "its visible list does not read"; return false; }
        if (idx < count && (out->hidden >> idx & 1)) ++out->visibleHidden;
    }
    out->ready = true;
    return true;
}

// The hidden meshes taken out of the visible list in place (it only shrinks: nothing allocated). `write(address, in, bytes)`. 1: the list changed - the game's
// rebuild (0xF3FDB0) must follow; 0: nothing to take out; -1: it does not read or write.
template <class Read, class Write>
int HideParts(Read&& read, Write&& write, uintptr_t inst, const Parts& p) {
    if (!p.ready || !p.visibleHidden) return 0;
    uintptr_t vis = 0;
    uint64_t n = 0;
    if (!read(inst + kInstVisible, &vis, sizeof vis) || !read(inst + kInstVisible + 8, &n, sizeof n) || vis < 0x10000 || n > (uint64_t)kMaxMeshes) return -1;
    uint64_t keep[kMaxMeshes];
    uint64_t m = 0;
    for (uint64_t k = 0; k < n; ++k) {
        uint64_t idx = 0;
        if (!read(vis + k * 8, &idx, sizeof idx)) return -1;
        if (idx < (uint64_t)p.meshes && (p.hidden >> idx & 1)) continue;
        keep[m++] = idx;
    }
    if (m == n || m == 0) return 0;   // (never the whole bottle gone)
    if (!write(vis, keep, (size_t)m * 8)) return -1;
    return write(inst + kInstVisible + 8, &m, sizeof m) ? 1 : -1;
}

// ---- the empties lying about ----
constexpr int kMaxLying = 6;   // at most this many; one more: the one that has lain longest goes
// An emptied bottle that came to rest `restedS` ago goes after keepS ([hands] belt_empty_keep_s); one that never comes to rest goes at keepS + 10 s after its release.
inline bool EmptyGoes(bool resting, double restedS, double sinceReleaseS, double keepS) {
    return resting ? restedS >= keepS : sinceReleaseS >= keepS + 10.0;
}
// Which of n lying empties goes to make room (the oldest release), or -1 when there is room.
inline int MakeRoom(const double* releasedAt, int n) {
    if (n < kMaxLying) return -1;
    int oldest = 0;
    for (int i = 1; i < n; ++i)
        if (releasedAt[i] < releasedAt[oldest]) oldest = i;
    return oldest;
}

}  // namespace beltbottle
