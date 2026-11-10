// host_rules.h - the host's rules: the mechanics keys of the Settings' Mechanics tab (and their kind) where WHO HOSTS DECIDES - in a TCP/IP
// game the host's key holds for every player with the mod, a joined player's own key means nothing there (vr/attack_path.h ClientKey,
// vr/melee_direct_client.h CleaveRule, vr/move_attack.h HostScope, vr/quick_blow.h). [hud] game_menu_settings (vr/flat_menu.h) shows them: the flat game's
// in-game panel has them as a section of its own, the list on a key / in the pause menu (VR too, read only) and the D2RLoader console's
// "vrrules" print them, Counter-Strike-like.
//
// Only a VIEW: each mechanic keeps its own channel and its own rule (attack path "AP", the cleave "DH"; move while attacking is the host's
// server alone). This channel ("HR") carries the host's values so a joined player SEES what the host has; nothing here changes play.
//
// The channel: a client says hello once a second (only with [hud] game_menu_settings on, in a game) with a random number of its process;
// the host answers with its rules and its own number. D2RLoader connects a single-player game and a host to their own channel (vr/
// pose_share_wire.h: "a single-player game is its own host"), so the answer tells the role: the host's number is ours - our game (single
// player or we host: our keys are the rules); another - joined (the host's keys are). No answer - no host of this version, or online.
//
// Pure (no Windows, no game); the test: cleanroom/tests/host_rules_test.cpp. The game's side: vr/flat_menu.cpp.
#pragma once

#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

#include "ui_text.h"

namespace hostrules {

// A rule: its key, the names of its values (0 .. count-1), the key's own default (a missing key in the ini).
struct Rule {
    uint8_t id;                // on the wire: never reused, never renumbered
    const wchar_t* section;
    const wchar_t* key;
    const char* en;            // its name in English (the console, the log; the panel asks Label for the player's language)
    int fallback;
    int count;                 // values 0 .. count-1
    const char* names[3];
};

// The Mechanics tab's "who hosts decides" keys (2026-11-05). A new one is a line more (and its Label below).
inline constexpr Rule kRules[] = {
    {1, L"gameplay", L"flat_attack_path", "The swing does not stop the hero (2D)", 0, 3, {"Off", "Walk", "Walk or run"}},
    {2, L"gameplay", L"flat_move_while_attacking", "2D views: strafe after each blow", 0, 3, {"Off", "Spear and bow", "Every weapon"}},
    {3, L"gameplay", L"move_while_attacking", "Spear and bow: walk on after the blow (VR F4)", 1, 2, {"Off", "On"}},
    {4, L"gameplay", L"melee_direct_client_cleave", "Two-handed swing hits each monster (VR)", 0, 2, {"Off", "On"}},
    {5, L"gameplay", L"quick_blow", "Quick blow: the swing never stops the hero (VR)", 1, 2, {"Off", "On"}},
    {6, L"gameplay", L"quick_blow_faster", "Quick blow: faster than the animation", 0, 2, {"Off", "On"}},
    // 2026-11-08 (vr/hit_share.h): the host's box decides whether the joined players' swing timers get their blows' outcomes
    {7, L"multiplayer", L"share_hit_outcomes", "Send the joined players what their blows did", 0, 2, {"Off", "On"}},
    {8, L"gameplay", L"shot_at_release", "The arrow goes where you aim as it leaves the bow", 0, 2, {"Off", "On"}},   // (vr/shot_release.h, 2026-11-06)
    // 2026-11-10 (vr/attack_path.h KindOf): the hero moves during special moves (Zeal, Double Swing, Frenzy, Bash ...)
    {9, L"gameplay", L"move_during_skills", "Move during special moves", 0, 3, {"Off", "Walk", "Walk or run"}},
};
inline constexpr int kCount = static_cast<int>(sizeof kRules / sizeof kRules[0]);
constexpr int kMaxRules = 16;   // what a message may carry (a newer host's extra ids are skipped by an older client)
static_assert(kCount <= kMaxRules, "more rules than a message carries");

inline int IndexOf(uint8_t id) {
    for (int i = 0; i < kCount; ++i) if (kRules[i].id == id) return i;
    return -1;
}
inline int Clamp(int i, int v) {
    if (i < 0 || i >= kCount) return 0;
    return v < 0 ? 0 : v >= kRules[i].count ? kRules[i].count - 1 : v;
}
// The ini's text as a value, read as vrcam's LoadSettings reads the key: a two-valued one as IniB (any number but 0 is on), a choice as
// (int)IniF (cut toward 0); not a number: the key's default; out of range: clamped (flat_attack_path 5 is "walk or run").
inline int Parse(int i, const wchar_t* text) {
    if (i < 0 || i >= kCount) return 0;
    if (!text || !*text) return kRules[i].fallback;
    wchar_t* end = nullptr;
    const double d = wcstod(text, &end);
    if (end == text) return kRules[i].fallback;
    if (kRules[i].count == 2) return d != 0.0 ? 1 : 0;
    return Clamp(i, static_cast<int>(d));
}

// The values of every rule, by index in kRules.
struct Values {
    int v[kMaxRules] = {};
    bool known[kMaxRules] = {};
};

// ---- the wire ----
constexpr uint16_t kChannel = 0x4852;                    // "HR"
constexpr uint64_t kToken = 0x3152484D56523244ull;       // "D2RVMHR1": client and host connect only with the same
constexpr uint16_t kMsgHello = 1, kMsgRules = 2;
constexpr uint8_t kVersion = 1;
constexpr uint32_t kHelloBytes = 5;                      // version, our number (little endian)
constexpr uint32_t kRulesHead = 10;                      // version, the asker's number, the host's number, the count
constexpr uint32_t kMaxPayload = 224;                    // D2RLoader's
static_assert(kRulesHead + 2 * kMaxRules <= kMaxPayload, "a rules message must fit D2RLoader's payload");
constexpr uint64_t kHelloMs = 1000, kFreshMs = 3500;

inline void Put32(uint8_t* b, uint32_t v) { for (int i = 0; i < 4; ++i) b[i] = static_cast<uint8_t>(v >> (8 * i)); }
inline uint32_t Get32(const uint8_t* b) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(b[i]) << (8 * i);
    return v;
}

