// ally_portraits_game.cpp - [hud] ally_portraits (vr/ally_portraits.h) in the game: the party's players, our mercenary and our summons.
//
// What is read, and how (all of it read only, five times a second while the box is ticked, in F4, in a game; on the game's UI thread, the thread its
// portrait panel reads them on):
//   - our hero's unit id from the client's own player (the same two getters the hand vitals and the wrist status read it by: LocalIndex, ClientPlayer);
//   - the party roster and the client's pet list, from the globals the game's own getters load (vr/ally_portraits.h: their bytes are compared first,
//     the globals' addresses taken from their rip displacements), walked by ReadParty / ReadPets;
//   - the merc's name: its client unit found in the client's unit hash (walked here: type 1's row, id & 127, the chain at +0x158 - the game's
//     FindClientUnit's way, not called), the monster data's name words, read as the game's name builder reads them (MonsterNameFrom): the name it
//     built and keeps at +0x40, or the string of the id at +0x32;
//   - the summons' names: their PetType row's name id (the loader's data tables) as their portraits take it (PetNameId);
//   - a string by its id: D2RLoader's localization service, and when it does not answer the game's own string lookup (the thunk 0x3E2B730 its name
//     builder and its summons' portraits call) - each id once.
// Every layout this relies on has its byte guard (d2rsig::Check); a game where they differ gets nothing, never a guess. A read that faults (guarded by
// __try) or does not add up is dropped; a getter that faults switches the reads off until the game restarts.

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "ally_portraits_game.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <D2RLPlugin/api.h>

#include "sigscan.h"
#include "ui_ticket.h"
#include "melee_arm.h"     // [hud] skill_under_map: the melee list's names (when the game's text does not answer)
#include "wheel_icons.h"   // ... the skill icon cache's string key of a skill's name

using namespace D2RL;

