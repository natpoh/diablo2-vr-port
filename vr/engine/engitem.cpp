// vr/engine/engitem.cpp - units the game draws as its own, with no unit of the game's behind them (engitem): the belt's potions and
// what the hands hold, the other players' copies, the sky dome, the cave dome, the cave ceiling's slab and beams, the chapel's dome,
// the weather round the viewer - cut whole from vr/vrcam.cpp (docs/plan_modules.md M7; their split into the engine's part, the sky's
// and the hands' follows). It reaches into many features: it sees vrcam.cpp's headers (vr/vrcam_headers.h).
#include "../vrcam_headers.h"
#include "engitem.h"
#include "held_units.h"   // the units we placed let go, as the game does, when a game is left
#include "sky_atlas.h"    // the domes' atlas: its layout's mark (DomeAtlasHasCap)
#include "world_holes_file.h"   // [holes] patch_world: the places and the measures file D2R_VR_Settings writes
#include "../wrist_models.h"    // [hud] wrist_game_models: the medallions' matrices
#include "wrist_orb_paths.h"    // ... and the units D2R_VR_Settings writes for them
#include "wrist_look.h"         // ... and what the ask for them found, for the headset panel (shared/)

#include <array>

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp: ::SafeRead and the rest are vrc's

namespace vrc {
// the area's ceiling as the ini has it (vrcam.cpp; declared here only: its defaults are its definition's)
void CeilingNow(float* height, float* bright, float* light, float* relief = nullptr, int* slot = nullptr, float* own = nullptr);
float CeilingHeadroomNeed(float heroOverFloor, float hang);   // [ceiling] min_headroom_m (vr/sky/sky_config.cpp)



// Items the game draws itself (2026-10-13). An item's 3D look is a unit of its own:
// data/hd/items/misc/<asset>.json, a UnitDefinition (data/hd/items/items.json names the asset
// by item code: hp1 = potion/lesser_healing_potion ... rvl = potion/full_rejuv_potion), with
// its model, ALB/NRM/ORM textures and the dropped items' skeleton. The game puts one in the
// renderer's ECS as UnitTranslationLayer::ProcessUnitCreation (0x778E00) does for every unit:
// EntityContainerDefinition::GetOrLoadDefinition<UnitDefinition> (0x669E70: the FileRequest's
// path from blz::string(const char*) 0xBF810, +0xB8 = 100, +0xC0 = 1, +0x108 = 1 as the game
// sets them; RequestMode 0 = there on return; UNIT_TYPE 4 = item), then
// UnitDefinition::PlaceInstance (0x9661C0) into the registry at 0x2677170 (what 0x9124E0
// returns, skeletons.cpp's RVA_ECS_REGISTRY), with no UNIT behind it (ProcessUnitCreation
// skips that part when there is none). The registry may be changed only on the thread that
// owns it (+0xA0, a thread id) while +0x110 is set ("Getting mutable registry, but that is not
// currently allowed!", 0x9125C0) - render jobs read it the rest of the time.
// Step 1, [debug] engine_unit=<the unit's .json path>: four of it round the hero, to see that the
// game draws them as its own - lit, fogged, hidden by walls. The belt's potions come next.
namespace engitem {
constexpr uint64_t RVA_STRING_CTOR = 0xBF810, RVA_LOAD_UNITDEF = 0x669E70, RVA_PLACE = 0x9661C0, RVA_REGISTRY = 0x2677170;
constexpr uint8_t kStringCtorBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x33};
constexpr uint8_t kLoadBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
constexpr uint8_t kPlaceBytes[] = {0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48};
struct Shared { void* p; void* ctrl; };   // blz::shared_ptr
using StringCtorFn = void* (__fastcall*)(void* str, const char* s);
using LoadFn = Shared* (__fastcall*)(Shared* out, void* request, int mode, int unitType);
// a member function: this, then the result's place, then the arguments
using PlaceFn = Shared* (__fastcall*)(void* def, Shared* out, void* registry, void* part, void* filter,
                                      const float* pos, const float* rot, const float* v3, bool b);

SRWLOCK g_lock = SRWLOCK_INIT;
std::string g_want;                       // [debug] engine_unit, under g_lock
std::atomic<uint32_t> g_wantGen{0};
uint32_t g_doneGen = 0;                   // the render thread's
std::vector<Shared> g_placed;             // the render thread's; held until the game is left (LetGoAll)

void SetWant(const std::wstring& path) {
    const std::string p = Utf8(path.c_str());
    AcquireSRWLockExclusive(&g_lock);
    const bool changed = p != g_want;
    g_want = p;
    ReleaseSRWLockExclusive(&g_lock);
    if (changed) g_wantGen.fetch_add(1);
}

bool ReadRegistry(uintptr_t reg, DWORD* owner, uint8_t* open) {
    __try { *owner = *(const DWORD*)(reg + 0xA0); *open = *(const uint8_t*)(reg + 0x110); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The game's calls themselves; no C++ objects in these (SEH).
bool LoadDef(const char* path, Shared* def) {
    const auto ctor = (StringCtorFn)d2rsig::Addr(RVA_STRING_CTOR);
    const auto load = (LoadFn)d2rsig::Addr(RVA_LOAD_UNITDEF);
    *def = {};
    __try {
        alignas(16) uint8_t req[0x200] = {};
        ctor(req, path);
        req[0xB8] = 100;
        *(uint64_t*)(req + 0xC0) = 1;
        *(uint64_t*)(req + 0x108) = 1;
        load(def, req, 0, 4);   // the request is the game's now (a by-value argument: it frees it)
        return def->p != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *def = {};
        return false;
    }
}
bool PlaceAt(void* def, const float pos[3], Shared* inst) {
    const auto place = (PlaceFn)d2rsig::Addr(RVA_PLACE);
    void* reg = (void*)d2rsig::Addr(RVA_REGISTRY);
    *inst = {};
    __try {
        alignas(16) uint8_t part[0x80] = {}, filter[0x100] = {};
        *(uint32_t*)(filter + 4) = 0x811C9DC5u;   // as the game hands it over (an empty hash)
        alignas(16) const float rot[4] = {0.0f, 0.0f, 0.0f, 1.0f}, none[4] = {};
        alignas(16) float at[4] = {pos[0], pos[1], pos[2], 0.0f};
        place(def, inst, reg, part, filter, at, rot, none, true);
        return inst->p != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *inst = {};
        return false;
    }
}
// A UnitInstance's entities: their ids at +0x10, how many at +0x18 (the character screen's
// heroes walk them so, 0x677A00). The root is the one without a parent.
uint32_t RootOf(void* inst) {
    uintptr_t list = 0;
    uint64_t n = 0;
    if (!::SafeRead(&list, (const uint8_t*)inst + 0x10, 8) || !::SafeRead(&n, (const uint8_t*)inst + 0x18, 8) || !list || !n || n > 64)
        return 0xFFFFFFFFu;
    for (uint64_t i = 0; i < n; ++i) {
        uint32_t e = 0, parent = 0;
        uintptr_t comp = 0;
        if (!::SafeRead(&e, (const void*)(list + i * 4), 4) || !skel::EntityTransform(e, &comp)) continue;
        if (::SafeRead(&parent, (const void*)(comp + 0xC0), 4) && (parent & 0xFFFFF) == 0xFFFFF) return e;
    }
    return 0xFFFFFFFFu;
}
// Its local matrix (+0x00): what the game sets for a unit before the frame's poses. The world
// (+0x40) does NOT follow by itself: Renderer::UpdateSystems works it out only for entities in
// a view of two more storages (0x68E9A0, without 0x6D9DD0), and a placed unit with nothing
// behind it is not in it - the belt's bottles stayed where they were placed and were gone as
// soon as the hero walked (2026-10-13). So the game's own update follows each write:
// 0xE3A8C0(out, entity, TransformComponent*) - what PlaceInstance itself calls after placing -
// works out the world from the local and the parent's world, keeps the old world at +0x80 (DLSS's
// motion vectors) and tells the entity's attachments.
constexpr uint64_t RVA_UPDATE_WORLD = 0xE3A8C0;
constexpr uint8_t kUpdateWorldBytes[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56};
using UpdateWorldFn = void*(__fastcall*)(void* out, uint32_t entity, void* comp);
bool UpdateWorld(uint32_t e) {
    uintptr_t comp = 0;
    if (e == 0xFFFFFFFFu || !skel::EntityTransform(e, &comp)) return false;
    const auto update = (UpdateWorldFn)d2rsig::Addr(RVA_UPDATE_WORLD);
    __try {
        alignas(16) float out[16];
        update(out, e, (void*)comp);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool WriteLocal(uint32_t e, const float m[16]) {
    uintptr_t comp = 0;
    if (e == 0xFFFFFFFFu || !skel::EntityTransform(e, &comp)) return false;
    __try { memcpy((void*)comp, m, 64); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
// The local laid, then every entity of the instance brought up to date - the root first, its
// children after it (their world hangs off the root's).
bool Lay(void* inst, uint32_t root, const float m[16]) {
    if (!WriteLocal(root, m)) return false;
    UpdateWorld(root);
    uintptr_t list = 0;
    uint64_t n = 0;
    if (!::SafeRead(&list, (const uint8_t*)inst + 0x10, 8) || !::SafeRead(&n, (const uint8_t*)inst + 0x18, 8) || !list || n > 64) return true;
    for (uint64_t i = 0; i < n; ++i) {
        uint32_t e = 0;
        if (::SafeRead(&e, (const void*)(list + i * 4), 4) && e != root) UpdateWorld(e);
    }
    return true;
}

// ---- Letting go, as the game does (vr/engine/held_units.h) ----
// One reference of ours let go (an instance's: its entities trimmed to the live ones first - TrimToLive). The holder is emptied either way:
// it is never let go twice.
heldunits::Life g_life;   // from the world's teardown until a menu: nothing placed (Work)
heldunits::Tally g_areaTally;   // the instances let go during a game as an area change took their entities (told when the game is left)
bool ReleaseRef(Shared* s) {
    void* const c = s->ctrl;
    *s = {};
    if (!c) return false;
    __try { return heldunits::ReleaseCtrl(c); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// An instance's entity list (+0x10, how many at +0x18) down to the entities that are still its registry's (+8): the game's removal
// (0x955CD0) frees each one's slot, and one an area change took may hold another entity by now. False: not readable (left as it is).
bool TrimToLive(void* inst, bool* stale) {
    *stale = false;
    __try {
        const uintptr_t reg = *(const uintptr_t*)((const uint8_t*)inst + 8);
        uint32_t* const list = *(uint32_t**)((uint8_t*)inst + 0x10);
        uint64_t* const n = (uint64_t*)((uint8_t*)inst + 0x18);
        if (!reg || (!list && *n) || *n > 64) return false;
        const uint32_t* const ents = *(const uint32_t* const*)(reg + 0x30);
        const uint32_t* const end = *(const uint32_t* const*)(reg + 0x38);
        const size_t count = ents && end > ents ? (size_t)(end - ents) : 0;
        const size_t k = heldunits::KeepLive(list, (size_t)*n, [ents, count](uint32_t e) { return heldunits::Live(ents, count, e); });
        *stale = k != *n;
        *n = k;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// An instance let go: only on the registry's owner thread (the game's removal asserts elsewhere) - false, and held as before, when not.
bool OnOwnerThread() {
    DWORD owner = 0;
    uint8_t open = 0;
    return ReadRegistry(d2rsig::Addr(RVA_REGISTRY), &owner, &open) && owner == GetCurrentThreadId();
}
void ReleaseInstance(Shared* inst, heldunits::Tally* t) {
    if (!inst->ctrl) { *inst = {}; return; }
    if (!OnOwnerThread()) { ++t->kept; return; }
    bool stale = false;
    if (inst->p && !TrimToLive(inst->p, &stale)) { ++t->kept; return; }   // (not readable: not handed to the game's removal)
    if (ReleaseRef(inst)) { ++t->instances; if (stale) ++t->stale; }
}
void ReleaseDefinition(Shared* def, heldunits::Tally* t) {
    if (ReleaseRef(def)) ++t->definitions;
}

// Step 1: [debug] engine_unit, four round the hero.
std::vector<Shared> g_placedDefs;         // their definitions (one asked per test)
void RunTest(const std::string& want) {
    if (g_placed.size() >= 32) { Log("vrcam: engine unit - 32 placed already this run: no more"); return; }
    const float* h = g_heroPosNow;
    const float pos[4][3] = {{h[0] + 3.0f, h[1], h[2]}, {h[0] - 3.0f, h[1], h[2]}, {h[0], h[1], h[2] + 3.0f}, {h[0], h[1], h[2] - 3.0f}};
    Shared def{};
    int n = 0;
    const char* why = "placed";
    if (!LoadDef(want.c_str(), &def)) why = "the definition did not load (wrong path?)";
    else
        for (const auto& p : pos) {
            Shared inst{};
            if (PlaceAt(def.p, p, &inst)) { g_placed.push_back(inst); ++n; }
        }
    if (def.p && !n) why = "PlaceInstance gave nothing";
    if (def.ctrl) g_placedDefs.push_back(def);   // (let go with the instances when the game is left)
    const uint32_t root = n ? RootOf(g_placed.back().p) : 0xFFFFFFFFu;
    LogF("vrcam: engine unit '%s' - %s: definition %p, %d instance(s) round the hero at %.1f %.1f %.1f (thread %lu), root entity %X",
         want.c_str(), why, def.p, n, h[0], h[1], h[2], GetCurrentThreadId(), root);
}

// ---- Step 2: the belt's potions, the game's own models ([hands] belt_models) ----
// Each potion in the belt's bottom row (gamestate::BeltCodes) is a unit of the game's, placed
// once and then laid each frame where its column sits on the hero: his model matrix (his
// TransformComponent's local, skel::HeroTransform) times an offset in his model frame - y up,
// z ahead (MatYawDeg's axis), x across - in parts of his standing eye height, so every class's
// belt sits on his own hips. The four go round him on an arc, as a belt does (a straight row
// across the belly looked wrong, the user's picture, 2026-10-13), each bottle turned to face
// out. One not wanted now is laid far under the ground, kept for later; one whose entity an area
// change took is let go, and all of them when the game is left (vr/engine/held_units.h).
struct PotionKind { char code[4]; const char* asset; };
constexpr PotionKind kPotions[] = {
    {"hp1", "potion/lesser_healing_potion"}, {"hp2", "potion/light_healing_potion"}, {"hp3", "potion/healing_potion2"},
    {"hp4", "potion/strong_healing_potion"}, {"hp5", "potion/greater_healing_potion"}, {"mp1", "potion/lesser_mana_potion"},
    {"mp2", "potion/light_mana_potion"}, {"mp3", "potion/mana_potion2"}, {"mp4", "potion/strong_mana_potion"},
    {"mp5", "potion/greater_mana_potion"}, {"rvs", "potion/rejuv_potion"}, {"rvl", "potion/full_rejuv_potion"},
    {"yps", "potion/antidote_potion"}, {"wms", "potion/thawing_potion"}, {"vps", "potion/stamina_potion"},
};
constexpr int kKinds = (int)(sizeof kPotions / sizeof kPotions[0]);
uint32_t Code3(const char* c) { return (uint32_t)(uint8_t)c[0] | (uint32_t)(uint8_t)c[1] << 8 | (uint32_t)(uint8_t)c[2] << 16; }
int KindOf(uint32_t code) {
    for (int k = 0; k < kKinds; ++k)
        if ((code & 0x00FFFFFFu) == Code3(kPotions[k].code)) return k;
    return -1;
}
// [multiplayer] pose_share_held: what each hand shows as drawn this frame - a belt bottle in it or let fall, an emptied one, an item taken off the ground: its code and
// its world matrix, in the hand / flying / at rest (poseshare::HeldState). In a hand wins over flying, flying over at rest, within one frame. The render thread's
// draws write it; the update thread's pose reads it (ShownIn).
SRWLOCK g_shownLock = SRWLOCK_INIT;
Shown g_shown[2];
uint64_t g_shownFrame = 0;   // the render thread's: a number a frame (Work)
void NoteShown(int hand, uint32_t code, const float m[16], uint8_t state) {
    if (hand < 0 || hand > 1 || !code || !g_poseHeld.load(std::memory_order_relaxed)) return;
    AcquireSRWLockExclusive(&g_shownLock);
    Shown& s = g_shown[hand];
    if (!(s.frame == g_shownFrame && s.state < state)) {
        s.code = code;
        s.state = state;
        memcpy(s.m, m, sizeof s.m);
        s.at = GetTickCount64();
        s.frame = g_shownFrame;
    }
    ReleaseSRWLockExclusive(&g_shownLock);
}
bool ShownIn(int hand, Shown* out) {
    if (hand < 0 || hand > 1 || !out) return false;
    AcquireSRWLockShared(&g_shownLock);
    *out = g_shown[hand];
    ReleaseSRWLockShared(&g_shownLock);
    return out->code && GetTickCount64() - out->at <= 150;
}
// ahead: the arc's radius round his middle; arc: degrees from one column to the next; side -1:
// columns 1 to 4 from his left to his right (his model's x runs to his left)
// hold: the bottle in the hand against the controller - x y z cm, pitch yaw roll degrees ([hands] belt_hold_*)
struct Belt { bool on = false; float up = 0.57f, ahead = 0.13f, arc = 35.0f, scale = 0.56f, side = -1.0f; bool grab = false; float hold[6] = {};
              float across = 0.0f, turn = 0.0f;   // shifted across him / swung round him, + to his right: parts of his eye height / degrees ([hands] belt_models_across, _turn)
              float grabM = 0.12f;                // how near a palm must be to a bottle to light it and take it, metres ([hands] belt_grab_cm)
              float holdClose = 0.55f, ballAlpha = 0.35f, ballRadius = 0.06f;
              float vial[6] = {}, vialClose = 0.55f;    // the thin vials' own (stamina, thawing: [hands] belt_vial_*)
              float anti[6] = {}, antiClose = 0.55f;    // the antidote's, the black one ([hands] belt_antidote_*)   // how far the hand closes round a bottle in it, a part ([hands] belt_hold_close %)
              bool glow = false;                        // [hands] belt_inner_glow: a bottle in reach glows from inside instead of the ball round it
              bool empty = false;                       // [hands] belt_empty_bottle: the drink leaves the emptied bottle in the hand, to be thrown away
              float emptyKeepS = 20.0f;                 // [hands] belt_empty_keep_s: how long an emptied bottle lies before it goes
              bool drop = false;                        // [hands] belt_drop_ground: a full bottle let go of away from the belt falls, and the game drops it where it lands
              float dropBackM = 0.25f;                  // [hands] belt_drop_back_cm: let go this near its place, it goes back on the belt
              bool merc = false;                        // [hands] belt_throw_merc: a healing / rejuvenation / antidote / thawing potion thrown at the mercenary is his
              float mercReachM = 1.5f;                  // [hands] belt_merc_reach_cm: landed this near his feet, it is his
              bool trace = false;                       // [debug] belt_bottle_trace: a log line for every step of the empties and the drops
              float glowH = 1.0f;                       // [hands] belt_glow: the lit bottle's highlight (its colour x (1 + h))
              float emptyDark = -0.35f, emptyAlpha = 0.45f; };   // [hands] belt_empty_dark / belt_empty_alpha: the emptied bottle's highlight and opacity
SRWLOCK g_beltLock = SRWLOCK_INIT;
Belt g_beltSet;                           // [hands] belt_models*, under g_beltLock
struct Bottle { int kind; Shared inst; uint32_t root; bool parked; bool dead; };
std::vector<Bottle> g_bottles;            // the render thread's
Shared g_defs[kKinds] = {};               // the render thread's; loaded once, kept
bool g_defFailed[kKinds] = {};

void SetBelt(bool on, float up, float ahead, float arc, float scale, float side, float across, float turn, float grabCm, bool grab, const float hold[6]) {
    AcquireSRWLockExclusive(&g_beltLock);
    g_beltSet = {on, up, ahead, arc, scale, side < 0.0f ? -1.0f : 1.0f, grab};
    g_beltSet.across = across;
    g_beltSet.turn = turn;
    g_beltSet.grabM = grabCm * 0.01f;
    memcpy(g_beltSet.hold, hold, sizeof g_beltSet.hold);
    g_beltSet.holdClose = std::clamp(IniF(L"hands", L"belt_hold_close", 55.0f), 0.0f, 150.0f) * 0.01f;
    {   // the thin vials (stamina, thawing, antidote) are held their own way; a key not set takes the flasks' value
        const wchar_t* const k[6] = {L"belt_vial_x", L"belt_vial_y", L"belt_vial_z", L"belt_vial_pitch", L"belt_vial_yaw", L"belt_vial_roll"};
        for (int i = 0; i < 6; ++i)
            g_beltSet.vial[i] = std::clamp(IniF(L"hands", k[i], g_beltSet.hold[i]), i < 3 ? -30.0f : -180.0f, i < 3 ? 30.0f : 180.0f);
        g_beltSet.vialClose = std::clamp(IniF(L"hands", L"belt_vial_close", g_beltSet.holdClose * 100.0f), 0.0f, 150.0f) * 0.01f;
        // the antidote (the black one) its own again; not set = the vials' value
        const wchar_t* const a[6] = {L"belt_antidote_x", L"belt_antidote_y", L"belt_antidote_z", L"belt_antidote_pitch",
                                     L"belt_antidote_yaw", L"belt_antidote_roll"};
        for (int i = 0; i < 6; ++i)
            g_beltSet.anti[i] = std::clamp(IniF(L"hands", a[i], g_beltSet.vial[i]), i < 3 ? -30.0f : -180.0f, i < 3 ? 30.0f : 180.0f);
        g_beltSet.antiClose = std::clamp(IniF(L"hands", L"belt_antidote_close", g_beltSet.vialClose * 100.0f), 0.0f, 150.0f) * 0.01f;
    }
    g_beltSet.ballAlpha = std::clamp(IniF(L"hands", L"belt_ball_alpha", 0.35f), 0.0f, 1.0f);   // the lit ball: how much of it shows
    g_beltSet.ballRadius = std::clamp(IniF(L"hands", L"belt_ball_radius", 6.0f), 1.0f, 30.0f) * 0.01f;   // and its size, cm
    // past the drink (vr/belt_bottle.h): each its own tick, off by default - off, exactly as before
    g_beltSet.glow = IniB(L"hands", L"belt_inner_glow", false);
    g_beltSet.empty = IniB(L"hands", L"belt_empty_bottle", false);
    g_beltSet.emptyKeepS = std::clamp(IniF(L"hands", L"belt_empty_keep_s", 20.0f), 1.0f, 600.0f);
    g_beltSet.drop = IniB(L"hands", L"belt_drop_ground", false);
    g_beltSet.dropBackM = std::clamp(IniF(L"hands", L"belt_drop_back_cm", 25.0f), 5.0f, 100.0f) * 0.01f;
    g_beltSet.merc = IniB(L"hands", L"belt_throw_merc", false);
    g_beltSet.mercReachM = std::clamp(IniF(L"hands", L"belt_merc_reach_cm", 150.0f), 20.0f, 500.0f) * 0.01f;
    g_beltSet.trace = IniB(L"debug", L"belt_bottle_trace", false);
    g_beltSet.glowH = std::clamp(IniF(L"hands", L"belt_glow", 1.0f), -0.9f, 4.0f);
    g_beltSet.emptyDark = std::clamp(IniF(L"hands", L"belt_empty_dark", -0.35f), -0.9f, 0.0f);
    g_beltSet.emptyAlpha = std::clamp(IniF(L"hands", L"belt_empty_alpha", 0.45f), 0.05f, 1.0f);
    ReleaseSRWLockExclusive(&g_beltLock);
}

void Park(Bottle& b) {
    if (b.parked) return;
    const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, g_heroPosNow[0], g_heroPosNow[1] - 5000.0f, g_heroPosNow[2], 1};
    if (Lay(b.inst.p, b.root, m)) b.parked = true;
}

// ---- Taking them in the hand ([hands] belt_grab) ----
// A hand's palm near a bottle on the belt (set.grabM) - it is lit: a see-through ball on it, the
// ball xr.cpp draws for BodyWalk's zones and the map in the hand, and the bottle a little larger
// (kHoverScale). The grip pressed there takes it: the bottle sits in the hero's palm and turns
// with the controller. Tipped over (its top pointing down more than kTipUp allows) while raised to
// the mouth's height or above (within kRaiseM under it), or at the mouth (kMouthM) - no need to bring it
// to the face, so nobody knocks the headset (the user, 2026-10-13) - it is drunk: the game's own
// "potion - belt N" command,
// as the key would; let go anywhere else it goes back to its place on the belt. The grip has to
// be let go once before the hand takes another.
// Hands: BodyWalk's frame (metres from the head, x right, y up, z back, the head's turn only),
// the frame the hero's arms are posed in; in the world it hangs off g_eyeWorld (the arms' own
// point), x along the belt frame's -ax (his model's x runs to his left), y along ay, z along -az.
constexpr float kMouthM = 0.13f, kRaiseM = 0.10f, kTipUp = 0.3f, kHoverScale = 1.05f;
struct HandGrab { int column = -1; int hover = -1; bool drinking = false; bool needRelease = false; ULONGLONG at = 0; uint32_t code = 0;
                  int kind = -1;          // the potion's kind (kPotions) since it was taken
                  uint32_t id = 0;        // the item's own id then (gamestate::BeltIds) - the game's word on a drink or a drop is that id leaving its column
                  int empty = -1;         // [hands] belt_empty_bottle: the emptied bottle in this hand (g_loose), from the drink on
                  bool letGo = false; };  // ... and the grip let go while the game had not answered the drink yet: it falls once it has
HandGrab g_grab[2];                       // [0] right, [1] left; the render thread's
bool g_gripWas[2] = {};                   // each grip past 0.6 last frame (a press is its rising edge): the render thread's
ULONGLONG g_gripToldAt[2] = {};           // when the last 'grip press' line was said for each hand (throttled)
std::atomic<bool> g_holding[2] = {};      // a bottle in that hand now (for the arms' fist, on another thread)
std::atomic<bool> g_lit[2] = {};          // a bottle lit for that hand AND the grip pressed now would take it: it takes no weapon then ([weight])
bool g_takes[2] = {};                     // the decision (vr/grab_priority.h) of the last frame: a grip pressed now takes the lit bottle (the render thread's)

// The controller's trigger as direct input has it (the layout may have taken the control: the frame is as the controller made it), 0 right / 1 left; false when there is no native frame.
bool RawTrigger(int hand, float* out) {
    directinput::Frame f;
    if (!directinput::Active() || !directinput::Latest(&f) || f.version != directinput::kVersion) return false;
    *out = std::clamp(f.hand[1 - hand].trigger, 0.0f, 1.0f);   // direct_input's hands: 0 left, 1 right
    return true;
}
// The fist's cap for a hand: how far it closes with a bottle in it, or -1 (none in it)
// The thin vials - stamina, thawing - against the round flasks (healing, mana, rejuvenation), and the
// antidote, the black one, apart again: another shape in the hand, a hold of its own (the user's, 2026-10-13).
// Its hold by shape: 0 a round flask, 1 a thin vial (stamina, thawing), 2 the antidote (the black one).
int Shape(int kind) { const char c = kind >= 0 ? kPotions[kind].code[0] : 0; return c == 'y' ? 2 : c == 'v' || c == 'w' ? 1 : 0; }
std::atomic<int> g_holdingShape[2] = {};
bool BeltBusy(int side) { return side >= 0 && side < 2 && (g_holding[side].load() || g_lit[side].load()); }
float FistCap(int side) {
    if (side < 0 || side > 1 || !g_holding[side].load()) return -1.0f;
    AcquireSRWLockShared(&g_beltLock);
    const int sh = g_holdingShape[side].load();
    const float c = sh == 2 ? g_beltSet.antiClose : sh == 1 ? g_beltSet.vialClose : g_beltSet.holdClose;
    ReleaseSRWLockShared(&g_beltLock);
    return c;
}
SRWLOCK g_hoverLock = SRWLOCK_INIT;
float g_hoverBalls[4][8] = {};            // x y z (BodyWalk's frame), radius m, alpha, r g b - under g_hoverLock
int g_hoverCount = 0;

// For xr.cpp: the balls over the bottles a hand is near now, in BodyWalk's frame.
int HoverSpheres(float (*out)[8], int max) {
    AcquireSRWLockShared(&g_hoverLock);
    const int n = std::min(max, g_hoverCount);
    for (int i = 0; i < n; ++i) memcpy(out[i], g_hoverBalls[i], sizeof out[i]);
    ReleaseSRWLockShared(&g_hoverLock);
    return n;
}
void SetHover(const float (*balls)[8], int n) {
    AcquireSRWLockExclusive(&g_hoverLock);
    g_hoverCount = std::min(n, 4);
    for (int i = 0; i < g_hoverCount; ++i) memcpy(g_hoverBalls[i], balls[i], sizeof g_hoverBalls[i]);
    ReleaseSRWLockExclusive(&g_hoverLock);
}

void Rotate(const float q[4], const float v[3], float o[3]) {
    // o = q v q*  (x y z w)
    const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]), ty = 2.0f * (q[2] * v[0] - q[0] * v[2]), tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
    o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

void Mul(const float a[4], const float b[4], float o[4]) {   // o = a b (x y z w)
    o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
// The ball's colour, the potion's own (the user's, 2026-10-13): healing red, mana light blue, stamina
// white, rejuvenation (both) lilac; antidote green, thawing orange - [hands] belt_ball_*.
float g_ballRgb[6][3] = {{0.95f, 0.12f, 0.10f}, {0.30f, 0.65f, 1.0f}, {0.95f, 0.95f, 0.95f},
                         {0.78f, 0.55f, 1.0f}, {0.30f, 0.90f, 0.30f}, {1.0f, 0.55f, 0.15f}};   // under g_beltLock
void SetBallColours(const float (*rgb)[3]) {
    AcquireSRWLockExclusive(&g_beltLock);
    memcpy(g_ballRgb, rgb, sizeof g_ballRgb);
    ReleaseSRWLockExclusive(&g_beltLock);
}
void BallColour(int kind, float rgb[3]) {
    const char c = kind >= 0 ? kPotions[kind].code[0] : 0;
    const int i = c == 'h' ? 0 : c == 'm' ? 1 : c == 'v' ? 2 : c == 'r' ? 3 : c == 'y' ? 4 : c == 'w' ? 5 : -1;
    if (i < 0) { rgb[0] = 1.0f; rgb[1] = 0.85f; rgb[2] = 0.4f; return; }
    AcquireSRWLockShared(&g_beltLock);
    memcpy(rgb, g_ballRgb[i], 3 * sizeof(float));
    ReleaseSRWLockShared(&g_beltLock);
}

// ---- Past the drink ([hands] belt_empty_bottle, belt_drop_ground; vr/belt_bottle.h, vr/bottle_fall.h) ----
// The emptied bottle: a model of the potion's own kind, placed apart from the belt's (never used for one: its liquid and its cork are hidden - BottleParts), held in the
// hand from the drink on; let go of, it falls with its weight and the hand's motion, lies, and goes after [hands] belt_empty_keep_s (laid away like a bottle not wanted).
// Only DRAWN: the game's item was drunk as ever (its belt key).
// A full bottle let go of away from the belt (farther than [hands] belt_drop_back_cm from its place) falls the same way - it is the belt's own model of that column, laid
// where the fall has it - and at its first touch of the ground the game is asked to drop that item (beltdrop::Ask: the game's own command, as the player's drop). The game's
// answer is the item's id leaving the column (done: the game's own potion lies on the ground now, ours goes) or not, a moment later (refused: back on the belt).
struct Loose { int kind = -1; Shared inst{}; uint32_t root = 0xFFFFFFFFu; bool parked = false, dead = false, active = false;
               int hand = -1;                        // in this hand (0 right, 1 left), or -1: falling / lying
               int fromHand = -1;                    // ... the hand it left ([multiplayer] pose_share_held: the others see it fly from that one)
               bottlefall::Sim sim; double floorY = 0, releasedAt = 0, restedAt = -1;
               float m[16] = {};                     // where it is laid this frame
               int partsTries = 0; bool partsHidden = false; };
std::vector<Loose> g_loose;               // the render thread's; reused by kind, let go as the belt's
constexpr size_t kMaxLoose = 12;
struct FullDrop { bool on = false; beltbottle::Drop st = beltbottle::Drop::Falling; bottlefall::Sim sim; double floorY = 0, askedAt = 0; uint32_t id = 0; int kind = -1;
                  bool forMerc = false;                  // [hands] belt_throw_merc: a potion the mercenary is given, thrown with the option on
                  meleetouch::HirelingView merc;         // ... and the hero's mercenary as seen at the release (merc.unit 0: none)
                  bool give = false;                     // the game was asked to give it to him (not to drop it)
                  int hand = -1; };                      // the hand that let it fall ([multiplayer] pose_share_held)
FullDrop g_drop[4];                       // the render thread's: column c's full bottle on its way to the ground
bottlefall::Tracker g_track[2];           // each hand's bottle's last poses (metres, the world's axes): the motion it leaves the hand with
double g_lastBottleStep = 0;

double NowS() {
    static const double inv = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return 1.0 / (double)f.QuadPart; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * inv;
}
void ParkLoose(Loose& l) {
    l.active = false;
    l.hand = -1;
    if (l.parked) return;
    const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, g_heroPosNow[0], g_heroPosNow[1] - 5000.0f, g_heroPosNow[2], 1};
    if (Lay(l.inst.p, l.root, m)) l.parked = true;
}
// An emptied bottle of this kind for a hand: a parked one of the kind, or a new one placed (at most kMaxLoose; with none free, the lying one let go of longest goes
// first). -1: none to be had.
int TakeLoose(int kind, const float m[16], bool trace) {
    for (size_t i = 0; i < g_loose.size(); ++i)
        if (!g_loose[i].active && !g_loose[i].dead && g_loose[i].kind == kind) { g_loose[i].active = true; memcpy(g_loose[i].m, m, sizeof g_loose[i].m); return (int)i; }
    if (g_loose.size() >= kMaxLoose) {   // full: a lying one of the kind makes room
        double at[kMaxLoose];
        int idx[kMaxLoose], n = 0;
        for (size_t i = 0; i < g_loose.size(); ++i)
            if (g_loose[i].active && g_loose[i].hand < 0 && g_loose[i].kind == kind) { at[n] = g_loose[i].releasedAt; idx[n++] = (int)i; }
        if (!n) { if (trace) LogF("vrcam: belt bottles - no emptied %.3s to be had (%zu kept, none free)", kPotions[kind].code, g_loose.size()); return -1; }
        int o = 0;
        for (int k = 1; k < n; ++k) if (at[k] < at[o]) o = k;
        Loose& l = g_loose[idx[o]];
        l.hand = -1;
        memcpy(l.m, m, sizeof l.m);
        return idx[o];
    }
    if (!g_defs[kind].p) return -1;
    Loose l;
    l.kind = kind;
    if (!PlaceAt(g_defs[kind].p, m + 12, &l.inst)) { LogF("vrcam: belt bottles - an emptied %.3s: PlaceInstance gave nothing", kPotions[kind].code); return -1; }
    l.root = RootOf(l.inst.p);
    l.active = true;
    memcpy(l.m, m, sizeof l.m);
    g_loose.push_back(l);
    LogF("vrcam: belt bottles - an emptied %.3s placed (instance %p, root entity %X), %zu kept", kPotions[kind].code, l.inst.p, l.root, g_loose.size());
    return (int)g_loose.size() - 1;
}
// A unit instance's entities (+0x10, how many at +0x18 - as RootOf reads them), at most max.
int Entities(void* inst, uint32_t* out, int max) {
    uintptr_t list = 0;
    uint64_t n = 0;
    if (!inst || max <= 0 || !::SafeRead(&list, (const uint8_t*)inst + 0x10, 8) || !::SafeRead(&n, (const uint8_t*)inst + 0x18, 8) || !list || n > 64) return 0;
    int k = 0;
    for (uint64_t i = 0; i < n && k < max; ++i)
        if (::SafeRead(&out[k], (const void*)(list + i * 4), 4)) ++k;
    return k;
}
// The emptied bottle's look: its liquid and its cork hidden on THIS instance only (vr/belt_bottle.h ReadParts / HideParts, then the game's own
// ModelInstance::RebuildRenderItems 0xF3FDB0 - on this thread, where the registry may be changed). Checked every frame: the game rebuilds the list on a LOD or
// quality change, and the hide is done again then. A model that never loads, or a fault: it stays as it is (a full-looking bottle), said once.
constexpr uint64_t RVA_REBUILD_ITEMS = 0xF3FDB0;
constexpr uint8_t kRebuildBytes[] = {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x48, 0x81};
using RebuildFn = void(__fastcall*)(void* inst);
int g_rebuildOk = 0;   // 0 not checked, 1 the game's, -1 not (or a fault)
bool WriteGuarded(uintptr_t a, const void* s, size_t bytes) {
    if (a < 0x10000) return false;
    __try { memcpy((void*)a, s, bytes); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool RebuildItems(uintptr_t inst) {
    __try { ((RebuildFn)d2rsig::Addr(RVA_REBUILD_ITEMS))((void*)inst); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void BottleParts(Loose& l, bool trace) {
    if (g_rebuildOk == 0) {
        g_rebuildOk = d2rsig::Addr(RVA_REBUILD_ITEMS) && d2rsig::Check(RVA_REBUILD_ITEMS, kRebuildBytes, sizeof kRebuildBytes) ? 1 : -1;
        if (g_rebuildOk < 0) Log("vrcam: belt bottles - the game's render-item rebuild (0xF3FDB0) is not as in 3.3.93787: an emptied bottle keeps its liquid and cork");
    }
    if (g_rebuildOk < 0 || l.partsTries < 0) return;
    uint32_t ents[8];
    const int n = Entities(l.inst.p, ents, 8);
    const auto read = [](uintptr_t a, void* d, size_t bytes) { return a > 0x10000 && ::SafeRead(d, (const void*)a, bytes); };
    const auto write = [](uintptr_t a, const void* s, size_t bytes) { return WriteGuarded(a, s, bytes); };
    for (int i = 0; i < n; ++i) {
        const uintptr_t inst = skel::EntityModelInstance(ents[i]);
        if (!inst) continue;
        beltbottle::Parts p;
        if (!beltbottle::ReadParts(read, inst, &p)) {
            if (++l.partsTries == 600) { LogF("vrcam: belt bottles - the emptied %.3s's meshes: %s (gave up)", kPotions[l.kind].code, p.why); l.partsTries = -1; }
            return;
        }
        const int r = beltbottle::HideParts(read, write, inst, p);
        if (r > 0 && !RebuildItems(inst)) { g_rebuildOk = -1; Log("vrcam: belt bottles - a fault in the game's render-item rebuild: emptied bottles keep their liquid and cork from now on"); return; }
        if (r < 0) { LogF("vrcam: belt bottles - the emptied %.3s's visible meshes do not write: left as it is", kPotions[l.kind].code); l.partsTries = -1; return; }
        if (!l.partsHidden || (r > 0 && trace)) {
            uint64_t visible = 0;
            read(inst + beltbottle::kInstVisible + 8, &visible, sizeof visible);
            const bool liquidMesh = strstr(p.names, "liquid") != nullptr;
            LogF("vrcam: belt bottles - the emptied %.3s: %s (%d meshes; '-' hidden: %d of them drawn now%s)%s", kPotions[l.kind].code, p.names, p.meshes, (int)visible,
                 r > 0 ? ", the game's render items rebuilt" : "",
                 liquidMesh ? " - its liquid mesh hidden" : " - its liquid is painted in the glass's own mesh: it stays (the bottle is drawn see-through and darker instead)");
        }
        l.partsHidden = true;
        return;
    }
}
// Let go of: it falls from where it is laid with the hand's motion (none known: it just drops).
void ReleaseLoose(Loose& l, int hand, float scale, float perM, double floorY, bool trace) {
    double base[3], q[4], vel[3] = {}, spin[3] = {};
    const double origin[3] = {0, 0, 0};
    const double now = NowS();
    const bool moving = hand >= 0 && hand < 2 && g_track[hand].Motion(now, 0.1, vel, spin);
    l.fromHand = hand;
    l.hand = -1;
    l.releasedAt = now;
    l.restedAt = -1;
    l.floorY = floorY;
    if (!beltbottle::FromMatrix(l.m, origin, perM, base, q)) { ParkLoose(l); return; }
    l.sim = bottlefall::Release(beltbottle::ShapeOf(beltbottle::SizeOf(kPotions[l.kind].code), scale, perM, true), base, q, vel, spin);
    if (!l.sim.valid) { ParkLoose(l); return; }
    if (trace)
        LogF("vrcam: belt bottles - the emptied %.3s leaves the %s hand at %.2f m/s (%.1f rad/s)%s, %.0f cm over the ground", kPotions[l.kind].code,
             hand < 0 ? "unseen" : hand ? "left" : "right",
             sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]), sqrt(spin[0] * spin[0] + spin[1] * spin[1] + spin[2] * spin[2]), moving ? "" : " (the hand's motion not known: it drops)",
             (bottlefall::Lowest(l.sim) - floorY) * 100.0);
}

void BeltFrame(bool fullBody) {
    AcquireSRWLockShared(&g_beltLock);
    const Belt set = g_beltSet;
    ReleaseSRWLockShared(&g_beltLock);
    skel::HeroXform hx;
    float H[16] = {};
    float eyeY = 0.0f, lift = 0.0f;
    const bool show = set.on && fullBody && skel::HeroTransform(&hx) && ::SafeRead(H, (const void*)hx.comp, 64) && H[15] == 1.0f;
    if (!show) {
        for (Bottle& b : g_bottles) Park(b);
        for (HandGrab& g : g_grab) g = HandGrab{};
        for (Loose& l : g_loose) if (l.active) ParkLoose(l);   // the emptied bottles go with the belt (a view change, out of F4): nothing of them is the game's
        for (FullDrop& d : g_drop) d = FullDrop{};             // a full one in the air goes back on the belt (a drop the game was asked for is its own)
        g_holding[0].store(false); g_holding[1].store(false);
        belttrigger::Publish(false, false, false);
        SetHover(nullptr, 0);
        beltglow::SetEntries(nullptr, 0);
        return;
    }
    if (!skel::HeroEye(&eyeY, &lift) || !(eyeY > 0.5f)) eyeY = 7.0f;
    uint32_t codes[4], ids[4];
    gamestate::BeltCodes(codes);
    gamestate::BeltIds(ids);
    std::vector<char> used(g_bottles.size(), 0);
    const double nowS = NowS();
    const double stepS = g_lastBottleStep > 0 ? std::clamp(nowS - g_lastBottleStep, 0.0, 0.1) : 0.0;
    g_lastBottleStep = nowS;
    // the hero's axes at unit length for the bottle's own turn; the offsets go along the raw rows (model units)
    auto unit = [](const float* r, float* o) {
        const float l = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        for (int i = 0; i < 3; ++i) o[i] = l > 1e-6f ? r[i] / l : 0.0f;
    };
    float ax[3], ay[3], az[3];
    unit(H + 0, ax); unit(H + 4, ay); unit(H + 8, az);
    // The frame the belt hangs in: under the head and turned with it ("they look where the legs
    // go while walking, not where the headset looks", 2026-10-13) - the head's centre put down
    // on the hero's ground and 9 cm back toward the spine, ahead its look along the ground, the
    // height a part of the head's height now (it goes down with a crouch). The hero's own frame
    // until a view has been built.
    const float k = sqrtf(H[0] * H[0] + H[1] * H[1] + H[2] * H[2]);   // world units a model unit
    const float eyeW = eyeY * (k > 1e-6f ? k : 1.0f);                  // his standing eyes, world units
    const float measuredEye = calib::MeasuredEyeM();   // [calibration] use=1: the measured height, else the ini's
    const float perM = eyeW / std::max(0.5f, measuredEye > 0.0f ? measuredEye : g_set.eyeHeightM.load());   // world units a metre
    float O[3] = {H[12], H[13], H[14]};
    float height = set.up * eyeW;
    if (g_headOk.load()) {
        const float head[3] = {g_headCentre[0].load(), g_headCentre[1].load(), g_headCentre[2].load()};
        float f[3] = {g_headAhead[0].load(), g_headAhead[1].load(), g_headAhead[2].load()};
        const float fu = f[0] * ay[0] + f[1] * ay[1] + f[2] * ay[2];
        for (int i = 0; i < 3; ++i) f[i] -= fu * ay[i];
        const float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        if (fl > 1e-4f && std::isfinite(head[0] + head[1] + head[2])) {
            for (int i = 0; i < 3; ++i) az[i] = f[i] / fl;
            // across, the same way round as his model's: x = up x ahead
            ax[0] = ay[1] * az[2] - ay[2] * az[1]; ax[1] = ay[2] * az[0] - ay[0] * az[2]; ax[2] = ay[0] * az[1] - ay[1] * az[0];
            const float hLive = (head[0] - H[12]) * ay[0] + (head[1] - H[13]) * ay[1] + (head[2] - H[14]) * ay[2];
            for (int i = 0; i < 3; ++i) O[i] = head[i] - hLive * ay[i] - 0.09f * perM * az[i];
            if (hLive > 0.3f * eyeW && hLive < 1.5f * eyeW) height = set.up * hLive;
        }
    }
    // BodyWalk's frame <-> the world (see above)
    const float eyeAt[3] = {g_eyeWorld[0].load(), g_eyeWorld[1].load(), g_eyeWorld[2].load()};
    auto toWorldDir = [&](const float v[3], float o[3]) { for (int i = 0; i < 3; ++i) o[i] = -v[0] * ax[i] + v[1] * ay[i] - v[2] * az[i]; };
    auto toWorld = [&](const float p[3], float o[3]) {
        float d[3]; toWorldDir(p, d);
        for (int i = 0; i < 3; ++i) o[i] = eyeAt[i] + d[i] * perM;
    };
    auto toBw = [&](const float w[3], float o[3]) {
        const float d[3] = {(w[0] - eyeAt[0]) / perM, (w[1] - eyeAt[1]) / perM, (w[2] - eyeAt[2]) / perM};
        o[0] = -(d[0] * ax[0] + d[1] * ax[1] + d[2] * ax[2]);
        o[1] = d[0] * ay[0] + d[1] * ay[1] + d[2] * ay[2];
        o[2] = -(d[0] * az[0] + d[1] * az[1] + d[2] * az[2]);
    };

    // Each column's place on the belt (its bottle's matrix there)
    float slot[4][16] = {};
    bool has[4] = {};
    int kindAt[4] = {-1, -1, -1, -1};
    for (int c = 0; c < 4; ++c) {
        const int kk = codes[c] ? KindOf(codes[c]) : -1;
        if (kk < 0) continue;
        if (!g_defs[kk].p && !g_defFailed[kk]) {
            const std::string path = std::string("data/hd/items/misc/") + kPotions[kk].asset + ".json";
            if (!LoadDef(path.c_str(), &g_defs[kk])) { g_defFailed[kk] = true; LogF("vrcam: belt potions - %s did not load", path.c_str()); continue; }
        }
        if (!g_defs[kk].p) continue;
        // round him: column c at th from straight ahead, on a circle of radius `ahead`; the arc swung round him by `turn` (his model's x runs
        // to his left, so to his right is a smaller th) and shifted across him by `across`
        const float th = set.side * set.arc * ((float)c - 1.5f) * 3.14159265f / 180.0f - set.turn * 3.14159265f / 180.0f;
        const float st = sinf(th), ct = cosf(th), r = set.ahead * eyeW;
        const float x = r * st - set.across * eyeW, y = height, z = r * ct;
        float* m = slot[c];
        for (int i = 0; i < 3; ++i) {
            m[i] = (ct * ax[i] - st * az[i]) * set.scale;   // across, turned about his up by th: it faces out
            m[4 + i] = ay[i] * set.scale;
            m[8 + i] = (st * ax[i] + ct * az[i]) * set.scale;
            m[12 + i] = O[i] + x * ax[i] + y * ay[i] + z * az[i];
        }
        m[15] = 1.0f;
        has[c] = true;
        kindAt[c] = kk;
    }

    // Past the drink: the ground the bottles fall to (the hero's, under the head: the render world is y up), metres; a full bottle on its way down, and the game's
    // word on its drop (vr/belt_bottle.h)
    const double floorM = (double)O[1] / perM;
    const double origin0[3] = {0, 0, 0};
    for (int c = 0; c < 4; ++c) {
        FullDrop& d = g_drop[c];
        if (!d.on) continue;
        const unsigned ev = bottlefall::Step(d.sim, bottlefall::Params(), d.floorY, stepS);
        const bool still = ids[c] == d.id && d.id != 0;
        beltbottle::Drop next = beltbottle::DropNext(d.st, (ev & bottlefall::kTouchedFirst) != 0, still, nowS - d.askedAt);
        if (d.st == beltbottle::Drop::Falling && still) {   // where is it going: to the mercenary ([hands] belt_throw_merc), onto the ground, back on the belt
            beltbottle::Merc m;
            float mx = 0.0f, mz = 0.0f;
            stroke::Body body;
            if (d.forMerc && d.merc.unit && meleetouch::HirelingPlace(d.merc.unit, d.merc.id, &mx, &mz) &&
                meleetouch::BodyFor(d.merc.row, mx, mz, (float)floorM, perM, body)) {   // his capsule in render metres - the bottle's own frame
                m.seen = true;
                m.bottom[0] = body.bottom.x; m.bottom[1] = body.bottom.y; m.bottom[2] = body.bottom.z;
                m.top[0] = body.top.x; m.top[1] = body.top.y; m.top[2] = body.top.z;
                m.radius = body.radius;
            }
            const bool touched = d.sim.touched;
            const beltbottle::Target t = beltbottle::OnFlight(set.merc, d.forMerc, set.drop, beltbottle::HitsMerc(m, d.sim.body.p, d.sim.shape.radius), touched,
                                                              beltbottle::LandsByMerc(m, d.sim.body.p, set.mercReachM));
            if (t == beltbottle::Target::Merc) {
                d.give = true;
                d.askedAt = nowS;
                const bool sent = beltdrop::Give(c, d.id);   // the game's own Shift + belt key: to the mercenary (on the UI thread)
                LogF("vrcam: belt bottles - belt %d's %.3s %s the mercenary: the game is asked to give it to him (item %u)%s", c + 1, kPotions[d.kind].code,
                     touched ? "landed by" : "reached", d.id, sent ? "" : " - NOT sent (the game's use of the belt is not to be had: back on the belt)");
                if (!sent) { d = FullDrop{}; continue; }
                next = beltbottle::Drop::Asked;
            } else if (t == beltbottle::Target::Back) {
                LogF("vrcam: belt bottles - belt %d's %.3s missed the mercenary%s: back on the belt", c + 1, kPotions[d.kind].code, m.seen ? "" : " (no mercenary seen)");
                d = FullDrop{};
                continue;
            } else if (t == beltbottle::Target::Flying) {
                next = beltbottle::Drop::Falling;
            }
        }
        if (next == beltbottle::Drop::Asked && d.st != beltbottle::Drop::Asked && !d.give) {
            d.askedAt = nowS;
            const bool sent = beltdrop::Ask(c, d.id);   // the game's own drop of that belt item, as the player's (on the UI thread)
            LogF("vrcam: belt bottles - the full %.3s of belt %d is on the ground: the game is asked to drop it (item %u)%s", kPotions[d.kind].code, c + 1, d.id,
                 sent ? "" : " - NOT sent (the game's drop is not to be had: it goes back on the belt)");
            if (!sent) { d = FullDrop{}; continue; }
        }
        if (next != d.st && beltbottle::DropEnded(next))
            LogF("vrcam: belt bottles - belt %d's %.3s: %s", c + 1, kPotions[d.kind].code,
                 next == beltbottle::Drop::Done ? (d.give ? "the mercenary got it (the item left the belt)" : "the game dropped it (its own potion lies on the ground now)")
                 : next == beltbottle::Drop::Refused ? (d.give ? "the game did not give it (still in the belt after 1.5 s): back on the belt"
                                                               : "the game did not drop it (still in the belt after 1.5 s): back on the belt")
                                                      : "gone from the belt before it landed (drunk with a key, or moved): nothing asked");
        d.st = next;
        if (beltbottle::DropEnded(d.st)) d = FullDrop{};
    }

    // The hands
    float hoverBalls[4][8];
    int nBalls = 0;
    bool held[4] = {}, gone[4] = {};
    float heldM[4][16] = {};
    skel::Input in;
    const bool hands = set.grab && FreshHands(&in);
    for (int h = 0; h < 2; ++h) {
        HandGrab& g = g_grab[h];
        g.hover = -1;
        if (!hands || !(in.handsValid & (1u << h)) || in.grip[h] < 0.0f) {
            g_gripWas[h] = false;
            if (g.empty >= 0 && !g.drinking) {   // the hand not seen: the emptied bottle drops from where it was
                ReleaseLoose(g_loose[g.empty], -1, set.scale, perM, floorM, set.trace);
                g.empty = -1;
            }
            if (g.column >= 0 && !g.drinking) g = HandGrab{};
            continue;
        }
        const bool pressEdge = in.grip[h] > 0.6f && !g_gripWas[h];   // a grip press: logged below with who takes it
        g_gripWas[h] = in.grip[h] > 0.6f;
        const bool gripOn = in.grip[h] > 0.6f && !shoulderwire::Claims(h) && !bowdrawwire::HoldsGrip(h) && !throwwire::GripIsThrowsAt(throwhand::XrOfSide(h)), gripOff = in.grip[h] < 0.35f;   // a grip behind a shoulder is the weapon swap's
        if (gripOff) g.needRelease = false;
        // the hand: the controller (the fist's middle) and its turn; the bottle held at the hold's offset
        // and turn against it ([hands] belt_hold_*: x y z cm and pitch yaw roll degrees, in its own frame).
        // The hold is set for the left hand; the right one is its mirror - x across the hand and the
        // turns about y and z the other way (set on the left, the right one's moved twice as far, 2026-10-13).
        float hold[6];
        const int shape = g.empty >= 0 ? Shape(g_loose[g.empty].kind) : g.column >= 0 ? Shape(kindAt[g.column]) : 0;   // its hold by shape: flask, vial, antidote
        memcpy(hold, shape == 2 ? set.anti : shape == 1 ? set.vial : set.hold, sizeof hold);
        if (h == 0) { hold[0] = -hold[0]; hold[4] = -hold[4]; hold[5] = -hold[5]; }
        const float offB[3] = {hold[0] * 0.01f, hold[1] * 0.01f, hold[2] * 0.01f};
        float offR[3];
        Rotate(in.rot[h], offB, offR);
        // held by the middle of the hero's palm as drawn (tied to the controller it slid out of the hand as the wrist
        // turned, 2026-10-13); the controller where the arms have not been drawn lately
        // his model space put in the world by his model matrix (H) - exactly where the hand is drawn; the
        // controller (through BodyWalk's frame) where the arms have not been drawn lately
        float palm[3];
        float pm[3];
        bool drawn = false;
#if D2RVR_FIRST_PERSON
        drawn = skel::HeroPalmModel(h, pm);
#endif
        if (drawn) {
            float offW[3];
            toWorldDir(offR, offW);
            for (int i = 0; i < 3; ++i) palm[i] = pm[0] * H[i] + pm[1] * H[4 + i] + pm[2] * H[8 + i] + H[12 + i] + offW[i] * perM;
        } else {
            const float palmBw[3] = {in.hand[h][0] + offR[0], in.hand[h][1] + offR[1], in.hand[h][2] + offR[2]};
            toWorld(palmBw, palm);
        }
        const float d2r = 3.14159265f / 360.0f;   // half a degree in radians (half angles)
        const float qy[4] = {0, sinf(hold[4] * d2r), 0, cosf(hold[4] * d2r)}, qx[4] = {sinf(hold[3] * d2r), 0, 0, cosf(hold[3] * d2r)},
                    qz[4] = {0, 0, sinf(hold[5] * d2r), cosf(hold[5] * d2r)};
        float qyx[4], qe[4], qt[4];
        Mul(qy, qx, qyx); Mul(qyx, qz, qe); Mul(in.rot[h], qe, qt);
        // the bottle's own axes in the world: up (its top) along the turned +y, ahead along the turned -z
        const float yAx[3] = {0, 1, 0}, zAx[3] = {0, 0, 1};
        float cyB[3], czB[3], cy[3], cz[3];
        Rotate(qt, yAx, cyB); Rotate(qt, zAx, czB);
        toWorldDir(cyB, cy); toWorldDir(czB, cz);
        // in the fist, turned with the controller (and the hold); a little low, so the fist holds its middle
        const float up[3] = {cy[0], cy[1], cy[2]}, ah[3] = {-cz[0], -cz[1], -cz[2]};
        const auto inFist = [&](float* m) {
            const float s = set.scale;
            const float ac[3] = {up[1] * ah[2] - up[2] * ah[1], up[2] * ah[0] - up[0] * ah[2], up[0] * ah[1] - up[1] * ah[0]};
            for (int i = 0; i < 3; ++i) {
                m[i] = ac[i] * s; m[4 + i] = up[i] * s; m[8 + i] = ah[i] * s;
                m[12 + i] = palm[i] - up[i] * 0.05f * perM;
            }
            m[3] = m[7] = m[11] = 0.0f;
            m[15] = 1.0f;
        };
        // the bottle's last poses in this hand (metres, the world's axes): the motion it leaves with when let go
        const auto track = [&](const float* m) {
            double base[3], q[4];
            if (beltbottle::FromMatrix(m, origin0, perM, base, q)) g_track[h].Add(nowS, base, q);
        };
        if (g.drinking && g.empty >= 0) {   // [hands] belt_empty_bottle: the emptied bottle in the hand from the drink on; the game's word by the item's id
            Loose& l = g_loose[g.empty];
            inFist(l.m);
            track(l.m);
            if (gripOff) g.letGo = true;   // let go before the game answered: it falls once it has
            const bool still = ids[g.column] == g.id && g.id != 0;
            const beltbottle::Drink d = beltbottle::DrinkNext(beltbottle::Drink::Waiting, still, (double)(GetTickCount64() - g.at) / 1000.0);
            if (d == beltbottle::Drink::Waiting) { if (still) gone[g.column] = true; continue; }
            if (d == beltbottle::Drink::Refused) {   // still in the belt: not drunk - no empty bottle, the full one is back on the belt
                if (set.trace) LogF("vrcam: belt bottles - belt %d's %.3s was not drunk (still in the belt after 1.5 s): no emptied bottle", g.column + 1, kPotions[l.kind].code);
                ParkLoose(l);
                const bool off = gripOff;
                g = HandGrab{};
                g.needRelease = !off;
                continue;
            }
            if (set.trace) LogF("vrcam: belt bottles - belt %d's %.3s drunk: the %s hand keeps the emptied bottle", g.column + 1, kPotions[l.kind].code, h ? "left" : "right");
            const int e = g.empty;
            const bool letGo = g.letGo || gripOff;
            g = HandGrab{};
            g.empty = e;
            if (letGo) {
                ReleaseLoose(l, h, set.scale, perM, floorM, set.trace);
                g.empty = -1;
            }
            continue;
        }
        if (g.drinking) {   // drunk: hidden until the belt shows it gone (or a moment passes), then free again
            if (codes[g.column] != g.code || GetTickCount64() - g.at > 1500) { g = HandGrab{}; g.needRelease = true; }
            else gone[g.column] = true;
            continue;
        }
        if (g.empty >= 0) {   // [hands] belt_empty_bottle: the emptied bottle in the hand until the grip lets go - then it falls with the hand's motion
            Loose& l = g_loose[g.empty];
            inFist(l.m);
            track(l.m);
            if (gripOff) {
                ReleaseLoose(l, h, set.scale, perM, floorM, set.trace);
                g.empty = -1;
            }
            continue;
        }
        if (g.column >= 0) {
            if (!has[g.column] || codes[g.column] != g.code) { g = HandGrab{}; g.needRelease = !gripOff; continue; }   // the belt changed under it
            if (gripOff) {   // let go: back on the belt - or ([hands] belt_drop_ground) away from it, onto the ground
                const float* p = slot[g.column] + 12;
                const float toSlot = sqrtf((p[0] - palm[0]) * (p[0] - palm[0]) + (p[1] - palm[1]) * (p[1] - palm[1]) + (p[2] - palm[2]) * (p[2] - palm[2])) / perM;
                const bool forMerc = set.merc && beltbottle::ForMerc(kindAt[g.column] >= 0 ? kPotions[kindAt[g.column]].code : nullptr);
                if (beltbottle::OnLetGo(set.drop, set.merc, forMerc, toSlot, set.dropBackM) == beltbottle::LetGo::Falls && ids[g.column] == g.id && g.id != 0) {
                    FullDrop& d = g_drop[g.column];
                    double base[3], q[4], vel[3] = {}, spin[3] = {};
                    float m[16];
                    inFist(m);
                    const bool moving = g_track[h].Motion(nowS, 0.1, vel, spin);
                    if (beltbottle::FromMatrix(m, origin0, perM, base, q)) {
                        d = FullDrop{};
                        d.sim = bottlefall::Release(beltbottle::ShapeOf(beltbottle::SizeOf(kPotions[kindAt[g.column]].code), set.scale, perM, false), base, q, vel, spin);
                        d.on = d.sim.valid;
                        d.floorY = floorM;
                        d.id = g.id;
                        d.kind = kindAt[g.column];
                        d.hand = h;
                        d.forMerc = forMerc;
                        if (forMerc && !meleetouch::FindHireling(merctint::kHirelingClasses, merctint::kHirelingCount, &d.merc)) d.merc = meleetouch::HirelingView{};
                        if (d.on)
                            LogF("vrcam: belt bottles - the %s hand lets belt %d's full %.3s fall, %.0f cm from its place, at %.2f m/s%s", h ? "left" : "right", g.column + 1,
                                 kPotions[d.kind].code, toSlot * 100.0f, sqrt(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]), moving ? "" : " (the hand's motion not known)");
                    }
                }
                g = HandGrab{};
                g_track[h].Clear();
                continue;
            }
            float* m = heldM[g.column];
            inFist(m);
            track(m);
            held[g.column] = true;
            // drunk? tipped over, raised to the mouth's height (or at the mouth)
            const float top[3] = {palm[0] + up[0] * 0.07f * perM, palm[1] + up[1] * 0.07f * perM, palm[2] + up[2] * 0.07f * perM};
            const float mouthBw[3] = {0.0f, -0.08f, -0.03f};
            float mouth[3];
            toWorld(mouthBw, mouth);
            const float dm = sqrtf((top[0] - mouth[0]) * (top[0] - mouth[0]) + (top[1] - mouth[1]) * (top[1] - mouth[1]) + (top[2] - mouth[2]) * (top[2] - mouth[2])) / perM;
            const float upness = up[0] * ay[0] + up[1] * ay[1] + up[2] * ay[2];
            const float raised = ((top[0] - mouth[0]) * ay[0] + (top[1] - mouth[1]) * ay[1] + (top[2] - mouth[2]) * ay[2]) / perM;
            if (upness < kTipUp && (dm < kMouthM || raised > -kRaiseM)) {
                gamecmd::Tap(0x17u + (uint32_t)g.column);
                haptics::Play(haptics::Event::Drink, haptics::FromSkel(h));   // [openxr] haptics
                LogF("vrcam: belt potions - the %s hand drinks belt %d (%.3s)", h ? "left" : "right", g.column + 1, kPotions[kindAt[g.column]].code);
                g.drinking = true;
                g.at = GetTickCount64();
                held[g.column] = false;
                gone[g.column] = true;
                if (set.empty && kindAt[g.column] >= 0) {   // [hands] belt_empty_bottle: the hand keeps an emptied one of the kind (the game's item is drunk as ever)
                    g.empty = TakeLoose(kindAt[g.column], m, set.trace);
                    if (g.empty >= 0) g_loose[g.empty].hand = h;
                    g.letGo = false;
                }
            }
            continue;
        }
        // on the weapon ([weight]): no bottle - and none taken by that grip once it lets go of it
        if (weight::HoldsWeapon(h)) { if (gripOn) g.needRelease = true; continue; }
        // free: the nearest bottle on the belt within reach ([hands] belt_grab_cm) is lit, the grip takes it - unless the weapon is nearer ([belt] grab_nearest: the hand's fist is nearer the shaft of the weapon
        // the other hand holds, the weapon on the floor or on the back than the palm is to the bottle; or the hand is behind a shoulder for the swap - then no bottle is lit or taken and the
        // weapon (or the swap) gets the press), and with [belt] grab_trigger only with the trigger squeezed too
        float best = set.grabM;
        int cand = -1;
        for (int c = 0; c < 4; ++c) {
            if (!has[c] || held[c] || gone[c] || g_grab[1 - h].column == c || g_drop[c].on) continue;
            const float* p = slot[c] + 12;
            const float d = sqrtf((p[0] - palm[0]) * (p[0] - palm[0]) + (p[1] - palm[1]) * (p[1] - palm[1]) + (p[2] - palm[2]) * (p[2] - palm[2])) / perM;
            if (d < best) { best = d; cand = c; }
        }
        {
            grabprio::In gi;
            gi.nearest = g_set.grabNearest.load();
            gi.needTrigger = g_set.grabTrigger.load();
            {
                float trig = 0.0f;
                gi.triggerKnown = gi.needTrigger && RawTrigger(h, &trig);
                gi.triggerDown = gi.triggerKnown && trig > 0.5f;
            }
            float zoneCm = 1e6f;
            gi.zone = (grabprio::Zone)weight::TakeZone(h, &zoneCm);
            gi.zoneCm = zoneCm;
            gi.zoneReachCm = g_set.weightGrabCm.load();
            gi.swapNear = shoulderswap::Near(in.hand[h]);
            gi.swapClaims = shoulderwire::Claims(h);
            gi.bottle = cand;
            gi.bottleCm = cand >= 0 ? best * 100.0f : 1e6f;
            const grabprio::Out go = grabprio::Decide(gi);
            g.hover = go.bottleLit ? cand : -1;
            g_takes[h] = go.bottleTakes;
            const ULONGLONG nowMs = GetTickCount64();
            if (pressEdge && nowMs - g_gripToldAt[h] >= 250) {   // one line per press (at most four a second a hand): who takes it, and the distances that decided
                g_gripToldAt[h] = nowMs;
                char kind[4] = {};
                if (cand >= 0 && kindAt[cand] >= 0) memcpy(kind, kPotions[kindAt[cand]].code, 3);
                char line[420];
                grabprio::Line(gi, go, h, kind[0] ? kind : nullptr, line, sizeof line);
                Log(line);
            }
        }
        if (g.hover >= 0 && g_takes[h] && gripOn && !g.needRelease) {
            g.column = g.hover;
            g.code = codes[g.hover];
            g.kind = kindAt[g.hover];
            g.id = ids[g.hover];
            g.hover = -1;
            g_track[h].Clear();
            haptics::Play(haptics::Event::BeltTake, haptics::FromSkel(h));   // [openxr] haptics
            LogF("vrcam: belt potions - the %s hand takes belt %d (%.3s)", h ? "left" : "right", g.column + 1, kPotions[kindAt[g.column]].code);
        }
        if (gripOn && g.hover < 0) g.needRelease = true;   // a grip closed on nothing (or one the weapon / the swap had): let go before taking. A lit bottle that the trigger has not released
                                                            // yet (grab_trigger) keeps the grip waiting: the trigger squeezed a moment later takes it
        if (g.hover >= 0 && nBalls < 4 && !(set.glow && beltglow::Wanted())) {   // ([hands] belt_inner_glow: the bottle itself glows instead, below)
            float c[3];
            toBw(slot[g.hover] + 12, c);
            hoverBalls[nBalls][0] = c[0]; hoverBalls[nBalls][1] = c[1] + 0.03f; hoverBalls[nBalls][2] = c[2];
            hoverBalls[nBalls][3] = set.ballRadius; hoverBalls[nBalls][4] = set.ballAlpha;
            BallColour(kindAt[g.hover], hoverBalls[nBalls] + 5);
            ++nBalls;
        }
    }
    SetHover(hoverBalls, nBalls);
    // (an emptied bottle in the hand holds the fist round it too, and keeps the weapon from that grip)
    for (int h = 0; h < 2; ++h) g_holding[h].store((g_grab[h].column >= 0 && !g_grab[h].drinking) || g_grab[h].empty >= 0);
    // a bottle in a hand, taken or being drunk: the triggers are the belt's (the game reads the left one as Shift, and a drink with Shift is not a drink)
    belttrigger::Publish(g_set.holdTakesTrigger.load(), g_grab[0].column >= 0, g_grab[1].column >= 0);
    for (int h = 0; h < 2; ++h) g_lit[h].store(g_grab[h].hover >= 0 && g_takes[h]);
    for (int h = 0; h < 2; ++h)
        g_holdingShape[h].store(g_grab[h].empty >= 0 ? Shape(g_loose[g_grab[h].empty].kind) : g_grab[h].column >= 0 ? Shape(kindAt[g_grab[h].column]) : 0);
    bool lit[4] = {};
    for (const HandGrab& g : g_grab) if (g.hover >= 0) lit[g.hover] = true;
    // the bottles' look this frame (vr/belt_glow.h): a lit bottle brighter ([hands] belt_inner_glow), an emptied one darker and see-through ([hands] belt_empty_*)
    beltglow::Entry look[beltglow::kMax];
    int nLook = 0;
    const auto addLook = [&](void* inst, float h, float opacity, bool isLit) {
        uint32_t ents[8];
        const int n = Entities(inst, ents, 8);
        for (int i = 0; i < n && nLook < beltglow::kMax; ++i)
            if (skel::EntityModelInstance(ents[i])) look[nLook++] = {ents[i], h, opacity, isLit};   // the entities that are drawn (the root is not)
    };

    // The emptied bottles: in a hand (laid where the hand loop put them), falling, lying; gone after belt_empty_keep_s. Their liquid and cork hidden (BottleParts).
    for (Loose& l : g_loose) {
        if (!l.active || l.dead) continue;
        if (l.hand < 0) {
            const unsigned ev = bottlefall::Step(l.sim, bottlefall::Params(), l.floorY, stepS);
            if (ev & bottlefall::kCameToRest) {
                l.restedAt = nowS;
                if (set.trace) LogF("vrcam: belt bottles - the emptied %.3s lies still, %.1f s after it was let go", kPotions[l.kind].code, nowS - l.releasedAt);
            }
            if (beltbottle::EmptyGoes(l.restedAt >= 0, l.restedAt >= 0 ? nowS - l.restedAt : 0.0, nowS - l.releasedAt, set.emptyKeepS)) {
                if (set.trace) LogF("vrcam: belt bottles - the emptied %.3s goes (%.0f s after it was let go)", kPotions[l.kind].code, nowS - l.releasedAt);
                ParkLoose(l);
                continue;
            }
            beltbottle::ToMatrix(l.sim, origin0, perM, set.scale, l.m);
        }
        BottleParts(l, set.trace);
        if (l.root == 0xFFFFFFFFu) l.root = RootOf(l.inst.p);
        if (Lay(l.inst.p, l.root, l.m)) {
            l.parked = false;
            NoteShown(l.hand >= 0 ? l.hand : l.fromHand, Code3(kPotions[l.kind].code), l.m,
                      l.hand >= 0 ? poseshare::kInHand : l.restedAt >= 0 ? poseshare::kAtRest : poseshare::kFlying);   // [multiplayer] pose_share_held
        } else { l.dead = true; l.active = false; ReleaseInstance(&l.inst, &g_areaTally); }   // its entity is gone (an area change): a hand that held it holds nothing; let go
        if (!l.dead) addLook(l.inst.p, set.emptyDark, set.emptyAlpha, false);
    }
    for (HandGrab& g : g_grab)
        if (g.empty >= 0 && (size_t)g.empty < g_loose.size() && g_loose[g.empty].dead) { g.empty = -1; g.drinking = false; g.column = -1; }

    // Each bottle laid: in a hand, on the belt (a little larger while a hand is at it), or put away
    for (int c = 0; c < 4; ++c) {
        if (!has[c] || gone[c]) continue;
        const int kk = kindAt[c];
        size_t at = g_bottles.size();
        for (size_t i = 0; i < g_bottles.size(); ++i)
            if (!used[i] && !g_bottles[i].dead && g_bottles[i].kind == kk) { at = i; break; }
        float m[16];
        memcpy(m, held[c] ? heldM[c] : slot[c], sizeof m);
        if (g_drop[c].on) beltbottle::ToMatrix(g_drop[c].sim, origin0, perM, set.scale, m);   // [hands] belt_drop_ground: on its way to the ground
        else if (!held[c] && lit[c])
            for (int i = 0; i < 12; ++i) if (i % 4 != 3) m[i] *= kHoverScale;
        if (at == g_bottles.size()) {
            if (g_bottles.size() >= 32) continue;
            Bottle b{kk, {}, 0xFFFFFFFFu, false, false};
            if (!PlaceAt(g_defs[kk].p, m + 12, &b.inst)) { LogF("vrcam: belt potions - %.3s: PlaceInstance gave nothing", kPotions[kk].code); continue; }
            b.root = RootOf(b.inst.p);
            LogF("vrcam: belt potions - a %.3s placed (instance %p, root entity %X), %zu kept", kPotions[kk].code, b.inst.p, b.root,
                 g_bottles.size() + 1);
            g_bottles.push_back(b);
            used.push_back(0);
        }
        used[at] = 1;
        Bottle& b = g_bottles[at];
        if (b.root == 0xFFFFFFFFu) b.root = RootOf(b.inst.p);
        if (Lay(b.inst.p, b.root, m)) {
            b.parked = false;
            if (held[c] || g_drop[c].on) {   // [multiplayer] pose_share_held: a bottle in a hand, or let fall from it
                int hand = g_drop[c].on ? g_drop[c].hand : -1;
                for (int hh = 0; hh < 2 && !g_drop[c].on; ++hh) if (g_grab[hh].column == c) hand = hh;
                NoteShown(hand, codes[c], m, g_drop[c].on ? poseshare::kFlying : poseshare::kInHand);
            }
        } else { b.dead = true; ReleaseInstance(&b.inst, &g_areaTally); }   // its entity is gone (an area change can take the registry's units): let go, another placed next frame
        if (lit[c] && !held[c] && !g_drop[c].on && set.glow && !b.dead) addLook(b.inst.p, set.glowH, -1.0f, true);   // [hands] belt_inner_glow
    }
    beltglow::SetEntries(look, nLook);
    for (size_t i = 0; i < g_bottles.size(); ++i)
        if (!used[i] && !g_bottles[i].dead) Park(g_bottles[i]);
    // the dead ones dropped (each let go as its entity went)
    const size_t before = g_bottles.size();
    g_bottles.erase(std::remove_if(g_bottles.begin(), g_bottles.end(), [](const Bottle& b) { return b.dead; }), g_bottles.end());
    if (g_bottles.size() != before) LogF("vrcam: belt potions - %zu gone with their entities, %zu kept", before - g_bottles.size(), g_bottles.size());
}

// When: the registry may be changed between Renderer::RenderToView opening it and
// UnitTranslationLayer::SimulationTick (0x780120) or Renderer::Tick closing it again - all
// through one setter, 0x912C90(registry, allowed): +0x110 = allowed, then its queue of
// deferred changes (+0x78) is worked off. At VrFrame (after CameraTranslation::RenderTick) it
// was always shut in a game (2026-10-13: "changes not allowed now", open only while an area
// loaded). So ours runs in that setter, just before the game shuts the window: the game's own
// units are made by then, the frame's world matrices not yet worked out.
constexpr uint64_t RVA_SET_MUTABLE = 0x912C90;
constexpr uint8_t kSetMutableBytes[] = {0x40, 0x53, 0x55, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xE9};
using SetMutableFn = void(__fastcall*)(void* registry, bool allowed);
SetMutableFn g_origSetMutable = nullptr;
std::atomic<int> g_hook{0};               // 0 not tried, 1 in, -1 failed
std::atomic<bool> g_fullBody{false};      // VrFrame's: first person with the body (F4) and vrcam's camera on

// ---- [weight] grab_items: our copy of an item a hand took off the ground, in that hand while its grip is held ----
// The item itself is the game's, in the inventory where the game put it; this is only its model drawn (its own UnitDefinition, as the belt's bottles are: placed
// once a kind, laid far under the ground while no hand shows it, let go as the belt's). Its model by its code: d2r_vr_props\item_models.txt, made by D2R VR Settings from the
// game's data/hd/items/items.json. Held by the drawn hand (skel::HeroArmWorld): its middle in the palm, its up along the knuckles' line, its front out of the palm.
struct HeldCopy { uint32_t code; Shared inst; uint32_t root; bool parked; bool dead; };
std::vector<HeldCopy> g_copies;
std::unordered_map<uint32_t, Shared> g_copyDefs;   // by code (a failed load: null, not asked again)
std::unordered_map<uint32_t, std::string> g_itemModels;
bool g_itemModelsRead = false;
const std::string* ItemModelOf(uint32_t code) {
    if (!g_itemModelsRead) {
        g_itemModelsRead = true;
        std::wstring path(g_iniPath);
        const size_t slash = path.find_last_of(L'\\');
        path = (slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1)) + L"d2r_vr_props\\item_models.txt";
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f) {
            char line[400];
            while (fgets(line, sizeof line, f)) {
                if (line[0] == '#') continue;
                char* tab = strchr(line, '\t');
                if (!tab || tab - line > 4 || tab == line) continue;
                char* end = tab + 1 + strcspn(tab + 1, "\r\n");
                *end = 0;
                char c[4] = {' ', ' ', ' ', ' '};
                memcpy(c, line, (size_t)(tab - line));
                g_itemModels[(uint32_t)(uint8_t)c[0] | (uint32_t)(uint8_t)c[1] << 8 | (uint32_t)(uint8_t)c[2] << 16 | (uint32_t)(uint8_t)c[3] << 24] = tab + 1;
            }
            fclose(f);
        }
        LogF("grab items: %zu item models known (d2r_vr_props\\item_models.txt, made by D2R VR Settings with Pick up items ticked)%s", g_itemModels.size(),
             g_itemModels.empty() ? " - none: a hand that takes an item off the ground shows nothing of it" : "");
    }
    const auto it = g_itemModels.find(code);
    return it == g_itemModels.end() ? nullptr : &it->second;
}
void ParkCopy(HeldCopy& c) {
    if (c.parked) return;
    const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, g_heroPosNow[0], g_heroPosNow[1] - 5000.0f, g_heroPosNow[2], 1};
    if (Lay(c.inst.p, c.root, m)) c.parked = true;
}
void HeldItemFrame(bool fullBody) {
    const int side = itemgrabwire::g_heldSide.load();
    const uint32_t code = itemgrabwire::g_heldCode.load();
    float m[16];
    bool want = fullBody && side >= 0 && code != 0;
#if D2RVR_FIRST_PERSON
    skel::HeroArm arm;
    if (want && skel::HeroArmWorld(side, &arm) && arm.hand) {
        const float perM = std::max(1.0f, ViewHeight()) / UserEyeHeightM();   // world units a metre
        float y[3] = {arm.across[0], arm.across[1], arm.across[2]}, z[3] = {arm.face[0], arm.face[1], arm.face[2]};
        const float d = z[0] * y[0] + z[1] * y[1] + z[2] * y[2];
        for (int i = 0; i < 3; ++i) z[i] -= d * y[i];
        const float lz = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]), ly = sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
        if (lz > 1e-4f && ly > 1e-4f) {
            for (int i = 0; i < 3; ++i) { z[i] /= lz; y[i] /= ly; }
            const float x[3] = {y[1] * z[2] - y[2] * z[1], y[2] * z[0] - y[0] * z[2], y[0] * z[1] - y[1] * z[0]};
            for (int i = 0; i < 3; ++i) {
                m[i] = x[i]; m[4 + i] = y[i]; m[8 + i] = z[i];
                // the palm's middle (half the way to the middle knuckle, 3 cm out of the palm), a little down along the item so the fist holds its middle
                m[12 + i] = arm.wrist[i] + arm.point[i] * 0.045f * perM + arm.face[i] * 0.03f * perM - y[i] * 0.05f * perM;
            }
            m[3] = m[7] = m[11] = 0.0f;
            m[15] = 1.0f;
        } else {
            want = false;
        }
    } else {
        want = false;
    }
#else
    want = false;
#endif
    HeldCopy* use = nullptr;
    if (want) {
        for (HeldCopy& c : g_copies) if (!c.dead && c.code == code) { use = &c; break; }
        if (!use) {
            auto def = g_copyDefs.find(code);
            if (def == g_copyDefs.end()) {
                Shared s{};
                const std::string* path = ItemModelOf(code);
                if (path && !LoadDef(path->c_str(), &s)) LogF("grab items: the model %s did not load: no copy in the hand", path->c_str());
                def = g_copyDefs.emplace(code, s).first;
            }
            if (def->second.p && g_copies.size() < 16) {
                HeldCopy c{code, {}, 0xFFFFFFFFu, false, false};
                if (PlaceAt(def->second.p, m + 12, &c.inst)) {
                    c.root = RootOf(c.inst.p);
                    g_copies.push_back(c);
                    use = &g_copies.back();
                    LogF("grab items: our copy of a '%.4s' placed for the hand (instance %p, root entity %X), %zu kept", (const char*)&code, c.inst.p, c.root, g_copies.size());
                }
            }
        }
    }
    for (HeldCopy& c : g_copies) {
        if (c.dead) continue;
        if (&c == use) {
            if (c.root == 0xFFFFFFFFu) c.root = RootOf(c.inst.p);
            if (Lay(c.inst.p, c.root, m)) { c.parked = false; NoteShown(side, code, m, poseshare::kInHand); }   // ([multiplayer] pose_share_held)
            else { c.dead = true; ReleaseInstance(&c.inst, &g_areaTally); }   // its entity is gone (an area change): let go, another placed next frame
        } else {
            ParkCopy(c);
        }
    }
    g_copies.erase(std::remove_if(g_copies.begin(), g_copies.end(), [](const HeldCopy& c) { return c.dead; }), g_copies.end());
}

// ---- [multiplayer] pose_share_held: the other players' potions and items, in their hands and in flight - copies of the game's own models, as ours are ----
// The update thread says what to draw where (SetRemoteCopies, world matrices); here each is laid, a copy kept for each code (let go as the belt's: LetGoAll), laid
// far under the ground while not wanted. Their game's items are theirs: nothing here is an item, nothing of the game's changes.
SRWLOCK g_remoteCopyLock = SRWLOCK_INIT;
std::vector<RemoteCopy> g_remoteCopyWant;
std::atomic<bool> g_remoteCopiesOn{false};    // asked for once (the hook: Wanted)
std::atomic<unsigned> g_remoteCopiesDrawn{0};
struct RemoteDrawn { uint32_t code; Shared inst; uint32_t root; bool parked; bool dead; };
std::vector<RemoteDrawn> g_remoteDrawn;       // the render thread's
void SetRemoteCopies(const RemoteCopy* c, int n) {
    AcquireSRWLockExclusive(&g_remoteCopyLock);
    if (c && n > 0) g_remoteCopyWant.assign(c, c + n);
    else g_remoteCopyWant.clear();
    ReleaseSRWLockExclusive(&g_remoteCopyLock);
    if (n > 0) g_remoteCopiesOn.store(true);
}
unsigned TakeRemoteCopiesDrawn() { return g_remoteCopiesDrawn.exchange(0); }
// The model a code is drawn by: a belt potion's own (as the belt's), else the item's (d2r_vr_props\item_models.txt). Null: none.
void* RemoteDefOf(uint32_t code) {
    const int kk = KindOf(code);
    if (kk >= 0) {
        if (!g_defs[kk].p && !g_defFailed[kk]) {
            const std::string path = std::string("data/hd/items/misc/") + kPotions[kk].asset + ".json";
            if (!LoadDef(path.c_str(), &g_defs[kk])) { g_defFailed[kk] = true; LogF("vrcam: belt potions - %s did not load", path.c_str()); }
        }
        return g_defs[kk].p;
    }
    auto def = g_copyDefs.find(code);
    if (def == g_copyDefs.end()) {
        Shared s{};
        const std::string* path = ItemModelOf(code);
        if (path && !LoadDef(path->c_str(), &s)) LogF("vrcam: pose share - the model %s did not load: another player's '%.4s' is not drawn", path->c_str(), (const char*)&code);
        def = g_copyDefs.emplace(code, s).first;
    }
    return def->second.p;
}
void RemoteCopiesFrame() {
    std::vector<RemoteCopy> want;
    AcquireSRWLockShared(&g_remoteCopyLock);
    want = g_remoteCopyWant;
    ReleaseSRWLockShared(&g_remoteCopyLock);
    std::vector<char> used(g_remoteDrawn.size(), 0);
    for (const RemoteCopy& w : want) {
        size_t at = g_remoteDrawn.size();
        for (size_t i = 0; i < g_remoteDrawn.size(); ++i)
            if (!used[i] && !g_remoteDrawn[i].dead && g_remoteDrawn[i].code == w.code) { at = i; break; }
        if (at == g_remoteDrawn.size()) {
            void* def = g_remoteDrawn.size() < 16 ? RemoteDefOf(w.code) : nullptr;
            RemoteDrawn r{w.code, {}, 0xFFFFFFFFu, false, false};
            if (!def || !PlaceAt(def, w.m + 12, &r.inst)) continue;
            r.root = RootOf(r.inst.p);
            g_remoteDrawn.push_back(r);
            used.push_back(0);
            LogF("vrcam: pose share - a copy of another player's '%.4s' placed (instance %p), %zu kept", (const char*)&w.code, r.inst.p, g_remoteDrawn.size());
        }
        used[at] = 1;
        RemoteDrawn& r = g_remoteDrawn[at];
        if (r.root == 0xFFFFFFFFu) r.root = RootOf(r.inst.p);
        if (Lay(r.inst.p, r.root, w.m)) { r.parked = false; g_remoteCopiesDrawn.fetch_add(1, std::memory_order_relaxed); }
        else { r.dead = true; ReleaseInstance(&r.inst, &g_areaTally); }   // its entity is gone (an area change): let go, another placed next frame
    }
    for (size_t i = 0; i < g_remoteDrawn.size(); ++i) {
        RemoteDrawn& r = g_remoteDrawn[i];
        if (used[i] || r.dead || r.parked) continue;
        const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, g_heroPosNow[0], g_heroPosNow[1] - 5000.0f, g_heroPosNow[2], 1};
        if (Lay(r.inst.p, r.root, m)) r.parked = true;
    }
    g_remoteDrawn.erase(std::remove_if(g_remoteDrawn.begin(), g_remoteDrawn.end(), [](const RemoteDrawn& r) { return r.dead; }), g_remoteDrawn.end());
}

// ---- the item models we place, told to the readers of the level (vr/own_models.h) ----
// [weight] world_contact's scan takes the level's entities with a parent for its pieces: our instances' meshes hang on a root of ours with no parent, never on the
// hero, so the belt's bottles were laid by their renderer's boxes as walls round his hips and the weapons stopped on them (Serg, 0.197.1, a weapon in each hand).
// Every entity of the belt's bottles, the emptied ones, the items in a hand and the other players' copies, five times a second (the scan reads twice a second).
// ---- [hud] wrist_game_models: the life and mana medallions on the wrists as the game's own models (vr/wrist_models.h, shared/wrist_orb_models.h) ----
// The effect's frame block (vr/render/effects/effects.cpp) works out each medallion as today (its place on the drawn wrist, its frame, the eased level, the
// low-life heartbeat) and hands it here (SetWristOrbs); each frame on the registry's thread the game's glass ball, its aluminium ball as the plate and our
// liquid lens in the potions' material are laid there (WristOrbsFrame) - the game draws them: both eyes, its depth, its lights, its glass. Placed once a
// game (let go as the belt's: LetGoAll), laid far under the ground while not wanted (not F4, a menu, no fresh want).
SRWLOCK g_wristLock = SRWLOCK_INIT;
SRWLOCK g_wristPartsLock = SRWLOCK_INIT;  // the parts below: Work holds it to place / lay / let go, the skeleton hook (WristArmPosed) to lay again
wristmodels::Want g_wristWant;            // under g_wristLock
float g_wristPulse = 1.0f;                // under g_wristLock: [hud] wrist_game_pulse
struct WristPart { Shared inst{}; uint32_t root = 0xFFFFFFFFu; bool parked = true; bool placed = false; bool modelTold = false; ULONGLONG placedAt = 0; int variant = -1; };
constexpr int kWristUnits = 6;            // glass, the gold plate, life liquid, mana liquid, the bronze plate, the silver plate
Shared g_wristDef[kWristUnits];
bool g_wristDefOk[kWristUnits] = {};
ULONGLONG g_wristDefAt = 0;               // when the units were last asked (again every 10 s while one is missing)
std::atomic<int> g_wristUnitsState{wristlook::kUnitsUnknown};   // what the last ask found (WristUnitsState: the headset panel's "Medallions: look" says "not made")
WristPart g_wristGlass[2], g_wristPlate[2], g_wristLiquid[2];   // the glass and the plate by the medallion's slot, the liquid by its kind
int g_wristLevel[2] = {};                 // the level mesh each liquid shows (1 - kLevels; the log)
unsigned g_wristLevelWaits = 0;           // frames a liquid's level could not be set yet (the log)
const char* WristUnit(int u) {
    switch (u) {
    case 0: return wristorbs::kGlassUnit;
    case 1: return wristorbs::kPlateMetal[0].unit;
    case 4: return wristorbs::kPlateMetal[1].unit;
    case 5: return wristorbs::kPlateMetal[2].unit;
    default: return wristorbs::kLiquidUnit[u - 2];
    }
}
int WristUnitsState() { return g_wristUnitsState.load(); }
void SetWristOrbs(const wristmodels::Want& w, float pulse) {
    AcquireSRWLockExclusive(&g_wristLock);
    g_wristWant = w;
    g_wristPulse = pulse;
    ReleaseSRWLockExclusive(&g_wristLock);
}

void PublishOwnModels() {
    static ULONGLONG at = 0;
    const ULONGLONG now = GetTickCount64();
    if (at && now - at < 200) return;
    at = now;
    std::vector<uint32_t> ents;
    const auto add = [&](void* inst) {
        uint32_t e[64];
        const int n = Entities(inst, e, 64);
        ents.insert(ents.end(), e, e + n);
    };
    for (const Bottle& b : g_bottles) if (!b.dead) add(b.inst.p);
    for (const Loose& l : g_loose) if (!l.dead) add(l.inst.p);
    for (const HeldCopy& c : g_copies) if (!c.dead) add(c.inst.p);
    for (int k = 0; k < 2; ++k)   // ([hud] wrist_game_models: the medallions ride the wrists)
        for (const WristPart* w : {&g_wristGlass[k], &g_wristPlate[k], &g_wristLiquid[k]}) if (w->inst.p && !w->parked) add(w->inst.p);
    for (const RemoteDrawn& r : g_remoteDrawn) if (!r.dead) add(r.inst.p);
    ownmodels::Publish(std::move(ents));
}

// ---- [sky] game_sky: our painted sky as the game's own dome (docs/game_sky_entity.md) ----
// A UnitDefinition of ours (tools/game_sky/make_sky_dome.py: the dome model, UnlitMaterial with our act 1 panorama in BC6H, lightMask 0,
// shadowMask 0) in the mod's data, loaded by the game's own loader and placed as the belt's potions are; then laid every frame with its
// middle at the eye, scaled to [sky] game_sky_radius: the game draws it in its own frame - its stereo, both eyes, the turn warp's depth,
// fire and spells over it - and it follows the hero through every area with nothing written into the areas' data. VR F4, in the world,
// the painted sky wanted ([sky] enabled, outdoors, no menu), act 1 (the first picture); anywhere else laid under the ground at a
// thousandth of its size. While it shows the post pass draws no sky (DomeShown).
// The fog: anything nearer than 10000 of the fog's planes gets the game's fog (ps_atmosphere_apply) and the depth's distance gets ours,
// so at 800 units the dome would be fogged over. At 10,000,000 (the default) it lies past 10000 planes of any plane we use (ours start
// at 150; the game's own at 885) - the game's fog leaves it clear as it leaves the void, and its depth (near / 10^7) reads as the void
// to our passes too. [sky] game_sky_radius=800 shows the other way, for the comparison.
constexpr const char* kDomeUnit = "data/hd/env/model/d2r_vr/sky/sky_dome_act1_unit.json";
// [debug] game_sky_test: the same dome in one bright magenta, no picture of the sky (make_sky_dome.py --test-colour): "not drawn" told from
// "drawn black" (PC1 2026-10-27: nothing seen even at 50 units)
constexpr const char* kDomeUnitTest = "data/hd/env/model/d2r_vr/sky/sky_dome_test_unit.json";
// [debug] game_sky_test 2: a unit of ours naming the game's own Arreat Summit sky model (an unlit HDR model the game draws): drawn through the
// same path - our model file is what fails; not drawn - the placing or the culling does (PC1 2026-10-27 11:16: none of our dome's draws)
constexpr const char* kDomeUnitSummit = "data/hd/env/model/d2r_vr/sky/sky_dome_summit_unit.json";
// loadedAt / placedAt: when (GetTickCount64) the definition was asked and the instance placed; tries: placings that made no model; the
// rest what its log last said
struct Dome { Shared def{}, inst{}; uint32_t root = 0xFFFFFFFFu; bool loadTried = false, parked = true; int test = 0, tries = 0;
              ULONGLONG loadedAt = 0, placedAt = 0, modelAt = 0; std::string told = "?"; float toldR = -1.0f; bool toldPlace = false;
              bool fileMissing = false; ULONGLONG fileCheckedAt = 0, loadAskedAt = 0; std::string fileTold; };   // (its unit's file not there yet, or its model not ours: looked for again - DomeRun)
std::vector<Shared> g_domeOld;   // placings given up on (no model made): laid away, kept until the game is left (LetGoAll)
Dome g_dome;
// [sky] game_dome_caves: the same dome, black, round the eye underground at its radius ([sky] game_dome_caves_radius_m,
// before it a share of [render] model_radius) - Serg (2026-10-27): "мы можем всё заворачивать в чёрную сферу". Past a cave's ceiling rim lay the void; the
// game's fog pass gave it its torch-lit mist whole (a light band behind the fires, PC1 2026-10-27). Inside the dome there is no void: the
// game's fog lands on the dome as on any wall, black on black. Its model at another of the game's developer test paths (Tests/test_cube_02,
// dome_assets.cpp), its texture plain black (make_sky_dome.py --cave).
constexpr const char* kDomeUnitCave = "data/hd/env/model/d2r_vr/sky/sky_dome_cave_unit.json";
Dome g_caveDome;
// Acts 2 - 5 (2026-10-28, Serg: the dome in every act): each act's own sky on its own dome (make_sky_dome.py --act, Settings' models at more of
// the game's unused test paths); g_dome above is act 1's (and the debug tests'). One shown at a time: the area's act; the others put away.
Dome g_actDomes[6];
const char* ActDomeUnit(int act) {
    static const char* const kUnits[6] = {nullptr, nullptr, "data/hd/env/model/d2r_vr/sky/sky_dome_act2_unit.json",
                                          "data/hd/env/model/d2r_vr/sky/sky_dome_act3_unit.json", "data/hd/env/model/d2r_vr/sky/sky_dome_act4_unit.json",
                                          "data/hd/env/model/d2r_vr/sky/sky_dome_act5_unit.json"};
    return act >= 2 && act <= 5 ? kUnits[act] : nullptr;
}
// The head's centre (the view's eye before the eyes' half shift), the camera's place until a view gave one
void DomeCentre(float out[3]) {
    const bool head = g_headOk.load();
    for (int i = 0; i < 3; ++i) out[i] = head ? g_headCentre[i].load() : g_eyeWorld[i].load();
}
// 3: the Summit's unit word for word, but naming OUR magenta model (data/hd/env/model/d2r_vr/sky/sky_dome_test.model) - is it the path?
// 4: the Summit's own unit, the game's own path - and D2R_VR_Settings puts our magenta model there in the mod (overriding the game's file
//    only in our mod): taken there, the game takes models only from paths it knows; not taken, it is the file (PC1 13:13: the card
//    re-meshed at our path made no model, the card itself at its path did)
constexpr const char* kDomeUnitSummitOurs = "data/hd/env/model/d2r_vr/sky/sky_dome_summitours_unit.json";
const char* DomeUnit() {
    switch (g_dome.test) {
    case 1: return kDomeUnitTest;
    case 2: case 4: return kDomeUnitSummit;
    case 3: return kDomeUnitSummitOurs;
    default: return kDomeUnit;
    }
}
// The model the game made for it: its entities' ModelInstance (vr/belt_bottle.h's layout, 3.3.93787) - the resource's file and whether it
// loaded (+0xC8 == 4), its meshes (+0x310 / +0x318; the first's triangles at Mesh +0x158), the visible list's count (+0x2A8), the layer mask
// (+0x00, bit 31 the whole model hidden), its place (+0x290). The draws counted are its first mesh's index count once known.
std::atomic<unsigned> g_domeIndices{skydome::kIndexCount};
unsigned DomeIndexCount() { return g_domeIndices.load(std::memory_order_relaxed); }
// (any placed instance's, [ceiling] game_ceiling's slab too; `indices` takes its first mesh's index count)
std::string ModelState(void* placed, std::atomic<unsigned>* indices) {
    uintptr_t list = 0;
    uint64_t n = 0;
    if (!placed || !::SafeRead(&list, (const uint8_t*)placed + 0x10, 8) || !::SafeRead(&n, (const uint8_t*)placed + 0x18, 8) || !list || n > 64)
        return "the instance's entities not read";
    std::string s;
    for (uint64_t i = 0; i < n; ++i) {
        uint32_t e = 0;
        if (!::SafeRead(&e, (const void*)(list + i * 4), 4)) continue;
        const uintptr_t inst = skel::EntityModelInstance(e);
        if (!inst) continue;
        uintptr_t res = 0, pathAt = 0, meshes = 0;
        uint8_t state = 0;
        uint32_t layers = 0, tri = 0;
        uint64_t nMesh = 0, nVis = 0;
        float at[3] = {};
        char path[97] = "?";
        if (::SafeRead(&res, (const void*)(inst + 0x20), 8) && res > 0x10000) {
            ::SafeRead(&state, (const void*)(res + 0xC8), 1);
            if (::SafeRead(&pathAt, (const void*)(res + 0x08), 8) && pathAt > 0x10000 && ::SafeRead(path, (const void*)pathAt, 96)) path[96] = 0;
            else strcpy(path, "?");
        }
        ::SafeRead(&layers, (const void*)inst, 4);
        ::SafeRead(&meshes, (const void*)(inst + 0x310), 8);
        ::SafeRead(&nMesh, (const void*)(inst + 0x318), 8);
        ::SafeRead(&nVis, (const void*)(inst + 0x2A8), 8);
        ::SafeRead(at, (const void*)(inst + 0x290), sizeof at);
        if (meshes > 0x10000 && nMesh && nMesh < 64) {
            uintptr_t mi = 0, mesh = 0;
            if (::SafeRead(&mi, (const void*)meshes, 8) && mi > 0x10000 && ::SafeRead(&mesh, (const void*)mi, 8) && mesh > 0x10000)
                ::SafeRead(&tri, (const void*)(mesh + 0x158), 4);
        }
        if (tri && tri < 1000000 && indices) indices->store(tri * 3);
        char b[320];
        snprintf(b, sizeof b, "%sentity %X: model %s - %s (state %u), %llu mesh(es) (the first %u triangles), %llu visible, layers %08X%s, at %.1f %.1f %.1f",
                 s.empty() ? "" : "; ", e, path, state == 4 ? "loaded" : "NOT loaded", state, (unsigned long long)nMesh, tri,
                 (unsigned long long)nVis, layers, (layers & 0x80000000u) ? " (hidden)" : "", at[0], at[1], at[2]);
        s += b;
    }
    return s.empty() ? "no entity of it has a model instance (no ModelComponent made: the model not taken)" : s;
}
// (a sky dome's - any act's, one shown at a time - counts its mesh's indices for the 10 s line; the cave dome's not. Act 1's alone did: in
// acts 2 - 5 the line watched the default count, 5952, and said "NONE: the game does not draw it" of a dome drawn - 0.192.5, act 5, flat F2)
std::string DomeModelState(const Dome& d = g_dome) { return ModelState(d.inst.p, &d != &g_caveDome ? &g_domeIndices : nullptr); }
// Any entity of the placed instance has a model instance (its ModelComponent made)
bool HasModel(void* placed) {
    uintptr_t list = 0;
    uint64_t n = 0;
    if (!placed || !::SafeRead(&list, (const uint8_t*)placed + 0x10, 8) || !::SafeRead(&n, (const uint8_t*)placed + 0x18, 8) || !list || n > 64)
        return false;
    for (uint64_t i = 0; i < n; ++i) {
        uint32_t e = 0;
        if (::SafeRead(&e, (const void*)(list + i * 4), 4) && skel::EntityModelInstance(e)) return true;
    }
    return false;
}
bool DomeHasModel(const Dome& d = g_dome) { return HasModel(d.inst.p); }
// A file of the mod's data on disk as text (at most 1 MiB): <mod>\<mod>.mpq\<rel> beside d2rloader\plugins\d2r_vr.ini (the game reads it from
// there, the mod's own); `file` gets its full path
std::string ModDataText(const char* rel, std::wstring* file) {
    std::wstring path = g_iniPath;
    for (int up = 0; up < 3; ++up) {
        const size_t cut = path.find_last_of(L"\\/");
        path = cut == std::wstring::npos ? std::wstring() : path.substr(0, cut);
    }
    const size_t leaf = path.find_last_of(L"\\/");
    const std::wstring mod = leaf == std::wstring::npos ? path : path.substr(leaf + 1);
    *file = path + L"\\" + mod + L".mpq\\";
    for (const char* c = rel; *c; ++c) *file += *c == '/' ? L'\\' : (wchar_t)*c;
    std::string text;
    if (FILE* f = _wfopen(file->c_str(), L"rb")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0 && text.size() < (1u << 20)) text.append(buf, n);
        fclose(f);
    }
    return text;
}
// The domes' atlas as D2R_VR_Settings made it holds each act's zenith in a square of its own (shared/sky_atlas.h HasCap: its header's mark,
// tools/game_sky/make_sky_dome.py): read once - the game loads it once, at its start
bool DomeAtlasHasCap() {
    static const bool cap = [] {
        std::wstring file;
        const std::string head = ModDataText(skyatlas::kAtlas, &file);
        return skyatlas::HasCap(head);
    }();
    return cap;
}
std::atomic<bool> g_domeShown{false};
std::atomic<uint64_t> g_domeFrames{0};   // SkyDomeFrame's runs (DomeHeartbeat)
// Where the domes' frame is (the test PC, 0.209.5, flat, act 2 town: the radius line and then nothing at all - no DomeRun line, no 10 s
// state, no heartbeat, while the frame kept being called: a fault swallowed each frame?). Set as it goes; a fault caught in Work names it.
std::atomic<int> g_domeStep{0};
const char* DomeStepName(int step) {
    switch (step) {
    case 1: return "the frame's start"; case 2: return "why not (DomeWhyNot)"; case 3: return "the radius (dome_past_far)";
    case 4: return "act 1's dome (DomeRun)"; case 5: return "the act's dome (DomeRun)"; case 6: return "the 10 s state line"; case 7: return "the frame's end";
    case 10: return "DomeRun's start"; case 11: return "the unit's file read"; case 12: return "the model's LOD files checked";
    case 13: return "the game asked for the definition (LoadDef)"; case 14: return "the definition's answer"; case 15: return "placing / laying the dome";
    default: return "?";
    }
}
std::atomic<float> g_domeRadius{0.0f};   // the radius it is laid at now (skydome::DrawableRadius)
bool DomeShown() { return g_domeShown.load(std::memory_order_relaxed); }
// The shown act's dome's band in the domes' picture (shared/sky_atlas.h: act - 1), -1: none shown, or a debug dome of its own picture
int DomeBand() {
    const int act = g_fogAct.load();
    return g_domeShown.load(std::memory_order_relaxed) && act >= 1 && act <= 5 && g_dome.test == 0 ? act - 1 : -1;
}
float DomeRadius() { return g_domeRadius.load(std::memory_order_relaxed); }
// Why not shown now ("" = shown), for the log (vr/sky_dome.h)
const char* DomeWhyNot() {
    skydome::In in;
    in.ticked = skypaths::GameDomeWanted(g_set.gameSky.load(), fx::noreshade::FxWaiting());
    // every view of our camera round the world (skydome::ViewWhyNot: not from above, the table, F12, flat without game_drawn_flat) - VR's
    // third person too (Serg: "почему в 103 уровне старое небо", VR F2)
    skydome::ViewIn v;
    v.vr = g_set.platform.load() == 1;
    v.view = ViewNow();
    v.enabled = g_enabled.load();
    v.gameDrawnFlat = g_set.gameDrawnFlat.load();
    v.tableFull = g_set.tableFull.load();
    v.topSky = TopSky();   // ([top] sky: VR F1 through our camera)
    in.viewWhy = skydome::ViewWhyNot(v);
    in.f4 = !*in.viewWhy && (!TopPersp() || TopSky()) && (!TableView() || TableFull());   // ([table] full_width: the table too; [top] sky: F1)
    in.inWorld = g_inWorld.load();
    in.palette = g_skyPalette.load();
    in.act = g_fogAct.load();
    in.eyeOk = g_eyeOk.load();
    in.gameSkybox = g_set.gameSkybox.load() && g_gameSkyboxHere.load();
    return skydome::WhyNot(in);
}
bool DomeRun(Dome& d, const char* unit, const char* why, float radius, const char* tag);
std::atomic<bool> g_caveDomeShown{false};
// The game's sky chosen but unable here for good (vr/sky_paths.h GameSkyCannot; nullptr: it can, or it is on its way): the area's act's dome's
// state as DomeRun left it - read from the present (flags and a pointer, no strings)
const char* SkyCannot() {
    const int act = g_fogAct.load();
    const Dome* d = act == 1 ? &g_dome : act >= 2 && act <= 5 ? &g_actDomes[act] : nullptr;
    skypaths::DomeTry t;
    if (d) {
        t.tried = d->loadTried;
        t.defLoaded = d->def.p != nullptr;
        t.gaveUp = d->tries > 4;
        t.placeFailed = d->toldPlace && !d->inst.p;
    }
    return skypaths::GameSkyCannot(GameDrawnView() || TopSky(), d != nullptr, t, g_set.gameSkybox.load() && g_gameSkyboxHere.load());   // ([top] sky: F1 too)
}
void SkyDomeFrame() {
    g_domeFrames.fetch_add(1, std::memory_order_relaxed);
    g_domeStep.store(1, std::memory_order_relaxed);
    if (g_set.gameSkyTest.load() != g_dome.test) {   // [debug] game_sky_test changed: another unit from now on (the one placed laid away, kept)
        if (g_dome.inst.p) {
            float m[16];
            float eye[3];
            DomeCentre(eye);
            skydome::Matrix(eye, 1.0f, false, m);
            Lay(g_dome.inst.p, g_dome.root, m);
        }
        const int test = g_set.gameSkyTest.load();
        g_dome = Dome{};
        g_dome.test = test;
        g_domeShown.store(false);
        g_domeIndices.store(skydome::kIndexCount);
        LogF("vrcam: game sky - [debug] game_sky_test %d: %s", test,
             test == 4 ? "the Summit's own unit and path, OUR magenta model in the mod at that path (D2R_VR_Settings puts it there)"
             : test == 3 ? "the Summit's unit word for word, naming OUR magenta model at our path"
             : test == 2 ? "the game's own Arreat Summit sky model, placed and laid as the dome"
             : test == 1 ? "the dome in one bright magenta, no sky's picture" : "the act's sky again");
    }
    const bool ticked = skypaths::GameDomeWanted(g_set.gameSky.load(), fx::noreshade::FxWaiting());
    const int act = g_fogAct.load();
    g_domeStep.store(2, std::memory_order_relaxed);
    const char* why = DomeWhyNot();
    g_domeStep.store(3, std::memory_order_relaxed);
    {   // our effect's sky chosen and our effect still being built for this size: nothing meanwhile (skypaths::GameDomeWanted - no stand-in)
        const int meanwhile = !g_set.gameSky.load() && fx::noreshade::FxWaiting() && g_skyPalette.load() >= 0 ? 1 : 0;
        static int told = 0;
        if (meanwhile != told) {
            if (meanwhile) Log("vrcam: the sky WAITS for our effect's build for this size - nothing drawn in its place meanwhile");
            else if (told) Log("vrcam: the sky - our effect is built: its own sky again");
            told = meanwhile;
        }
    }
    // [sky] game_skybox (an option, off by default): at the Summits the dome behind the game's mist - as far as the depth holds it (skydome::SkyboxRadius)
    const bool skybox = g_set.gameSkybox.load() && g_gameSkyboxHere.load();
    // (GameSkyRadiusNow: VR F3's own Draw distance there, [table] draw_distance)
    float radius = skybox ? skydome::SkyboxRadius(GameSkyRadiusNow(), g_lastNear.load()) : GameSkyRadiusNow();
    // [sky] dome_past_far: off VR F4 (the flat game's views, VR F2) the dome past the far end of the game's depth range in every pixel on the
    // screen - nearer, the game's own haze lay on part of it (skydome::PastFarRadius); F4 as set. (0.209.8 laid it inside the range in flat
    // F3, so the game's fog would fade into its low band - but the game's fog does not thin with height (game_fog_height is ruled out), and
    // at 1007 units against our fog full at 140 it covered the whole dome: Serg, flat F3, act 1 at night, "туман закрывает все небо". So
    // flat F3 is past the range again, and the fade into the fog is the dome's low band baked in the fog's colour ([sky] dome_fog_baked,
    // in the texture the game samples, the colour the game's fog draws now) - Serg's "надо красить и запекать".)
    if (g_set.domePastFar.load() && !viewgate::F4()) {
        float proj[4] = {};
        AcquireSRWLockShared(&g_skyLock);
        if (g_skyView[0].proj_ok) memcpy(proj, g_skyView[0].proj, sizeof proj);
        ReleaseSRWLockShared(&g_skyLock);
        const float asked = radius;
        radius = skydome::DrawableRadius(skydome::PastFarRadius(asked, depthsrc::LastFar(), proj), g_lastNear.load());
        static float toldPast = -1.0f;
        if (radius != asked && std::fabs(radius - toldPast) > 50.0f) {
            toldPast = radius;
            LogF("vrcam: game sky - the dome laid %.0f units round, not %.0f: past the game's depth range (its far end %.0f) in every pixel of this "
                 "view, so the game's own haze leaves it all ([sky] dome_past_far; VR F4 as set)", radius, asked, depthsrc::LastFar());
        }
    }
    static bool toldSkybox = false;
    if (skybox && !toldSkybox && !*why)
        LogF("vrcam: game sky - the Arreat Summit: the dome laid %.0f units round, as far as the depth holds (game_sky_radius %.0f elsewhere) - the "
             "game's mist (its emitters along the presets' 210 x 291 units) and the mountain in front of it ([sky] game_skybox)", radius,
             g_set.gameSkyRadius.load());
    toldSkybox = skybox && !*why;
    bool shown = false;
    // act 1's (and the debug tests'); then acts 2 - 5's, each only for its act - one placed in another act is put away
    g_domeStep.store(4, std::memory_order_relaxed);
    if (ticked || !g_dome.parked) shown |= DomeRun(g_dome, DomeUnit(), act == 1 || *why ? why : "another act's dome", radius, "game sky");
    g_domeStep.store(5, std::memory_order_relaxed);
    for (int a = 2; a <= 5; ++a) {
        Dome& d = g_actDomes[a];
        if (a != act && d.parked) continue;   // (never placed, or put away already: nothing to do)
        if (!ticked && d.parked) continue;
        char tag[32];
        snprintf(tag, sizeof tag, "game sky, act %d", a);
        shown |= DomeRun(d, ActDomeUnit(a), a == act || *why ? why : "another act's dome", radius, tag);
    }
    g_domeShown.store(shown);
    g_domeStep.store(6, std::memory_order_relaxed);
    {   // [sky] game_sky chosen and no dome shown: every 10 s what each dome's state is - so a dome never stays away without a line (the test PC,
        // 0.209.3, flat, act 2 town: no line at all after the radius one)
        static ULONGLONG toldAt = 0;
        const ULONGLONG now = GetTickCount64();
        if (ticked && !shown && g_inWorld.load() && now - toldAt >= 10000) {
            toldAt = now;
            auto state = [](const Dome& x) {
                char b[160];
                snprintf(b, sizeof b, "%s%s%s%s%s", x.parked ? "put away" : "laid", x.loadTried ? ", asked" : ", never asked",
                         x.def.p ? ", loaded" : ", not loaded", x.fileMissing ? ", files not right: " : "", x.fileMissing ? x.fileTold.c_str() : "");
                return std::string(b);
            };
            LogF("vrcam: game sky - no dome shown, 10 s: act %d, why \"%s\"; act 1's dome: %s; act %d's: %s", act, *why ? why : "(wanted)",
                 state(g_dome).c_str(), act >= 2 && act <= 5 ? act : 0, act >= 2 && act <= 5 ? state(g_actDomes[act]).c_str() : "-");
        }
    }
    g_domeStep.store(7, std::memory_order_relaxed);
}

// One dome a frame: its unit loaded once, placed (again, while no model came of it), laid at the eye at `radius` while `why` is "" - put
// away, small under the ground, while not; true while shown. `tag` heads its log lines.
bool DomeRun(Dome& d, const char* unit, const char* why, float radius, const char* tag) {
    g_domeStep.store(10, std::memory_order_relaxed);
    if (!unit || !why || !tag) {   // (never: the act's unit path missing - told, nothing done)
        static bool told = false;
        if (!told) { told = true; LogF("vrcam: game sky - DomeRun given no %s - no dome", !unit ? "unit" : !why ? "reason" : "tag"); }
        return false;
    }
    const bool want = !*why;
    // The definition, once a run - and only a file that holds what the game needs (vr/sky_dome.h). Its file not there (yet): looked for again
    // every 3 s - Serg on 0.192.1: "Sky and fog drawn by" set to the game while it ran, the Settings window made the files a moment (or
    // minutes) later, and the dome was never asked again: our old sky all along.
    const ULONGLONG checkMs = GetTickCount64();
    if (want && (!d.loadTried || (d.fileMissing && !d.def.p && checkMs - d.fileCheckedAt >= 3000))) {
        const bool again = d.loadTried;
        d.loadTried = true;
        d.fileCheckedAt = checkMs;
        std::wstring file;
        g_domeStep.store(11, std::memory_order_relaxed);
        const std::string text = ModDataText(unit, &file);
        g_domeStep.store(12, std::memory_order_relaxed);
        // ... and the model it names, every LOD file, ours (vr/sky_dome.h ModelFileProblem): another file there is what the game draws as the dome
        std::string modelBad;
        const std::string name = skydome::DomeName(unit);
        if (!skydome::UnitFileProblem(text) && skydome::ChecksModel(name)) {
            const std::string model = skydome::UnitModel(text);
            for (int lod = 0; lod < 5 && modelBad.empty(); ++lod) {
                std::wstring lodFile;
                const std::string rel = skydome::LodFile(model, lod);
                const char* bad = rel.empty() ? "the unit names no model file" : skydome::ModelFileProblem(ModDataText(rel.c_str(), &lodFile), name);
                if (bad) modelBad = (rel.empty() ? model : Utf8(lodFile.c_str())) + " - " + bad;
            }
        }
        if (const char* bad = skydome::UnitFileProblem(text)) {
            if (d.fileTold != bad)
                LogF("vrcam: %s - the dome's unit NOT given to the game: %s - %s (the Settings window makes it); looked for again every 3 s", tag,
                     Utf8(file.c_str()).c_str(), bad);
            d.fileTold = bad;
            d.fileMissing = true;
        } else if (!modelBad.empty()) {
            if (d.fileTold != modelBad)
                LogF("vrcam: %s - the dome's unit NOT given to the game: its model %s - no dome (our effect's sky instead); looked for again every 3 s",
                     tag, modelBad.c_str());
            d.fileTold = modelBad;
            d.fileMissing = true;
        } else if ((LogF("vrcam: %s - the dome's files check out (%s and its model's five LOD files ours): asking the game for its definition "
                         "now (a line follows: loaded, or did NOT load; none means the game's loader never came back)", tag, unit),
                    d.loadAskedAt = GetTickCount64(), g_domeStep.store(13, std::memory_order_relaxed), LoadDef(unit, &d.def)) && d.def.p) {
            d.fileMissing = false;
            d.loadedAt = GetTickCount64();
            LogF("vrcam: %s - the dome's definition loaded%s in %llu ms: %s (one unit root; the model file asks UnlitMaterial - models_unlit_p -, "
                 "lightMask 0, shadowMask 0; whether the game drew it: the 10 s line's draws of its mesh)", tag, again ? " (its file there now)" : "",
                 (unsigned long long)(d.loadedAt - d.loadAskedAt), unit);
        } else {
            d.fileMissing = false;
            d.def = {};
            LogF("vrcam: %s - the dome's definition did NOT load (%s): the game refused it - no dome%s", tag, unit,
                 again ? " (its files were written after the game started: the game may take the mod's files only at its start - restart the game)"
                       : "");
        }
    }
    g_domeStep.store(14, std::memory_order_relaxed);
    if (!d.def.p) {   // (never loaded: wanted nowhere yet - the reason told all the same, once a change)
        if (std::string(why) != d.told) {
            d.told = why;
            if (*why) LogF("vrcam: %s - no dome: %s", tag, why);
        }
        return false;
    }
    g_domeStep.store(15, std::memory_order_relaxed);
    // its middle at the head's centre, between the eyes (not one eye's camera: the game's Summit card laid at the camera stood in one eye
    // only - PC1 2026-10-27, real stereo; a dome 50+ units round differs by a hair, but the middle is the right place)
    float eye[3];
    DomeCentre(eye);
    const ULONGLONG nowMs = GetTickCount64();
    // A placing whose entities got no model (PC1 2026-10-27 11:43: "no entity of it has a model instance", the definition asked and the
    // instance placed in the same frame - the model file not loaded yet; the belt's potions' models are the game's, loaded already): the
    // first placing waits 2 s after the definition; one with no model 3 s on is laid away and placed again, 4 times at most.
    if (want && d.inst.p && !DomeHasModel(d) && nowMs - d.placedAt > 3000) {
        if (d.tries < 4) {
            float away[16];
            skydome::Matrix(eye, 1.0f, false, away);
            Lay(d.inst.p, d.root, away);
            g_domeOld.push_back(d.inst);
            ++d.tries;
            LogF("vrcam: %s - the dome's placing %d made no model in 3 s: laid away, placed again (its definition asked %.1f s ago)", tag, d.tries,
                 (nowMs - d.loadedAt) / 1000.0);
            d.inst = {};
            d.root = 0xFFFFFFFFu;
        } else if (d.tries == 4) {
            ++d.tries;
            LogF("vrcam: %s - the dome made no model in 5 placings over 15 s: the game does not take its model file (the unit's model entry or the "
                 "file itself) - placed no more", tag);
        }
    }
    if (want && !d.inst.p && d.tries <= 4 && nowMs - d.loadedAt < 2000) return false;   // (its files loading)
    if (want && !d.inst.p && d.tries <= 4) {   // placed (again after an area change took its entity, or after a placing with no model)
        if (PlaceAt(d.def.p, eye, &d.inst)) {
            d.root = RootOf(d.inst.p);
            d.parked = false;
            d.placedAt = nowMs;
            LogF("vrcam: %s - the dome placed (instance %p, root entity %X) at the eye %.1f %.1f %.1f, %.1f s after its definition was asked", tag,
                 d.inst.p, d.root, eye[0], eye[1], eye[2], (nowMs - d.loadedAt) / 1000.0);
        } else {
            if (!d.toldPlace) { d.toldPlace = true; LogF("vrcam: %s - PlaceInstance gave nothing for the dome - no dome", tag); }
            return false;
        }
    }
    if (!d.inst.p) return false;
    if (d.root == 0xFFFFFFFFu) d.root = RootOf(d.inst.p);
    // the radius exactly as set (the Settings slider, live): nothing caps it - Serg looks for where it goes (2026-10-27)
    const float r = radius, k = want ? r / skydome::kModelRadius : 0.001f;
    if (&d != &g_caveDome && want) g_domeRadius.store(r);   // (a sky dome's: the post pass leaves its depth band)
    if (want && r != d.toldR) {
        d.toldR = r;
        const float nearPlane = g_lastNear.load(), steps = nearPlane > 0.0f ? nearPlane / r * 16777216.0f : 0.0f;
        LogF("vrcam: %s - the dome's radius %.0f units (as set); the game's projection: near %.3f, no far plane "
             "(reversed-Z infinite); its post passes' depth range ends at %.1f; the 24-bit depth holds the dome to %.0f units - at %.0f it is %.1f "
             "of its steps above 0%s", tag, r, nearPlane, depthsrc::LastFar(), skydome::DrawableRadius(1.0e9f, nearPlane), r, steps,
             steps < 1.0f ? " (under one: it rounds to the far plane's 0 and is not drawn)" : "");
    }
    float m[16];
    skydome::Matrix(eye, r, want, m);
    if (!Lay(d.inst.p, d.root, m)) {   // its entity is gone (an area change): let go, placed again next frame
        LogF("vrcam: %s - the dome's entity is gone (an area change): let go, placed again", tag);
        ReleaseInstance(&d.inst, &g_areaTally);
        d.inst = {};
        d.root = 0xFFFFFFFFu;
        return false;
    }
    d.parked = !want;
    if (std::string(why) != d.told) {
        d.told = why;
        if (want) {
            float world[16] = {};
            uintptr_t comp = 0;
            const bool read = skel::EntityTransform(d.root, &comp) && ::SafeRead(world, (const void*)(comp + 0x40), sizeof world);
            const float nearPlane = g_lastNear.load();
            // every entity of the instance: the model's own world must follow the root's (a model left at the origin is drawn nowhere near)
            std::string ents;
            uintptr_t list = 0;
            uint64_t n = 0;
            if (::SafeRead(&list, (const uint8_t*)d.inst.p + 0x10, 8) && ::SafeRead(&n, (const uint8_t*)d.inst.p + 0x18, 8) && list && n <= 64)
                for (uint64_t i = 0; i < n; ++i) {
                    uint32_t e = 0;
                    uintptr_t c = 0;
                    float w[16] = {};
                    char b[160];
                    if (!::SafeRead(&e, (const void*)(list + i * 4), 4)) continue;
                    const bool ok = skel::EntityTransform(e, &c) && ::SafeRead(w, (const void*)(c + 0x40), sizeof w);
                    snprintf(b, sizeof b, "%s entity %X%s: %s %.1f %.1f %.1f scale %.2f", ents.empty() ? "" : ";", e, e == d.root ? " (root)" : "",
                             ok ? "world" : "no transform", w[12], w[13], w[14], w[0]);
                    ents += b;
                }
            LogF("vrcam: %s - the dome's entities (%llu): %s", tag, (unsigned long long)n, ents.empty() ? "none read" : ents.c_str());
            LogF("vrcam: %s - the dome's model: %s", tag, DomeModelState(d).c_str());
            LogF("vrcam: %s - the dome shown: laid at the eye every frame, radius %.0f units (scale %.0f; asked %.0f, the 24-bit depth holds "
                 "%.0f at near %.3f: %.0f of its steps above the far plane; the game's projection is reversed-Z with no far plane - depth_fix_mode 2 "
                 "checks it); the game's world for it %s %.1f %.1f %.1f, scale %.1f; our post pass's sky stands aside, our fog leaves the dome's "
                 "depth band (from %.0f units on); the game's own fog (game_fog) draws over it as over all else", tag, r, k, radius, skydome::DrawableRadius(1.0e9f, nearPlane), nearPlane,
                 nearPlane > 0.0f ? nearPlane / r * 16777216.0f : 0.0f, read ? "read" : "NOT read", world[12], world[13], world[14], world[0],
                 r * skydome::kDomeBand);
        } else {
            LogF("vrcam: %s - the dome put away (under the ground, small): %s", tag, why);
        }
    }
    const ULONGLONG now = GetTickCount64();   // its model as the game has it, every 10 s while shown (it may load after the placing)
    if (want && now - d.modelAt >= 10000) {
        d.modelAt = now;
        LogF("vrcam: %s - the dome's model, 10 s: %s", tag, DomeModelState(d).c_str());
    }
    return want;
}

// [sky] game_dome_caves: the black dome underground (g_caveDome above)
void CaveDomeFrame() {
    const bool ticked = g_set.gameDomeCaves.load();
    if (!ticked && g_caveDome.parked) { g_caveDomeShown.store(false); return; }
    skydome::CaveIn in;
    in.ticked = ticked;
    in.f4 = GameDrawnView();   // (VR F4, or the flat game with [render] game_drawn_flat)
    in.inWorld = g_inWorld.load();
    in.underground = g_underground.load();
    in.eyeOk = g_eyeOk.load();
    const float r = skydome::CaveRadius(g_set.gameDomeCavesRadius.load());
    g_caveDomeShown.store(DomeRun(g_caveDome, kDomeUnitCave, skydome::CaveWhyNot(in), r, "cave dome"));
}

// ---- [ceiling] game_ceiling: the cave ceiling as the game's own lit rock slab (shared/game_ceiling.h, docs/game_ceiling.md) ----
// The unit D2R_VR_Settings writes (the slab's model at one of the game's own test models' paths, the floor's tiling rock as its maps,
// lightMask 19 as the caves' own pieces), loaded and placed as the dome is, then laid every frame with its top over the area's floor and its
// middle on the whole tile nearest the hero - the rock stays put in the world as he walks. The game draws it lit by its torches and the
// hero's light, fogged by its fog, fire and spells in their own order. VR F4 where a ceiling is wanted (fx::CeilWanted: [ceiling] enabled,
// first person, an area on [ceiling] biomes, no menu); anywhere else laid under the ground at a thousandth of its size. While it shows
// the post pass draws no ceiling (CeilingShown).
// [ceiling] game_ceiling_wall: the slab's rim bent down - a ring of the same rock (its own unit and model) laid with it, its middle at the
// hero, from the slab's top down to [ceiling] floor_depth under the area's floor: the void past the slab's edge closed. Live: placed or put
// away with the tick (D2R_VR_Settings makes its files with the slab's).
// Every room its own kind (shared/game_ceiling.h kKinds: a cave's rock, a crypt's stone, the barracks' planks...): a slab - and for a cave
// its wall - per kind, each its own unit and model; the area's kind laid, every other kind's put away.
// [ceiling] game_ceiling_beams: in the barracks the game's own wooden ceiling frame (one unit: a grid of whole frames) laid 2 x 2 round the
// hero at its own size, on whole frames, its bottom at the ceiling's height; the slab goes up by the frame's depth. [ceiling]
// height_from_walls: that height the area's walls' top (shared/ceiling_measures.h, read at each area change).
// One piece (a kind's slab or wall): its unit, its definition and instance, its placings, what the log was last told
struct Slab { std::string what, unit, model; std::atomic<unsigned>* indices = nullptr;
              // the log's words for it ([holes] patch_world's pieces: their own)
              const char* tag = "game ceiling", *madeWhile = "[ceiling] game_ceiling is the game's",
                          *modelNote = " - the game's lit rock model with this kind's own terrain maps, lightMask 19, shadowMask 0";
              Shared def{}, inst{}; uint32_t root = 0xFFFFFFFFu; bool loadTried = false, parked = true; int tries = 0;
              ULONGLONG loadedAt = 0, placedAt = 0, modelAt = 0; std::string told = "?"; };
std::atomic<unsigned> g_slabIndices[gameceiling::kKindCount], g_wallIndices[gameceiling::kKindCount];
Slab g_slabs[gameceiling::kKindCount], g_walls[gameceiling::kKindCount];
// [ceiling] game_ceiling_beams: the barracks' own frame in a grid (one unit), laid 2 x 2 round the hero under the slab
std::atomic<unsigned> g_beamIndices{0};   // (its mesh's, as the game has it: ModelState's)
Slab g_beams[gameceiling::kBeamPieces];
// [ceiling] game_ceiling_dome: the cathedral's vault (one unit: the slab over its preset with the chapel's dome in it), laid world-fixed where
// the game's own pillars stand round the altar's canopy, in place of the cathedral's slab. The canopy is found among the renderer's pieces by
// its place in the preset and a pillar's beside it (knockprops::PresetPieces, a whole pass over the transforms: on a thread of its own), once
// an area - again every 2 s while not found, kChapelTries times.
// [ceiling] game_ceiling_worldstone_dome: the same for every dome over a tall piece (ceilmeasure::DomeOf, kTallFactor), each its own vault
// (slot); an area may hold more than one (the Throne of Destruction's biome: the throne room's arch, the Worldstone Chamber's stone) - all
// of the area's are looked for at once, the one whose preset is among the game's pieces laid (the nearest the hero if more), and while the
// area has more than one they are looked for again every 5 s (the same biome over two levels: no area change between them).
Slab g_vaults[gameceiling::kVaultSlots];
std::atomic<unsigned> g_vaultIndices[gameceiling::kVaultSlots];
constexpr int kChapelTries = 15;
struct ChapelFound {
    unsigned gen = ~0u; bool read = false; int matches = 0; size_t pieces = 0; float world[3] = {}; double ms = 0.0;
    ceilmeasure::Dome dome;   // the one found (matches 1), else the last asked
    int asked = 0;            // how many domes were looked for
};
SRWLOCK g_chapelLock = SRWLOCK_INIT;
ChapelFound g_chapelFound;   // (under g_chapelLock)
std::atomic<bool> g_chapelBusy{false};
struct ChapelAsk { unsigned gen; std::vector<ceilmeasure::Dome> domes; float hero[3]; };
DWORD WINAPI ChapelThread(void* arg) {
    const ChapelAsk* ask = static_cast<const ChapelAsk*>(arg);
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    std::vector<float> places;   // each dome's anchor and witness
    for (const ceilmeasure::Dome& d : ask->domes)
        places.insert(places.end(), {d.anchor[0], d.anchor[1], d.anchor[2], d.witness[0], d.witness[1], d.witness[2]});
    std::vector<knockprops::PresetPiece> got;
    ChapelFound r;
    r.gen = ask->gen;
    r.asked = (int)ask->domes.size();
    if (!ask->domes.empty()) r.dome = ask->domes.back();
    r.read = knockprops::PresetPieces(reinterpret_cast<const float(*)[3]>(places.data()), (int)(places.size() / 3), &got);
    std::vector<ceilmeasure::Piece> pieces;
    for (const knockprops::PresetPiece& p : got) {
        ceilmeasure::Piece c;
        c.parent = p.parent;
        std::memcpy(c.local, p.local, sizeof c.local);
        std::memcpy(c.world, p.world, sizeof c.world);
        pieces.push_back(c);
    }
    r.pieces = pieces.size();
    float best = 1e30f;
    for (const ceilmeasure::Dome& d : ask->domes) {   // the domes found once each: the nearest the hero
        float world[3];
        const int n = ceilmeasure::MatchPreset(pieces, d.anchor, d.witness, world);
        if (n != 1) { if (best >= 1e30f) r.matches = (std::max)(r.matches, n); continue; }
        const float dist = std::hypot(world[0] + d.centreX - d.anchor[0] - ask->hero[0], world[2] + d.centreZ - d.anchor[2] - ask->hero[2]);
        if (dist < best) { best = dist; r.matches = 1; r.dome = d; std::memcpy(r.world, world, sizeof world); }
    }
    QueryPerformanceCounter(&t1);
    r.ms = 1000.0 * (double)(t1.QuadPart - t0.QuadPart) / (double)f.QuadPart;
    AcquireSRWLockExclusive(&g_chapelLock); g_chapelFound = r; ReleaseSRWLockExclusive(&g_chapelLock);
    delete ask;
    g_chapelBusy.store(false);
    return 0;
}
std::atomic<bool> g_slabGivenUp[gameceiling::kKindCount];
// (named once, on the first frame: the table's)
void NamePieces() {
    static bool named = false;
    if (named) return;
    named = true;
    for (int k = 0; k < gameceiling::kKindCount; ++k) {
        const gameceiling::Kind& kind = gameceiling::kKinds[k];
        g_slabs[k].what = std::string(kind.name) + " ceiling";
        g_slabs[k].unit = gameceiling::UnitPath(k, false);
        g_slabs[k].model = gameceiling::ModelPath(kind.slab);
        g_slabs[k].indices = &g_slabIndices[k];
        g_slabIndices[k].store(gameceiling::kIndexCount);
        g_walls[k].what = std::string(kind.name) + " wall";
        g_walls[k].unit = gameceiling::UnitPath(k, true);
        g_walls[k].model = kind.wall ? gameceiling::ModelPath(kind.wall) : std::string();
        g_walls[k].indices = &g_wallIndices[k];
        g_wallIndices[k].store(gameceiling::kWallIndexCount);
    }
    for (int i = 0; i < gameceiling::kBeamPieces; ++i) {
        g_beams[i].what = "barracks beams " + std::to_string(i + 1);
        g_beams[i].unit = gameceiling::kBeamUnit;
        g_beams[i].model = gameceiling::ModelPath(gameceiling::kBeamModelFile);
        g_beams[i].indices = &g_beamIndices;
    }
    for (int i = 0; i < gameceiling::kVaultSlots; ++i) {
        g_vaults[i].what = i == 0 ? std::string("cathedral vault") : "tall piece's vault " + std::to_string(i);
        g_vaults[i].unit = gameceiling::kVaultUnits[i];
        g_vaults[i].model = gameceiling::ModelPath(gameceiling::kVaultModelFiles[i]);
        g_vaults[i].indices = &g_vaultIndices[i];
    }
}
std::vector<Shared> g_slabOld;   // placings given up on (no model made): laid away, kept - the game's
std::atomic<bool> g_slabShown{false};
std::atomic<int> g_gameCeilKind{-1};   // the area's kind (-1: none - not on [ceiling] game_ceiling_biomes, or no fitting model)
bool CeilingShown() { return g_slabShown.load(std::memory_order_relaxed); }
std::atomic<float> g_slabLowestY{0.0f};   // CeilingFrame: the game's slab's lowest, world y (set while it is wanted)
bool CeilingByGame();
// [body] leap_camera_up_pct (vr/under_camera.h Headroom): the lowest of the ceiling drawn over the hero now, world y. The game's rock slab
// ([ceiling] game_ceiling): its top less its rock's hang, the barracks' frame under it, over the area's floor (CeilingFrame); else our vault
// (the post pass's, [ceiling] enabled in a ceiling area): its height less [ceiling] relief (the rock hanging down), over the hero's ground
// (`groundY`). False: no ceiling drawn here (outdoors, [ceiling] off, not first person) - the leap's camera is not held.
bool CeilingLowestHere(float groundY, float* lowestY) {
    if (CeilingByGame()) {
        if (!fx::GameCeilWanted()) return false;
        *lowestY = g_slabLowestY.load(std::memory_order_relaxed);
        return std::isfinite(*lowestY);
    }
    if (!fx::CeilWanted()) return false;
    float height = 0.0f, bright = 0.0f, light = 0.0f, relief = 0.0f;
    CeilingNow(&height, &bright, &light, &relief);
    *lowestY = groundY + height - relief;
    return std::isfinite(*lowestY);
}
// The ceiling is the game's: [ceiling] game_ceiling, VR F4, an area on [ceiling] game_ceiling_biomes with a kind of its own, and that kind's
// slab not given up on (its unit refused or missing, 5 placings with no model, PlaceInstance giving nothing, the game's functions or our hook
// not as known) - whether or not it is laid yet. The post pass's ceiling stays off on these terms from the area's first frame: while the
// slab's model loads (seconds at the first such area of a run, a frame after an area change) nothing is drawn rather than our old vault for
// a moment (Serg, test-0186: the old ceiling first, then the new one). Elsewhere - a room with no fitting kind, or its kind given up on - our
// vault with the room's own settings.
bool CeilingByGame() {
    const int kind = g_gameCeilKind.load(std::memory_order_relaxed);
    return gameceiling::ByGame(g_set.gameCeiling.load(), GameDrawnView(), fx::GameCeilWanted() && kind >= 0,
                               kind < 0 || g_slabGivenUp[kind].load(std::memory_order_relaxed), g_hook.load() < 0);
}
// A piece's frame: its definition loaded once a run, placed (again after an area change, or after a placing that made no model), laid at
// `m` when wanted, at `away` when not. True: shown this frame.
bool PieceFrame(Slab& p, bool want, const char* why, const float hero[3], const float m[16], const float away[16]) {
    if (want && !p.loadTried) {   // the definition, once a run - and only a file that holds what the game needs
        p.loadTried = true;
        std::wstring file;
        const std::string text = ModDataText(p.unit.c_str(), &file);
        if (const char* bad = skydome::UnitFileProblem(text)) {
            LogF("vrcam: %s - the %s's unit NOT given to the game: %s - %s (D2R_VR_Settings makes it while %s)%s", p.tag, p.what.c_str(),
                 Utf8(file.c_str()).c_str(), bad, p.madeWhile, p.indices >= g_slabIndices && p.indices < g_slabIndices + gameceiling::kKindCount
                 ? "; our own vault in this room" : "");
        } else if (LoadDef(p.unit.c_str(), &p.def) && p.def.p) {
            p.loadedAt = GetTickCount64();
            LogF("vrcam: %s - the %s's definition loaded: %s (one unit root; the model %s%s)", p.tag, p.what.c_str(), p.unit.c_str(), p.model.c_str(),
                 p.modelNote);
        } else {
            p.def = {};
            LogF("vrcam: %s - the %s's definition did NOT load (%s): the game refused it", p.tag, p.what.c_str(), p.unit.c_str());
        }
    }
    if (!p.def.p) return false;
    const ULONGLONG nowMs = GetTickCount64();
    // a placing whose entities got no model in 3 s (its file still loading): laid away and placed again, 4 times at most (as the dome's)
    if (want && p.inst.p && !HasModel(p.inst.p) && nowMs - p.placedAt > 3000) {
        if (p.tries < 4) {
            Lay(p.inst.p, p.root, away);
            g_slabOld.push_back(p.inst);
            ++p.tries;
            LogF("vrcam: %s - the %s's placing %d made no model in 3 s: laid away, placed again (its definition asked %.1f s ago)", p.tag, p.what.c_str(),
                 p.tries, (nowMs - p.loadedAt) / 1000.0);
            p.inst = {};
            p.root = 0xFFFFFFFFu;
        } else if (p.tries == 4) {
            ++p.tries;
            LogF("vrcam: %s - the %s made no model in 5 placings over 15 s: the game does not take its model file - placed no more", p.tag, p.what.c_str());
        }
    }
    if (want && !p.inst.p && p.tries <= 4 && nowMs - p.loadedAt < 2000) return false;   // (its files loading)
    if (want && !p.inst.p && p.tries <= 4) {   // placed (again after an area change took its entity, or after a placing with no model)
        if (PlaceAt(p.def.p, hero, &p.inst)) {
            p.root = RootOf(p.inst.p);
            p.parked = false;
            p.placedAt = nowMs;
            LogF("vrcam: %s - the %s placed (instance %p, root entity %X) at the hero %.1f %.1f %.1f", p.tag, p.what.c_str(), p.inst.p, p.root, hero[0], hero[1], hero[2]);
        } else {
            LogF("vrcam: %s - PlaceInstance gave nothing for the %s - not asked again this run", p.tag, p.what.c_str());
            p.tries = 5;
            return false;
        }
    }
    if (!p.inst.p) return false;
    if (p.root == 0xFFFFFFFFu) p.root = RootOf(p.inst.p);
    if (!Lay(p.inst.p, p.root, want ? m : away)) {   // its entity is gone (an area change): let go, placed again next frame
        LogF("vrcam: %s - the %s's entity is gone (an area change): let go, placed again", p.tag, p.what.c_str());
        ReleaseInstance(&p.inst, &g_areaTally);
        p.inst = {};
        p.root = 0xFFFFFFFFu;
        return false;
    }
    p.parked = !want;
    if (std::string(why) != p.told) {
        p.told = why;
        if (want) LogF("vrcam: %s - the %s shown at %.1f %.1f %.1f, scale %.2f round / %.2f down; its model: %s", p.tag, p.what.c_str(), m[12], m[13], m[14],
                       m[0], m[5], ModelState(p.inst.p, p.indices).c_str());
        else LogF("vrcam: %s - the %s put away (under the ground, small): %s", p.tag, p.what.c_str(), why);
    }
    if (want && nowMs - p.modelAt >= 10000) {   // its model as the game has it, every 10 s while shown (it may load after the placing)
        p.modelAt = nowMs;
        LogF("vrcam: %s - the %s's model, 10 s: %s", p.tag, p.what.c_str(), ModelState(p.inst.p, p.indices).c_str());
    }
    return want;
}
void CeilingFrame() {
    NamePieces();
    gameceiling::In in;
    in.ticked = g_set.gameCeiling.load();
    if (!in.ticked) {   // never wanted (or every piece put away): nothing to do
        bool parked = true;
        for (int k = 0; k < gameceiling::kKindCount; ++k) parked = parked && g_slabs[k].parked && g_walls[k].parked;
        for (const Slab& b : g_beams) parked = parked && b.parked;
        for (const Slab& v : g_vaults) parked = parked && v.parked;
        if (parked) { g_slabShown.store(false); return; }
    }
    const int kind = g_gameCeilKind.load();
    in.f4 = GameDrawnView();   // (VR F4, or the flat game with [render] game_drawn_flat)
    in.wanted = fx::GameCeilWanted() && kind >= 0;   // (an area on [ceiling] game_ceiling_biomes with a kind of its own)
    in.heroOk = g_eyeOk.load();
    const char* why = gameceiling::WhyNot(in);
    const bool want = !*why, wallTick = g_set.gameCeilingWall.load();
    const bool wall = want && wallTick && gameceiling::kKinds[kind].wall;   // (rooms have walls of their own)
    const float hero[3] = {g_heroPosNow[0], g_heroPosNow[1], g_heroPosNow[2]};
    // its top: the area's own height_<biome> over the area's floor (the lowest the hero stood since the area changed); a tile: the post ceiling's picture, 4 of [ceiling] scale's tiles; the wall's reach down to [ceiling] floor_depth under
    // the floor - all live
    static float floorY = 0.0f;
    static unsigned floorGen = ~0u;
    const float floorAt = gameceiling::Floor(&floorY, &floorGen, g_biomeGen.load(), hero[1]);
    float areaHeight, bright, light;
    float ownHeight = 0.0f;
    CeilingNow(&areaHeight, &bright, &light, nullptr, nullptr, &ownHeight);
    // the room's own height_<biome> when set; else [ceiling] height_from_walls: on the area's walls' top (a rock that hangs lower rests its
    // lowest on them); else 18
    const float tile = gameceiling::TileWorld(g_set.ceilScale.load());
    const float walls = g_set.ceilFromWalls.load() && ownHeight <= 0.0f ? g_wallsTopHere.load() : 0.0f;
    const float roomHeight = gameceiling::HeightOf(ownHeight, g_set.ceilFromWalls.load(), g_wallsTopHere.load(),
                                                   kind >= 0 ? gameceiling::kKinds[kind].relief : 0.0f, tile / gameceiling::kTile);
    // [ceiling] min_headroom_m (Serg 2026-11-10, Den of Evil: the rock at his eyes): the slab's lowest - its top less its rock's hang - never
    // under the eyes + that many metres; what that needs is held for the area (only ever up, in half-unit steps: no breathing, no sliding)
    const float hang = kind >= 0 ? gameceiling::kKinds[kind].relief * tile / gameceiling::kTile : 0.0f;
    static gameceiling::HeadroomHold headroom;
    const float headroomHeld = gameceiling::HeadroomHeld(&headroom, g_biomeGen.load(), want ? CeilingHeadroomNeed(hero[1] - floorAt, hang) : 0.0f);
    const float height = gameceiling::Guarded(roomHeight, headroomHeld);
    {
        static float toldHeld = -1.0f;
        if (want && height > roomHeight && headroomHeld != toldHeld) {
            toldHeld = headroomHeld;
            LogF("vrcam: game ceiling - raised for headroom: its top %.1f over the area's floor, not the room's %.1f - its lowest (%.1f of rock "
                 "hanging) kept %.2f m over the eyes ([ceiling] min_headroom_m; the hero %.1f over the floor it stands on, his eyes %.1f + the "
                 "head %.1f over them)", height, roomHeight, hang, g_set.ceilHeadroomM.load(), hero[1] - floorAt, ViewHeight(),
                 g_headOverStand.load());
        }
    }
    const float reach = gameceiling::WallReach(height, g_set.ceilFloor.load());
    // [ceiling] game_ceiling_beams: the barracks' frame with its bottom at that height, the slab over it by the frame's depth
    ceilmeasure::Beam beam;
    AcquireSRWLockShared(&g_biomeLock); beam = g_beam; ReleaseSRWLockShared(&g_biomeLock);
    const bool beamTick = g_set.gameCeilingBeams.load(), beams = want && beamTick && g_beamsHere.load();
    const char* beamWhy = !want ? why : !beamTick ? "[ceiling] game_ceiling_beams=0" : !beams ? "not the barracks, or their frame not measured (D2R_VR_Settings)" : "";
    const float slabTop = beams ? gameceiling::BeamSlabHeight(height, beam.bottom, beam.top) : height;
    static float toldHeight = -1.0f, toldTile = -1.0f, toldReach = -1.0f, toldTop = -1.0f;
    static int toldKind = -2;
    if (want && (height != toldHeight || tile != toldTile || (wall ? reach : -1.0f) != toldReach || kind != toldKind || slabTop != toldTop)) {
        toldHeight = height;
        toldTile = tile;
        toldReach = wall ? reach : -1.0f;
        toldKind = kind;
        toldTop = slabTop;
        LogF("vrcam: game ceiling - the %s ceiling (%s): its top %.1f units over the area's floor (%s), %s; %.0f units a tile (4 x [ceiling] "
             "scale), %.0f units across", gameceiling::kKinds[kind].name, gameceiling::kKinds[kind].maps, slabTop,
             walls > 0.0f ? "on the area's walls' top - the game's own pieces, [ceiling] height_from_walls"
                          : "the area's height_<biome>",
             gameceiling::kKinds[kind].relief > 0.0f ? "its rock hanging lower" : "flat", tile, 2.0f * gameceiling::kTilesHalf * tile);
        if (beams)
            LogF("vrcam: game ceiling - the barracks' own beams under it: %d x 2 x 2 frames of %.1f x %.1f units (the game's frame at its own "
                 "size), their bottom at %.1f, the slab %.2f higher (the frame's depth)", beam.cols * beam.rows, beam.periodX, beam.periodZ, height,
                 beam.top - beam.bottom);
        if (wall)
            LogF("vrcam: game ceiling - its rim bent down: the wall %.0f units round the hero, %.1f units down from the ceiling's top ([ceiling] "
                 "floor_depth %.1f under the floor)", gameceiling::kWallRadius * tile / gameceiling::kTile, reach, g_set.ceilFloor.load());
    }
    // [ceiling] game_ceiling_dome / game_ceiling_worldstone_dome: a vault - once its piece is found among the game's pieces - in place of the
    // slab, its flat at the slab's top, its dome raised to pass over the piece's top as drawn. The area's domes the keys allow: the
    // cathedral's (slot 0) by game_ceiling_dome, the Worldstone's by game_ceiling_worldstone_dome
    const bool domeTick = g_set.gameCeilingDome.load(), tallTick = g_set.gameCeilingDomesTall.load();
    std::vector<ceilmeasure::Dome> domes;
    AcquireSRWLockShared(&g_biomeLock);
    for (const ceilmeasure::Dome& d : g_chapelDomes)
        if (d.slot >= 0 && d.slot < gameceiling::kVaultSlots && (d.slot == 0 ? domeTick : tallTick)) domes.push_back(d);   // (a slot past ours: not laid)
    ReleaseSRWLockShared(&g_biomeLock);
    const bool domeArea = want && !domes.empty();
    const unsigned domeGen = g_chapelGen.load() * 4u + (domeTick ? 1u : 0u) + (tallTick ? 2u : 0u);   // (a key changed: looked for again)
    ChapelFound found;
    AcquireSRWLockShared(&g_chapelLock); found = g_chapelFound; ReleaseSRWLockShared(&g_chapelLock);
    static unsigned askedGen = ~0u, toldGen = ~0u;
    static int tries = 0, toldTries = -1;
    static ULONGLONG askedAt = 0;
    const ULONGLONG nowMs = GetTickCount64();
    const bool canopy = found.gen == domeGen && found.matches == 1;
    const ceilmeasure::Dome dome = canopy ? found.dome : domes.empty() ? ceilmeasure::Dome{} : domes[0];
    const bool cathedral = dome.slot == 0;
    // (not found yet: every 2 s, kChapelTries times; found with more than one dome in the area: again every 5 s - another level of the biome)
    const bool again = canopy && domes.size() > 1 && nowMs - askedAt >= 5000;
    if (domeArea && (!canopy || again) && !g_chapelBusy.load()) {
        if (askedGen != domeGen) { askedGen = domeGen; tries = 0; askedAt = 0; }
        if ((tries < kChapelTries || again) && nowMs - askedAt >= 2000) {
            if (!canopy) ++tries;
            askedAt = nowMs;
            g_chapelBusy.store(true);
            for (const ceilmeasure::Dome& d : domes) g_vaultIndices[d.slot].store(d.indices);
            if (HANDLE th = CreateThread(nullptr, 0, ChapelThread, new ChapelAsk{domeGen, domes, {hero[0], hero[1], hero[2]}}, 0, nullptr)) CloseHandle(th);
            else g_chapelBusy.store(false);   // (its ask leaks once: a thread that never ran)
        }
    }
    static int toldSlot = -1;
    if (domeArea && found.gen == domeGen && (toldGen != domeGen || toldTries != tries || (canopy && dome.slot != toldSlot))) {   // each look's answer, once
        toldGen = domeGen;
        toldTries = tries;
        toldSlot = canopy ? dome.slot : -1;
        if (canopy)
            LogF("vrcam: game ceiling - %s found among the game's pieces at %.1f %.1f %.1f (%zu pieces at the %d dome(s)' pieces' and pillars' "
                 "places, %.1f ms): the dome's middle %.1f %.1f, %.1f round through its pillars, made rising %.1f over %.1f (its vault: slot %d)",
                 cathedral ? "the cathedral's canopy" : "the Worldstone", found.world[0], found.world[1], found.world[2], found.pieces, found.asked,
                 found.ms, found.world[0] + dome.centreX - dome.anchor[0], found.world[2] + dome.centreZ - dome.anchor[2], dome.radius, dome.rise,
                 dome.springs, dome.slot);
        else
            LogF("vrcam: game ceiling - %s NOT found among the game's pieces (%s; %zu pieces at their and their pillars' places, %d agreeing, %.1f "
                 "ms; look %d of %d)%s", domes.size() > 1 ? "the area's domes' pieces" : cathedral ? "the cathedral's canopy" : "the Worldstone",
                 found.read ? "the renderer read" : "the renderer's registry does not read", found.pieces, found.matches, found.ms, tries,
                 kChapelTries, tries < kChapelTries ? " - again in 2 s; its slab meanwhile" : " - its slab stays");
    }
    const float baseY = canopy ? found.world[1] - dome.anchor[1] : 0.0f;
    // [ceiling] game_ceiling_dome_size / _rise: the vault laid that many times its size round the measured middle, its rise worked out for
    // the rim it has then (still over the canopy) and that many times higher - live
    // (the cathedral's: its size and height sliders; the Worldstone's: its own width in metres - the world's units a metre as the camera's)
    const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
    const float size = cathedral ? gameceiling::DomeSize(g_set.gameCeilingDomeSize.load(), dome.reach, dome.radius)
                                 : gameceiling::WiderSize(g_set.worldstoneWiderM.load(), upm, dome.reach, dome.radius);
    const float riseBy = cathedral ? gameceiling::DomeRiseScale(g_set.gameCeilingDomeRise.load()) : 1.0f;
    const float rise = canopy ? riseBy * gameceiling::VaultRise(baseY, dome.top, floorAt + slabTop, dome.reach, dome.radius * size) : 0.0f;
    const bool vault = domeArea && canopy && rise > 0.0f;
    const char* vaultWhy = !want ? why : !g_chapelHere.load() ? "no dome measured for this area (D2R_VR_Settings)"
                           : domes.empty() ? "[ceiling] game_ceiling_dome / game_ceiling_worldstone_dome=0"
                           : !canopy ? "its piece not found among the game's pieces (yet)" : !vault ? "the ceiling already over its piece: no dome needed" : "";
    static float toldRise = -1.0f, toldSize = -1.0f;
    if (vault && (std::fabs(rise - toldRise) > 0.05f || std::fabs(size - toldSize) > 0.005f)) {   // (the units a metre follow the hero's eyes: not every small step)
        toldRise = rise;
        toldSize = size;
        char sizeWhy[160];
        if (cathedral) snprintf(sizeWhy, sizeof sizeWhy, "[ceiling] game_ceiling_dome_size %.2f", g_set.gameCeilingDomeSize.load());
        else snprintf(sizeWhy, sizeof sizeWhy, "[ceiling] game_ceiling_worldstone_dome_wider_m %.1f m across at %.2f units a metre: %.1f units further out all round",
                      g_set.worldstoneWiderM.load(), upm, dome.radius * (size - 1.0f));
        LogF("vrcam: game ceiling - the vault (slot %d): its flat %.1f units over the area's floor; the dome %.1f round (size %.2f of the measured "
             "%.1f, %s), rising %.1f over it (rise %.2f of the least, game_ceiling_dome_rise; the piece's top at "
             "%.1f over the preset's floor %.1f; the mesh made rising %.1f: scaled %.2f up), %.0f units round its middle", dome.slot, slabTop, dome.radius * size,
             size, dome.radius, sizeWhy, rise, riseBy, dome.top, baseY, dome.rise, rise / dome.rise, gameceiling::VaultHalf(dome.half) * size);
    }
    // [body] leap_camera_up_pct: the slab's lowest in the world (its top less its rock's hang, the barracks' frame under it), for the leap's camera
    if (want) {   // (hang: the rock's, above)
        g_slabLowestY.store(floorAt + std::min(beams ? height : slabTop, slabTop - hang));
    }
    float m[16], w[16], away[16], v[16];
    gameceiling::Matrix(hero, floorAt, slabTop, tile, want, m);
    gameceiling::WallMatrix(hero, floorAt, height, tile, reach, wall, w);
    gameceiling::Matrix(hero, 0.0f, 0.0f, 1.0f, false, away);
    gameceiling::VaultMatrix(hero, found.world[0] + dome.centreX - dome.anchor[0], found.world[2] + dome.centreZ - dome.anchor[2], floorAt + slabTop, rise,
                             dome.rise, size, vault, v);
    bool vaultShown = false;
    for (int i = 0; i < gameceiling::kVaultSlots; ++i) {
        const bool mineV = vault && dome.slot == i;
        vaultShown = PieceFrame(g_vaults[i], mineV, mineV || !vault ? vaultWhy : "another vault laid", hero, mineV ? v : away, away) || vaultShown;
    }
    bool shown = vaultShown;
    for (int k = 0; k < gameceiling::kKindCount; ++k) {
        const bool mine = want && k == kind && !vaultShown;
        const char* whyK = mine || !want ? why : k == kind ? "a vault laid instead ([ceiling] game_ceiling_dome / _worldstone_dome)" : "another kind's area";
        shown = PieceFrame(g_slabs[k], mine, whyK, hero, m, away) || shown;
        g_slabGivenUp[k].store(gameceiling::GivenUp(g_slabs[k].loadTried, g_slabs[k].def.p != nullptr, g_slabs[k].tries));
        if (gameceiling::kKinds[k].wall)
            PieceFrame(g_walls[k], mine && wall, mine && !wallTick ? "[ceiling] game_ceiling_wall=0" : whyK, hero, w, away);
    }
    for (int i = 0; i < gameceiling::kBeamPieces; ++i) {
        float b[16];
        gameceiling::BeamMatrix(hero, floorAt, height, beam.periodX, beam.periodZ, beam.cols, beam.rows, beam.bottom, i, beams, b);
        PieceFrame(g_beams[i], beams, beamWhy, hero, b, away);
    }
    g_slabShown.store(shown);
}

// ---- [holes] patch_world: the world's holes patched by the game's own pieces (shared/world_holes_file.h, docs/notes/world_holes_pipeline.md) ----
// D2R_VR_Settings makes each place's patch from the player's game (tools/settings/holes_assets.cpp): the rim floor's own model file with the patch's
// triangles in it, at a free model path, a unit, and a line of the measures file (data/hd/env/model/d2r_vr/holes/holes.txt): the patch's anchor
// piece and a witness piece of the place's preset (their places in the preset). Here, at each area change while the tick is on, the anchor is looked
// for among the renderer's own pieces (knockprops::PresetPieces: at its place in its preset, with the witness at its own place under the same preset
// root, as far apart drawn as placed - ceilmeasure::MatchPreset; a whole pass over the transforms on a thread of its own, again every 2 s while not
// found, kHoleTries times); found, the patch is placed and laid every frame at the anchor's place as drawn (the patch is made in the anchor's own
// space; a level preset is laid unturned). VR F4 only (viewgate::F4); anywhere else, off, or not found: laid away under the ground, small.
constexpr int kHoleTries = 15;
Slab g_holes[holeslist::kPlaceCount];
std::atomic<unsigned> g_holeIndices[holeslist::kPlaceCount];
std::vector<holeslist::Line> g_holeLines;   // (the render thread's: read at each area change while the tick is on)
struct HolesFound { unsigned gen = ~0u; bool read = false; size_t pieces = 0; double ms = 0.0; std::vector<int> matches; std::vector<std::array<float, 3>> world; };
SRWLOCK g_holesLock = SRWLOCK_INIT;
HolesFound g_holesFound;   // (under g_holesLock)
std::atomic<bool> g_holesBusy{false};
struct HolesAsk { unsigned gen; std::vector<holeslist::Line> lines; };
DWORD WINAPI HolesThread(void* arg) {
    const HolesAsk* ask = static_cast<const HolesAsk*>(arg);
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    std::vector<float> places;   // each patch's anchor and witness
    for (const holeslist::Line& l : ask->lines) places.insert(places.end(), {l.anchor[0], l.anchor[1], l.anchor[2], l.witness[0], l.witness[1], l.witness[2]});
    std::vector<knockprops::PresetPiece> got;
    HolesFound r;
    r.gen = ask->gen;
    r.read = !places.empty() && knockprops::PresetPieces(reinterpret_cast<const float(*)[3]>(places.data()), (int)(places.size() / 3), &got);
    std::vector<ceilmeasure::Piece> pieces;
    for (const knockprops::PresetPiece& p : got) {
        ceilmeasure::Piece c;
        c.parent = p.parent;
        std::memcpy(c.local, p.local, sizeof c.local);
        std::memcpy(c.world, p.world, sizeof c.world);
        pieces.push_back(c);
    }
    r.pieces = pieces.size();
    for (const holeslist::Line& l : ask->lines) {
        std::array<float, 3> w{};
        r.matches.push_back(ceilmeasure::MatchPreset(pieces, l.anchor, l.witness, w.data()));
        r.world.push_back(w);
    }
    QueryPerformanceCounter(&t1);
    r.ms = 1000.0 * (double)(t1.QuadPart - t0.QuadPart) / (double)f.QuadPart;
    AcquireSRWLockExclusive(&g_holesLock); g_holesFound = r; ReleaseSRWLockExclusive(&g_holesLock);
    delete ask;
    g_holesBusy.store(false);
    return 0;
}
void HolesFrame() {
    static bool named = false;
    if (!named) {
        named = true;
        for (int i = 0; i < holeslist::kPlaceCount; ++i) {
            Slab& s = g_holes[i];
            s.what = std::string("patch '") + holeslist::kPlaces[i].name + "'";
            s.unit = holeslist::UnitPath(holeslist::kPlaces[i]);
            s.model = holeslist::ModelPath(holeslist::kPlaces[i]);
            s.indices = &g_holeIndices[i];
            s.tag = "world holes";
            s.madeWhile = "[holes] patch_world is ticked";
            s.modelNote = " - the rim floor's own model file of the game's with the patch's triangles in it";
        }
    }
    const bool tick = g_set.holesPatch.load();
    if (!tick) {   // never wanted (or every patch put away): nothing to do
        bool parked = true;
        for (const Slab& s : g_holes) parked = parked && s.parked;
        if (parked) return;
    }
    // every view: flat F1 - F3 and VR alike (Serg 2026-11-09, on 0.209.7 in flat: "Yes, every view") - the game draws the patch as its own
    // piece wherever it draws the bridge
    const float hero[3] = {g_heroPosNow[0], g_heroPosNow[1], g_heroPosNow[2]};
    float away[16];
    gameceiling::Matrix(hero, 0.0f, 0.0f, 1.0f, false, away);
    // the measures, at each area change (and the first frame the tick is on)
    const unsigned gen = g_biomeGen.load() * 2u + (tick ? 1u : 0u);
    static unsigned readGen = ~0u;
    static std::string readWhy = "not read yet";
    if (tick && readGen != gen) {
        readGen = gen;
        std::wstring file;
        const std::string text = ModDataText(holeslist::kFile, &file);
        if (text.empty()) { g_holeLines.clear(); readWhy = "no measures file (D2R_VR_Settings makes it while [holes] patch_world is ticked)"; }
        else if (!holeslist::Parse(text, &g_holeLines)) { g_holeLines.clear(); readWhy = "the measures file is not of this version - D2R_VR_Settings makes it again"; }
        else readWhy = "";
        for (const holeslist::Line& l : g_holeLines)
            for (int i = 0; i < holeslist::kPlaceCount; ++i)
                if (l.name == holeslist::kPlaces[i].name) g_holeIndices[i].store((unsigned)l.triangles * 3u);
        LogF("vrcam: world holes - the measures: %s%s (%zu places made by D2R_VR_Settings: %s)", Utf8(file.c_str()).c_str(), readWhy.empty() ? "" : (" - " + readWhy).c_str(),
             g_holeLines.size(), [&] { std::string n; for (const auto& l : g_holeLines) n += (n.empty() ? "" : ", ") + l.name; return n.empty() ? std::string("none") : n; }().c_str());
        for (const holeslist::Line& l : g_holeLines)
            LogF("vrcam: world holes - '%s': %d triangles (%d floor strips, %d skirts; %d ramp strips to the parapet, %d side skirts, %.1f units of side left open; "
                 "%.1f units of floor edge with an opening seen from the eyes), %.0f sq "
                 "units over a hole of %.0f (%.1f left by the maker's check); the material of %s's mesh %s (%s); its anchor at %.2f %.2f %.2f in its preset, the witness "
                 "at %.2f %.2f %.2f", l.name.c_str(), l.triangles, l.strips, l.skirts, l.ramps, l.sides, l.sideLeft, l.seen, l.area, l.holeArea, l.left, l.source.c_str(), l.sourceMesh.c_str(), l.technique.c_str(), l.anchor[0], l.anchor[1], l.anchor[2], l.witness[0], l.witness[1], l.witness[2]);
    }
    // looked for among the game's pieces: at the area change, again every 2 s while one is not found, kHoleTries times
    HolesFound found;
    AcquireSRWLockShared(&g_holesLock); found = g_holesFound; ReleaseSRWLockShared(&g_holesLock);
    static unsigned askedGen = ~0u, toldGen = ~0u;
    static int tries = 0, toldTries = -1;
    static ULONGLONG askedAt = 0;
    const ULONGLONG nowMs = GetTickCount64();
    bool allFound = found.gen == gen && found.matches.size() == g_holeLines.size();
    for (int n : found.matches) allFound = allFound && n >= 1;
    if (tick && !g_holeLines.empty() && !allFound && !g_holesBusy.load()) {
        if (askedGen != gen) { askedGen = gen; tries = 0; askedAt = 0; }
        if (tries < kHoleTries && nowMs - askedAt >= 2000) {
            ++tries;
            askedAt = nowMs;
            g_holesBusy.store(true);
            if (HANDLE th = CreateThread(nullptr, 0, HolesThread, new HolesAsk{gen, g_holeLines}, 0, nullptr)) CloseHandle(th);
            else g_holesBusy.store(false);
        }
    }
    if (found.gen == gen && (toldGen != gen || toldTries != tries)) {   // each look's answer, once
        toldGen = gen;
        toldTries = tries;
        for (size_t k = 0; k < found.matches.size() && k < g_holeLines.size(); ++k)
            LogF("vrcam: world holes - '%s' %s (%zu pieces at the patches' anchors' and witnesses' places, %.1f ms; look %d of %d)%s", g_holeLines[k].name.c_str(),
                 found.matches[k] >= 1 ? "found among the game's pieces" : found.read ? "NOT here (its preset not in this area)" : "NOT looked for (the renderer's registry does not read)",
                 found.pieces, found.ms, tries, kHoleTries,
                 found.matches[k] >= 1 ? (" - its anchor drawn at " + std::to_string(found.world[k][0]) + " " + std::to_string(found.world[k][1]) + " " + std::to_string(found.world[k][2]) +
                                          (found.matches[k] > 1 ? " (" + std::to_string(found.matches[k]) + " agreeing: the first taken)" : std::string())).c_str() : "");
    }
    // each place's patch: laid at its anchor as drawn, or away
    for (int i = 0; i < holeslist::kPlaceCount; ++i) {
        int k = -1;
        for (size_t j = 0; j < g_holeLines.size(); ++j) if (g_holeLines[j].name == holeslist::kPlaces[i].name) k = (int)j;
        const bool here = k >= 0 && found.gen == gen && (size_t)k < found.matches.size() && found.matches[(size_t)k] >= 1;
        const bool want = tick && here;
        const char* why = !tick ? "[holes] patch_world=0" : k < 0 ? (readWhy.empty() ? "not made by D2R_VR_Settings (its line not in the measures)" : readWhy.c_str())
                        : !here ? "its preset not found among the game's pieces in this area" : "";
        float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        if (here) { m[12] = found.world[(size_t)k][0]; m[13] = found.world[(size_t)k][1]; m[14] = found.world[(size_t)k][2]; }
        PieceFrame(g_holes[i], want, why, hero, want ? m : away, away);
    }
}

// ---- The game left: every unit of ours let go before the game's own teardown (vr/engine/held_units.h) ----
// From the world's teardown (HookTeardown, the game's thread that owns the registry): every instance first - its entities still the
// registry's, the game's own removal takes them out - then every definition; everything placed is forgotten, and the next game's first
// world loads and places it all afresh (g_life: nothing placed in between).
void ResetDome(Dome& d) {
    d.def = {}; d.inst = {}; d.root = 0xFFFFFFFFu; d.loadTried = false; d.parked = true; d.tries = 0;
    d.loadedAt = d.placedAt = d.modelAt = 0; d.told = "?"; d.toldR = -1.0f; d.toldPlace = false; d.fileTold.clear();
}
// [hud] wrist_game_models: the liquid's level - its file holds wristorbs::kLevels meshes (the dome cut flat at each level), the game's own mesh flipbook
// shows one: ModelInstance::SetActiveMesh (0xF3F390: `cmp [rcx+8],rdx; je; mov [rcx+8],rdx; jmp 0xF3FFF0` - the visible list and its render items
// rebuilt), on the registry's thread, only once the model is loaded (the rebuild blocks on a model still loading). -1 shows every mesh at once.
constexpr uint64_t RVA_SET_ACTIVE_MESH = 0xF3F390;
constexpr uint8_t kSetActiveMeshBytes[] = {0x48, 0x39, 0x51, 0x08, 0x74, 0x09, 0x48, 0x89, 0x51, 0x08, 0xE9};
using SetActiveMeshFn = void(__fastcall*)(void* inst, int64_t index);
int g_levelCheck = 0;   // 1 the game's function is where 3.3.93787 has it, -1 not
// The level shown on the placed liquid: true when its model shows mesh `index` now (set if it did not)
bool ShowLevel(void* placed, int64_t index) {
    if (g_levelCheck == 0) {
        g_levelCheck = d2rsig::Check(RVA_SET_ACTIVE_MESH, kSetActiveMeshBytes, sizeof kSetActiveMeshBytes) ? 1 : -1;
        Log(g_levelCheck > 0 ? "vrcam: wrist medallions - the liquid's level: the game's mesh flipbook (SetActiveMesh 0xF3F390) is where 3.3.93787 has it"
                             : "vrcam: wrist medallions - the game's mesh flipbook (0xF3F390) is NOT as in 3.3.93787: the liquid is not shown");
    }
    if (g_levelCheck < 0) return false;
    uint32_t e[16];
    const int n = Entities(placed, e, 16);
    bool shown = false;
    for (int i = 0; i < n; ++i) {
        const uintptr_t mi = skel::EntityModelInstance(e[i]);
        if (!mi) continue;
        int64_t now = 0;
        uintptr_t res = 0;
        uint32_t state = 0;
        if (!::SafeRead(&now, (const void*)(mi + 0x08), 8)) continue;
        if (now == index) { shown = true; continue; }
        if (!::SafeRead(&res, (const void*)(mi + 0x20), 8) || !res || !::SafeRead(&state, (const void*)(res + 0xC8), 4) || state != 4) continue;   // (not loaded yet)
        __try {
            reinterpret_cast<SetActiveMeshFn>(d2rsig::Addr(RVA_SET_ACTIVE_MESH))((void*)mi, index);
            shown = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_levelCheck = -1;
            Log("vrcam: wrist medallions - a fault setting the liquid's level (the game's mesh flipbook): the liquid is not shown from now");
            return false;
        }
    }
    return shown;
}
void ParkWrist(WristPart& w);
// The skeleton hook, as the hero's arm is posed at last (skel::SetArmPosed): each medallion on that wrist laid again on the arm as drawn NOW, from its place in
// the wrist's frame the frame block worked out - the same frame the game skins the hero with (Work's lay is the frame block's world place, a frame old:
// it lagged behind the wrist while the hero moved - 0.214.5). Only the parts Work shows; the liquid's level stays Work's. Any thread: the parts' lock,
// skipped (not waited for) while Work holds it.
std::atomic<unsigned> g_wristRelaid{0}, g_wristRelaySkipped{0};
// Our instance's matrices written straight (no game call - this runs on the skeleton hook's thread, not the registry's): the root's local and world
// (+0x00, +0x40: a root, no parent) `m`, every other entity's world its own local times it. The renderer works out no world for a unit with nothing
// behind it (see Lay), so nothing of the game writes these after us.
bool WriteWorld(uint32_t e, const float* local, const float m[16], bool root) {
    uintptr_t comp = 0;
    if (e == 0xFFFFFFFFu || !skel::EntityTransform(e, &comp)) return false;
    __try {
        float* t = reinterpret_cast<float*>(comp);
        if (root) { memcpy(t, m, 64); memcpy(t + 16, m, 64); return true; }
        float l[16], w[16];
        memcpy(l, local ? local : t, 64);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) w[r * 4 + c] = l[r * 4] * m[c] + l[r * 4 + 1] * m[4 + c] + l[r * 4 + 2] * m[8 + c] + l[r * 4 + 3] * m[12 + c];
        memcpy(t + 16, w, 64);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
void LayFast(void* inst, uint32_t root, const float m[16]) {
    if (!WriteWorld(root, nullptr, m, true)) return;
    uint32_t e[16];
    const int n = Entities(inst, e, 16);
    for (int i = 0; i < n; ++i) if (e[i] != root) WriteWorld(e[i], nullptr, m, false);
}
// Each wrist's arm as the skeleton hook last posed it, and when (QPC) and in which eye's pass (0 left, 1 right; g_eye): Work lays from it too (0.214.6, native
// two passes: Work, once a pair, laid the frame block's world place - a frame old - over the left pass's fresh re-lay; the left eye lagged, the right did not)
SRWLOCK g_wristArmLock = SRWLOCK_INIT;
#if D2RVR_FIRST_PERSON
skel::HeroArm g_wristArm[2];                  // under g_wristArmLock
#endif
LARGE_INTEGER g_wristArmAt[2] = {};           // under g_wristArmLock: QPC when posed (0 never)
std::atomic<unsigned> g_wristPosedEye[2] = {};   // re-lays by the eye's pass (the log)
std::atomic<unsigned> g_wristWorkFromArm{0}, g_wristWorkFromWorld{0};   // Work's lays: from the last posed arm / from the frame block's world place
std::atomic<long long> g_wristWorkArmAgeUs{0};    // the summed age of the posed arm at Work's lays (the log: its mean)
// One medallion `k` of the want on its wrist's posed arm: its place, back and along from its place in the wrist's frame. False: no place in the frame.
#if D2RVR_FIRST_PERSON
bool OrbOnArm(const wristmodels::Want& w, int k, const skel::HeroArm& arm, wristmodels::Orb* o) {
    if (!w.rel[k] || !(w.perM > 0.0f)) return false;
    wristmodels::ArmFrame fr;
    if (!wristmodels::MakeFrame(arm.wrist, arm.elbow, arm.hand ? arm.point : nullptr, arm.hand ? arm.face : arm.across, &fr)) return false;
    *o = w.orb[k];
    wristmodels::FromFrame(fr, w.relC[k], true, w.perM, o->c);
    wristmodels::FromFrame(fr, w.relBack[k], false, 1.0f, o->back);
    wristmodels::FromFrame(fr, w.relAlong[k], false, 1.0f, o->along);
    o->radius = w.radiusM[k] * w.perM;
    return true;
}
void WristArmPosed(int side, const skel::HeroArm& arm) {
    if (!g_set.wristGameModels.load(std::memory_order_relaxed)) return;
    side &= 1;
    {   // kept for Work: the newest posed arm of this wrist
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        AcquireSRWLockExclusive(&g_wristArmLock);
        g_wristArm[side] = arm;
        g_wristArmAt[side] = t;
        ReleaseSRWLockExclusive(&g_wristArmLock);
    }
    wristmodels::Want w;
    AcquireSRWLockShared(&g_wristLock);
    w = g_wristWant;
    ReleaseSRWLockShared(&g_wristLock);
    const ULONGLONG now = GetTickCount64();
    if (!w.stamp || now < w.stamp || now - w.stamp >= 250 || !(w.perM > 0.0f)) return;
    if (!TryAcquireSRWLockShared(&g_wristPartsLock)) { g_wristRelaySkipped.fetch_add(1, std::memory_order_relaxed); return; }
    for (int k = 0; k < 2; ++k) {
        if (!w.rel[k] || w.side[k] != side) continue;
        wristmodels::Orb o;
        if (!OrbOnArm(w, k, arm, &o)) continue;
        float m[wristmodels::kParts][16];
        bool show[wristmodels::kParts] = {};
        if (!wristmodels::Matrices(o, m, show)) continue;
        const int kind = o.kind == wristorbs::kMana ? 1 : 0;
        WristPart* parts[wristmodels::kParts] = {&g_wristGlass[k], &g_wristPlate[k], &g_wristLiquid[kind]};
        for (int p = 0; p < wristmodels::kParts; ++p)
            if (show[p] && parts[p]->inst.p && !parts[p]->parked && parts[p]->root != 0xFFFFFFFFu) LayFast(parts[p]->inst.p, parts[p]->root, m[p]);
        g_wristRelaid.fetch_add(1, std::memory_order_relaxed);
        g_wristPosedEye[g_eye.load(std::memory_order_relaxed) & 1].fetch_add(1, std::memory_order_relaxed);
    }
    ReleaseSRWLockShared(&g_wristPartsLock);
}
#endif
void ParkWrist(WristPart& w) {
    if (w.parked || !w.inst.p) return;
    const float m[16] = {0.001f, 0, 0, 0, 0, 0.001f, 0, 0, 0, 0, 0.001f, 0, g_heroPosNow[0], g_heroPosNow[1] - 5000.0f, g_heroPosNow[2], 1};
    if (Lay(w.inst.p, w.root, m)) w.parked = true;
}
// One part shown at `m`: placed the first time (its unit's definition `def`), laid; false when it could not be (its entity gone: let go, placed again next frame)
bool ShowWrist(WristPart& w, const Shared& def, const float m[16], const char* what) {
    if (!w.inst.p) {
        if (!def.p || w.placed) return false;   // (placed once a game: a placing that made no instance is not asked again)
        w.placed = true;
        w.placedAt = GetTickCount64();
        if (!PlaceAt(def.p, m + 12, &w.inst)) { LogF("vrcam: wrist medallions - the %s could not be placed: not drawn", what); return false; }
        w.root = RootOf(w.inst.p);
        LogF("vrcam: wrist medallions - the %s placed (instance %p, root entity %X)", what, w.inst.p, w.root);
    }
    if (w.root == 0xFFFFFFFFu) w.root = RootOf(w.inst.p);
    if (!Lay(w.inst.p, w.root, m)) {   // its entity is gone (an area change): let go, placed again
        ReleaseInstance(&w.inst, &g_areaTally);
        w = WristPart{};
        return false;
    }
    w.parked = false;
    if (!w.modelTold) {
        const bool model = HasModel(w.inst.p);
        if (model || GetTickCount64() - w.placedAt > 5000) {
            w.modelTold = true;
            LogF("vrcam: wrist medallions - the %s: %s", what, model ? "the game made its model - drawn by the game" :
                 "NO model made 5 s after placing (its model file not where the game takes it: run D2R VR Settings with the tick on, then restart the game)");
        }
    }
    return true;
}
void WristOrbsFrameLocked(bool fullBody);
void WristOrbsFrame(bool fullBody) {
#if D2RVR_FIRST_PERSON
    static bool hooked = false;   // the medallions laid again on the arm as it is posed (the skeleton hook)
    if (!hooked && g_set.wristGameModels.load()) { hooked = true; skel::SetArmPosed(&WristArmPosed); }
#endif
    AcquireSRWLockExclusive(&g_wristPartsLock);
    WristOrbsFrameLocked(fullBody);
    ReleaseSRWLockExclusive(&g_wristPartsLock);
}
void WristOrbsFrameLocked(bool fullBody) {
    const bool on = g_set.wristGameModels.load();
    bool any = false;
    for (int k = 0; k < 2; ++k) any = any || g_wristGlass[k].inst.p || g_wristPlate[k].inst.p || g_wristLiquid[k].inst.p;
    if (!on && !any) return;
    wristmodels::Want w;
    float pulse = 1.0f;
    AcquireSRWLockShared(&g_wristLock);
    w = g_wristWant;
    pulse = g_wristPulse;
    ReleaseSRWLockShared(&g_wristLock);
    const ULONGLONG now = GetTickCount64();
    const bool fresh = on && fullBody && viewgate::F4() && w.stamp && now >= w.stamp && now - w.stamp < 250;
    {   // once per change: why nothing is drawn (0.214.1 said only "0 of N frames")
        const char* why = !on ? "[hud] wrist_game_models off" : !viewgate::F4() || !fullBody ? "not VR first person (F4)" :
                          !w.stamp ? "the wrists' frame never handed over (the effect's frame block: wrists not on, the effect runtime not running, a menu, or the "
                                     "glass-ball look not chosen)" :
                          now < w.stamp || now - w.stamp >= 250 ? "the wrists' frame stale (older than 250 ms: a menu, the hero loading, the effect paused)" : "";
        static std::string toldWhy = "-";
        if (toldWhy != why) {
            toldWhy = why;
            if (*why) LogF("vrcam: wrist medallions - not drawn by the game now: %s", why);
            else Log("vrcam: wrist medallions - the wrists' frame comes fresh: drawn by the game from now (its units asked if not yet)");
        }
    }
    if (fresh) {   // the units: asked once a game, again every 10 s while one is missing (D2R VR Settings not run yet)
        bool missing = false;
        for (int u = 0; u < kWristUnits; ++u) missing = missing || !g_wristDefOk[u];
        if (missing && (!g_wristDefAt || now - g_wristDefAt >= 10000)) {
            g_wristDefAt = now;
            std::string told;
            for (int u = 0; u < kWristUnits; ++u) {
                if (g_wristDefOk[u]) continue;
                g_wristDefOk[u] = LoadDef(WristUnit(u), &g_wristDef[u]);
                if (!g_wristDefOk[u]) told += std::string(told.empty() ? "" : ", ") + WristUnit(u);
            }
            g_wristUnitsState.store(told.empty() ? wristlook::kUnitsLoaded : wristlook::kUnitsMissing);
            static std::string toldWas = "-";
            if (told != toldWas) {
                toldWas = told;
                if (told.empty()) {
                    Log("vrcam: wrist medallions - the glass dome, the gold / bronze / silver bases and the liquid lens in the potions' own materials "
                        "(life, mana): all six units loaded - drawn by the game in VR F4 ([hud] wrist_game_models)");
                    // which material the liquids' made files are in (0.216.3: Serg's ini still had 0.216.0's wrist_game_liquid_glass on - the installed
                    // liquid was the game's GlassMaterial, whose shader takes no per-entity opacity and draws a potion picture solid; the opacity
                    // reached the draw, 0.15 in every call, and nothing changed on the wrist)
                    bool glass[2] = {}, found[2] = {};
                    for (int kind = 0; kind < 2; ++kind) {
                        std::wstring file;
                        const std::string bytes = ModDataText(wristorbs::kLiquidFile[kind], &file);
                        found[kind] = !bytes.empty();
                        glass[kind] = bytes.find("GlassMaterial") != std::string::npos;
                    }
                    const auto name = [&](int kind) { return !found[kind] ? "not read" : glass[kind] ? "the game's GlassMaterial" : "StandardMaterial"; };
                    LogF("vrcam: wrist medallions - the liquids' made files: life in %s, mana in %s. %s", name(0), name(1),
                         glass[0] || glass[1] ? "A GLASSMATERIAL LIQUID is one made by 0.216.0's wrist_game_liquid_glass (not the potions' own liquid) - D2R VR "
                                                "Settings of this version makes them in the potions' own material again at its start; then the game "
                                                "once more"
                                              : "the potions' own material, as the glass dome's");
                }
                else LogF("vrcam: wrist medallions - these units did not load: %s (made by D2R VR Settings with [hud] wrist_game_models ticked, into the mod's data); "
                          "asked again every 10 s - not drawn till then", told.c_str());
            }
        }
    }
    uint32_t litEnt[beltglow::kMaxOrbs];
    float litH[beltglow::kMaxOrbs], litOp[beltglow::kMaxOrbs];
    uint8_t litRole[beltglow::kMaxOrbs] = {};   // (0 the glass, 1 the liquid: the log's line of what the draw has)
    int lit = 0;
    bool liquidUsed[2] = {};
    for (int k = 0; k < 2; ++k) {
        wristmodels::Orb o = w.orb[k];
#if D2RVR_FIRST_PERSON
        if (fresh && w.rel[k]) {   // the newest posed arm of its wrist (under 100 ms old): the place the skeleton hook lays it, not the frame block's older one
            skel::HeroArm arm;
            LARGE_INTEGER at, t, hz;
            AcquireSRWLockShared(&g_wristArmLock);
            arm = g_wristArm[w.side[k] & 1];
            at = g_wristArmAt[w.side[k] & 1];
            ReleaseSRWLockShared(&g_wristArmLock);
            QueryPerformanceCounter(&t);
            QueryPerformanceFrequency(&hz);
            const long long ageUs = at.QuadPart && hz.QuadPart ? (t.QuadPart - at.QuadPart) * 1000000LL / hz.QuadPart : -1;
            wristmodels::Orb onArm;
            if (ageUs >= 0 && ageUs < 100000 && OrbOnArm(w, k, arm, &onArm)) {
                o = onArm;
                g_wristWorkFromArm.fetch_add(1, std::memory_order_relaxed);
                g_wristWorkArmAgeUs.fetch_add(ageUs, std::memory_order_relaxed);
            } else {
                g_wristWorkFromWorld.fetch_add(1, std::memory_order_relaxed);
            }
        }
#endif
        float m[wristmodels::kParts][16];
        bool show[wristmodels::kParts] = {};
        int level = 0;
        const bool shown = fresh && wristmodels::Matrices(o, m, show, &level);
        const int kind = o.kind == wristorbs::kMana ? 1 : 0;
        if (shown && show[wristmodels::kGlass] && ShowWrist(g_wristGlass[k], g_wristDef[0], m[wristmodels::kGlass], k ? "second medallion's glass" : "first medallion's glass")) {
            uint32_t e[16];   // see-through: the game's own per-entity opacity on the glass's drawn entity (the draw blends below 1; its root is not drawn
            const int n = Entities(g_wristGlass[k].inst.p, e, 16);   // and has no such component)
            const float op = std::min(1.0f, std::max(wristmodels::kGlassOpacityMin, o.glass));
            for (int i = 0; i < n && lit < beltglow::kMaxOrbs; ++i)
                if (skel::EntityModelInstance(e[i])) { litEnt[lit] = e[i]; litH[lit] = 0.0f; litOp[lit] = op; litRole[lit] = 0; ++lit; }
        } else {
            ParkWrist(g_wristGlass[k]);
        }
        {   // the base of the metal chosen ([hud] hand_vitals_orb_metal): its own unit - another metal's instance let go; clean or aged by the patina (its two meshes)
            const int metal = o.metal >= 0 && o.metal <= 2 ? o.metal : 0;
            const int def = metal == 0 ? 1 : metal == 1 ? 4 : 5;
            if (g_wristPlate[k].inst.p && g_wristPlate[k].variant != metal) {
                ReleaseInstance(&g_wristPlate[k].inst, &g_areaTally);
                g_wristPlate[k] = WristPart{};
            }
            if (shown && show[wristmodels::kPlate] && ShowWrist(g_wristPlate[k], g_wristDef[def], m[wristmodels::kPlate], k ? "second medallion's plate" : "first medallion's plate")) {
                g_wristPlate[k].variant = metal;
                ShowLevel(g_wristPlate[k].inst.p, o.patina >= wristorbs::kPatinaAged ? 1 : 0);
            } else {
                ParkWrist(g_wristPlate[k]);
            }
        }
        if (shown && show[wristmodels::kLiquid] && !liquidUsed[kind] &&
            ShowWrist(g_wristLiquid[kind], g_wristDef[2 + kind], m[wristmodels::kLiquid], kind ? "mana liquid" : "life liquid")) {
            liquidUsed[kind] = true;
            if (!ShowLevel(g_wristLiquid[kind].inst.p, level - 1)) ++g_wristLevelWaits;   // (its model not loaded yet: every mesh at once till it is)
            g_wristLevel[kind] = level;
            uint32_t e[16];
            const int n = Entities(g_wristLiquid[kind].inst.p, e, 16);
            const float h = wristmodels::GlowHighlight(g_set.wristGameGlow.load()) + wristmodels::Highlight(o.glow, pulse);   // (the glow at rest, the beat on top)
            // (its opacity is left alone: a see-through liquid cannot be had under the dome - the game lays a faded model's depth before its colour,
            // so the see-through dome hid a see-through liquid whole; [hud] wrist_game_liquid of the 0.216 test builds is gone)
            for (int i = 0; i < n && lit < beltglow::kMaxOrbs; ++i)
                if (skel::EntityModelInstance(e[i])) { litEnt[lit] = e[i]; litH[lit] = h; litOp[lit] = -1.0f; litRole[lit] = 1; ++lit; }
        }
    }
    for (int kind = 0; kind < 2; ++kind) if (!liquidUsed[kind]) ParkWrist(g_wristLiquid[kind]);
    beltglow::SetOrbs(litEnt, litH, litOp, lit, litRole);
    static ULONGLONG toldAt = 0;   // every 30 s while on: what is shown
    static unsigned frames = 0, shownFrames = 0;
    ++frames;
    if (fresh) ++shownFrames;
    if (on && now - toldAt >= 30000) {
        toldAt = now;
        const int life = w.orb[0].kind == wristorbs::kLife ? 0 : 1;
        int units = 0, placed = 0, modelled = 0;
        for (int u = 0; u < kWristUnits; ++u) units += g_wristDefOk[u] ? 1 : 0;   // (of six)
        for (int k = 0; k < 2; ++k)
            for (const WristPart* p : {&g_wristGlass[k], &g_wristPlate[k], &g_wristLiquid[k]})
                if (p->inst.p) { ++placed; modelled += HasModel(p->inst.p) ? 1 : 0; }
        LogF("vrcam: wrist medallions, 30 s - drawn by the game in %u of %u frames (VR F4, the wrists' frame fresh); units loaded %d of 6, parts placed %d of 6, "
             "with a model %d; the frame handed over %s; life level %.2f (mesh %d of %d), mana level %.2f (mesh %d); the base %s, %s; %u frames a level waited for its model; "
             "laid again on the posed arm %u times (left eye's pass %u, right eye's %u; %u skipped: Work busy); Work laid them from the last posed arm %u "
             "times (it was %.1f ms old on average), from the frame block's world place %u times (no fresh posed arm); heartbeat life %.2f, mana %.2f "
             "(the liquid's highlight up to %.2f at its top)",
             shownFrames, frames, units, placed, modelled, !w.stamp ? "NEVER" : now - w.stamp < 250 ? "just now" : "a while ago",
             w.orb[life].fill, g_wristLevel[0], wristorbs::kLevels, w.orb[1 - life].fill, g_wristLevel[1],
             wristorbs::kPlateMetal[w.orb[0].metal >= 0 && w.orb[0].metal <= 2 ? w.orb[0].metal : 0].name,
             w.orb[0].patina >= wristorbs::kPatinaAged ? "aged (its rosette)" : "clean", g_wristLevelWaits,
             g_wristRelaid.exchange(0), g_wristPosedEye[0].exchange(0), g_wristPosedEye[1].exchange(0), g_wristRelaySkipped.exchange(0),
             g_wristWorkFromArm.load(), g_wristWorkFromArm.load() ? (double)g_wristWorkArmAgeUs.load() / g_wristWorkFromArm.load() / 1000.0 : 0.0,
             g_wristWorkFromWorld.load(), w.orb[life].glow, w.orb[1 - life].glow, pulse);
        // the sizes, both ways (0.215.12, Serg: "the old medallions' diameter is a bit bigger"): the effect's base radius Rb (the size slider / 2) against the
        // game-drawn base's - its entities' world matrices as they are now, read back (the x axis's length: the mesh's radius 1 times it), in metres
        for (int k = 0; k < 2; ++k) {
            const WristPart& pl = g_wristPlate[k];
            float drawn = 0.0f, glassDrawn = 0.0f;
            auto axis = [](const void* inst, uint32_t root) {
                float most = 0.0f;
                uint32_t e[16];
                int n = inst ? Entities(const_cast<void*>(inst), e, 16) : 0;
                if (n <= 0 && root != 0xFFFFFFFFu) { e[0] = root; n = 1; }
                for (int i = 0; i < n; ++i) {
                    uintptr_t comp = 0;
                    if (!skel::EntityTransform(e[i], &comp)) continue;
                    __try {
                        const float* t = reinterpret_cast<const float*>(comp) + 16;
                        most = std::max(most, std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]));
                    } __except (EXCEPTION_EXECUTE_HANDLER) {}
                }
                return most;
            };
            if (pl.inst.p && !pl.parked) drawn = axis(pl.inst.p, pl.root);
            if (g_wristGlass[k].inst.p && !g_wristGlass[k].parked) glassDrawn = axis(g_wristGlass[k].inst.p, g_wristGlass[k].root);
            const float per = w.perM > 0.0f ? w.perM : 1.0f;
            LogF("vrcam: wrist medallions - sizes, %s: the effect's base radius Rb %.4f m (hand_vitals_orb_size_cm %.2f), its dome %.4f m; the game-drawn base "
                 "%.4f m (world x axis %.2f units / %.1f units a metre x its mesh's radius 1.000), its glass dome %.4f m%s",
                 w.orb[k].kind == wristorbs::kMana ? "mana" : "life", w.radiusM[k], w.radiusM[k] * 200.0f, w.radiusM[k] * handorbs::kDomeR, drawn / per, drawn, per,
                 glassDrawn / per, drawn > 0.0f && std::fabs(drawn / per - w.radiusM[k]) > 0.02f * w.radiusM[k] ? " - NOT THE SAME" : drawn > 0.0f ? " - the same" : " (not shown now)");
        }
        g_wristWorkFromArm.store(0);
        g_wristWorkFromWorld.store(0);
        g_wristWorkArmAgeUs.store(0);
        g_wristLevelWaits = 0;
        frames = shownFrames = 0;
    }
}
void LetGoWrist(heldunits::Tally* t, bool instances) {
    AcquireSRWLockExclusive(&g_wristPartsLock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&g_wristPartsLock); } } unlock;
    for (int k = 0; k < 2; ++k)
        for (WristPart* w : {&g_wristGlass[k], &g_wristPlate[k], &g_wristLiquid[k]}) {
            if (instances) ReleaseInstance(&w->inst, t);
            else *w = WristPart{};
        }
    if (!instances) {
        for (int u = 0; u < kWristUnits; ++u) { ReleaseDefinition(&g_wristDef[u], t); g_wristDefOk[u] = false; }
        g_wristDefAt = 0;
        g_wristUnitsState.store(wristlook::kUnitsUnknown);
        beltglow::SetOrbs(nullptr, nullptr, nullptr, 0);
    }
}

