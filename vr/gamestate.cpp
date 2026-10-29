// gamestate.cpp - what the hero holds and whether a menu is open, read through
// D2RLoader's services and published: for BodyWalk as Game Link's GameState
// (vr/link.cpp: the Mapping tabs, FlatVR's Head Lock and screen distance), and
// for D2R VR Settings (shared/d2r_vr_state.h).
//
// Items: the local player and his equipped items may only be read on the UI
// thread, so Tick() (vrcam's timer, 5 times a second) queues a read there.
// Body locations 4 and 5 are the right and left hand of the active set.
//
// Menus: D2R's UI sends messages ("target", "command", "text"). PanelManager's
// OpenPanel / OpenExclusivePanel / ClosePanel / UnloadPanel carry the panel's
// name in the text and UnloadAll clears them; the set of open panels is kept,
// and a menu is open while one of them is a menu (inventory, trade, stash,
// dialogue...) - not the HUD, the automap or the chat, which are open all game.
// The first guess (any "open" in a command) stuck at "menu" for the whole game:
// the HUD's own panels open that way and never close.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>

#include <D2RLPlugin/api.h>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "d2r_vr_state.h"
#include "thrust_stroke.h"   // hit by touch's thrust: the weapon's class (1ht, 2ht) found in the item rows
#include "link.h"
#include "ui_ticket.h"
#include "panel_input.h"
#include "shield_smite_game.h"
#include "double_swing_game.h"
#include "melee_arm_game.h"
#include "point_cast_wire.h"
#include "render_resolution.h"
#include "sigscan.h"
#include "minimap_side.h"
#include "plate_world.h"   // the hook's latest anchor for the plate, taken with each read (FollowBox)
#include "hand_inventory.h"   // [openxr] hand_inventory: which panels are the inventory
#include "notice_card.h"   // [hud] notice_card: the game's quest / level-up notice, counted and its panel's rect read here

namespace skel { int HeroClass(); }   // the hero's class from the posed model's folder (vr/skeletons.cpp): -1 not known

using namespace D2RL;