inline uint32_t BuildHello(uint32_t nonce, uint8_t* b, uint32_t cap) {
    if (!b || cap < kHelloBytes || !nonce) return 0;
    b[0] = kVersion;
    Put32(b + 1, nonce);
    return kHelloBytes;
}
inline bool ParseHello(const void* data, uint32_t size, uint32_t* nonce) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || size != kHelloBytes || b[0] != kVersion) return false;
    const uint32_t n = Get32(b + 1);
    if (!n) return false;
    *nonce = n;
    return true;
}
// The host's answer: every rule it knows, (id, value) pairs.
inline uint32_t BuildRules(uint32_t asker, uint32_t host, const Values& v, uint8_t* b, uint32_t cap) {
    uint32_t n = 0;
    for (int i = 0; i < kCount; ++i) if (v.known[i]) ++n;
    const uint32_t size = kRulesHead + 2 * n;
    if (!b || cap < size || !host) return 0;
    b[0] = kVersion;
    Put32(b + 1, asker);
    Put32(b + 5, host);
    b[9] = static_cast<uint8_t>(n);
    uint32_t at = kRulesHead;
    for (int i = 0; i < kCount; ++i) {
        if (!v.known[i]) continue;
        b[at++] = kRules[i].id;
        b[at++] = static_cast<uint8_t>(Clamp(i, v.v[i]));
    }
    return size;
}
// False: not a rules message of this version. Ids this client does not know are skipped; a value out of its range is clamped.
inline bool ParseRules(const void* data, uint32_t size, uint32_t* asker, uint32_t* host, Values* out) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || size < kRulesHead || b[0] != kVersion) return false;
    const uint32_t n = b[9];
    if (n > kMaxRules || size != kRulesHead + 2 * n) return false;
    const uint32_t h = Get32(b + 5);
    if (!h) return false;
    Values v;
    for (uint32_t k = 0; k < n; ++k) {
        const int i = IndexOf(b[kRulesHead + 2 * k]);
        if (i < 0) continue;
        v.v[i] = Clamp(i, b[kRulesHead + 2 * k + 1]);
        v.known[i] = true;
    }
    *asker = Get32(b + 1);
    *host = h;
    *out = v;
    return true;
}

