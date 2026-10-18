// wheel_game.cpp - the skill wheel in the game (vr/wheel_game.h): the controller's skill slots and the player's skills read on the
// UI thread, and an assignment carried out by the game's own setter.
//
// Which functions and tables, and how the setter is called (the skill-select button's own, with the controller table chosen by the
// game's controller-layout query), were found by bearstonem/d2r-pcvr (MIT, vr/native_ui.cpp and native_ui_signatures.h) for this
// same build; written again here on our signatures (game_sigs.h, area "Skill wheel"), so another build finds them by pattern.
// Nothing of the game's is written except through the setter: no skill record, no table entry. Every call into the game is guarded
// against a fault; a fault switches the wheel's game side off for the session.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <D2RLPlugin/api.h>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "direct_input.h"
#include "sigscan.h"
#include "ui_ticket.h"
#include "wheel_bank.h"
#include "wheel_game.h"
#include "wheel_icons.h"

using namespace D2RL;

namespace wheelgame {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h).
constexpr uint64_t kLocalIndex = 0x8B2D0, kClientPlayer = 0x9A480, kButtonSlot = 0x1448C0, kControllerBank = 0x1195C0,
                   kSkillLookup = 0x33DCD0, kSkillPositive = 0x33A660, kAssignSkill = 0x1185C0, kClearSkill = 0x118B40,
                   kControllerSkills = 0x2A48A90, kIsController = 0x77E10;
// the game's run / walk switch ("Always run": the dword its key command 0x23 flips), read for the RUN / WALK control's caption
constexpr uint64_t kRunLock = 0x2A2375C;
// code whose bytes prove a layout (found = the offsets below are the build's)
constexpr uint64_t kSkillNodeLayout = 0x33DD09, kSkillListHead = 0x34B70E, kSkillPositiveLevel = 0x33A68E, kSkillLeftFlag = 0x2389C0;
// the layouts: the player unit's skill list head, a skill node, a Skills row, a controller binding record
constexpr uintptr_t kPlayerSkillList = 0x100;
constexpr uintptr_t kRowPassive = 0x24, kRowLeft = 0x27;
constexpr uint8_t kPassiveBit = 0x10, kLeftBit = 0x40;
constexpr uint32_t kBindingSize = 0x1C;

#pragma pack(push, 1)
struct Node {
    uintptr_t row, next;
    uint8_t pad[0x30];
    int32_t level, bonus, base, owner;
};
struct RowHead {
    int16_t id;
    uint8_t pad[0x22];
    uint8_t flags[4];
};
struct Record {
    int32_t skill, owner;
    uint32_t reserved[5];
};
#pragma pack(pop)
static_assert(sizeof(Node) == 0x50 && offsetof(Node, owner) == 0x4C && offsetof(Node, level) == 0x40, "the game's skill node");
static_assert(offsetof(RowHead, flags) == kRowPassive, "the Skills row's flags");
static_assert(sizeof(Record) == kBindingSize, "the game's controller binding record");

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
const LocalizationService* g_strings = nullptr;

std::mutex g_lock;
wheel::Snapshot g_snap;
bool g_have = false;
wheel::Request g_req;
bool g_haveReq = false;
std::atomic<uint64_t> g_reply{0};       // sequence << 8 | Result
uiticket::Ticket g_capQueued, g_reqQueued;
std::atomic<ULONGLONG> g_nextCapture{0};
std::atomic<bool> g_dead{false};        // a call into the game faulted: off for the session
std::map<int32_t, std::string> g_names; // UI thread only
std::map<int32_t, ULONGLONG> g_nameMiss; // UI thread only: a name the game's text service did not give, tried again after kMissMs
constexpr ULONGLONG kMissMs = 5000;
std::map<std::string, std::string> g_textByKey;   // UI thread only: the skill trees' names, by their string key
std::map<std::string, ULONGLONG> g_keyMiss;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

template <class S> const S* Query(const PluginContext* ctx) {
    const S* s = nullptr;
    return ctx->QueryService(&s) == ServiceQueryResult::Success ? s : nullptr;
}

// -- guarded calls ----------------------------------------------------------------------------------------------------------