namespace gamestate {
namespace {

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
// The map: the game's "AutoMap" panel, open or closed as PanelManager says
// (OpenPanel / ClosePanel 'AutoMap', the same messages its own key sends), and
// set by sending those messages ourselves (SetAutoMap) - no key, no button.
const WidgetService* g_widgets = nullptr;
std::atomic<int> g_autoMap{0};
const InventoryService* g_inventory = nullptr;
const ItemService* g_items = nullptr;
const SharedEventService* g_events = nullptr;
SharedEvents::ListenerHandle g_uiListener = 0;

HANDLE g_map = nullptr;
D2RVR_State* g_state = nullptr;
uiticket::Ticket g_readQueued;   // ReadItems() queued (ui_ticket.h: a run that never comes is queued again)
std::atomic<uint32_t> g_weapon{D2RVR_WEAPON_UNKNOWN};
std::atomic<uint32_t> g_menu{0};
std::atomic<bool> g_npcDialogOnly{false};   // the only menu open is an NPC's small dialogue box (Talk / Trade / Cancel), [hud] npc_dialog_no_bar
std::atomic<bool> g_laserPanels{false};   // [openxr] laser on: d2r-pcvr's extra modal panels are menus too (IsMenuPanel)
std::atomic<uint32_t> g_set{0};      // 1 / 2: the active weapon set
std::atomic<uint32_t> g_view{0};     // vrcam's view: 0 off, 1 first person, 2 third person, 3 F1 in perspective, 4 the game on the floor

void Log(const char* msg) { if (g_ctx) logasync::Info(g_ctx, msg); }

std::string CodeText(uint32_t code) {
    char c[5] = {(char)(code & 0xFF), (char)((code >> 8) & 0xFF), (char)((code >> 16) & 0xFF), (char)((code >> 24) & 0xFF), 0};
    for (char& ch : c) if (ch && (ch < 32 || ch > 126)) ch = '?';
    return c;
}

// Weapons.txt codes: bows (normal, exceptional, elite, Amazon) and crossbows.
bool IsBow(const std::string& c) {
    static const std::set<std::string> k = {"sbw ", "hbw ", "lbw ", "cbw ", "sbb ", "lbb ", "swb ", "lwb ",
                                            "8sb ", "8hb ", "8lb ", "8cb ", "8s8 ", "8l8 ", "8sw ", "8lw ",
                                            "6sb ", "6hb ", "6lb ", "6cb ", "6s7 ", "6l7 ", "6sw ", "6lw ",
                                            "am1 ", "am2 ", "am6 ", "am7 ", "amb ", "amc "};
    return k.count(c) != 0;
}
bool IsCrossbow(const std::string& c) {
    static const std::set<std::string> k = {"lxb ", "mxb ", "hxb ", "rxb ", "8lx ", "8mx ", "8hx ", "8rx ", "6lx ", "6mx ", "6hx ", "6rx "};
    return k.count(c) != 0;
}

// The weapon's kind, from the game's tables: an Items row holds the index of
// its ItemTypes row, at an offset the SDK does not name. It is found once, on
// the UI thread (the tables answer only there): the u16 that holds the right
// type index for a short bow, a light crossbow, a short sword and a hand axe
// at once. Each kind's ItemTypes index then maps to a BodyWalk tab.
const DataTableService* g_tables = nullptr;
std::atomic<uint32_t> g_type{D2RVR_TYPE_UNKNOWN};
std::atomic<uint32_t> g_held{0};   // bit 0: something in the right hand's slot (body location 4), bit 1: the left's (5)
int g_typeOffset = -2;                       // -2 not tried yet, -1 failed
DataTables::Bank g_bank = DataTables::Bank::Lod;
std::vector<std::pair<uint32_t, uint32_t>> g_typeIndex;   // ItemTypes row index -> D2RVR_WeaponType
std::atomic<bool> g_leftShield{false};   // [gameplay] shield_smite: a shield in the left hand of the active set
std::vector<uint32_t> g_shieldTypes;     // ItemTypes row indexes of the shields: shie, ashd (paladin), head (necromancer)
std::atomic<int> g_headSide{-1};         // [shield] heads_hang: the hand slot of the active set a Necromancer's head (ItemTypes head) is in - 1 left,
                                         // 0 right (the game takes a shield in either slot: one in the weapon's box is body location 4), -1 none
int g_headType = -1;                     // ItemTypes row index of head; -1 not found
// Weapons.txt "2handed" in an Items row: a byte at an offset the SDK does not
// name either, found the same way - the one byte that reads 1 for a set of
// two-handed weapons and 0 for a set of one-handed ones (CalibrateTwoHanded).
int g_twoOffset = -2;                        // -2 not tried yet, -1 failed
std::atomic<uint32_t> g_two{0};
std::atomic<uint32_t> g_stickBits{0};   // bits 13 - 15 of the state block's twoHanded: did the runtime bind the thumbsticks (vr/xr_stick_check.h; xr.cpp sets it)
std::atomic<uint32_t> g_from{0};
std::atomic<uint32_t> g_kindSide[2] = {};      // D2RVR_TYPE_* of each hand's slot alone (0: empty, a shield or a quiver) - for two blades (HandKind)
std::atomic<uint32_t> g_codeSide[2] = {};      // each hand's item code, four characters (space padded) as one number, a | b << 8 | c << 16 | d << 24 (HandCode)
std::atomic<uint32_t> g_handsKey{0};          // what is in the two hands, as one number (HandsKey)             // the hand slot the weapon kind came from: 0 right, 1 left              // D2RVR_TWO_HANDED* bits of what is held

constexpr uint32_t Four(const char* s) { return DataTables::MakeFourCC(s[0], s[1], s[2], s[3]); }

bool TypeRow(uint32_t code, uint32_t* index) {
    DataTables::RowView v{};
    v.structSize = DataTables::RowViewSize;
    if (g_tables->findRowByCode(g_ctx, g_bank, DataTables::TableId::ItemTypes, code, &v) != DataTables::Result::Success) return false;
    *index = v.rowIndex;
    return true;
}

const uint8_t* ItemRow(uint32_t code, uint32_t* size) {
    DataTables::RowView v{};
    v.structSize = DataTables::RowViewSize;
    if (g_tables->findRowByCode(g_ctx, g_bank, DataTables::TableId::Items, code, &v) != DataTables::Result::Success || !v.row) return nullptr;
    *size = v.rowSize;
    return (const uint8_t*)v.row;
}

void CalibrateTypes() {
    g_typeOffset = -1;
    if (!g_tables) { Log("gamestate: no data table service - weapon kinds unknown, tabs by bow/crossbow only"); return; }
    for (DataTables::Bank bank : {DataTables::Bank::Lod, DataTables::Bank::Rotw, DataTables::Bank::Classic}) {
        g_bank = bank;
        const char* probe[4][2] = {{"sbw ", "bow "}, {"lxb ", "xbow"}, {"ssd ", "swor"}, {"hax ", "axe "}};
        const uint8_t* rows[4]; uint32_t sizes[4], want[4];
        bool ok = true;
        for (int i = 0; i < 4 && ok; ++i) ok = (rows[i] = ItemRow(Four(probe[i][0]), &sizes[i])) && TypeRow(Four(probe[i][1]), &want[i]);
        if (!ok) continue;
        const uint32_t size = std::min({sizes[0], sizes[1], sizes[2], sizes[3]});
        for (uint32_t o = 0; o + 2 <= size; o += 2) {
            bool all = true;
            for (int i = 0; i < 4 && all; ++i) { uint16_t t; memcpy(&t, rows[i] + o, 2); all = t == want[i]; }
            if (all) { g_typeOffset = (int)o; break; }
        }
        if (g_typeOffset >= 0) break;
    }
    if (g_typeOffset < 0) { Log("gamestate: the item type field was not found - weapon kinds unknown, tabs by bow/crossbow only"); return; }
    static const struct { const char* code; uint32_t type; } kKinds[] = {
        {"bow ", D2RVR_TYPE_BOW}, {"abow", D2RVR_TYPE_BOW}, {"xbow", D2RVR_TYPE_CROSSBOW},
        {"swor", D2RVR_TYPE_SWORD}, {"axe ", D2RVR_TYPE_AXE},
        {"club", D2RVR_TYPE_MACE}, {"mace", D2RVR_TYPE_MACE}, {"hamm", D2RVR_TYPE_MACE},
        {"scep", D2RVR_TYPE_SCEPTER}, {"wand", D2RVR_TYPE_WAND}, {"staf", D2RVR_TYPE_STAFF},
        {"pole", D2RVR_TYPE_POLEARM}, {"spea", D2RVR_TYPE_SPEAR}, {"aspe", D2RVR_TYPE_SPEAR},
        {"knif", D2RVR_TYPE_DAGGER}, {"tkni", D2RVR_TYPE_THROWING}, {"taxe", D2RVR_TYPE_THROWING},
        {"jave", D2RVR_TYPE_JAVELIN}, {"ajav", D2RVR_TYPE_JAVELIN},
        {"h2h ", D2RVR_TYPE_CLAW}, {"h2h2", D2RVR_TYPE_CLAW}, {"orb ", D2RVR_TYPE_ORB}};
    std::string found;
    for (const char* code : {"shie", "ashd", "head"}) {
        uint32_t index = 0;
        if (TypeRow(Four(code), &index)) g_shieldTypes.push_back(index);
    }
    {
        uint32_t index = 0;
        if (TypeRow(Four("head"), &index)) g_headType = (int)index;
    }
    for (const auto& k : kKinds) {
        uint32_t idx;
        if (TypeRow(Four(k.code), &idx)) g_typeIndex.push_back({idx, k.type});
        else found += std::string(" -") + k.code;
    }
    char b[200];
    snprintf(b, sizeof b, "gamestate: item type at row offset 0x%X (bank %u), %d weapon kinds%s", g_typeOffset, (unsigned)g_bank,
             (int)g_typeIndex.size(), found.empty() ? "" : (", not in ItemTypes:" + found).c_str());
    Log(b);
}

// Weapons.txt "2handed" in the Items row (bank as CalibrateTypes found it): the
// byte - or failing that, the bit - that is 1 for every weapon of the first
// list and 0 for every one of the second. Swords stay out of the first try (a
// barbarian's sword is "1or2handed" as well, should a build keep that apart);
// they only narrow it down when more than one place fits.
void CalibrateTwoHanded() {
    g_twoOffset = -1;
    if (!g_tables || g_typeOffset < 0) return;
    // spear, bardiche, short staff, short bow, light crossbow, large axe, great axe, maul
    static const char* const kTwo[] = {"spr ", "bar ", "sst ", "sbw ", "lxb ", "lax ", "gax ", "mau "};
    // short sword, long sword, broad sword, scimitar, hand axe, axe, wand, club, dagger
    static const char* const kOne[] = {"ssd ", "lsd ", "bsd ", "scm ", "hax ", "axe ", "wnd ", "clb ", "dgr "};
    // two-handed sword, claymore, great sword
    static const char* const kSwords[] = {"2hs ", "clm ", "gsd "};
    struct Probe { const uint8_t* row; uint32_t size; uint8_t want; };
    std::vector<Probe> probes;
    auto add = [&](const char* const* codes, size_t count, uint8_t want) {
        for (size_t i = 0; i < count; ++i) {
            uint32_t size = 0;
            if (const uint8_t* r = ItemRow(Four(codes[i]), &size)) probes.push_back({r, size, want});
        }
    };
    add(kTwo, std::size(kTwo), 1);
    add(kOne, std::size(kOne), 0);
    if (probes.size() < 10) {
        char b[120]; snprintf(b, sizeof b, "gamestate: two-handed field not looked for - only %d probe weapons in the tables", (int)probes.size()); Log(b);
        return;
    }
    // candidates: (offset << 4) | bit, bit 8 = the whole byte
    auto find = [&](std::vector<uint32_t>& out) {
        out.clear();
        uint32_t size = UINT32_MAX;
        for (const Probe& p : probes) size = std::min(size, p.size);
        for (int bit = 8; bit >= 0 && out.empty(); --bit)   // whole bytes first
            for (uint32_t o = 0; o < size; ++o) {
                bool all = true;
                for (const Probe& p : probes) {
                    const uint8_t v = bit == 8 ? p.row[o] : (uint8_t)((p.row[o] >> bit) & 1u);
                    if (v != p.want) { all = false; break; }
                }
                if (all) out.push_back((o << 4) | (uint32_t)bit);
            }
    };
    std::vector<uint32_t> found;
    find(found);
    if (found.size() > 1) { add(kSwords, std::size(kSwords), 1); find(found); }
    std::string list;
    for (size_t i = 0; i < found.size() && i < 8; ++i) {
        char c[24]; snprintf(c, sizeof c, " 0x%X%s", found[i] >> 4, (found[i] & 15u) == 8 ? "" : (":" + std::to_string(found[i] & 15u)).c_str());
        list += c;
    }
    if (found.size() == 1) g_twoOffset = (int)found[0];
    char b[300];
    snprintf(b, sizeof b, "gamestate: two-handed field %s (%d place(s) fit:%s)", g_twoOffset >= 0 ? "found" : "NOT found - by kind only (spear, polearm, staff)",
             (int)found.size(), list.empty() ? " none" : list.c_str());
    Log(b);
    if (g_twoOffset >= 0) {   // what the swords read there: a barbarian's two-handed sword must be 1
        std::string swords;
        for (const char* s : kSwords) {
            uint32_t size = 0;
            const uint8_t* r = ItemRow(Four(s), &size);
            const uint32_t o = (uint32_t)g_twoOffset >> 4, bit = (uint32_t)g_twoOffset & 15u;
            swords += std::string(" ") + std::string(s, 3) + "=" + (r && o < size ? std::to_string(bit == 8 ? r[o] : (r[o] >> bit) & 1u) : "?");
        }
        snprintf(b, sizeof b, "gamestate: two-handed swords read:%s", swords.c_str());
        Log(b);
    }
}

// The item's "2handed"; -1 not known (no field found, or no such row).
int TwoHandedOf(const std::string& code) {
    if (code.size() != 4 || g_twoOffset < 0) return -1;
    uint32_t size = 0;
    const uint8_t* row = ItemRow(Four(code.c_str()), &size);
    const uint32_t o = (uint32_t)g_twoOffset >> 4, bit = (uint32_t)g_twoOffset & 15u;
    if (!row || o >= size) return -1;
    return bit == 8 ? row[o] != 0 : (int)((row[o] >> bit) & 1u);
}

// [weight] hero_strength (vr/hero_strength.h): the weapon's Strength requirement - Weapons.txt "reqstr" - in its Items row, a field the SDK does not name either; found the way the
// two above are: the offset and width at which a set of weapons whose requirements the game's own tables fix (a few of each tier) all read exactly those numbers. A build or a
// mod that changes them finds no field: the requirement is then a typical one for the weapon's kind (vr/hero_strength.h NominalReq).
int g_reqOffset = -2;                        // -2 not tried yet, -1 failed
int g_reqWidth = 0;                          // bytes: 1, 2 or 4
std::atomic<int> g_reqSide[2] = {{-1}, {-1}};   // each hand's weapon: its requirement, -1 not known
std::atomic<int> g_heroStrength{-1};         // the hero's Strength stat, -1 not read
std::atomic<bool> g_statFaulted{false};      // the stat getter faulted: not called again until the game restarts
std::atomic<bool> g_wantStrength{false};     // [weight] hero_strength is ticked: only then is the stat read at all (SetWantHeroStrength)

void CalibrateReqStr() {
    g_reqOffset = -1;
    if (!g_tables || g_typeOffset < 0) return;
    static const struct { const char* code; uint32_t req; } kAnchors[] = {
        {"axe ", 32}, {"lax ", 35}, {"2hs ", 35}, {"clm ", 47}, {"mpi ", 49}, {"lsd ", 55}, {"pik ", 60}, {"wax ", 67}, {"mau ", 69}, {"hal ", 75}, {"gma ", 99}, {"gsd ", 100}};
    struct Probe { const uint8_t* row; uint32_t size; uint32_t want; };
    std::vector<Probe> probes;
    for (const auto& a : kAnchors) {
        uint32_t size = 0;
        if (const uint8_t* r = ItemRow(Four(a.code), &size)) probes.push_back({r, size, a.req});
    }
    if (probes.size() < 8) {
        char b[160]; snprintf(b, sizeof b, "gamestate: strength requirement not looked for - only %d of %d probe weapons in the tables", (int)probes.size(), (int)std::size(kAnchors)); Log(b);
        return;
    }
    uint32_t size = UINT32_MAX;
    for (const Probe& p : probes) size = std::min(size, p.size);
    for (const int width : {2, 4, 1}) {
        std::vector<uint32_t> found;
        for (uint32_t o = 0; o + (uint32_t)width <= size; o += (uint32_t)width) {
            bool all = true;
            for (const Probe& p : probes) {
                uint32_t v = 0;
                memcpy(&v, p.row + o, (size_t)width);
                if (v != p.want) { all = false; break; }
            }
            if (all) found.push_back(o);
        }
        if (found.size() == 1) { g_reqOffset = (int)found[0]; g_reqWidth = width; break; }
        if (found.size() > 1) {   // more than one place says the same: not a field to trust
            char b[160]; snprintf(b, sizeof b, "gamestate: strength requirement: %d places fit at width %d - not used", (int)found.size(), width); Log(b);
            return;
        }
    }
    char b[220];
    if (g_reqOffset >= 0) snprintf(b, sizeof b, "gamestate: strength requirement at row offset 0x%X (%d bytes, bank %u, %d probe weapons)", g_reqOffset, g_reqWidth, (unsigned)g_bank, (int)probes.size());
    else snprintf(b, sizeof b, "gamestate: the strength requirement field was not found (%d probe weapons) - a typical one for the weapon's kind is used", (int)probes.size());
    Log(b);
}

// The item's Strength requirement; -1 not known (no field found, or no such row).
int ReqStrOf(const std::string& code) {
    if (code.size() != 4 || g_reqOffset < 0) return -1;
    uint32_t size = 0;
    const uint8_t* row = ItemRow(Four(code.c_str()), &size);
    if (!row || (uint32_t)(g_reqOffset + g_reqWidth) > size) return -1;
    uint32_t v = 0;
    memcpy(&v, row + g_reqOffset, (size_t)g_reqWidth);
    return v <= 1000 ? (int)v : -1;
}

// Hit by touch's thrust (vr/thrust_stroke.h): the weapon's animation class - Weapons.txt "wclass", the three letters the game swings it with (1ht one-hand thrust, 2ht
// two-hand thrust, 1hs, stf, bow...) - in its Items row, a field the SDK does not name either; found the way the ones above are: the place where a few base items whose
// class the game's own tables fix all spell it. A build that keeps it otherwise finds none: the weapon's kind decides then (spears, daggers, javelins thrust).
int g_classOffset = -2;                       // -2 not tried yet, -1 failed
std::atomic<int> g_thrustSide[2] = {{-1}, {-1}};   // each hand's weapon: 1 a thrust class, 0 another, -1 not known

void CalibrateWeaponClass() {
    g_classOffset = -1;
    if (!g_tables || g_typeOffset < 0) return;
    // dagger, javelin, spear, trident, short sword, hand axe, short staff, short bow, light crossbow
    static const struct { const char* code; const char* cls; } kAnchors[] = {
        {"dgr ", "1ht"}, {"jav ", "1ht"}, {"spr ", "2ht"}, {"tri ", "2ht"}, {"ssd ", "1hs"}, {"hax ", "1hs"}, {"sst ", "stf"}, {"sbw ", "bow"}, {"lxb ", "xbw"}};
    std::vector<thrust::ClassProbe> probes;
    for (const auto& a : kAnchors) {
        uint32_t size = 0;
        if (const uint8_t* r = ItemRow(Four(a.code), &size)) probes.push_back({r, size, a.cls});
    }
    char b[220];
    if (probes.size() < 6) {
        snprintf(b, sizeof b, "gamestate: weapon class not looked for - only %d of %d probe weapons in the tables (a thrust is by kind: spear, dagger, javelin)", (int)probes.size(), (int)std::size(kAnchors));
        Log(b);
        return;
    }
    uint32_t at[4] = {};
    const size_t found = thrust::FindClassField(probes.data(), probes.size(), at, 4);
    if (found >= 1) g_classOffset = (int)at[0];
    if (g_classOffset >= 0)
        snprintf(b, sizeof b, "gamestate: weapon class at row offset 0x%X (%d place(s) fit, %d probe weapons) - a thrust is by the weapon's own class (1ht, 2ht)", g_classOffset, (int)found, (int)probes.size());
    else snprintf(b, sizeof b, "gamestate: the weapon class field was not found (%d probe weapons) - a thrust is by kind: spear, dagger, javelin", (int)probes.size());
    Log(b);
}

// 1: the item's class is a thrust class (1ht, 2ht); 0: another; -1 not known (no field found, or no such row).
int ThrustOf(const std::string& code) {
    if (code.size() != 4 || g_classOffset < 0) return -1;
    uint32_t size = 0;
    const uint8_t* row = ItemRow(Four(code.c_str()), &size);
    if (!row || (uint32_t)g_classOffset + 4 > size) return -1;
    return thrust::ThrustClass(row + g_classOffset) ? 1 : 0;
}

// The corner map for the hand kept in a corner (vr/minimap_side.h; the setter is bearstonem/d2r-pcvr's finding, MIT):
// -1 off, else [hud] map_corner - set by vrcam in VR with [hud] map=1. Plain data only here (__try).
std::atomic<int> g_mapSideCorner{-1};
// [hud] map_click_big (vr/map_laser.h): the map made big by the laser - the game's whole-screen map (mode 0) by the same setter, put back in its
// corner by the Keeper once it is let go. Set by xr.cpp.
std::atomic<bool> g_mapBig{false};
int ReadMapMode(uint64_t miniRva, uint64_t leftRva) noexcept {
    const uintptr_t mini = d2rsig::Addr(miniRva), left = d2rsig::Addr(leftRva);
    if (!mini || !left) return -1;
    __try {
        return minimapside::Mode(*reinterpret_cast<const volatile int32_t*>(mini), *reinterpret_cast<const volatile uint8_t*>(left));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
bool CallMapSetter(uintptr_t fn, int mode) noexcept {
    __try {
        reinterpret_cast<void (*)(int)>(fn)(mode);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// On the UI thread (ReadItems): the game's map in the middle while it is to come into the hand - put in its corner.
void MaintainMapSide(bool havePlayer) {
    static int checked = 0;   // 0 not yet, 1 the setter's bytes as 3.3.93787 has them, -1 not (this build: nothing done)
    static uint64_t triedAt = 0;
    static minimapside::Globals globals;
    static minimapside::Keeper keeper;
    static int told = 0;
    const int corner = g_mapSideCorner.load();
    const bool want = corner >= 0 && havePlayer && g_menu.load() == 0;
    if (!want || checked < 0) return;
    const uint64_t now = GetTickCount64();
    if (checked == 0) {
        if (triedAt && now - triedAt < 5000) return;
        triedAt = now;
        if (!d2rsig::Check(minimapside::kSetterRva, minimapside::kSetterSig, sizeof minimapside::kSetterSig)) {
            if (d2rsig::Addr(minimapside::kSetterRva)) {   // read, and other bytes: another build - the start's Settings.json rewrite only
                checked = -1;
                Log("gamestate: the game's map-mode setter is not where D2R 3.3.93787 has it - the map in the middle is not put in a corner while the game runs "
                    "(only at the start, Settings.json); put the mini map on the right in the game's options");
            }
            return;
        }
        globals = minimapside::MapGlobals(minimapside::kSetterRva, minimapside::kSetterSig);
        checked = 1;
    }
    const int mode = ReadMapMode(globals.mini, globals.left);
    {   // [hud] map_click_big: the whole screen while big (asked again a second later if the game put it back), the Keeper's corner after
        static uint64_t bigAt = 0;
        static int bigTold = 0;
        if (g_mapBig.load()) {
            if (mode != 0 && (!bigAt || now - bigAt > 1000)) {
                bigAt = now ? now : 1;
                if (!CallMapSetter(d2rsig::Addr(minimapside::kSetterRva), 0)) {
                    checked = -1;
                    Log("gamestate: the game's map-mode setter faulted - not called again");
                    return;
                }
                if (bigTold < 5) { ++bigTold; Log("gamestate: the map made big by the laser - the game's whole-screen map (its own setter, this session only)"); }
            }
            return;
        }
        bigAt = 0;
    }
    const int set = keeper.Step(true, mode, corner, now);
    if (set < 0) return;
    if (!CallMapSetter(d2rsig::Addr(minimapside::kSetterRva), set)) {
        checked = -1;
        Log("gamestate: the game's map-mode setter faulted - not called again");
        return;
    }
    if (told < 5) {
        ++told;
        char b[260];
        snprintf(b, sizeof b, "gamestate: the game's map was in the middle (the whole screen: nothing for the hand) - put in the %s corner "
                 "(the game's own setter, this session only; its options keep the player's choice)", set == 1 ? "left" : "right");
        Log(b);
    }
}
// The game's map mode now (minimapside::Mode: 0 the whole screen, 1 the left corner, 2 the right), -1 not known (another build of the
// game, or not read yet): where [hud] map_direction's arrow goes. Read on the UI thread (ReadItems, as MaintainMapSide) only while
// vrcam asked for it within the last second (MapMode); vrcam gets the last value read.
std::atomic<int> g_mapModeLast{-1};
std::atomic<uint64_t> g_mapModeAskedAt{0};
std::atomic<int> g_mapModeChecked{0};   // 0 not yet, 1 the setter's bytes as 3.3.93787 has them, -1 not
minimapside::Globals g_mapModeGlobals;
int MapModeNow() {
    int checked = g_mapModeChecked.load();
    if (checked == 0) {
        if (!d2rsig::Addr(minimapside::kSetterRva)) return -1;   // (not yet: asked again)
        if (d2rsig::Check(minimapside::kSetterRva, minimapside::kSetterSig, sizeof minimapside::kSetterSig)) {
            g_mapModeGlobals = minimapside::MapGlobals(minimapside::kSetterRva, minimapside::kSetterSig);
            checked = 1;
        } else {
            checked = -1;
        }
        g_mapModeChecked.store(checked);
    }
    return checked > 0 ? ReadMapMode(g_mapModeGlobals.mini, g_mapModeGlobals.left) : -1;
}

// The hero's Strength: the game's unit stat getter (stat 0), found through the same three signatures as the life and mana read (vr/hand_vitals_game.cpp, vr/low_hp.cpp). Plain data
// only: __try cannot sit in a function that unwinds C++ objects. -1 when it cannot be read.
int ReadHeroStrength() noexcept {
    constexpr uint64_t kLocalIndex = 0x8B2D0, kClientPlayer = 0x9A480, kStat = 0x2F5020;
    const uintptr_t indexFn = d2rsig::Addr(kLocalIndex), playerFn = d2rsig::Addr(kClientPlayer), statFn = d2rsig::Addr(kStat);
    if (!indexFn || !playerFn || !statFn) return -1;
    __try {
        const int index = reinterpret_cast<int (*)()>(indexFn)();
        if (index < 0 || index >= 8) return -1;
        const uintptr_t player = reinterpret_cast<uintptr_t (*)(int)>(playerFn)(index);
        if (player < 0x10000) return -1;
        if (*reinterpret_cast<const volatile uint32_t*>(player) != 0) return -1;   // a player unit: its type is 0
        const uint8_t* thunk = reinterpret_cast<const uint8_t*>(statFn);
        if (thunk[0] == 0xFF && thunk[1] == 0x25) {   // a jump thunk through a slot the loader fills in: a slot still empty is not a getter
            int32_t disp;
            memcpy(&disp, thunk + 2, sizeof disp);
            if (*reinterpret_cast<const volatile uintptr_t*>(statFn + 6 + (intptr_t)disp) < 0x10000) return -1;
        }
        const int v = reinterpret_cast<int (*)(uintptr_t, int, int)>(statFn)(player, 0, 0);
        return v >= 0 && v <= 2000 ? v : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_statFaulted.store(true);
        return -1;
    }
}

// The six throwing potions (Rancid Gas, Oil, Choking Gas, Exploding, Strangling Gas, Fulminating): item codes. They sit in a weapon slot
// like a javelin and are thrown by the weapon's attack, so they are a thrown weapon here whatever their ItemTypes row says.
constexpr uint32_t kThrowPotions[6] = {Four("gps "), Four("ops "), Four("gpm "), Four("opm "), Four("gpl "), Four("opl ")};
bool IsThrowPotionCode(const std::string& code) {
    if (code.size() != 4) return false;
    const uint32_t c = Four(code.c_str());
    for (const uint32_t p : kThrowPotions) if (c == p) return true;
    return false;
}

// Every Items row's weapon kind by its row index (an item unit's class id, unit +4), built once on the UI thread (the tables answer only
// there) for the readers on other threads: [gameplay] move_while_attacking reads a hero's weapon on the game's server (KindOfItemClass).
constexpr uint32_t kClassKinds = 4096;
std::atomic<uint8_t> g_classKind[kClassKinds] = {};
std::atomic<uint32_t> g_classKindCount{0};
bool g_classKindsBuilt = false;   // the UI thread only
void BuildClassKinds() {
    g_classKindsBuilt = true;
    if (!g_tables || !g_tables->getTable || g_typeOffset < 0) return;
    DataTables::TableView v{};
    v.structSize = DataTables::TableViewSize;
    if (g_tables->getTable(g_ctx, g_bank, DataTables::TableId::Items, &v) != DataTables::Result::Success || !v.rows || !v.rowSize ||
        (uint32_t)g_typeOffset + 2 > v.rowSize)
        return;
    const uint32_t n = std::min<uint32_t>(v.rowCount, kClassKinds);
    uint32_t weapons = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t t;
        memcpy(&t, (const uint8_t*)v.rows + (size_t)i * v.rowSize + g_typeOffset, 2);
        uint8_t kind = 0;
        for (const auto& [idx, type] : g_typeIndex) if (idx == t) { kind = (uint8_t)type; break; }
        g_classKind[i].store(kind, std::memory_order_relaxed);
        weapons += kind != 0;
    }
    g_classKindCount.store(n, std::memory_order_release);
    char b[160];
    snprintf(b, sizeof b, "gamestate: weapon kinds by item class ready (%u rows, %u weapons) - for a hero's weapon read on the game's server", n, weapons);
    Log(b);
}

// [weight] grab_items: each item class's code (the Items table's row index -> its four letters), built once on the UI thread for readers on other threads - the hand that
// took an item off the ground shows our copy of it by its code. The code's place in a row is found from three rows the code finds (findRowByCode).
std::atomic<uint32_t> g_classCode[kClassKinds] = {};
std::atomic<uint32_t> g_classCodeCount{0};
bool g_classCodesBuilt = false;   // the UI thread only
void BuildClassCodes() {
    g_classCodesBuilt = true;
    if (!g_tables || !g_tables->getTable || !g_tables->findRowByCode) return;
    DataTables::TableView v{};
    v.structSize = DataTables::TableViewSize;
    if (g_tables->getTable(g_ctx, g_bank, DataTables::TableId::Items, &v) != DataTables::Result::Success || !v.rows || !v.rowSize) return;
    const char* probe[3] = {"hp1 ", "hax ", "cap "};
    const uint8_t* rows[3];
    uint32_t sizes[3];
    for (int i = 0; i < 3; ++i) if (!(rows[i] = ItemRow(Four(probe[i]), &sizes[i]))) return;
    int offset = -1;
    for (uint32_t o = 0; o + 4 <= std::min({sizes[0], sizes[1], sizes[2], v.rowSize}) && offset < 0; o += 4) {
        bool all = true;
        for (int i = 0; i < 3 && all; ++i) { uint32_t c; memcpy(&c, rows[i] + o, 4); all = c == Four(probe[i]); }
        if (all) offset = (int)o;
    }
    if (offset < 0) { Log("gamestate: item codes by class - the code's place in the Items table not found: no copies of items taken off the ground"); return; }
    const uint32_t n = std::min<uint32_t>(v.rowCount, kClassKinds);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t c;
        memcpy(&c, (const uint8_t*)v.rows + (size_t)i * v.rowSize + offset, 4);
        g_classCode[i].store(c, std::memory_order_relaxed);
    }
    g_classCodeCount.store(n, std::memory_order_release);
    char b[160];
    snprintf(b, sizeof b, "gamestate: item codes by class ready (%u rows, the code at +0x%X of a row) - for the copy of an item taken off the ground", n, (unsigned)offset);
    Log(b);
}

// D2RVR_TYPE_* of one hand's item; 0 for nothing, or a quiver/shield (not a weapon).
uint32_t KindOf(const std::string& code) {
    if (IsThrowPotionCode(code)) return D2RVR_TYPE_THROWING;
    if (code.size() != 4 || g_typeOffset < 0) return 0;
    uint32_t size = 0;
    const uint8_t* row = ItemRow(Four(code.c_str()), &size);
    if (!row || (uint32_t)g_typeOffset + 2 > size) return 0;
    uint16_t t; memcpy(&t, row + g_typeOffset, 2);
    for (const auto& [idx, type] : g_typeIndex) if (idx == t) return type;
    return 0;
}

// A shield: its Items row's type is one of the shield types (not a weapon, so KindOf reads 0 for it).
bool IsShield(const std::string& code) {
    if (code.size() != 4 || g_typeOffset < 0) return false;
    uint32_t size = 0;
    const uint8_t* row = ItemRow(Four(code.c_str()), &size);
    uint16_t type = 0;
    if (!row || (uint32_t)g_typeOffset + sizeof type > size) return false;
    memcpy(&type, row + g_typeOffset, sizeof type);
    return std::find(g_shieldTypes.begin(), g_shieldTypes.end(), (uint32_t)type) != g_shieldTypes.end();
}
// A Necromancer's head: its Items row's type is head.
bool IsHead(const std::string& code) {
    if (code.size() != 4 || g_typeOffset < 0 || g_headType < 0) return false;
    uint32_t size = 0;
    const uint8_t* row = ItemRow(Four(code.c_str()), &size);
    uint16_t type = 0;
    if (!row || (uint32_t)g_typeOffset + sizeof type > size) return false;
    memcpy(&type, row + g_typeOffset, sizeof type);
    return type == (uint32_t)g_headType;
}

void Publish() {
    D2RVR_State s{};
    s.version = D2RVR_STATE_VERSION;
    s.weaponClass = g_weapon.load();
    s.menuOpen = g_menu.load();
    s.weaponSet = g_set.load();
    s.weaponType = g_type.load();
    s.viewMode = g_view.load();
    s.twoHanded = g_two.load();
    link::PublishState(s);
    if (!g_state) return;
    g_state->weaponClass = g_weapon.load();
    g_state->menuOpen = g_menu.load();
    g_state->weaponSet = g_set.load();
    g_state->weaponType = g_type.load();
    g_state->viewMode = g_view.load();
    const int hero = skel::HeroClass();
    g_state->twoHanded = g_two.load() | D2RVRHeroBits(hero) | g_stickBits.load();   // (and the hero's class, for D2R VR Settings: bits 8 - 12; the thumbstick check: 13 - 15)
    {   // once per game start / change of hero
        static int told = -2;
        static const char* const kNames[8] = {"Amazon", "Sorceress", "Necromancer", "Paladin", "Barbarian", "Druid", "Assassin", "Warlock"};
        if (hero != told && hero >= 0 && hero < 8) {
            told = hero;
            char b[120];
            snprintf(b, sizeof b, "gamestate: hero class: %s", kNames[hero]);
            Log(b);
        }
    }
    g_state->version = D2RVR_STATE_VERSION;
    g_state->counter++;
}

std::atomic<uint64_t> g_player{0};   // the local player's handle, as the SDK gives it (vrcam tries it as the hero unit)
void CalibrateObjectLights();   // the Objects table's light fields, below
void ReadSkills();              // which skills fly along the hand, below
extern int g_objLitOffset;

// The belt's bottom row: the item code in each of its four columns (0 = empty), what the potion
// keys 1-4 drink. Each belt item comes as x 0..15 (column x % 4, row x / 4) with y 0, or as
// column x and row y - both read (the d2r-pcvr fork found the same, 2026-10-13).
std::atomic<uint32_t> g_belt[4] = {};
std::atomic<uint32_t> g_beltId[4] = {};   // ... and that item's own id (the game's unit id): a bottle drunk or dropped is told from the next of its kind falling into its place
struct BeltRow { uint32_t code[4] = {}, id[4] = {}; };
Inventory::IterationAction __cdecl BeltItem(const PluginContext*, const Items::ItemInfo* i, void* user) noexcept {
    auto* bottom = static_cast<BeltRow*>(user);
    if (!i || i->container != Items::ItemContainer::Belt) return Inventory::IterationAction::Continue;
    int column = -1, row = -1;
    if (i->x >= 0 && i->x < 16 && i->y == 0) { column = i->x % 4; row = i->x / 4; }
    else if (i->x >= 0 && i->x < 4 && i->y >= 0 && i->y < 4) { column = i->x; row = i->y; }
    if (column >= 0 && row == 0) { bottom->code[column] = i->code; bottom->id[column] = i->runtimeId; }
    return Inventory::IterationAction::Continue;
}
void ReadBelt(const PluginContext* ctx, PlayerHandle player, bool havePlayer) {
    BeltRow bottom;
    if (havePlayer && g_inventory->forEachInventoryItem) {
        const Inventory::ItemFilter filter{sizeof filter, 0, Items::ContainerBit(Items::ItemContainer::Belt), 0};
        if (g_inventory->forEachInventoryItem(ctx, player, &filter, &BeltItem, &bottom) != Inventory::Result::Success)
            return;   // kept as it was
    }
    for (int c = 0; c < 4; ++c) { g_belt[c].store(bottom.code[c]); g_beltId[c].store(bottom.id[c]); }
}

// On the UI thread: the two hands of the active set.
void __cdecl ReadItems(const PluginContext* ctx, void* user) noexcept {
    if (!g_readQueued.Claim(uiticket::FromUser(user))) return;   // come after its lease, replaced
    PlayerHandle player = InvalidPlayerHandle;
    const bool havePlayer = g_inventory && g_inventory->getLocalPlayer(ctx, &player) == Inventory::Result::Success && player != InvalidPlayerHandle;
    g_player.store(havePlayer ? player : 0);
    // The swap count below is one player's (2026-10-13: it went on from the last game's into the
    // next, another hero's): another player starts it again. With no player (the menus, perhaps
    // a load) the set is not known - kept for the same player coming back.
    static PlayerHandle lastPlayer = InvalidPlayerHandle;
    static uint32_t lastHands[2] = {}, lastOther[2] = {}, keptSet = 0;
    static bool seen = false;
    if (havePlayer && player != lastPlayer) {
        lastPlayer = player;
        seen = false;
        lastHands[0] = lastHands[1] = lastOther[0] = lastOther[1] = 0;
        keptSet = 0;
        g_set.store(0);
    }
    panelinput::UiTick(havePlayer, g_menu.load() != 0);   // the laser's mouse done: the pad's layout back in a game
    MaintainMapSide(havePlayer);   // the corner map for the hand: the game's map out of the middle (vr/minimap_side.h)
    if (const uint64_t asked = g_mapModeAskedAt.load(); asked && GetTickCount64() - asked < 1000)   // [hud] map_direction: the map's mode for vrcam
        g_mapModeLast.store(havePlayer ? MapModeNow() : -1);
    if (g_inventory) ReadBelt(ctx, player, havePlayer);
    if (!g_inventory || !g_items || !havePlayer) {
        g_weapon.store(D2RVR_WEAPON_UNKNOWN);
        g_type.store(D2RVR_TYPE_UNKNOWN);
        g_leftShield.store(false);
        g_headSide.store(-1);
        g_held.store(0);
        g_two.store(0);
        g_heroStrength.store(-1);
        g_reqSide[0].store(-1);
        g_reqSide[1].store(-1);
        g_thrustSide[0].store(-1);
        g_thrustSide[1].store(-1);
        if (const uint32_t s = g_set.exchange(0)) keptSet = s;
        Publish();
        return;
    }
    if (keptSet) { g_set.store(keptSet); keptSet = 0; }
    // Body locations 4/5 are the hands as the game asks for them; 11/12 the
    // other set's. Which set is active - I or II - the SDK does not say, so:
    // the item's own body location, if it reads 11/12 while it sits in the
    // hands, says set II; failing that, every swap (the two pairs trading
    // places) flips the count, starting from set I.
    std::string code[2];          // [0] right (body location 4), [1] left (5)
    int32_t ownLoc[2] = {-1, -1}; // the items' own body location
    uint32_t id[4] = {};          // runtime ids at 4, 5, 11, 12
    std::string other[2];
    for (int i = 0; i < 4; ++i) {
        const int loc = i < 2 ? 4 + i : 9 + i;
        ItemHandle item = InvalidItemHandle;
        if (g_inventory->getEquippedItem(ctx, player, loc, &item) != Inventory::Result::Success || item == InvalidItemHandle) continue;
        Items::ItemInfo info{};
        info.structSize = sizeof info;
        if (g_items->getItemInfo(ctx, item, &info) != Items::Result::Success) continue;
        id[i] = info.runtimeId ? info.runtimeId : info.code;
        if (i < 2) { code[i] = CodeText(info.code); ownLoc[i] = info.bodyLocation; }
        else other[i - 2] = CodeText(info.code);
    }
    uint32_t set = g_set.load();
    if (ownLoc[0] == 11 || ownLoc[1] == 12) set = 2;                    // the game's own word for it
    else if (ownLoc[0] == 4 || ownLoc[1] == 5 || (!id[0] && !id[1])) {
        // no word: a swap shows as the two pairs trading places - the hands holding what the
        // other set held, the other set what the hands held. Empty hands count (2026-10-13: a
        // swap to an empty set was never seen, so the swap back flipped the count the wrong way
        // round for good); a weapon taken off is no swap - the other set did not take it.
        const bool swapped = seen && (id[0] != lastHands[0] || id[1] != lastHands[1]) &&
                             id[0] == lastOther[0] && id[1] == lastOther[1] &&
                             id[2] == lastHands[0] && id[3] == lastHands[1];
        if (set == 0) set = 1;
        else if (swapped) set = set == 1 ? 2 : 1;
    }
    if (id[0] || id[1] || id[2] || id[3]) {
        seen = true;
        lastHands[0] = id[0]; lastHands[1] = id[1]; lastOther[0] = id[2]; lastOther[1] = id[3];
    }
    if (set != g_set.load()) {
        g_set.store(set);
        char b[200];
        snprintf(b, sizeof b, "gamestate: weapon set %u (hands '%s' '%s' own locations %d %d, other set '%s' '%s')", set,
                 code[0].c_str(), code[1].c_str(), ownLoc[0], ownLoc[1], other[0].c_str(), other[1].c_str());
        Log(b);
    }
    uint32_t cls = D2RVR_WEAPON_NONE;
    for (const std::string& c : code) {
        if (IsBow(c)) cls = D2RVR_WEAPON_BOW;
        else if (IsCrossbow(c) && cls != D2RVR_WEAPON_BOW) cls = D2RVR_WEAPON_CROSSBOW;
    }
    if (cls == D2RVR_WEAPON_NONE && (!code[0].empty() || !code[1].empty())) cls = D2RVR_WEAPON_MELEE;
    // The kind, whichever set: the right hand's weapon, else the left's (a bow
    // sits in the left with the quiver in the right); a hand holding no weapon
    // kind we know (a shield) is skipped. Unknown kinds fall back to the class.
    if (g_typeOffset == -2) CalibrateTypes();
    if (!g_classKindsBuilt && g_typeOffset >= 0) BuildClassKinds();
    if (!g_classCodesBuilt && g_typeOffset >= 0) BuildClassCodes();   // (g_bank is the one the types were found in)
    if (g_objLitOffset == -2) CalibrateObjectLights();
    if (g_twoOffset == -2 && g_typeOffset >= 0) CalibrateTwoHanded();
    if (g_classOffset == -2 && g_typeOffset >= 0) CalibrateWeaponClass();
    for (int side = 0; side < 2; ++side) g_thrustSide[side].store(ThrustOf(code[side]));   // hit by touch's thrust: each hand's weapon's class (vr/thrust_stroke.h)
    ReadSkills();
    if (!g_wantStrength.load()) {                                                      // [weight] hero_strength unticked: nothing of the game is asked
        g_heroStrength.store(-1);
        g_reqSide[0].store(-1);
        g_reqSide[1].store(-1);
    } else {
        if (g_reqOffset == -2 && g_typeOffset >= 0) CalibrateReqStr();
        for (int side = 0; side < 2; ++side) g_reqSide[side].store(ReqStrOf(code[side]));   // [weight] hero_strength: each hand's weapon's Strength requirement ...
        if (!g_statFaulted.load()) {                                                       // ... and the hero's Strength
            const int str = ReadHeroStrength();
            const int was = g_heroStrength.exchange(str);
            if ((was < 0) != (str < 0)) {
                char b[160];
                if (str >= 0) snprintf(b, sizeof b, "gamestate: the hero's Strength is read: %d", str);
                else snprintf(b, sizeof b, "gamestate: the hero's Strength cannot be read now");
                Log(b);
            }
        }
    }
    uint32_t kind = KindOf(code[0]);
    int from = 0;   // the hand the kind came from
    if (!kind && (kind = KindOf(code[1])) != 0) from = 1;
    if (!kind) kind = cls == D2RVR_WEAPON_BOW ? D2RVR_TYPE_BOW : cls == D2RVR_WEAPON_CROSSBOW ? D2RVR_TYPE_CROSSBOW
                    : cls == D2RVR_WEAPON_MELEE ? D2RVR_TYPE_OTHER : D2RVR_TYPE_UNARMED;
    g_type.store(kind);
    g_kindSide[0].store(KindOf(code[0]));
    g_kindSide[1].store(KindOf(code[1]));
    for (int side = 0; side < 2; ++side) {   // the item code as one number (vr/blade_shape.h Code): its blade is per base item
        uint32_t packed = 0;
        for (int i = 0; i < 4; ++i) packed |= static_cast<uint32_t>(static_cast<uint8_t>(i < static_cast<int>(code[side].size()) ? code[side][i] : ' ')) << (8 * i);
        g_codeSide[side].store(code[side].empty() ? 0u : packed);
    }
    g_leftShield.store(IsShield(code[1]));
    g_headSide.store(IsHead(code[1]) ? 1 : IsHead(code[0]) ? 0 : -1);
    g_held.store((code[0].empty() ? 0u : 1u) | (code[1].empty() ? 0u : 2u));
    // Two-handed: the weapon's own "2handed"; without the field, a spear, a polearm
    // or a staff (all two-handed in D2) and nothing else. In both hands: the other
    // hand's slot is empty too - a barbarian's two-handed sword beside another weapon
    // is held in one (a bow's quiver sits in the other slot, but a bow is never held so).
    uint32_t two = 0;
    if (const int t = TwoHandedOf(code[from]); t >= 0) two = (t ? D2RVR_TWO_HANDED : 0u) | D2RVR_TWO_HANDED_KNOWN;
    else if (kind == D2RVR_TYPE_SPEAR || kind == D2RVR_TYPE_POLEARM || kind == D2RVR_TYPE_STAFF) two = D2RVR_TWO_HANDED;
    if ((two & D2RVR_TWO_HANDED) && code[from ^ 1].empty()) two |= D2RVR_TWO_HANDS_ON;
    if (KindOf(code[1]) != 0) two |= D2RVR_LEFT_WEAPON;   // a weapon in the left hand (KindOf: no shield, no quiver)
    g_two.store(two);
    g_from.store((uint32_t)from);
    {   // FNV-1a over both codes: a new grip for any change of what the hands hold
        uint32_t h = 2166136261u;
        for (const std::string* c : {&code[0], &code[1]}) {
            for (const char ch : *c) { h ^= (uint8_t)ch; h *= 16777619u; }
            h ^= 0xFFu; h *= 16777619u;
        }
        g_handsKey.store(h);
    }
    static std::string told[2];
    if (code[0] != told[0] || code[1] != told[1]) {
        told[0] = code[0]; told[1] = code[1];
        char b[200];
        snprintf(b, sizeof b, "gamestate: hands right '%s' left '%s' -> %s, tab D2R %s%s%s", code[0].c_str(), code[1].c_str(),
                 cls == D2RVR_WEAPON_BOW ? "bow" : cls == D2RVR_WEAPON_CROSSBOW ? "crossbow" : cls == D2RVR_WEAPON_MELEE ? "melee" : "empty",
                 kD2RVRWeaponTypeNames[kind], !(two & D2RVR_TWO_HANDED) || cls != D2RVR_WEAPON_MELEE ? "" : (two & D2RVR_TWO_HANDS_ON) ? ", two-handed, in both hands" : ", two-handed, in one hand (the other holds something)",
                 (two & D2RVR_TWO_HANDED) && cls == D2RVR_WEAPON_MELEE && !(two & D2RVR_TWO_HANDED_KNOWN) ? " (by kind)" : "");
        Log(b);
    }
    g_weapon.store(cls);
    if (g_state) {
        memset(g_state->rightCode, ' ', 4); memset(g_state->leftCode, ' ', 4);
        memcpy(g_state->rightCode, code[0].data(), std::min<size_t>(4, code[0].size()));
        memcpy(g_state->leftCode, code[1].data(), std::min<size_t>(4, code[1].size()));
    }
    shieldsmite::UiRead();   // [gameplay] shield_smite: which pad slot holds Smite (the game's own tables, UI thread)
    doubleswing::UiRead();   // [hands] double_swing_by_hands: which pad slots hold Double Swing and Frenzy, the hero's level in them (UI thread; nothing when off)
    meleearm::UiRead();      // [gameplay] melee_skill_armed: the skills on the pad's face-button slots, the hero's level in the armed one (UI thread; nothing when off)
    pointwire::UiRead();     // [gameplay] teleport_circle: which pad slots hold a point skill (UI thread; nothing when off)
    Publish();
}

std::set<std::string> g_panels;   // open panels, UI thread only
std::atomic<bool> g_invOnly{false};   // a menu is open and every menu open is the inventory (handinv::IsInventoryPanel; InventoryOnly)

// A panel that takes the controller away from the game. Names as D2R 3.3
// loads them (2026-10-02 log): VendorPanelLayout, PlayerInventoryExpansionLayout,
// BankExpansionLayout, HoradricCubeLayout, CharacterStatsPanel, SkillsTreePanel,
// SkillSelect, QuestLogPanelExpansion, PauseLayoutGarden. The HUD's own panels
// (HUDPanel, MiniMenuPanel, HireablesPanel, TooltipsPanel, ShowItemsPanel...)
// stay open all game and are not menus - "Menu" alone matched MiniMenuPanel.
// With the laser on the menus ([openxr] laser, native OpenXR; SetLaserPanels) the modal ones
// bearstonem/d2r-pcvr found besides (MIT, its steamvr/native_panels.h) too - the laser has them on
// the flat picture: an NPC's speech scroll (it may outlive NpcDialogPanel), Inifuss' scroll, the
// gold and quantity boxes, a confirmation, and the pad layout's tabbed menu (UISwitcher: its page
// changes closed the menu for a frame, 2026-10-12). Not its ControllerOverlay, which d2r-pcvr counts
// too: it stayed open after the menu closed (the log, 2026-10-12) - first person would stay flat.
bool IsMenuPanel(const std::string& n) {
    const bool laser = g_laserPanels.load();
    if (laser && n == "UISwitcher") return true;
    if (n.rfind("HUD", 0) == 0 || n.rfind("MiniMenu", 0) == 0) return false;
    static const char* const kMenu[] = {"Vendor", "PlayerInventory", "Bank", "Stash", "HoradricCube", "CharacterStats",
                                        "SkillsTree", "SkillSelect", "QuestLog", "Pause", "Waypoint", "Npc", "NPC",
                                        "Trade", "Gamble", "HirelingInventory", "MercenaryInventory", "Options"};
    static const char* const kLaserMenu[] = {"ScrollSpeech", "ScrollOfInifuss", "GoldTransfer", "Quantity", "Confirmation"};
    for (const char* k : kMenu) if (n.find(k) != std::string::npos) return true;
    if (laser) for (const char* k : kLaserMenu) if (n.find(k) != std::string::npos) return true;
    return false;
}

bool Has(const char* s, const char* part) {
    if (!s) return false;
    for (; *s; ++s) if (_strnicmp(s, part, strlen(part)) == 0) return true;
    return false;
}

// When the game's title screen first came up (GetTickCount64, 0 = not yet): D2R builds it at its window's
// size and dies when that is very tall (3000 on a 4000x4000 monitor, 2026-10-11) - vrcam's square window
// grows the window only after it.
std::atomic<uint64_t> g_titleAt{0};
SharedEvents::UiMessageAction __cdecl OnUiMessage(const PluginContext*, const SharedEvents::UiMessageEvent* e, void*) noexcept {
    if (!e) return SharedEvents::UiMessageAction::Continue;
    const char* target = e->target ? e->target : "";
    const char* command = e->command ? e->command : "";
    const char* text = e->text ? e->text : "";
    static std::set<std::string> seen;
    std::string key = std::string(target) + ":" + command;
    const std::string keyText = key + ":" + text;   // the panel's name is in the text
    if (seen.size() < 4000 && seen.insert(keyText).second && strcmp(target, "InputMessage") != 0) {
        char b[300];
        snprintf(b, sizeof b, "gamestate: ui message '%s' '%s' text '%.80s'", target, command, text);
        Log(b);
    }
    if (strcmp(target, "PanelManager") == 0) {
        const bool before = g_menu.load() != 0;
        if (!strcmp(command, "OpenPanel") || !strcmp(command, "OpenExclusivePanel")) {
            if (*text) g_panels.insert(text);
            if (!strcmp(text, "TitleScreenPanel") && !g_titleAt.load()) g_titleAt.store(GetTickCount64());
        }
        else if (!strcmp(command, "ClosePanel") || !strcmp(command, "UnloadPanel")) g_panels.erase(text);
        else if (!strcmp(command, "UnloadAll") || !strcmp(command, "CloseAll")) g_panels.clear();
        g_autoMap.store(g_panels.count("AutoMap") ? 1 : 0);
        noticecard::SetOpen(g_panels.count("NotificationPanel") != 0);
        bool menu = false, invOnly = true;
        for (const std::string& n : g_panels)
            if (IsMenuPanel(n)) { menu = true; if (!handinv::IsInventoryPanel(n.c_str())) invOnly = false; }
        g_menu.store(menu ? 1 : 0);
        g_invOnly.store(menu && invOnly);   // [openxr] hand_inventory: every menu open is the inventory
        {   // an NPC's small box (NpcDialogPanel: Talk / Trade / Cancel) and no other menu: the toolbar need not go with it (hud.cpp)
            bool npc = false, other = false;
            for (const std::string& n : g_panels) {
                if (!IsMenuPanel(n)) continue;
                (n == "NpcDialogPanel" ? npc : other) = true;
            }
            g_npcDialogOnly.store(npc && !other);
        }
        if (menu != before) {
            // the menu's own panels first: the whole list is cut at 200 characters, and alphabetically the one that made it a menu
            // (UISwitcher, the controller layout's inventory screen that Esc opens) fell off the end (Serg's log 2026-10-28 15:46:59)
            std::string by, list;
            for (const std::string& n : g_panels) {
                if (IsMenuPanel(n)) by += n + " ";
                list += n + " ";
            }
            char b[420];
            snprintf(b, sizeof b, "gamestate: menu %s%s%.100s(open: %.200s)", menu ? "OPEN" : "closed", by.empty() ? " " : " by ", by.c_str(), list.c_str());
            Log(b);
        }
    }
    // [render] vr_resolution: the game's own graphics Apply can reset its render state without a WM_SIZE - the size is asked for
    // again once it has settled (render_resolution.h). Not on "ResolutionChanged": our own WM_SIZE sends that, and we would ask for ever.
    if (!strcmp(target, "OptionsPanelMessage") && !strcmp(command, "AcceptChanges")) vrres::OptionsApplied();
    // [hud] notice_card (vr/notice_card.h): a quest update or a level-up - the notice's panel opening, a message sent to it ('NotificationMessage'
    // 'QuestUpdate', 2026-10-14) - is counted; the card in the room comes from the count. Only read: the panel is the game's.
    if (noticecard::IsPoke(target, command, text)) noticecard::Poke();
    if (g_state) {
        strncpy_s(g_state->lastUi, key.c_str(), _TRUNCATE);
        Publish();
    }
    return SharedEvents::UiMessageAction::Continue;
}

// Which objects give light, from the game's own Objects table (Objects.txt): Lit0..Lit7,
// the light's radius in each of the object's modes (a torch: 0 unlit, 19 burning), and
// Red/Green/Blue, its colour. The compiled row's offsets are not named by the SDK: found
// once, on the UI thread, as the bytes that read Objects.txt's values for five objects
// at once - a brazier, a tiki torch, a bonfire, a small fire and hellfire (2026-10-07).
struct ObjLight { uint8_t lit[8]; uint8_t rgb[3]; };
constexpr uint32_t kObjMax = 1024;
ObjLight g_objLight[kObjMax];
std::atomic<uint32_t> g_objCount{0};   // rows known; 0 = not (yet)
int g_objLitOffset = -2;                // -2 not tried yet, -1 failed (declared above ReadItems)

void CalibrateObjectLights() {
    g_objLitOffset = -1;
    if (!g_tables || !g_tables->getTable) return;
    static const struct { uint32_t id; uint8_t lit[8]; uint8_t rgb[3]; } kProbe[] = {
        {29, {0, 19, 18, 0, 0, 0, 0, 0}, {255, 236, 176}},   // Brazier
        {37, {0, 19, 19, 0, 0, 0, 0, 0}, {255, 236, 176}},   // TikiTorch1
        {39, {0, 19, 19, 0, 0, 0, 0, 0}, {255, 236, 176}},   // RogueBonfire
        {160, {0, 16, 17, 0, 0, 0, 0, 0}, {255, 255, 255}},  // FireSmall
        {345, {6, 0, 0, 0, 0, 0, 0, 0}, {255, 100, 100}}};   // Hellfire1
    for (DataTables::Bank bank : {DataTables::Bank::Lod, DataTables::Bank::Rotw, DataTables::Bank::Classic}) {
        DataTables::TableView v{};
        v.structSize = DataTables::TableViewSize;
        if (g_tables->getTable(g_ctx, bank, DataTables::TableId::Objects, &v) != DataTables::Result::Success || !v.rows || v.rowCount <= 345 || !v.rowSize)
            continue;
        const uint8_t* rows = (const uint8_t*)v.rows;
        int lit = -1, rgb = -1;
        for (uint32_t o = 0; o + 8 <= v.rowSize && (lit < 0 || rgb < 0); ++o) {
            bool l = lit < 0, c = rgb < 0 && o + 3 <= v.rowSize;
            for (const auto& p : kProbe) {
                const uint8_t* r = rows + (size_t)p.id * v.rowSize;
                l = l && memcmp(r + o, p.lit, 8) == 0;
                c = c && memcmp(r + o, p.rgb, 3) == 0;
            }
            if (l) lit = (int)o;
            if (c) rgb = (int)o;
        }
        char b[160];
        snprintf(b, sizeof b, "gamestate: object lights - bank %u, %u rows of %u bytes, Lit at %d, colour at %d", (unsigned)bank, v.rowCount, v.rowSize, lit, rgb);
        Log(b);
        if (lit < 0 || rgb < 0) continue;
        const uint32_t n = std::min(v.rowCount, kObjMax);
        uint32_t lights = 0;
        for (uint32_t i = 0; i < n; ++i) {
            const uint8_t* r = rows + (size_t)i * v.rowSize;
            memcpy(g_objLight[i].lit, r + lit, 8);
            memcpy(g_objLight[i].rgb, r + rgb, 3);
            for (uint8_t x : g_objLight[i].lit) if (x) { ++lights; break; }
        }
        g_objLitOffset = lit;
        g_objCount.store(n);
        snprintf(b, sizeof b, "gamestate: object lights - %u of %u objects give light", lights, n);
        Log(b);
        return;
    }
    Log("gamestate: object lights NOT found in the Objects table - the ceiling's torches stay as seen in the picture");
}

// Which skills fly along the hand (vrcam, [hands] spell_aim), from the game's own Skills table:
// a missile that leaves the hero - Skills.txt srvmissile, or one of the server functions that
// send their own (Charged Bolt, Teeth, Chain Lightning, the streams, Guided Arrow and Bone
// Spirit, Blade Fury, Twister) - for a skill that needs no unit or corpse to aim at, is not
// passive and not lobbed (a lobbed bomb lands where it is aimed, at the game's distance).
// Ground placements, summons, curses and buffs keep the game's targeting. The list and the
// compiled row's offsets are bearstonem/d2r-pcvr's (MIT, audited against all 429 rows of
// skills.bin): +0x00 the id (u16), +0x24 the flags (bit 1 lob, 4 passive, 20 TargetableOnly,
// 24 TargetCorpse), +0x4E srvdofunc (s16), +0x66 srvmissile's row (s16, -1 none) - used only
// once eight skills read back what skills.txt of 3.3.93787 says of them (SkillsCheckOut).
constexpr uint32_t kSkillMax = 1024;
std::atomic<uint64_t> g_skillAimed[kSkillMax / 64] = {};
std::atomic<uint32_t> g_skillCount{0};   // rows classified; 0 = not (yet)
constexpr uint32_t kLob = 1u << 1, kPassive = 1u << 4, kTargetableOnly = 1u << 20, kTargetCorpse = 1u << 24;

struct SkillRow { uint16_t id; uint32_t flags; int16_t fn, missile; };
bool ReadSkillRow(const uint8_t* rows, uint32_t rowSize, uint32_t i, SkillRow* r) {
    const uint8_t* p = rows + (size_t)i * rowSize;
    memcpy(&r->id, p, 2); memcpy(&r->flags, p + 0x24, 4); memcpy(&r->fn, p + 0x4E, 2); memcpy(&r->missile, p + 0x66, 2);
    return r->id == i;
}

bool Aimed(const SkillRow& r) {
    if (r.flags & (kLob | kPassive | kTargetableOnly | kTargetCorpse)) return false;
    if (r.missile >= 0) return true;
    switch (r.fn) {
        case 8:     // Teeth, Multiple Shot, Shock Wave: the game's own fan, turned
        case 10:    // Guided Arrow, Bone Spirit: aimed first, then seeking
        case 17:    // Charged Bolt
        case 19:    // Inferno, Arctic Blast
        case 26:    // Chain Lightning
        case 48:    // Blade Fury
        case 118:   // Twister, Tornado
            return true;
        default: return false;
    }
}

// The offsets above read back what skills.txt (3.3.93787, 2026-10-13) says of eight skills.
bool SkillsCheckOut(const uint8_t* rows, uint32_t count, uint32_t rowSize) {
    if (count <= 251 || rowSize < 0x68) return false;
    SkillRow r{};
    auto row = [&](uint32_t i) { return ReadSkillRow(rows, rowSize, i, &r); };
    return row(0) && (r.flags & kTargetableOnly) &&                                   // Attack
           row(9) && (r.flags & kPassive) &&                                          // Critical Strike
           row(36) && r.missile >= 0 && !(r.flags & (kLob | kPassive | kTargetableOnly | kTargetCorpse)) &&   // Fire Bolt
           row(38) && r.missile < 0 && r.fn == 17 &&                                  // Charged Bolt
           row(53) && r.missile < 0 && r.fn == 26 &&                                  // Chain Lightning
           row(67) && r.missile < 0 && r.fn == 8 &&                                   // Teeth
           row(70) && (r.flags & kTargetCorpse) &&                                    // Raise Skeleton
           row(251) && (r.flags & kLob) && r.missile >= 0;                            // Fire Trauma
}

// On the UI thread, with the hands; again whenever the table in use is loaded anew. The
// newest bank that checks out: RotW holds every class, the others the same seven.
void ReadSkills() {
    static DataTables::Bank used{};
    static uint64_t usedRev = 0, refusedRev[4] = {};
    static bool refused[4] = {};
    if (!g_tables || !g_tables->getTable) return;
    for (DataTables::Bank bank : {DataTables::Bank::Rotw, DataTables::Bank::Lod, DataTables::Bank::Classic}) {
        DataTables::TableView v{};
        v.structSize = DataTables::TableViewSize;
        if (g_tables->getTable(g_ctx, bank, DataTables::TableId::Skills, &v) != DataTables::Result::Success || !v.rows || !v.rowSize) continue;
        if (g_skillCount.load() && bank == used && v.revision == usedRev) return;   // in use, as it was
        const unsigned b = (unsigned)bank & 3;
        if (refused[b] && refusedRev[b] == v.revision) continue;
        const uint8_t* rows = (const uint8_t*)v.rows;
        char line[640];
        if (!SkillsCheckOut(rows, v.rowCount, v.rowSize)) {
            refused[b] = true; refusedRev[b] = v.revision;
            snprintf(line, sizeof line, "gamestate: skills - bank %u, %u rows of %u bytes, do NOT read as skills.txt - spells keep the game's targeting",
                     (unsigned)bank, v.rowCount, v.rowSize);
            Log(line);
            continue;
        }
        uint64_t bits[kSkillMax / 64] = {};
        const uint32_t n = std::min(v.rowCount, kSkillMax);
        uint32_t aimed = 0;
        int at = snprintf(line, sizeof line, "gamestate: skills - bank %u, %u rows of %u bytes; fly along the hand:", (unsigned)bank, v.rowCount, v.rowSize);
        for (uint32_t i = 0; i < n; ++i) {
            SkillRow r{};
            if (!ReadSkillRow(rows, v.rowSize, i, &r) || !Aimed(r)) continue;
            bits[i >> 6] |= 1ull << (i & 63);
            ++aimed;
            if (at > 0 && at < (int)sizeof line - 8) at += snprintf(line + at, sizeof line - at, " %u", i);
        }
        for (uint32_t w = 0; w < kSkillMax / 64; ++w) g_skillAimed[w].store(bits[w]);
        g_skillCount.store(n);
        used = bank; usedRev = v.revision; refused[b] = false;
        Log(line);
        snprintf(line, sizeof line, "gamestate: skills - %u of %u fly along the hand ([hands] spell_aim)", aimed, n);
        Log(line);
        return;
    }
}

template <class S> const S* Query(const PluginContext* ctx) {
    const S* s = nullptr;
    return ctx->QueryService(&s) == ServiceQueryResult::Success ? s : nullptr;
}

}  // namespace

// From D2RLoaderLoadPlugin.
void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    g_threads = Query<ThreadService>(ctx);
    g_widgets = Query<WidgetService>(ctx);
    Log(g_widgets ? "gamestate: widgets ok - the map can be opened and closed directly" : "gamestate: widgets MISSING - the map cannot be set");
    g_inventory = Query<InventoryService>(ctx);
    g_items = Query<ItemService>(ctx);
    g_events = Query<SharedEventService>(ctx);
    g_tables = Query<DataTableService>(ctx);
    char b[160];
    snprintf(b, sizeof b, "gamestate: threads %s, inventory %s, items %s, events %s", g_threads ? "ok" : "MISSING",
             g_inventory ? "ok" : "MISSING", g_items ? "ok" : "MISSING", g_events ? "ok" : "MISSING");
    Log(b);
    if (g_events) {
        SharedEvents::UiMessageListener l{};
        l.structSize = sizeof l;
        l.priority = -1000;   // after everyone: only listens
        l.callback = &OnUiMessage;
        if (g_events->registerUiMessageListener(ctx, &l, &g_uiListener) != SharedEvents::Result::Success) Log("gamestate: could not listen to UI messages");
    }
    // Low integrity may read it too (D2R VR Settings and the game can run at different levels).
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, FALSE};
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)S:(ML;;NW;;;LW)", SDDL_REVISION_1, &sd, nullptr))
        sa.lpSecurityDescriptor = sd;
    g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(D2RVR_State), D2RVR_STATE_NAME);
    if (sd) LocalFree(sd);
    if (g_map) g_state = (D2RVR_State*)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(D2RVR_State));
    if (g_state) { memset(g_state, 0, sizeof *g_state); g_state->version = D2RVR_STATE_VERSION; }
}