void ResetSlab(Slab& s) {
    s.def = {}; s.inst = {}; s.root = 0xFFFFFFFFu; s.loadTried = false; s.parked = true; s.tries = 0;
    s.loadedAt = s.placedAt = s.modelAt = 0; s.told = "?";
}
heldunits::Tally LetGoAll() {
    heldunits::Tally t;
    // the instances
    for (Bottle& b : g_bottles) ReleaseInstance(&b.inst, &t);
    for (Loose& l : g_loose) ReleaseInstance(&l.inst, &t);
    for (HeldCopy& c : g_copies) ReleaseInstance(&c.inst, &t);
    for (RemoteDrawn& r : g_remoteDrawn) ReleaseInstance(&r.inst, &t);
    for (Shared& s : g_placed) ReleaseInstance(&s, &t);
    for (Shared& s : g_domeOld) ReleaseInstance(&s, &t);
    for (Shared& s : g_slabOld) ReleaseInstance(&s, &t);
    Dome* const domes[] = {&g_dome, &g_caveDome, &g_actDomes[0], &g_actDomes[1], &g_actDomes[2], &g_actDomes[3], &g_actDomes[4], &g_actDomes[5]};
    for (Dome* d : domes) ReleaseInstance(&d->inst, &t);
    for (Slab& s : g_slabs) ReleaseInstance(&s.inst, &t);
    for (Slab& s : g_walls) ReleaseInstance(&s.inst, &t);
    for (Slab& s : g_beams) ReleaseInstance(&s.inst, &t);
    for (Slab& s : g_vaults) ReleaseInstance(&s.inst, &t);
    for (Slab& s : g_holes) ReleaseInstance(&s.inst, &t);
    LetGoWrist(&t, true);   // ([hud] wrist_game_models)
    if (t.kept) return t;   // (not the owner thread: nothing forgotten - the definitions stay with the instances that need them)
    // then the definitions
    for (Shared& d : g_defs) ReleaseDefinition(&d, &t);
    for (auto& kv : g_copyDefs) ReleaseDefinition(&kv.second, &t);
    for (Shared& d : g_placedDefs) ReleaseDefinition(&d, &t);
    for (Dome* d : domes) ReleaseDefinition(&d->def, &t);
    for (Slab& s : g_slabs) ReleaseDefinition(&s.def, &t);
    for (Slab& s : g_walls) ReleaseDefinition(&s.def, &t);
    for (Slab& s : g_beams) ReleaseDefinition(&s.def, &t);
    for (Slab& s : g_vaults) ReleaseDefinition(&s.def, &t);
    for (Slab& s : g_holes) ReleaseDefinition(&s.def, &t);
    LetGoWrist(&t, false);
    // and all of it forgotten: the next game places its own
    g_bottles.clear(); g_loose.clear(); g_copies.clear(); g_remoteDrawn.clear(); g_placed.clear(); g_domeOld.clear(); g_slabOld.clear();
    g_copyDefs.clear(); g_placedDefs.clear();
    for (HandGrab& g : g_grab) { g.empty = -1; g.drinking = false; g.column = -1; }
    for (FullDrop& d : g_drop) d.on = false;
    beltglow::SetEntries(nullptr, 0);
    for (Dome* d : domes) ResetDome(*d);
    for (Slab& s : g_slabs) ResetSlab(s);
    for (Slab& s : g_walls) ResetSlab(s);
    for (Slab& s : g_beams) ResetSlab(s);
    for (Slab& s : g_vaults) ResetSlab(s);
    for (Slab& s : g_holes) ResetSlab(s);
    g_slabShown.store(false);
    return t;
}
// The world's teardown (0xD72610: the game's systems shut down, then the registry cleared, then its assets unloaded - where a reference
// still held is "Assets have been leaked"): ours let go at its entry, while our entities are still the registry's. Every leave of a game
// runs it (and the quit from inside one).
constexpr uint64_t RVA_TEARDOWN = 0xD72610;
constexpr uint8_t kTeardownBytes[] = {0x40, 0x53, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00};
using TeardownFn = void(__fastcall*)(void*, void*, void*, void*);   // (it reads no argument; whatever is in the registers is passed on)
TeardownFn g_origTeardown = nullptr;
void __fastcall HookTeardown(void* a, void* b, void* c, void* d) {
    if (!OnOwnerThread()) {   // (not the thread our units are placed on: nothing touched here - Work may be running; held as before)
        Log("vrcam: game left (the world's teardown) on another thread than the registry's owner: our units held as before 0.202");
        g_origTeardown(a, b, c, d);
        return;
    }
    g_life.Teardown();
    const heldunits::Tally area = g_areaTally;
    g_areaTally = {};
    const heldunits::Tally t = LetGoAll();
    char also[160] = "";
    if (area.instances || area.kept)
        snprintf(also, sizeof also, "; during the game %d more let go as area changes took their entities", area.instances);
    Log((heldunits::Line("game left (the world's teardown)", t) + also).c_str());
    g_origTeardown(a, b, c, d);
}
void InstallTeardownHook() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    const bool in = d2rsig::Hook(RVA_TEARDOWN, kTeardownBytes, sizeof kTeardownBytes, (void*)&HookTeardown, (void**)&g_origTeardown);
    Log(in ? "vrcam: engine items - the game's units of ours let go when a game is left (the world's teardown, 0xD72610)"
           : "vrcam: engine items - the world's teardown (0xD72610) not as in 3.3.93787: our units are held to the end, as before 0.202 (the game may report leaked assets when a game is left)");
}
// SkyTick, each area: a menu (no world) after the teardown lets the next game place again
void WorldSeen(bool inWorld) { g_life.World(inWorld); }
// The update thread, each tick: with [sky] game_sky in a world, a line every 30 s when the domes' frame did not run at all - the units' work
// (Work, on the registry owner's thread) not called, so no dome is ever asked (the test PC, 0.209.3, flat: silence after the radius line)
void DomeHeartbeat(bool inWorld) {
    static ULONGLONG toldAt = 0;
    static uint64_t seen = 0;
    const ULONGLONG now = GetTickCount64();
    {   // every 30 s in a world, whatever the state: each input of the dome's choice (the test PC, 0.209.6, flat: no dome and not one dome
        // line - "the sky drawn by nothing - no sky wanted here" said game_sky read 0 there though its ini has 1)
        static ULONGLONG inputsAt = 0;
        if (inWorld && now - inputsAt >= 30000) {
            inputsAt = now;
            const char* why = DomeWhyNot();
            LogF("vrcam: game sky - the dome's inputs, 30 s: [sky] game_sky read %d, [sky] enabled %d, in a world %d, the act %d, the sky's palette %d, "
                 "platform %d, view %d, [render] game_drawn_flat %d, VR camera on %d; why not: \"%s\"; the domes' frame ran %llu times, the last step "
                 "%d (%s), shown %d", g_set.gameSky.load() ? 1 : 0, g_set.skyOn.load() ? 1 : 0, inWorld ? 1 : 0, g_fogAct.load(), g_skyPalette.load(),
                 g_set.platform.load(), ViewNow(), g_set.gameDrawnFlat.load() ? 1 : 0, g_enabled.load() ? 1 : 0, *why ? why : "(none: wanted)",
                 (unsigned long long)g_domeFrames.load(), g_domeStep.load(), DomeStepName(g_domeStep.load()), g_domeShown.load() ? 1 : 0);
        }
    }
    if (!inWorld || !g_set.gameSky.load()) { toldAt = now; seen = g_domeFrames.load(); return; }
    if (now - toldAt < 30000) return;
    const uint64_t n = g_domeFrames.load();
    if (n == seen) LogF("vrcam: game sky - the domes' frame did not run in 30 s (the game's units' work not called on the registry's thread): no dome asked or laid");
    else if (!g_domeShown.load()) {
        const int step = g_domeStep.load();
        LogF("vrcam: game sky - no dome shown, 30 s: the domes' frame ran %llu times, the last step it reached %d (%s)", (unsigned long long)(n - seen),
             step, DomeStepName(step));
    }
    seen = n;
    toldAt = now;
}