// Who decides here.
enum class Role { Unknown, Own, Joined };
inline bool Fresh(uint64_t now, uint64_t at, uint64_t forMs = kFreshMs) { return at && now >= at && now - at <= forMs; }
inline Role RoleOf(bool answeredLately, uint32_t ours, uint32_t hosts) {
    if (!answeredLately || !ours || !hosts) return Role::Unknown;
    return hosts == ours ? Role::Own : Role::Joined;
}
// A joined client's next ask for the host's channel: 10 s while one is under way; else 3 s, then 10, 30 and 60 s after each that came to
// nothing (a host without this version refuses each with lines in D2RLoader's log; vr/melee_direct_client.h RetryMs).
inline uint64_t RetryMs(unsigned failed, bool connecting) {
    if (connecting) return 10000;
    static constexpr uint64_t kSteps[] = {3000, 10000, 30000, 60000};
    return kSteps[failed < 3 ? failed : 3];
}

// ---- the words ----
// The rule's name and its value's name in the player's language ([hud] headset_language; English until the batch before a release).
inline const char* Label(int i) {
    switch (i < 0 || i >= kCount ? 0 : kRules[i].id) {
        case 1: return uitext::Tr("vr.rules.flat_attack_path", "The swing does not stop the hero (2D)");
        case 2: return uitext::Tr("vr.rules.flat_move_while_attacking", "2D views: strafe after each blow");
        case 3: return uitext::Tr("vr.rules.move_while_attacking", "Spear and bow: walk on after the blow (VR F4)");
        case 4: return uitext::Tr("vr.rules.melee_direct_client_cleave", "Two-handed swing hits each monster (VR)");
        case 5: return uitext::Tr("vr.rules.quick_blow", "Quick blow: the swing never stops the hero (VR)");
        case 6: return uitext::Tr("vr.rules.quick_blow_faster", "Quick blow: faster than the animation");
        case 7: return uitext::Tr("vr.rules.share_hit_outcomes", "Send the joined players what their blows did");
        case 8: return uitext::Tr("vr.rules.shot_at_release", "The arrow goes where you aim as it leaves the bow");
        case 9: return uitext::Tr("vr.rules.move_during_skills", "Move during special moves");
        default: return "?";
    }
}
inline const char* ValueName(int i, int v) {
    if (i < 0 || i >= kCount) return "?";
    const int c = Clamp(i, v);
    const char* en = kRules[i].names[c];
    if (kRules[i].count == 2) return c ? uitext::Tr("vr.rules.on", "On") : uitext::Tr("vr.rules.off", "Off");
    switch (kRules[i].id * 4 + c) {
        case 1 * 4 + 0: case 2 * 4 + 0: case 9 * 4 + 0: return uitext::Tr("vr.rules.off", "Off");
        case 1 * 4 + 1: case 9 * 4 + 1: return uitext::Tr("vr.rules.walk", "Walk");
        case 1 * 4 + 2: case 9 * 4 + 2: return uitext::Tr("vr.rules.walk_or_run", "Walk or run");
        case 2 * 4 + 1: return uitext::Tr("vr.rules.spear_and_bow", "Spear and bow");
        case 2 * 4 + 2: return uitext::Tr("vr.rules.every_weapon", "Every weapon");
        default: return en;
    }
}
inline const char* RoleLine(Role r) {
    switch (r) {
        case Role::Own: return uitext::Tr("vr.rules.role_own", "Your game: your boxes are the rules - for every joined player with this mod version too");
        case Role::Joined: return uitext::Tr("vr.rules.role_joined", "Joined: the host's rules hold here (your own boxes mean nothing in this game)");
        default: return uitext::Tr("vr.rules.role_unknown", "No host's answer (not in a game, online, or a host without this mod version): your own boxes below");
    }
}

// The list as lines (the console, the pause menu's corner, the key's list): a title, who decides, then "name: value" a rule. Joined, the
// host's values (a rule the host did not send: "?"); else our own.
struct Line { std::string text; bool title = false; bool host = false; };
inline std::vector<Line> List(Role role, const Values& own, const Values& host) {
    std::vector<Line> out;
    out.push_back({uitext::Tr("vr.rules.title", "Host rules (who hosts decides)"), true, false});
    out.push_back({RoleLine(role), false, false});
    const bool joined = role == Role::Joined;
    for (int i = 0; i < kCount; ++i) {
        const Values& v = joined ? host : own;
        std::string s = "  ";
        s += Label(i);
        s += ": ";
        s += v.known[i] ? ValueName(i, v.v[i]) : "?";
        out.push_back({s, false, joined});
    }
    return out;
}

}  // namespace hostrules