// vrcam's timer, every 200 ms: queue a read of the hands on the UI thread.
void Tick() {
    if (!g_threads || !g_ctx) return;
    bool lost = false;
    const uint32_t token = g_readQueued.Start(GetTickCount64(), &lost);
    if (!token) return;
    static int toldLost = 0;
    if (lost && toldLost < 10) { ++toldLost; Log("gamestate: the UI thread's read of the hands did not come in a second, queued again"); }
    if (g_threads->runOnUiThread(g_ctx, &ReadItems, uiticket::ToUser(token)) != Threads::Result::Success) g_readQueued.Claim(token);
}

uint32_t WeaponClass() { return g_weapon.load(); }
uint32_t WeaponSet() { return g_set.load(); }
uint32_t WeaponType() { return g_type.load(); }
uint32_t CodeOfItemClass(uint32_t classId) { return classId < g_classCodeCount.load(std::memory_order_acquire) ? g_classCode[classId].load(std::memory_order_relaxed) : 0; }
uint32_t KindOfItemClass(uint32_t classId) { return classId < g_classKindCount.load(std::memory_order_acquire) ? g_classKind[classId].load(std::memory_order_relaxed) : 0; }
void BeltCodes(uint32_t out[4]) { for (int c = 0; c < 4; ++c) out[c] = g_belt[c].load(); }
void BeltIds(uint32_t out[4]) { for (int c = 0; c < 4; ++c) out[c] = g_beltId[c].load(); }   // the same items' unit ids (0 = none)
uint32_t HandsHeld() { return g_held.load(); }
uint32_t TwoHanded() { return g_two.load(); }
uint32_t WeaponHand() { return g_from.load(); }
uint32_t HandKind(int side) { return g_kindSide[side & 1].load(); }
uint32_t HandCode(int side) { return g_codeSide[side & 1].load(); }
int HandThrust(int side) { return g_thrustSide[side & 1].load(); }            // hit by touch's thrust: 1 the weapon in a hand is of a thrust class (1ht, 2ht), 0 not, -1 not known
void SetWantHeroStrength(bool on) { g_wantStrength.store(on); }              // [weight] hero_strength: ticked or not - only then is the stat (and the weapon's requirement) read
int HeroStrength() { return g_heroStrength.load(); }                          // ... the hero's Strength stat, -1 not read
int HandRequiredStrength(int side) { return g_reqSide[side & 1].load(); }    // ... the Strength requirement of the weapon in a hand (0 right, 1 left), -1 not known
bool HandIsThrowPotion(int side) {
    const uint32_t c = g_codeSide[side & 1].load();
    for (const uint32_t p : kThrowPotions) if (c == p) return true;
    return false;
}
uint32_t HandsKey() { return g_handsKey.load(); }
bool LeftShield() { return g_leftShield.load(); }
int HeadSide() { return g_headSide.load(); }
bool MenuOpen() { return g_menu.load() != 0; }
bool InventoryOnly() { return g_invOnly.load(); }   // [openxr] hand_inventory: the menu open is the inventory alone (the tabbed menu or the inventory panel)
bool NpcDialogOnly() { return g_npcDialogOnly.load(); }   // an NPC's dialogue box is the only menu open
void SetStickBits(uint32_t bits) { g_stickBits.store(bits & (7u << 13)); }
void SetLaserPanels(bool on) { g_laserPanels.store(on); }   // from the next panel message on
uint64_t TitleScreenAt() { return g_titleAt.load(); }
uint64_t LocalPlayer() { return g_player.load(); }
// An object's light in a mode: its radius (Objects.txt Lit, 0 = none) and colour 0..1.
int ObjectLight(uint32_t txt, uint32_t mode, float rgb[3]) {
    if (txt >= g_objCount.load() || mode > 7) return 0;
    const ObjLight& o = g_objLight[txt];
    for (int c = 0; c < 3; ++c) rgb[c] = o.rgb[c] / 255.0f;
    return o.lit[mode];
}