namespace allyportraits {
namespace game {
namespace {

// The client's hero (as vr/wrist_status_game.cpp), and the client's unit hash (vr/melee_touch.cpp RVA_UNITS; vr/unit_scan.h's layout).
constexpr uint64_t kLocalIndex = 0x8B2D0, kClientPlayer = 0x9A480, kUnits = 0x2A23910;
constexpr uintptr_t kRowBytes = 0x400, kUnitNext = 0x158, kUnitData = 0x10;
constexpr int kMonsterRow = 1;
constexpr size_t kKinds = sizeof kPets / sizeof kPets[0];

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
const LocalizationService* g_strings = nullptr;

std::atomic<bool> g_dead{false};
uiticket::Ticket g_ticket;
std::atomic<int> g_toldFail{0};

std::mutex g_m;   // the newest read
Look g_look;
ULONGLONG g_at = 0;

// the UI thread's own (the job's alone)
PetName g_mercName;   // the merc's id and name, asked once per merc

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

bool ReadMem(uintptr_t at, void* out, size_t n) noexcept {
    if (at < 0x10000 || at > UINTPTR_MAX - n) return false;
    __try {
        memcpy(out, reinterpret_cast<const void*>(at), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
const auto kRead = [](uintptr_t at, auto& value) { return ReadMem(at, &value, sizeof value); };

enum class HeroRead { Ok, NoAddress, NoPlayer, Fault };
HeroRead ReadHeroId(uint32_t* id, uint8_t* bank) noexcept {
    const uintptr_t indexFn = d2rsig::Addr(kLocalIndex), playerFn = d2rsig::Addr(kClientPlayer);
    if (!indexFn || !playerFn) return HeroRead::NoAddress;
    __try {
        const int index = reinterpret_cast<int (*)()>(indexFn)();
        if (index < 0 || index >= 8) return HeroRead::NoPlayer;
        const uintptr_t player = reinterpret_cast<uintptr_t (*)(int)>(playerFn)(index);
        if (player < 0x10000) return HeroRead::NoPlayer;
        uint32_t head[3];
        memcpy(head, reinterpret_cast<const void*>(player), sizeof head);
        if (head[0] != 0 || head[2] == 0) return HeroRead::NoPlayer;
        *id = head[2];
        *bank = *reinterpret_cast<const volatile uint8_t*>(player + 0x1BD);   // its data bank (vr/unit_scan.h kOffBank): the PetType table's
        return HeroRead::Ok;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return HeroRead::Fault;
    }
}

// A guard whose first instruction is a rip-relative load: its bytes but the displacement, and the global it loads from.
bool GuardWithGlobal(uint64_t rva, const uint8_t* sig, uint32_t n, uintptr_t* global) {
    if (!d2rsig::Check(rva, sig, kDispAt) || !d2rsig::Check(rva + kDispAt + 4, sig + kDispAt + 4, n - kDispAt - 4)) return false;
    const uintptr_t at = d2rsig::Addr(rva);
    int32_t disp = 0;
    if (!at || !ReadMem(at + kDispAt, &disp, sizeof disp)) return false;
    *global = (uintptr_t)RipTarget((uint64_t)at, disp);
    return true;
}
// Kept once they pass (the game's code is decrypted as it runs: a guard that fails early is asked again).
bool PartyGuard(uintptr_t* global) {
    static std::atomic<uintptr_t> ok{0};
    if (const uintptr_t g = ok.load()) { *global = g; return true; }
    uintptr_t g = 0;
    if (!GuardWithGlobal(RVA_PARTY_LIFE, kSigPartyLife, sizeof kSigPartyLife, &g) || !d2rsig::Check(RVA_PARTY_LEVEL, kSigPartyLevel, sizeof kSigPartyLevel) ||
        !d2rsig::Check(RVA_PARTY_CLASS, kSigPartyClass, sizeof kSigPartyClass))
        return false;
    ok.store(g);
    *global = g;
    return true;
}
bool PetGuard(uintptr_t* global) {
    static std::atomic<uintptr_t> ok{0};
    if (const uintptr_t g = ok.load()) { *global = g; return true; }
    uintptr_t g = 0;
    if (!GuardWithGlobal(RVA_PET_LIFE, kSigPetLife, sizeof kSigPetLife, &g) || !d2rsig::Check(RVA_PET_MAKE, kSigPetMake, sizeof kSigPetMake)) return false;
    ok.store(g);
    *global = g;
    return true;
}
bool NameGuard() {
    static std::atomic<bool> ok{false};
    if (ok.load()) return true;
    const bool r = d2rsig::Check(RVA_MON_DATA, kSigMonData, sizeof kSigMonData) && d2rsig::Check(RVA_MON_NAME, kSigMonName, sizeof kSigMonName);
    if (r) ok.store(true);
    return r;
}
bool KeptNameGuard() {
    static std::atomic<bool> ok{false};
    if (ok.load()) return true;
    const bool r = NameGuard() && d2rsig::Check(RVA_MON_NAME_KEEP, kSigMonNameKeep, sizeof kSigMonNameKeep) &&
                   d2rsig::Check(RVA_MON_NAME_STORE, kSigMonNameStore, sizeof kSigMonNameStore);
    if (r) ok.store(true);
    return r;
}

// A monster of the client's unit hash by its id (the game's FindClientUnit's walk, read here): 0 when it is not there.
uintptr_t ClientMonster(uint32_t id) {
    const uintptr_t table = d2rsig::Addr(kUnits);
    if (!table || !id) return 0;
    uintptr_t at = 0;
    if (!ReadMem(table + kMonsterRow * kRowBytes + (uintptr_t)(id & 0x7F) * 8, &at, sizeof at)) return 0;
    for (int i = 0; at && i < kMaxWalk; ++i) {
        uint32_t head[3];
        if (!ReadMem(at, head, sizeof head) || head[0] != (uint32_t)kMonsterRow) return 0;
        if (head[2] == id) return at;
        if (!ReadMem(at + kUnitNext, &at, sizeof at)) return 0;
    }
    return 0;
}

// ---- the game's strings: the names (Serg, test 0.205.6: still "Mercenary", and "0 of 14 summon names" - D2RLoader's localization service answers
// nothing here). A string by its id: the service first; when it does not answer, the game's own string thunk 0x3E2B730 - the call the game's name
// builder and its summons' portraits make with that id (vr/ally_portraits.h RVA_STRING) - a lookup, nothing changed. Each id asked once (kept here).

bool StringGuard() {
    static std::atomic<bool> ok{false};
    if (ok.load()) return true;
    const bool r = d2rsig::Check(RVA_STRING, kSigStringThunk, sizeof kSigStringThunk) && d2rsig::Check(RVA_STRING_CALL_NAME, kSigStringCallName, sizeof kSigStringCallName) &&
                   d2rsig::Check(RVA_STRING_CALL_PET, kSigStringCallPet, sizeof kSigStringCallPet);
    if (r) ok.store(true);
    return r;
}

// The text the thunk hands back for `id`, copied (guarded): false when its slot is still empty, it faulted, or no text came.
bool CallString(uint16_t id, char* out, size_t n) noexcept {
    const uintptr_t fn = d2rsig::Addr(RVA_STRING);
    if (!fn || n < 2) return false;
    const char* text = nullptr;
    __try {
        int32_t disp;
        memcpy(&disp, reinterpret_cast<const void*>(fn + 2), sizeof disp);   // jmp [rip + disp]: a slot D2RLoader fills
        if (*reinterpret_cast<const volatile uintptr_t*>(fn + 6 + (intptr_t)disp) < 0x10000) return false;
        text = reinterpret_cast<const char* (*)(uint32_t)>(fn)(id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!text) return false;
    size_t k = 0;
    for (; k + 1 < n; ++k) {
        char c = 0;
        if (!ReadMem((uintptr_t)text + k, &c, 1)) break;
        out[k] = c;
        if (!c) break;
    }
    out[k < n ? k : n - 1] = 0;
    return out[0] != 0;
}

struct Said {
    uint16_t id = 0;
    char text[kNameBytes] = {};
};
Said g_said[48];
int g_saidN = 0;
int g_serviceCode = -1;   // the localization service's last answer that was not a text (the log's)

// The game's string `id` into `out`; `how` says where it came from. False: neither answered.
bool GameString(uint16_t id, char (&out)[kNameBytes], const char** how) {
    out[0] = 0;
    if (!id || id == kNoName) return false;
    for (int i = 0; i < g_saidN; ++i)
        if (g_said[i].id == id) { CopyName(out, g_said[i].text); if (how) *how = "kept"; return out[0] != 0; }
    char buf[kNameBytes * 2] = {};
    const char* from = nullptr;
    if (g_strings && g_strings->getStringById) {
        uint32_t need = 0;
        const Localization::Result r = g_strings->getStringById(g_ctx, id, buf, sizeof buf, &need);
        if (r == Localization::Result::Success && buf[0]) from = "the localization service";
        else g_serviceCode = (int)r;
    }
    if (!from && StringGuard() && CallString(id, buf, sizeof buf)) from = "the game's string lookup";
    if (!from) return false;
    CopyName(out, buf);
    if (g_saidN < (int)(sizeof g_said / sizeof g_said[0])) { g_said[g_saidN].id = id; CopyName(g_said[g_saidN].text, buf); ++g_saidN; }
    if (how) *how = from;
    return out[0] != 0;
}

// ---- the merc's name, as the game's name builder gives it (vr/ally_portraits.h MonsterNameFrom): asked at every read until it is there ------------

int g_mercTries = 0;
void NameMerc(uint32_t id) {
    if (g_mercName.id != id) {
        g_mercName = PetName{};
        g_mercName.id = id;
        g_mercTries = 0;
    }
    if (g_mercName.name[0] || !NameGuard()) return;
    const uintptr_t unit = ClientMonster(id);
    uintptr_t data = 0;
    uint16_t words[4] = {kNoName, kNoName, kNoName, kNoName};
    uint32_t cls = 0;
    const bool haveUnit = unit && ReadMem(unit + 4, &cls, sizeof cls) && ReadMem(unit + kUnitData, &data, sizeof data) && data;
    const bool haveWords = haveUnit && ReadMem(data + kMonName0, words, sizeof words);
    uint16_t stringId = kNoName;
    const NameFrom from = haveWords ? MonsterNameFrom(words, &stringId) : NameFrom::None;
    const char* how = nullptr;
    uintptr_t kept = 0;
    if (from == NameFrom::Kept && KeptNameGuard()) {   // the name it built from all four, kept at +0x40
        char bytes[kNameBytes * 2] = {};
        size_t got = 0;
        if (ReadMem(data + kMonNameKept, &kept, sizeof kept) && kept)
            for (; got < sizeof bytes; got += 16)   // in steps: a short string may end just before a page that cannot be read
                if (!ReadMem(kept + got, bytes + got, 16)) break;
        char name[kNameBytes] = {};
        if (got && KeptName(bytes, got, name, sizeof name)) {
            CopyName(g_mercName.name, name);
            how = "the name the game built and keeps (+0x40)";
        }
    } else if (from == NameFrom::StringId) {
        char name[kNameBytes] = {};
        const char* src = nullptr;
        if (GameString(stringId, name, &src)) {
            CopyName(g_mercName.name, name);
            how = src;
        }
    }
    char b[300];
    if (how) {
        snprintf(b, sizeof b, "vrcam: ally portraits - the mercenary (id %u, class %u): name words %04X %04X %04X %04X -> \"%s\" (%s)", id, cls, words[0], words[1],
                 words[2], words[3], g_mercName.name, how);
        Log(b);
    } else if (++g_mercTries == 25) {   // five seconds of reads: once, with what was read
        snprintf(b, sizeof b,
                 "vrcam: ally portraits - the mercenary's (id %u) name not found: unit %s, class %u, name words %04X %04X %04X %04X, kept %p, the localization service's "
                 "answer %d, the string lookup %s - \"Mercenary\" for now",
                 id, unit ? "found" : "NOT in the client's units", cls, words[0], words[1], words[2], words[3], (void*)kept, g_serviceCode,
                 StringGuard() ? "there" : "not where this build knows it");
        Log(b);
    }
}

// ---- the summons' names, as their portraits have them (vr/ally_portraits.h PetNameId): the PetType row's name id, then the game's string -------------

const DataTableService* g_tables = nullptr;
PetName g_petNames[kMaxPets];   // per unit: the merc's, and each summon's (a class's own name - the warlock's demons - or its kind's)
int g_petNamesN = 0;

bool PetRowGuard() {
    static std::atomic<bool> ok{false};
    if (ok.load()) return true;
    const bool r = d2rsig::Check(RVA_PET_ROW, kSigPetRow, sizeof kSigPetRow) && d2rsig::Check(RVA_PET_OVERRIDE, kSigPetOverride, sizeof kSigPetOverride) &&
                   d2rsig::Check(RVA_PET_NAME, kSigPetName, sizeof kSigPetName);
    if (r) ok.store(true);
    return r;
}

void NamePets(const Pet* pets, int npets, uint8_t bank) {
    g_petNamesN = 0;
    if (g_mercName.id && g_petNamesN < kMaxPets) g_petNames[g_petNamesN++] = g_mercName;
    static bool told = false;
    static int tries = 0;
    if (npets == 0 || !g_tables || !g_tables->getTable || !PetRowGuard()) return;
    DataTables::TableView view{};
    view.structSize = DataTables::TableViewSize;
    const DataTables::Bank b = static_cast<DataTables::Bank>(bank >= 1 && bank <= 3 ? bank : 3);
    const DataTables::Result r = g_tables->getTable(g_ctx, b, DataTables::TableId::PetType, &view);
    const bool rowsOk = r == DataTables::Result::Success && view.rows && view.rowSize == kPetRowSize && view.rowCount >= 20 && view.rowCount <= 64;
    char line[400];
    int n = snprintf(line, sizeof line, "vrcam: ally portraits - summon names (bank %u):", (unsigned)bank);
    int named = 0, asked = 0;
    for (int i = 0; i < npets && rowsOk && g_petNamesN < kMaxPets; ++i) {
        const Pet& p = pets[i];
        if (p.type == kHirelingType || p.type >= view.rowCount) continue;
        const PetKind* kind = KindOf(p.type);
        if (!kind) continue;
        uint8_t row[kPetRowSize];
        if (!ReadMem((uintptr_t)view.rows + (uintptr_t)p.type * kPetRowSize, row, sizeof row)) continue;
        ++asked;
        const uint16_t id = PetNameId(row, *kind, p.cls);
        PetName pn;
        pn.id = p.id;
        const char* how = nullptr;
        if (GameString(id, pn.name, &how)) {
            ++named;
            g_petNames[g_petNamesN++] = pn;
            if (!told && n < (int)sizeof line - 60) n += snprintf(line + n, sizeof line - (size_t)n, " %s=\"%s\" (id %u, %s)", kind->english, pn.name, (unsigned)id, how);
        } else if (!told && n < (int)sizeof line - 60) {
            n += snprintf(line + n, sizeof line - (size_t)n, " %s: string %u not found", kind->english, (unsigned)id);
        }
    }
    if (told) return;
    if (!rowsOk) {
        if (++tries == 3) {
            told = true;
            snprintf(line, sizeof line, "vrcam: ally portraits - the PetType table (bank %u) did not answer as expected (result %d, %u rows of %u bytes): summon names in English",
                     (unsigned)bank, (int)r, view.rowCount, view.rowSize);
            Log(line);
        }
        return;
    }
    if (asked && (named == asked || ++tries >= 3)) {   // once, with what was read
        told = true;
        if (named < asked) snprintf(line + n, sizeof line - (size_t)n, " (the rest in English; the localization service's answer %d)", g_serviceCode);
        Log(line);
    }
}
void Store(const Look& l) {
    std::lock_guard<std::mutex> lock(g_m);
    g_look = l;
    g_at = GetTickCount64();
}
void Forget() {
    std::lock_guard<std::mutex> lock(g_m);
    g_look = Look{};
    g_at = 0;
}

void LogChange(const Look& l) {
    static Look last;
    static ULONGLONG lastAt = 0;
    const ULONGLONG now = GetTickCount64();
    if (l.n == last.n) {   // a life that changes is no news for the log: only who is there
        bool same = true;
        for (int i = 0; i < l.n && same; ++i) same = l.tile[(size_t)i].icon == last.tile[(size_t)i].icon && l.tile[(size_t)i].count == last.tile[(size_t)i].count;
        if (same) return;
    }
    if (now - lastAt < 2000) return;   // a line costs the writer about a millisecond
    last = l;
    lastAt = now;
    char line[400];
    int n = snprintf(line, sizeof line, "vrcam: ally portraits - %d tile%s:", (int)l.n, l.n == 1 ? "" : "s");
    for (int i = 0; i < l.n && n < (int)sizeof line - 60; ++i) {
        const Tile& t = l.tile[(size_t)i];
        n += snprintf(line + n, sizeof line - (size_t)n, " [%s%s x%u %d%%]", t.kind == Kind::Player ? "player " : t.kind == Kind::Mercenary ? "merc " : "", t.name,
                      (unsigned)t.count, (int)t.life);
    }
    Log(line);
}

// ---- [hud] skill_under_map: the armed skills' names (the skill icon cache's string key, asked of the localization service as "d2r:<key>" - the way the
// skill wheel names its skills, vr/wheel_game.cpp; the melee list's English word when the game does not answer) -----------------------------------------

std::atomic<int32_t> g_skillWanted[kMaxSkills] = {-1, -1};
struct SkillNamed {
    int32_t id = -1;
    char name[kNameBytes] = {};
    ULONGLONG triedAt = 0;   // not found: asked again after a few seconds (the text service may not be ready yet)
};
std::mutex g_skillMutex;
SkillNamed g_skillNamed[16];

void NameSkills() {
    for (int h = 0; h < kMaxSkills; ++h) {
        const int32_t id = g_skillWanted[h].load();
        if (id < 0) continue;
        {
            std::lock_guard<std::mutex> lock(g_skillMutex);
            bool known = false;
            for (const SkillNamed& s : g_skillNamed)
                if (s.id == id && (s.name[0] || GetTickCount64() - s.triedAt < 5000)) known = true;
            if (known) continue;
        }
        char text[kNameBytes * 2] = {};
        const std::string key = wheelicons::NameKey(id);
        if (!key.empty() && g_strings && g_strings->getStringByKey) {
            const std::string full = "d2r:" + key;
            uint32_t need = 0;
            if (g_strings->getStringByKey(g_ctx, full.c_str(), text, sizeof text, &need) != Localization::Result::Success) text[0] = 0;
        }
        if (!text[0] && meleearm::IsMelee(id)) snprintf(text, sizeof text, "%s", meleearm::Name(id));
        std::lock_guard<std::mutex> lock(g_skillMutex);
        SkillNamed* slot = &g_skillNamed[0];
        for (SkillNamed& s : g_skillNamed)
            if (s.id == id) { slot = &s; break; }
            else if (s.triedAt < slot->triedAt) slot = &s;   // (the oldest goes)
        slot->id = id;
        CopyName(slot->name, text);
        slot->triedAt = GetTickCount64();
        static int told = 0;
        if (told < 6) {
            ++told;
            char b[200];
            snprintf(b, sizeof b, "vrcam: skill under the map - skill %d is \"%s\" (key \"%s\")", (int)id, slot->name, key.c_str());
            Log(b);
        }
    }
}

void __cdecl UiJob(const PluginContext*, void* user) noexcept {
    if (!g_ticket.Claim(uiticket::FromUser(user))) return;
    NameSkills();   // (before the hero's read: the skills are named in a menu's pause too)
    uint32_t me = 0;
    uint8_t bank = 0;
    const HeroRead r = ReadHeroId(&me, &bank);
    if (r != HeroRead::Ok) {
        Forget();
        if (r == HeroRead::Fault) {
            g_dead.store(true);
            Log("vrcam: ally portraits - reading the client's hero faulted: no portraits until the game restarts");
        } else if (g_toldFail.fetch_add(1) < 2) {
            Log(r == HeroRead::NoAddress ? "vrcam: ally portraits - the game's player lookup was not found (another build?): no portraits"
                                         : "vrcam: ally portraits - no hero to read yet (a title or character screen, or a load)");
        }
        return;
    }
    uintptr_t partyGlobal = 0, petGlobal = 0;
    const bool partyOk = PartyGuard(&partyGlobal), petOk = PetGuard(&petGlobal);
    if (!partyOk && !petOk) {
        Forget();
        if (g_toldFail.fetch_add(1) < 3) Log("vrcam: ally portraits - the game's party and pet lists are not where this build of the mod knows them (another build?): no portraits");
        return;
    }
    Member party[kMaxMembers];
    int np = 0;
    uintptr_t head = 0;
    if (partyOk && ReadMem(partyGlobal, &head, sizeof head) && head) np = std::max(0, ReadParty(head, me, kRead, party, kMaxMembers));   // (-1: not now, none shown)
    Pet pets[kMaxPets];
    int npets = 0;
    head = 0;
    if (petOk && ReadMem(petGlobal, &head, sizeof head)) npets = std::max(0, ReadPets(head, me, kRead, pets, kMaxPets));
    for (int i = 0; i < npets; ++i)
        if (pets[i].type == kHirelingType) NameMerc(pets[i].id);
    NamePets(pets, npets, bank);   // (the merc's first: NameMerc above)
    const Look look = MakeLook(party, np, pets, npets, g_petNames, g_petNamesN, nullptr, 0);
    Store(look);
    LogChange(look);
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    if (!ctx) return;
    if (ctx->QueryService(&g_threads) != ServiceQueryResult::Success) g_threads = nullptr;
    if (ctx->QueryService(&g_strings) != ServiceQueryResult::Success) g_strings = nullptr;
    if (ctx->QueryService(&g_tables) != ServiceQueryResult::Success) g_tables = nullptr;   // the PetType rows: the summons' names
    if (!g_threads || !g_threads->runOnUiThread) Log("vrcam: ally portraits - no thread service: the portraits cannot be read");
    if (!g_strings) Log("vrcam: ally portraits - no localization service: the summons' names in English, the merc's kind for its name");
}

void Tick(bool wantRead) {
    static bool wasWanted = false;
    if (!wantRead) {
        if (wasWanted) { wasWanted = false; Forget(); }
        return;
    }
    wasWanted = true;
    if (!g_ctx || !g_threads || !g_threads->runOnUiThread || g_dead.load()) return;
    static ULONGLONG next = 0;
    const ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 200;
    if (const uint32_t token = g_ticket.Start(now))
        if (g_threads->runOnUiThread(g_ctx, &UiJob, uiticket::ToUser(token)) != Threads::Result::Success) g_ticket.Claim(token);
}

bool Latest(Look* look, double* ageMs) {
    std::lock_guard<std::mutex> lock(g_m);
    if (!g_at || g_dead.load()) return false;
    *look = g_look;
    *ageMs = (double)(GetTickCount64() - g_at);
    return true;
}

void WantSkillNames(int32_t a, int32_t b) {
    g_skillWanted[0].store(a);
    g_skillWanted[1].store(b);
}

bool SkillName(int32_t id, char (&out)[kNameBytes]) {
    out[0] = 0;
    std::lock_guard<std::mutex> lock(g_skillMutex);
    for (const SkillNamed& s : g_skillNamed)
        if (s.id == id && s.name[0]) { CopyName(out, s.name); return true; }
    return false;
}

}  // namespace game
}  // namespace allyportraits