// SkyDomeFrame under a structured guard (no C++ object here, so __try is allowed): 0 ran through, else the exception's code
unsigned long GuardedSkyDomeFrame() {
    __try { SkyDomeFrame(); return 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
}
void Work() {
    if (!g_life.MayPlace(true)) return;   // (between the world's teardown and the menu: nothing placed into a registry the game is clearing)
    static int checked = 0;   // 1 the game's functions are where 3.3.93787 has them, -1 not
    static bool told = false;
    if (!checked) {
        // (every one is asked, so the line says which. 0xE3A8C0 may carry the potion hide's hook: d2rsig::Check vouches for a function we hooked with the bytes the hook proved)
        const bool ctor = d2rsig::Check(RVA_STRING_CTOR, kStringCtorBytes, sizeof kStringCtorBytes);
        const bool load = d2rsig::Check(RVA_LOAD_UNITDEF, kLoadBytes, sizeof kLoadBytes);
        const bool place = d2rsig::Check(RVA_PLACE, kPlaceBytes, sizeof kPlaceBytes);
        const bool update = d2rsig::Check(RVA_UPDATE_WORLD, kUpdateWorldBytes, sizeof kUpdateWorldBytes);
        checked = ctor && load && place && update ? 1 : -1;
        if (checked > 0)
            LogF("vrcam: engine unit - ready: the game's four functions are where 3.3.93787 has them (%s) - items can be drawn by the game (the belt potions)",
                 potionhide::HooksIn() ? "0xE3A8C0 carries the potion hide's hook, vouched for by its original bytes" : "no hook of ours on them");
        else if (!told) {
            told = true;
            LogF("vrcam: engine unit - the game's functions are not where 3.3.93787 has them: no items drawn by the game (string ctor %s, load %s, place %s, update world 0xE3A8C0 %s; "
                 "the potion hide's hooks are %s)", ctor ? "ok" : "NOT", load ? "ok" : "NOT", place ? "ok" : "NOT", update ? "ok" : "NOT", potionhide::HooksIn() ? "in" : "not in");
        }
    }
    if (checked < 0) { for (std::atomic<bool>& g : g_slabGivenUp) g.store(true); return; }   // ([ceiling] game_ceiling: no slab can be laid - our own ceiling stays)
    const uint32_t gen = g_wantGen.load();
    if (gen != g_doneGen) {
        g_doneGen = gen;
        AcquireSRWLockShared(&g_lock);
        const std::string want = g_want;
        ReleaseSRWLockShared(&g_lock);
        if (!want.empty()) RunTest(want);
    }
    ++g_shownFrame;
    BeltFrame(g_fullBody.load());
    if (g_set.weightGrabItems.load() || !g_copies.empty()) HeldItemFrame(g_fullBody.load());   // [weight] grab_items: the copy of an item taken off the ground
    if (g_remoteCopiesOn.load()) RemoteCopiesFrame();   // [multiplayer] pose_share_held: the other players' potions and items
    WristOrbsFrame(g_fullBody.load());   // [hud] wrist_game_models: the life and mana medallions as the game's own models
    PublishOwnModels();   // ([weight] world_contact: the belt's bottles and what the hands hold are never walls)
    if (const unsigned long code = GuardedSkyDomeFrame()) {   // [sky] game_sky: our sky as the game's own dome - a fault in it told, not swallowed
        static unsigned long toldCode = 0;
        static int toldStep = -1;
        const int step = g_domeStep.load();
        if (code != toldCode || step != toldStep) {
            toldCode = code;
            toldStep = step;
            LogF("vrcam: game sky - the domes' frame FAULTED (exception 0x%08lX) at step %d, %s - the cave dome and the ceiling after it skipped "
                 "this frame; told once a change", code, step, DomeStepName(step));
        }
    }
    CaveDomeFrame();  // [sky] game_dome_caves: a black dome round the eye underground
    CeilingFrame();   // [ceiling] game_ceiling: the cave ceiling as the game's own lit rock
    HolesFrame();     // [holes] patch_world: the world's holes patched by the game's own pieces
}

// [sky] summit_backdrop_hidden: at the Arreat Summits (120, 137) the game's backdrop - a picture of the ground far below, made for the view from above - stood as a flat
// wall in first person (Serg's 40.webp, flat); hidden in VR F4 and flat F3, our sky and fog in its place. The views from above and F2 keep it.
bool SummitBackdropOff() {
    if (!g_set.summitBackdropHidden.load() || !g_gameSkyboxHere.load() || !g_enabled.load() || !g_inWorld.load()) return false;
    return g_set.platform.load() == 1 ? FullBody() : g_set.flatView.load() == 3;
}

// [sky] weather_first_person (vr/weather_place.h): the game's weather box - rain, snow - laid at its own camera every update (0x5F2330 in the
// decrypted image of dump_ingame; found by its pattern, game_sigs.h "WeatherPlace"). After the game's update, in VR F4 and flat F3: hidden
// far under the world, or laid at our head. Only the weather entity's own matrices (its local and, a root, its world) are written - on the
// game's thread, right after the game wrote them itself. The hook goes in once the setting asks for it; 0 again, the call passes through.
namespace weather {
constexpr uint64_t RVA_PLACE = 0x5F2330;
// mov [rsp+10h],rbx; push rdi; sub rsp,0B0h (the cookie's rip-relative load follows: not in the bytes)
constexpr uint8_t kPlaceBytes[13] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00};
constexpr size_t kSystemOffEntity = 0x64, kXformLocal = 0x00, kXformWorld = 0x40, kXformParent = 0xC0;
using PlaceFn = void(__fastcall*)(void* system);
PlaceFn g_orig = nullptr;
bool g_in = false, g_failed = false;
std::atomic<uint32_t> g_moved{0}, g_seenEntity{0xFFFFFFFFu};
std::atomic<int> g_toldMode{-1};
std::atomic<float> g_gameAt[3], g_putAt[3];

bool FirstPerson() {
    if (!g_enabled.load(std::memory_order_relaxed)) return false;
    return g_set.platform.load(std::memory_order_relaxed) == 1 ? FullBody() : g_set.flatView.load(std::memory_order_relaxed) == 3;
}

// After the game's own update: its matrices given our place (or none, outside first person / at 0)
void Move(void* system) noexcept {
    const int mode = g_set.weatherFirstPerson.load(std::memory_order_relaxed);
    if (!weatherplace::Wanted(mode, FirstPerson(), g_inWorld.load(std::memory_order_relaxed))) return;
    __try {
        const uint32_t e = *reinterpret_cast<const uint32_t*>(reinterpret_cast<uintptr_t>(system) + kSystemOffEntity);
        uintptr_t comp = 0;
        if (e == 0xFFFFFFFFu || !skel::EntityTransform(e, &comp) || !comp) return;
        const uint32_t parent = *reinterpret_cast<const uint32_t*>(comp + kXformParent);
        if (!weatherplace::IsRoot(parent)) return;   // (a parent: its local is not a world place - left as the game has it)
        float local[16], world[16];
        std::memcpy(local, reinterpret_cast<const void*>(comp + kXformLocal), sizeof local);
        std::memcpy(world, reinterpret_cast<const void*>(comp + kXformWorld), sizeof world);
        const float game[3] = {local[12], local[13], local[14]};
        float head[3], at[3];
        DomeCentre(head);
        if (!weatherplace::Place(mode, game, head, at) || !weatherplace::SetPlace(local, at) || !weatherplace::SetPlace(world, at)) return;
        std::memcpy(reinterpret_cast<void*>(comp + kXformLocal), local, sizeof local);
        std::memcpy(reinterpret_cast<void*>(comp + kXformWorld), world, sizeof world);
        g_seenEntity.store(e, std::memory_order_relaxed);
        for (int i = 0; i < 3; ++i) { g_gameAt[i].store(game[i], std::memory_order_relaxed); g_putAt[i].store(at[i], std::memory_order_relaxed); }
        g_moved.fetch_add(1, std::memory_order_relaxed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
void __fastcall HookPlace(void* system) {
    g_orig(system);
    if (system) Move(system);
}

// The update thread: tried until the bytes are there (their page decrypted), once the setting asks
void Install() {
    if (g_in || g_failed || !g_ctx || weatherplace::ClampMode(g_set.weatherFirstPerson.load()) == weatherplace::kGame) return;
    if (!d2rsig::Addr(RVA_PLACE)) {
        static unsigned tries = 0;
        if (!d2rsig::NotInBuild(RVA_PLACE) && ++tries < 120) return;   // (not found yet: asked again)
        g_failed = true;
        Log("vrcam: weather - the game's weather update (WeatherPlace) NOT found in this build: its rain and snow stay at its camera");
        return;
    }
    g_in = d2rsig::Hook(RVA_PLACE, kPlaceBytes, sizeof kPlaceBytes, (void*)&HookPlace, (void**)&g_orig);
    g_failed = !g_in;
    if (g_in)
        LogF("vrcam: weather - hook in on the game's weather update (RVA 0x%llX): its rain and snow %s in VR F4 and flat F3 ([sky] weather_first_person)",
             (unsigned long long)d2rsig::Rva(RVA_PLACE), weatherplace::ModeName(g_set.weatherFirstPerson.load()));
    else
        Log("vrcam: weather - the hook on the game's weather update FAILED: its rain and snow stay at its camera");
}

// Every 10 s while it moves: where the game put the box and where it went (a line also when the mode in force changes)
void Tick() {
    if (!g_in) return;
    static ULONGLONG last = 0;
    const int mode = weatherplace::Wanted(g_set.weatherFirstPerson.load(), FirstPerson(), g_inWorld.load()) ? g_set.weatherFirstPerson.load() : 0;
    if (g_toldMode.exchange(mode) != mode)
        LogF("vrcam: weather - the game's rain and snow: %s ([sky] weather_first_person %d)", mode ? weatherplace::ModeName(mode) : "at the game's camera, as it lays them",
             g_set.weatherFirstPerson.load());
    if (GetTickCount64() - last < 10000) return;
    last = GetTickCount64();
    if (const uint32_t n = g_moved.exchange(0))
        LogF("vrcam: weather, 10 s: its box (entity %08X) moved %u times - the game laid it at %.1f %.1f %.1f, ours at %.1f %.1f %.1f (%s)", g_seenEntity.load(), n,
             g_gameAt[0].load(), g_gameAt[1].load(), g_gameAt[2].load(), g_putAt[0].load(), g_putAt[1].load(), g_putAt[2].load(), weatherplace::ModeName(mode));
}
}  // namespace weather

void __fastcall HookSetMutable(void* registry, bool allowed) {
    if (!allowed && g_inWorld.load() && (uintptr_t)registry == d2rsig::Addr(RVA_REGISTRY)) {
        DWORD owner = 0;
        uint8_t open = 0;
        // still open, and ours to change: the game's own thread
        if (ReadRegistry((uintptr_t)registry, &owner, &open) && open && owner == GetCurrentThreadId()) { Work(); owneffects::Tick(); owneffects::HideSummitBackdrop(SummitBackdropOff()); owneffects::BackdropTick(); }   // (the hero's own effects: their opacity, vr/own_effects.cpp)
    }
    g_origSetMutable(registry, allowed);
}

// Wanted at all: the belt's models on, or a test asked for. Unticked, nothing is hooked.
bool Wanted() {
    AcquireSRWLockShared(&g_beltLock);
    const bool on = g_beltSet.on;
    ReleaseSRWLockShared(&g_beltLock);
    return on || g_wantGen.load() != g_doneGen || owneffects::Wanted() || g_set.weightGrabItems.load() ||   // ([weight] grab_items: the copy in the hand)
           g_remoteCopiesOn.load() ||                                                                            // ([multiplayer] pose_share_held: the others' copies)
           g_set.gameSky.load() ||                                                                               // ([sky] game_sky: the dome)
           g_set.summitBackdropHidden.load() ||                                                                  // ([sky] summit_backdrop_hidden: the Summit's ground picture)
           g_set.gameCeiling.load() ||                                                                           // ([ceiling] game_ceiling: the slab)
           g_set.holesPatch.load() ||                                                                            // ([holes] patch_world: the patches)
           g_set.wristGameModels.load();                                                                         // ([hud] wrist_game_models: the medallions)
}
void InstallHook() {
    if (g_hook.load() != 0 || !Wanted()) return;
    const bool in = d2rsig::Hook(RVA_SET_MUTABLE, kSetMutableBytes, sizeof kSetMutableBytes, (void*)&HookSetMutable, (void**)&g_origSetMutable);
    g_hook.store(in ? 1 : -1);
    if (in) InstallTeardownHook();   // (what we place is let go when a game is left)
    Log(in ? "vrcam: engine items in - the game's own models placed while its registry may be changed (0x912C90)"
           : "vrcam: engine items hook FAILED (0x912C90 not as in 3.3.93787) - no items drawn by the game");
}

// From VrFrame, every frame: what the next window gets to know.
void Frame(bool fullBody) { g_fullBody.store(fullBody); }
}  // namespace engitem

}  // namespace vrc