bool AutoMapOpen() { return g_autoMap.load() != 0; }
int MapMode() { g_mapModeAskedAt.store(GetTickCount64()); return g_mapModeLast.load(); }   // [hud] map_direction: the whole-screen map (0) or a corner (1, 2); -1 not known
// A skill whose missile flies along the hand (ReadSkills); false while the table is not read.
bool SkillAimed(int skill) {
    return skill >= 0 && (uint32_t)skill < g_skillCount.load() && ((g_skillAimed[skill >> 6].load() >> (skill & 63)) & 1) != 0;
}

// On the UI thread, as widget calls must be: the same message the game's own
// map key sends, so the game opens or closes its map itself.
void __cdecl DispatchAutoMap(const PluginContext* ctx, void* user) noexcept {
    if (!g_widgets || !g_widgets->dispatchUiAction) return;
    const bool open = user != nullptr;
    Widgets::UiAction a{};
    a.structSize = sizeof a;
    a.target = "PanelManager";
    a.command = open ? "OpenPanel" : "ClosePanel";
    a.text = "AutoMap";
    const Widgets::Result r = g_widgets->dispatchUiAction(ctx, &a);
    char b[120];
    snprintf(b, sizeof b, "gamestate: map %s sent to the game (result %u)", open ? "OPEN" : "CLOSE", (unsigned)r);
    Log(b);
}