bool SafeRead(void* dst, uintptr_t src, size_t n) noexcept {
    if (src < 0x10000) return false;
    __try { memcpy(dst, reinterpret_cast<const void*>(src), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template <class T> bool Rd(uintptr_t src, T* out) noexcept { return SafeRead(out, src, sizeof(T)); }

bool CallInt(uint64_t rva, int* out) noexcept {
    const uintptr_t a = d2rsig::Addr(rva);
    if (!a) return false;
    __try { *out = reinterpret_cast<int (*)()>(a)(); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallBool(uint64_t rva, bool* out) noexcept {
    const uintptr_t a = d2rsig::Addr(rva);
    if (!a) return false;
    __try { *out = reinterpret_cast<bool (*)()>(a)(); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallPlayer(int index, uintptr_t* out) noexcept {
    const uintptr_t a = d2rsig::Addr(kClientPlayer);
    if (!a) return false;
    __try { *out = reinterpret_cast<uintptr_t (*)(int)>(a)(index); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallButtonSlot(int buttons, bool alt, int* out) noexcept {
    const uintptr_t a = d2rsig::Addr(kButtonSlot);
    if (!a) return false;
    __try { *out = reinterpret_cast<int (*)(int, bool)>(a)(buttons, alt); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallLookup(uintptr_t player, int id, int owner, uintptr_t* out) noexcept {
    const uintptr_t a = d2rsig::Addr(kSkillLookup);
    if (!a) return false;
    __try { *out = reinterpret_cast<uintptr_t (*)(uintptr_t, int, int)>(a)(player, id, owner); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallPositive(uintptr_t player, uintptr_t node, bool* out) noexcept {
    const uintptr_t a = d2rsig::Addr(kSkillPositive);
    if (!a) return false;
    __try { *out = reinterpret_cast<bool (*)(uintptr_t, uintptr_t)>(a)(player, node); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallAssign(int slot, bool left, int skill, int owner) noexcept {
    const uintptr_t a = d2rsig::Addr(kAssignSkill);
    if (!a) return false;
    __try { reinterpret_cast<void (*)(int, bool, int, int)>(a)(slot, left, skill, owner); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallClear(int slot) noexcept {
    const uintptr_t a = d2rsig::Addr(kClearSkill);
    if (!a) return false;
    __try { reinterpret_cast<void (*)(int)>(a)(slot); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// -- the game's string key for a skill's name, when the cache has none -------------------------------------------------------
// The keys are not regular across the expansion (d2r-pcvr's list, identifiers only): the manifest has the exact key of every skill
// the game's tables name, this is the guess for a build with no cache.
const char* GuessKey(int32_t id, char key[80]) {
    if (id == 61) return "skillsname61";
    if (id == 197) return "Skillname226";
    if (id >= 357 && id <= 364) {
        static const char* const special[] = {"SkillNameInteract", "SkillNameLoot", "skillname219", "SkillNameEmoteWheel",
                                              "SkillNameSwapWeapons", "SkillNameMap", "SkillNameShowItems", "SkillNameRunToggle"};
        return special[id - 357];
    }
    if (id == 370) return "SkillNameCubeLoot";
    if (id > 372 || id < 0) return nullptr;
    std::snprintf(key, 80, "skillname%d", (int)(id + (id >= 221 && id <= 280 ? 1 : 0)));
    return key;
}

// A call into the game that faulted: the game's side of the wheel is off until the next session (Reset), and the log says so.
void Fault(const char* what) {
    g_dead.store(true);
    char b[160];
    std::snprintf(b, sizeof b, "vrcam: skill wheel - %s faulted: the wheel's game side is off until the next session", what);
    Log(b);
}

// The game's own text for a string key ("d2r:" is added), in the player's language; cached, a miss asked again after kMissMs.
bool Localize(const std::string& key, std::string* out) {
    const auto hit = g_textByKey.find(key);
    if (hit != g_textByKey.end()) { *out = hit->second; return true; }
    const auto miss = g_keyMiss.find(key);
    if (miss != g_keyMiss.end() && GetTickCount64() - miss->second < kMissMs) return false;
    if (g_strings && g_strings->getStringByKey) {
        const std::string full = "d2r:" + key;
        char buf[256] = {};
        uint32_t required = 0;
        if (g_strings->getStringByKey(g_ctx, full.c_str(), buf, sizeof buf, &required) == Localization::Result::Success && buf[0]) {
            g_keyMiss.erase(key);
            *out = g_textByKey.emplace(key, buf).first->second;
            return true;
        }
    }
    g_keyMiss[key] = GetTickCount64();
    return false;
}

class RealGame final : public wheel::Game {
    uintptr_t player_ = 0;
    int class_ = -1;
    bool ok_ = false;

public:
    RealGame() {
        const uint64_t needed[] = {kLocalIndex, kClientPlayer, kButtonSlot, kControllerBank, kSkillLookup, kSkillPositive, kAssignSkill,
                                   kClearSkill, kControllerSkills, kIsController, kSkillNodeLayout, kSkillListHead, kSkillPositiveLevel, kSkillLeftFlag};
        for (const uint64_t rva : needed)
            if (!d2rsig::Addr(rva)) return;
        int index = -1;
        if (!CallInt(kLocalIndex, &index) || index < 0 || index >= 8 || !CallPlayer(index, &player_) || !player_) return;
        uint32_t head[4] = {};
        if (!Rd(player_, &head) || head[0] != 0) return;   // unit type 0: a player
        class_ = head[1] < 16 ? (int)head[1] : -1;         // (the unit's class: the game's own index of the player's class)
        ok_ = true;
    }
    bool Ok() const { return ok_; }
    uintptr_t Player() const { return player_; }

    bool ControllerLayout() override {
        bool b = false;
        return CallBool(kIsController, &b) && b;
    }
    int Bank() override {
        int b = -1;
        return CallInt(kControllerBank, &b) ? b : -1;
    }
    int ButtonSlot(uint16_t button, bool alt) override {
        int s = -1;
        return CallButtonSlot(button, alt, &s) ? s : -1;
    }
    bool ReadBinding(int index, wheel::Binding* out) override {
        if (index < 0 || index >= 32) return false;
        Record r;
        if (!Rd(d2rsig::Addr(kControllerSkills) + (uintptr_t)index * kBindingSize, &r) || r.skill < -1 || r.skill > 4095) return false;
        out->skill = r.skill;
        out->owner = r.owner;
        return true;
    }
    bool HasSkill(int32_t id, int32_t owner, int32_t* level) override {
        uintptr_t node = 0;
        bool positive = false;
        if (!CallLookup(player_, id, owner, &node) || !node || !CallPositive(player_, node, &positive) || !positive) return false;
        Node n;
        if (!Rd(node, &n)) return false;
        *level = n.base;
        return true;
    }
    bool LeftFlag(int32_t id, int32_t owner, bool* left) override {
        uintptr_t node = 0, row = 0;
        uint8_t flag = 0;
        if (!CallLookup(player_, id, owner, &node) || !node || !Rd(node, &row) || !Rd(row + kRowLeft, &flag)) return false;
        *left = (flag & kLeftBit) != 0;
        return true;
    }
    bool LearnedSkills(std::vector<wheel::Learned>* out) override {
        uintptr_t list = 0, node = 0;
        if (!Rd(player_ + kPlayerSkillList, &list) || list < 0x10000 || !Rd(list, &node)) return false;
        for (unsigned visited = 0; node && visited < 256; ++visited) {
            Node n;
            RowHead row;
            if (!Rd(node, &n) || !Rd(n.row, &row) || row.id < 0 || row.id > 4095 || n.next == node) return false;
            bool positive = false;
            if (!CallPositive(player_, node, &positive)) return false;
            if (!(row.flags[0] & kPassiveBit) && positive) out->push_back(wheel::Learned{row.id, n.owner, n.level + n.bonus});
            node = n.next;
        }
        return node == 0;   // a list that did not end is not a list
    }
    bool Name(int32_t id, char* out, int n) override {
        auto it = g_names.find(id);
        if (it == g_names.end()) {
            const auto miss = g_nameMiss.find(id);
            if (miss != g_nameMiss.end() && GetTickCount64() - miss->second < kMissMs) return false;
            std::string text;
            if (g_strings && g_strings->getStringByKey) {
                char guess[80] = {};
                std::string key = wheelicons::NameKey(id);
                if (key.empty()) {
                    const char* g = GuessKey(id, guess);
                    if (g) key = g;
                }
                if (!key.empty()) {
                    const std::string full = "d2r:" + key;
                    char buf[256] = {};
                    uint32_t required = 0;
                    if (g_strings->getStringByKey(g_ctx, full.c_str(), buf, sizeof buf, &required) == Localization::Result::Success && buf[0]) text = buf;
                }
            }
            if (text.empty()) {   // (asked again after a few seconds: the text service may not be ready yet)
                g_nameMiss[id] = GetTickCount64();
                return false;
            }
            g_nameMiss.erase(id);
            it = g_names.emplace(id, text).first;
        }
        std::snprintf(out, (size_t)n, "%s", it->second.c_str());
        return true;
    }
    // The skill trees: from the cache the player made from their own game (which class and tree page each skill is on, the trees' name keys) and
    // the player's class from the unit. Nothing of the game's table is in the plugin.
    int PlayerClass() override { return class_; }
    bool TreesKnown() override { return class_ >= 0 && wheelicons::HasTrees(); }
    int Tree(int32_t id, int32_t owner) override {
        if (!TreesKnown()) return wheel::kTreeUnknown;
        if (owner != -1) return 0;   // an item's skill is General / Items
        int cls = -1, page = 0;
        if (!wheelicons::SkillTree(id, &cls, &page)) return 0;   // no row in the cache: a command of the game's, on no tree
        return cls == class_ && page >= 1 && page <= 3 ? page : 0;
    }
    bool TreeName(int tree, char* out, int n) override {
        const std::string key = wheelicons::TreeKey(class_, tree);
        std::string text;
        if (key.empty() || !Localize(key, &text)) return false;
        std::snprintf(out, (size_t)n, "%s", text.c_str());
        return true;
    }
    int RunState() override {
        const uintptr_t a = d2rsig::Addr(kRunLock);
        uint32_t v = 0;
        if (!a || !Rd(a, &v)) return wheel::kRunUnknown;
        return v ? wheel::kRunning : wheel::kWalking;
    }
    void DoAssign(int slotIndex, bool left, int32_t skill, int32_t owner) override {
        if (!CallAssign(slotIndex, left, skill, owner)) Fault("the game's setter");
    }
    void DoClear(int slotIndex) override {
        if (!CallClear(slotIndex)) Fault("the game's clearer");
    }
};

// -- the UI thread's work -----------------------------------------------------------------------------------------------------

bool SameContent(const wheel::Snapshot& a, const wheel::Snapshot& b) {
    if (a.session != b.session || a.ready != b.ready || a.editable != b.editable || a.bank != b.bank || a.count != b.count ||
        a.playerClass != b.playerClass || a.treesKnown != b.treesKnown || a.run != b.run || std::memcmp(a.treeName, b.treeName, sizeof a.treeName) != 0)
        return false;
    for (int i = 0; i < wheel::kSlotCount; ++i) if (!(a.slot[i] == b.slot[i])) return false;
    for (uint32_t i = 0; i < a.count && i < (uint32_t)wheel::kCatalogMax; ++i) if (!(a.catalog[i] == b.catalog[i])) return false;
    return true;
}

void Publish(const wheel::Snapshot& s) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_have && SameContent(g_snap, s)) {
        g_snap.at = s.at;
        return;
    }
    const uint32_t rev = g_snap.revision + 1;
    g_snap = s;
    g_snap.revision = rev;
    g_have = true;
}

void CaptureNow() {
    static wheel::Snapshot s;   // (the UI thread's, one at a time; 12 KB off the stack)
    s = wheel::Snapshot{};
    s.session = directinput::Session();
    s.at = GetTickCount64();
    try {
        RealGame g;
        static int toldNot = 0;
        if (g_dead.load()) {
            s.ready = false;
        } else if (!g.Ok()) {
            if (toldNot < 2) {
                ++toldNot;
                Log("vrcam: skill wheel - the game's player or its controller skill functions are not there yet (or this is another build)");
            }
        } else if (wheel::Capture(g, &s)) {
            static bool told = false;
            if (!told) {
                told = true;
                char b[360];
                std::snprintf(b, sizeof b, "vrcam: skill wheel - the controller's skill slots read (weapon set bank %d, %u skills to choose from)%s", s.bank,
                              s.count, wheelicons::Available() ? ", icons from the local cache" : ", no icon cache (names only)");
                Log(b);
                // which index of the game's table each of the twelve slots is (A and LT + A from the game's own lookup, RT by elimination)
                if (!s.treesKnown)
                    Log("vrcam: skill wheel - the icon cache has no skill-tree data (or the class is not known): the editor has no category strip; run "
                        "tools/skill_icons/prepare_skill_icons.py again to make it");
                else
                    Log("vrcam: skill wheel - the editor's categories: All, the class's three skill trees, General / Items (from the icon cache)");
                int idx[wheel::kSlotCount];
                if (wheel::SlotIndices(g, idx)) {
                    std::snprintf(b, sizeof b, "vrcam: skill wheel - the game's slot indices: B %d X %d Y %d RB %d | with LT: B %d X %d Y %d RB %d | A %d RT %d | LT + A %d LT + RT %d (-1 = not told)",
                                  idx[0], idx[1], idx[2], idx[3], idx[4], idx[5], idx[6], idx[7], idx[8], idx[9], idx[10], idx[11]);
                    Log(b);
                }
            }
        }
    } catch (...) {
        s.ready = false;
    }
    Publish(s);
}

void __cdecl CaptureJob(const PluginContext*, void* user) noexcept {
    if (!g_capQueued.Claim(uiticket::FromUser(user))) return;
    CaptureNow();
}

void SetReply(uint64_t sequence, wheel::Result r) { g_reply.store(sequence << 8 | (uint64_t)r); }

void __cdecl RequestJob(const PluginContext*, void* user) noexcept {
    if (!g_reqQueued.Claim(uiticket::FromUser(user))) return;
    wheel::Request r;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_haveReq) return;
        r = g_req;
        g_haveReq = false;
    }
    CaptureNow();   // the state the setter is checked against is the game's now, not 400 ms ago
    wheel::Result result = wheel::Result::Refused;
    const char* why = "";
    try {
        wheel::Snapshot chosen;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            chosen = g_snap;
        }
        RealGame g;
        if (g_dead.load()) why = "the game's side is off after a fault";
        else if (!g.Ok()) why = "the game's player is not there";
        else result = wheel::Apply(g, r, chosen, GetTickCount64(), &why);
    } catch (...) {
        why = "an exception";
    }
    char b[240];
    if (result == wheel::Result::Applied)
        std::snprintf(b, sizeof b, "vrcam: skill wheel - slot %d: %s applied, read back from the game's table", r.slot, r.op == wheel::Op::Clear ? "clear" : "assign");
    else
        std::snprintf(b, sizeof b, "vrcam: skill wheel - slot %d: %s refused (%s)", r.slot, r.op == wheel::Op::Clear ? "clear" : "assign", why);
    Log(b);
    SetReply(r.sequence, result);
    CaptureNow();   // the wheel shows what the game has now
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    g_threads = Query<ThreadService>(ctx);
    g_strings = Query<LocalizationService>(ctx);
    if (!g_threads || !g_threads->runOnUiThread) Log("vrcam: skill wheel - no UI thread service: the wheel cannot read or set the game's skills");
    if (!g_strings || !g_strings->getStringByKey) Log("vrcam: skill wheel - no localization service: skills are shown as numbers");
}

void Tick(bool on, bool open) {
    if (!on || !g_ctx || !g_threads || !g_threads->runOnUiThread || g_dead.load()) return;
    const ULONGLONG now = GetTickCount64();
    bool haveReq;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        haveReq = g_haveReq;
    }
    if (haveReq) {
        if (const uint32_t token = g_reqQueued.Start(now))
            if (g_threads->runOnUiThread(g_ctx, &RequestJob, uiticket::ToUser(token)) != Threads::Result::Success) g_reqQueued.Claim(token);
    }
    if (now >= g_nextCapture.load()) {
        g_nextCapture.store(now + (open ? 100 : 400));
        if (const uint32_t token = g_capQueued.Start(now))
            if (g_threads->runOnUiThread(g_ctx, &CaptureJob, uiticket::ToUser(token)) != Threads::Result::Success) g_capQueued.Claim(token);
    }
}

void Submit(const wheel::Request& r) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_req = r;
    g_haveReq = true;
    SetReply(r.sequence, wheel::Result::Pending);
}

wheel::Reply ReplyNow() {
    const uint64_t v = g_reply.load();
    wheel::Reply r;
    r.sequence = v >> 8;
    r.result = (wheel::Result)(v & 0xFF);
    return r;
}

bool Latest(wheel::Snapshot* out, uint32_t* revision) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_have) return false;
    if (*revision != g_snap.revision) {
        *out = g_snap;
        *revision = g_snap.revision;
    } else {
        out->at = g_snap.at;
        out->session = g_snap.session;
    }
    return true;
}

void Reset() {
    g_dead.store(false);
    std::lock_guard<std::mutex> lock(g_lock);
    g_snap = wheel::Snapshot{};
    g_have = false;
    g_haveReq = false;
    g_reply.store(0);
}

}  // namespace wheelgame