// The name plate over the target monster: the game's HUDMonsterHealth panel (its layouts, 2026-10-12:
// data\global\ui\Layouts\HUDMonsterHealthHD.json and Controller\HUDMonsterHealthHD.json). The panel is 0 x 0,
// anchored at the top middle. With the mouse its child Health is the plate there; with a pad - D2R VR plays
// with one - the plate hangs over the monster: the child TargetAttached stands where the monster is (its
// rectangle follows it), and below it Health (the bar with the name, as wide as the name) between its
// backgrounds NormalMonsterBackground or UniqueMonsterBackground. Every rectangle is local to its parent, in
// the layout's units. Read on the UI thread (widget calls must be), one read at a time, each frame wanted.
std::atomic<int32_t> g_plate[4] = {};
std::atomic<int32_t> g_plateAnchor[2] = {};   // where the plate hangs from: the panel's place plus TargetAttached's (the pad's layout), off the same top middle
std::atomic<bool> g_plateAttached{false};     // ... and TargetAttached was there to read (else the anchor is the panel's own place)
std::atomic<ULONGLONG> g_plateAt{0};
std::atomic<uintptr_t> g_plateUnit{0};       // the unit and the anchor (px) the hook had last given when the plate was read: the box moves from there
std::atomic<float> g_plateAtPx[2] = {};
std::atomic<bool> g_plateAtOk{false};
uiticket::Ticket g_plateReading;   // ReadPlate() queued (ui_ticket.h)
Widgets::WidgetHandle g_platePanel = Widgets::InvalidHandle;
// a child's rectangle (found once, the handle kept: a path, it outlives the panel's closing)
Widgets::Result ChildRect(const PluginContext* ctx, Widgets::WidgetHandle parent, const char* name, Widgets::WidgetHandle* h, Widgets::Rect* out) {
    *out = Widgets::Rect{};
    if (parent == Widgets::InvalidHandle) { *h = Widgets::InvalidHandle; return Widgets::Result::NotFound; }
    Widgets::Result r = Widgets::Result::Success;
    if (*h == Widgets::InvalidHandle) r = g_widgets->findWidget(ctx, parent, name, h);
    if (r == Widgets::Result::Success && *h != Widgets::InvalidHandle) r = g_widgets->getWidgetRect(ctx, *h, out);
    else if (r == Widgets::Result::Success) r = Widgets::Result::NotFound;
    if (r == Widgets::Result::StaleHandle || r == Widgets::Result::NotFound) *h = Widgets::InvalidHandle;
    return r;
}
// a background's own size is the game's to set; only a sane one goes into the plate
bool SaneBack(const Widgets::Rect& r) { return r.width > 0 && r.height > 0 && r.width < 4000 && r.height < 600; }
void __cdecl ReadPlate(const PluginContext* ctx, void* user) noexcept {
    if (!g_plateReading.Claim(uiticket::FromUser(user))) return;   // come after its lease, replaced
    if (!g_widgets || !g_widgets->findPanel || !g_widgets->getWidgetRect || !g_widgets->findWidget) return;
    static int told = 0;
    static int32_t last[4] = {};
    static Widgets::WidgetHandle target = Widgets::InvalidHandle, health = Widgets::InvalidHandle,
                                 normal = Widgets::InvalidHandle, unique = Widgets::InvalidHandle;
    Widgets::Result r = Widgets::Result::Success;
    if (g_platePanel == Widgets::InvalidHandle) r = g_widgets->findPanel(ctx, "HUDMonsterHealth", &g_platePanel);
    Widgets::Rect rc{};
    if (r == Widgets::Result::Success && g_platePanel != Widgets::InvalidHandle) r = g_widgets->getWidgetRect(ctx, g_platePanel, &rc);
    if (r != Widgets::Result::Success) {
        if (r == Widgets::Result::StaleHandle || r == Widgets::Result::NotFound) g_platePanel = Widgets::InvalidHandle;
        target = health = normal = unique = Widgets::InvalidHandle;
        return;
    }
    Widgets::Rect tc{}, hc{}, nc{}, uc{};
    const Widgets::Result tr = ChildRect(ctx, g_platePanel, "TargetAttached", &target, &tc);
    const bool attached = tr == Widgets::Result::Success;   // the pad's layout: the plate over the monster
    const Widgets::Result hr = ChildRect(ctx, attached ? target : g_platePanel, "Health", &health, &hc);
    if (attached) {
        ChildRect(ctx, target, "NormalMonsterBackground", &normal, &nc);
        ChildRect(ctx, target, "UniqueMonsterBackground", &unique, &uc);
    }
    if (hr != Widgets::Result::Success || hc.width <= 0 || hc.height <= 0) {
        if (told < 12) {
            ++told;
            char b[200];
            snprintf(b, sizeof b, "gamestate: the monster's name plate - HUDMonsterHealth at %d,%d, TargetAttached (result %u) at %d,%d, "
                     "its Health not read (result %u, %dx%d)", rc.x, rc.y, (unsigned)tr, tc.x, tc.y, (unsigned)hr, hc.width, hc.height);
            Log(b);
        }
        return;
    }
    // the bar, and the background round it, in the panel's anchor's units (the top middle)
    const int32_t ox = rc.x + (attached ? tc.x : 0), oy = rc.y + (attached ? tc.y : 0);
    int32_t x0 = hc.x, y0 = hc.y, x1 = hc.x + hc.width, y1 = hc.y + hc.height;
    for (const Widgets::Rect* bk : {&nc, &uc}) {
        if (!SaneBack(*bk)) continue;
        x0 = (std::min)(x0, bk->x); y0 = (std::min)(y0, bk->y);
        x1 = (std::max)(x1, bk->x + bk->width); y1 = (std::max)(y1, bk->y + bk->height);
    }
    const int32_t plate[4] = {ox + x0, oy + y0, x1 - x0, y1 - y0};
    for (int i = 0; i < 4; ++i) g_plate[i].store(plate[i]);
    g_plateAnchor[0].store(ox);
    g_plateAnchor[1].store(oy);
    g_plateAttached.store(attached);
    plateworld::Follow f;   // (the same thread as the hook: the layout read is the one its latest answer made)
    const bool followed = plateworld::GetFollow(&f, GetTickCount64());
    g_plateUnit.store(followed ? f.unit : 0);
    g_plateAtPx[0].store(followed ? f.px[0][0] : 0.0f);
    g_plateAtPx[1].store(followed ? f.px[0][1] : 0.0f);
    g_plateAtOk.store(followed);
    g_plateAt.store(GetTickCount64());
    if (told < 12 && memcmp(last, plate, sizeof last) != 0) {
        ++told;
        memcpy(last, plate, sizeof last);
        char b[400];
        snprintf(b, sizeof b, "gamestate: the monster's name plate %d,%d %dx%d - HUDMonsterHealth at %d,%d, TargetAttached (result %u) "
                 "at %d,%d %dx%d, Health at %d,%d %dx%d, NormalMonsterBackground at %d,%d %dx%d, UniqueMonsterBackground at %d,%d %dx%d",
                 plate[0], plate[1], plate[2], plate[3], rc.x, rc.y, (unsigned)tr, tc.x, tc.y, tc.width, tc.height,
                 hc.x, hc.y, hc.width, hc.height, nc.x, nc.y, nc.width, nc.height, uc.x, uc.y, uc.width, uc.height);
        Log(b);
    }
}
void PollPlate() {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || !g_widgets) return;
    const uint32_t token = g_plateReading.Start(GetTickCount64());   // 0: the last read not come yet
    if (!token) return;
    if (g_threads->runOnUiThread(g_ctx, &ReadPlate, uiticket::ToUser(token)) != Threads::Result::Success) g_plateReading.Claim(token);
}
// The plate's rectangle (x, y, w, h, the panel's own units), if read lately.
bool PlateRect(int32_t out[4]) {
    if (GetTickCount64() - g_plateAt.load() > 250) return false;   // no target, no plate
    for (int i = 0; i < 4; ++i) out[i] = g_plate[i].load();
    return true;
}

// The unit and the place (px of the viewport) the hook had hung the plate at when PlateRect's rect was read; false: the hook had not (plateworld::FollowBox).
bool PlateReadAt(uintptr_t* unit, float px[2]) {
    if (GetTickCount64() - g_plateAt.load() > 250 || !g_plateAtOk.load()) return false;
    *unit = g_plateUnit.load();
    px[0] = g_plateAtPx[0].load();
    px[1] = g_plateAtPx[1].load();
    return true;
}

// Where the plate hangs from (x, y, the panel's own units off the top middle: the panel's place plus TargetAttached's), if read lately. [hud]
// monster_bar_room checks it against the point it gave the game (plateworld::AnchorAtFixed): the game's default before a target and its mouse
// layout both leave it at the panel's own place, and the plate there is not one to cut out.
bool PlateAnchor(int32_t out[2]) {
    if (GetTickCount64() - g_plateAt.load() > 250 || !g_plateAttached.load()) return false;
    out[0] = g_plateAnchor[0].load();
    out[1] = g_plateAnchor[1].load();
    return true;
}

// The item-condition figure (vr/durability_figure.h): the game's HUDWarnings panel - top right of the interface area, rect -40, 12 in its layout - and its child
// DurabilityWarning, a DurabilityWidget (data\global\ui\Layouts\HUDWarningsHD.json). Their rects as the widget service gives them (layout units, local to the
// parent), read on the UI thread a few times a second while the figure is moved or hidden ([hud] durability_*); the first few changes go into the log, to check
// against where the figure shows.
std::atomic<int32_t> g_durPanel[4] = {}, g_durWidget[4] = {};
std::atomic<bool> g_durWidgetRead{false};
std::atomic<ULONGLONG> g_durAt{0};
uiticket::Ticket g_durReading;   // ReadDurability() queued (ui_ticket.h)
Widgets::WidgetHandle g_durPanelH = Widgets::InvalidHandle, g_durWidgetH = Widgets::InvalidHandle;
void __cdecl ReadDurability(const PluginContext* ctx, void* user) noexcept {
    if (!g_durReading.Claim(uiticket::FromUser(user))) return;   // come after its lease, replaced
    if (!g_widgets || !g_widgets->findPanel || !g_widgets->getWidgetRect || !g_widgets->findWidget) return;
    static int told = 0;
    static int32_t last[9] = {};
    Widgets::Result r = Widgets::Result::Success;
    if (g_durPanelH == Widgets::InvalidHandle) r = g_widgets->findPanel(ctx, "HUDWarnings", &g_durPanelH);
    Widgets::Rect rc{};
    if (r == Widgets::Result::Success && g_durPanelH != Widgets::InvalidHandle) r = g_widgets->getWidgetRect(ctx, g_durPanelH, &rc);
    if (r != Widgets::Result::Success) {   // no such panel now (a menu, the title): nothing read
        if (r == Widgets::Result::StaleHandle || r == Widgets::Result::NotFound) g_durPanelH = Widgets::InvalidHandle;
        g_durWidgetH = Widgets::InvalidHandle;
        g_durAt.store(0);
        return;
    }
    Widgets::Rect dc{};
    const Widgets::Result dr = ChildRect(ctx, g_durPanelH, "DurabilityWarning", &g_durWidgetH, &dc);
    const bool read = dr == Widgets::Result::Success;
    g_durPanel[0].store(rc.x); g_durPanel[1].store(rc.y); g_durPanel[2].store(rc.width); g_durPanel[3].store(rc.height);
    g_durWidget[0].store(read ? dc.x : 0); g_durWidget[1].store(read ? dc.y : 0); g_durWidget[2].store(read ? dc.width : 0); g_durWidget[3].store(read ? dc.height : 0);
    g_durWidgetRead.store(read);
    g_durAt.store(GetTickCount64());
    const int32_t now[9] = {rc.x, rc.y, rc.width, rc.height, read ? dc.x : 0, read ? dc.y : 0, read ? dc.width : 0, read ? dc.height : 0, read ? 1 : 0};
    if (told < 12 && memcmp(last, now, sizeof last) != 0) {
        ++told;
        memcpy(last, now, sizeof last);
        char b[300];
        snprintf(b, sizeof b, "gamestate: the item-condition figure - HUDWarnings at %d,%d %dx%d, DurabilityWarning (result %u) at %d,%d %dx%d",
                 rc.x, rc.y, rc.width, rc.height, (unsigned)dr, dc.x, dc.y, dc.width, dc.height);
        Log(b);
    }
}
void PollDurability() {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || !g_widgets) return;
    const uint32_t token = g_durReading.Start(GetTickCount64());   // 0: the last read not come yet
    if (!token) return;
    if (g_threads->runOnUiThread(g_ctx, &ReadDurability, uiticket::ToUser(token)) != Threads::Result::Success) g_durReading.Claim(token);
}
// The panel's rect and the figure widget's (x, y, width, height), if read lately; widgetRead false = the panel is there and the widget could not be found.
bool DurabilityRect(int32_t panel[4], int32_t widget[4], bool* widgetRead) {
    if (GetTickCount64() - g_durAt.load() > 1500) return false;
    for (int i = 0; i < 4; ++i) { panel[i] = g_durPanel[i].load(); widget[i] = g_durWidget[i].load(); }
    *widgetRead = g_durWidgetRead.load();
    return true;
}

// [hud] notice_card (vr/notice_card.h): the game's NotificationPanel's border (NotificationPanelHD.json: the panel 0, -500 off the interface area's
// bottom middle, its child NotificationBorder 1200 x 244) as the widget service gives it - the panel's place, the border's size - read on the UI
// thread a few times a second while the panel is open and the card is in force. The first few changes go into the log.
std::atomic<int32_t> g_noticeRect[4] = {};
std::atomic<ULONGLONG> g_noticeAt{0};
uiticket::Ticket g_noticeReading;   // ReadNotice() queued (ui_ticket.h)
Widgets::WidgetHandle g_noticePanelH = Widgets::InvalidHandle, g_noticeBorderH = Widgets::InvalidHandle;
void __cdecl ReadNotice(const PluginContext* ctx, void* user) noexcept {
    if (!g_noticeReading.Claim(uiticket::FromUser(user))) return;   // come after its lease, replaced
    if (!g_widgets || !g_widgets->findPanel || !g_widgets->getWidgetRect || !g_widgets->findWidget) return;
    static int told = 0;
    static int32_t last[8] = {};
    Widgets::Result r = Widgets::Result::Success;
    if (g_noticePanelH == Widgets::InvalidHandle) r = g_widgets->findPanel(ctx, "NotificationPanel", &g_noticePanelH);
    Widgets::Rect rc{};
    if (r == Widgets::Result::Success && g_noticePanelH != Widgets::InvalidHandle) r = g_widgets->getWidgetRect(ctx, g_noticePanelH, &rc);
    if (r != Widgets::Result::Success) {   // not loaded now: nothing read (the layout's rect is used)
        if (r == Widgets::Result::StaleHandle || r == Widgets::Result::NotFound) g_noticePanelH = Widgets::InvalidHandle;
        g_noticeBorderH = Widgets::InvalidHandle;
        g_noticeAt.store(0);
        return;
    }
    Widgets::Rect bc{};
    const Widgets::Result br = ChildRect(ctx, g_noticePanelH, "NotificationBorder", &g_noticeBorderH, &bc);
    const bool border = br == Widgets::Result::Success && bc.width > 0 && bc.height > 0;
    g_noticeRect[0].store(rc.x); g_noticeRect[1].store(rc.y);
    g_noticeRect[2].store(border ? bc.width : rc.width); g_noticeRect[3].store(border ? bc.height : rc.height);
    g_noticeAt.store(GetTickCount64());
    const int32_t now[8] = {rc.x, rc.y, rc.width, rc.height, border ? bc.x : 0, border ? bc.y : 0, border ? bc.width : 0, border ? bc.height : 0};
    if (told < 8 && memcmp(last, now, sizeof last) != 0) {
        ++told;
        memcpy(last, now, sizeof last);
        char b[300];
        snprintf(b, sizeof b, "gamestate: the quest / level-up notice - NotificationPanel at %d,%d %dx%d, NotificationBorder (result %u) at %d,%d %dx%d",
                 rc.x, rc.y, rc.width, rc.height, (unsigned)br, bc.x, bc.y, bc.width, bc.height);
        Log(b);
    }
}
void PollNotice() {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || !g_widgets) return;
    const uint32_t token = g_noticeReading.Start(GetTickCount64());   // 0: the last read not come yet
    if (!token) return;
    if (g_threads->runOnUiThread(g_ctx, &ReadNotice, uiticket::ToUser(token)) != Threads::Result::Success) g_noticeReading.Claim(token);
}
// The panel's place and the border's size (x, y, width, height; layout units), if read lately.
bool NoticeRect(int32_t out[4]) {
    if (GetTickCount64() - g_noticeAt.load() > 1500) return false;
    for (int i = 0; i < 4; ++i) out[i] = g_noticeRect[i].load();
    return true;
}

// [openxr] hand_inventory (vr/hand_inventory.h): is an item on the game's cursor (the inventory's held item)? Read on the UI thread with the SDK's
// own query, a few times a second while the inventory is on the hand: an item dragged off the panel with the laser and let go there gets the
// click outside the panel that drops it, only while the game holds one.
std::atomic<int> g_cursorHeld{-1};   // 1 an item on the cursor, 0 none, -1 not known
std::atomic<ULONGLONG> g_cursorAt{0};
uiticket::Ticket g_cursorReading;
void __cdecl ReadCursor(const PluginContext* ctx, void* user) noexcept {
    if (!g_cursorReading.Claim(uiticket::FromUser(user))) return;
    if (!g_inventory || !g_inventory->getLocalPlayer || !g_inventory->getCursorItem) return;
    PlayerHandle player = InvalidPlayerHandle;
    ItemHandle item = InvalidItemHandle;
    if (g_inventory->getLocalPlayer(ctx, &player) != Inventory::Result::Success || player == InvalidPlayerHandle) { g_cursorHeld.store(-1); return; }
    const Inventory::Result r = g_inventory->getCursorItem(ctx, player, &item);
    g_cursorHeld.store(r == Inventory::Result::Success ? (item != InvalidItemHandle ? 1 : 0) : -1);
    g_cursorAt.store(GetTickCount64());
}
void PollCursor() {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || !g_inventory) return;
    const uint32_t token = g_cursorReading.Start(GetTickCount64());
    if (!token) return;
    if (g_threads->runOnUiThread(g_ctx, &ReadCursor, uiticket::ToUser(token)) != Threads::Result::Success) g_cursorReading.Claim(token);
}
// 1 an item on the cursor, 0 none, -1 not known (not read in the last 400 ms).
int CursorHeld() { return GetTickCount64() - g_cursorAt.load() <= 400 ? g_cursorHeld.load() : -1; }

// [openxr] hand_inventory: vrcam's walk with the inventory up (the stick's own walk command) on the game's UI thread - where the game's
// controller update, its clicks and its commands run. One queued at a time (a run that never comes is queued again after the ticket's lease).
uiticket::Ticket g_handWalkQueued;
void (*g_handWalkFn)() = nullptr;
void __cdecl RunHandWalk(const PluginContext*, void* user) noexcept {
    if (!g_handWalkQueued.Claim(uiticket::FromUser(user))) return;
    if (void (*fn)() = g_handWalkFn) fn();
}
bool QueueHandWalk(void (*fn)()) {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || !fn) return false;
    g_handWalkFn = fn;
    const uint32_t token = g_handWalkQueued.Start(GetTickCount64());
    if (!token) return false;   // the last one has not run yet
    if (g_threads->runOnUiThread(g_ctx, &RunHandWalk, uiticket::ToUser(token)) == Threads::Result::Success) return true;
    g_handWalkQueued.Claim(token);
    return false;
}

bool SetAutoMap(bool open) {
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || !g_widgets) return false;
    return g_threads->runOnUiThread(g_ctx, &DispatchAutoMap, open ? (void*)1 : nullptr) == Threads::Result::Success;
}
void SetMapSide(int corner) { g_mapSideCorner.store(std::clamp(corner, -1, 2)); }
void SetMapBig(bool big) { g_mapBig.store(big); }   // [hud] map_click_big: the game's whole-screen map while true (MaintainMapSide)   // -1 off, else [hud] map_corner
void SetViewMode(uint32_t mode) { if (g_view.exchange(mode) != mode) Publish(); }

}  // namespace gamestate
