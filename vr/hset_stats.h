#pragma once
// The headset panel's Stats page ([stats] strike_stats; Serg 2026-11-06: "статистика сколько ударов они нанесли за сессию и за все время в общем
// и для каждого оружия скорость и сила удара"): the strike statistics the touch counts (vr/strike_stats.h, kept by vr/strike_stats_wire.cpp) -
// one hero at a time (< > go round them), this game or all time, the four totals (blows, speed, force, the share of swings that struck)
// and a row a weapon, the most used first. Pure: what the page says, from a copy of the numbers the plugin hands in (vr/hset_stats_feed.h);
// the rectangles are hset_model.h's, the scene hset_scene.h's. Nothing to change here: the page only shows (the tick is D2R VR Settings').

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "d2r_vr_state.h"   // the touch's weapon kinds (D2RVR_TYPE_*), for the line under a weapon's name
#include "strike_stats.h"
#include "ui_text.h"
#include "weapon_names.h"

namespace hset {
namespace stats {

constexpr int kRows = 4;   // the weapons the page has room for (the most used of the period), two lines each

// What the plugin hands in, every few frames while the page is up.
struct View {
    bool on = false;          // [stats] strike_stats ticked
    bool known = false;       // the file was read (or there was none): the numbers are there
    uint64_t revision = 0;    // the plugin's: anything below changed
    int64_t started = 0;      // this game's start (seconds since 1970)
    int current = -1;         // the hero playing now (an index into heroes), -1 none yet
    std::vector<strikestats::Hero> heroes;
    bool operator==(const View& o) const { return on == o.on && known == o.known && revision == o.revision && current == o.current; }
    bool operator!=(const View& o) const { return !(*this == o); }
};

// (each word its own literal call: tools/lang_sync.py finds them for lang/en.txt)
inline const char* ClassName(int cls) {
    switch (cls) {
        case 0: return uitext::Tr("vr.stats.class.amazon", "Amazon");
        case 1: return uitext::Tr("vr.stats.class.sorceress", "Sorceress");
        case 2: return uitext::Tr("vr.stats.class.necromancer", "Necromancer");
        case 3: return uitext::Tr("vr.stats.class.paladin", "Paladin");
        case 4: return uitext::Tr("vr.stats.class.barbarian", "Barbarian");
        case 5: return uitext::Tr("vr.stats.class.druid", "Druid");
        case 6: return uitext::Tr("vr.stats.class.assassin", "Assassin");
        case 7: return uitext::Tr("vr.stats.class.warlock", "Warlock");
        default: return "";
    }
}
// "Barbarian  ·  Bar"; the unnamed hero (his name did not read): his class alone, or "Your hero".
inline std::string HeroTitle(const strikestats::Hero& h) {
    const std::string cls = ClassName(h.cls);
    if (h.name.empty()) return cls.empty() ? std::string(uitext::Tr("vr.stats.hero_unnamed", "Your hero")) : cls;
    return cls.empty() ? h.name : cls + "  \xC2\xB7  " + h.name;
}
inline std::string WeaponName(const strikestats::Weapon& w) {
    if (w.code == strikestats::kBareHands) return uitext::Tr("vr.stats.bare_fists", "Bare fists");
    if (w.code == strikestats::kPaws) return uitext::Tr("vr.stats.paws", "Claws (shapeshifted)");
    if (const char* n = weaponnames::Name(w.code)) return n;   // (the game's English name: the items' names are not ours to translate)
    return strikestats::CodeText(w.code);
}
inline const char* KindText(uint32_t kind) {
    switch (kind) {
        case D2RVR_TYPE_UNARMED: return uitext::Tr("vr.stats.kind.unarmed", "unarmed");
        case D2RVR_TYPE_BOW: return uitext::Tr("vr.stats.kind.bow", "bow");
        case D2RVR_TYPE_CROSSBOW: return uitext::Tr("vr.stats.kind.crossbow", "crossbow");
        case D2RVR_TYPE_SWORD: return uitext::Tr("vr.stats.kind.sword", "sword");
        case D2RVR_TYPE_AXE: return uitext::Tr("vr.stats.kind.axe", "axe");
        case D2RVR_TYPE_MACE: return uitext::Tr("vr.stats.kind.mace", "mace");
        case D2RVR_TYPE_SCEPTER: return uitext::Tr("vr.stats.kind.scepter", "scepter");
        case D2RVR_TYPE_WAND: return uitext::Tr("vr.stats.kind.wand", "wand");
        case D2RVR_TYPE_STAFF: return uitext::Tr("vr.stats.kind.staff", "staff");
        case D2RVR_TYPE_POLEARM: return uitext::Tr("vr.stats.kind.polearm", "polearm");
        case D2RVR_TYPE_SPEAR: return uitext::Tr("vr.stats.kind.spear", "spear");
        case D2RVR_TYPE_DAGGER: return uitext::Tr("vr.stats.kind.dagger", "dagger");
        case D2RVR_TYPE_THROWING: return uitext::Tr("vr.stats.kind.throwing", "throwing weapon");
        case D2RVR_TYPE_JAVELIN: return uitext::Tr("vr.stats.kind.javelin", "javelin");
        case D2RVR_TYPE_CLAW: return uitext::Tr("vr.stats.kind.claw", "claw");
        case D2RVR_TYPE_ORB: return uitext::Tr("vr.stats.kind.orb", "orb");
        case D2RVR_TYPE_OTHER: return uitext::Tr("vr.stats.kind.other", "other");
        default: return "";
    }
}

// 1 284 (a thin gap every three digits: the game's own way of long numbers)
inline std::string Count(uint64_t n) {
    std::string d = std::to_string(n), out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += ' ';
        out += d[i];
    }
    return out;
}
inline std::string Num1(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", (double)v);
    return b;
}
// a row's "average  /  best", "-" none counted
inline std::string AvgBest(float avg, float best, uint64_t n) { return n ? Num1(avg) + "  /  " + Num1(best) : std::string("-"); }
// the share of the swings that touched a monster which struck (blows over blows and misses), "-" none
inline std::string Landed(const strikestats::Agg& g) {
    const uint64_t all = g.blows + g.Misses();
    if (!all) return "-";
    return std::to_string((int)((g.blows * 100 + all / 2) / all)) + " %";
}

inline const strikestats::Agg& Of(const strikestats::Hero& h, bool allTime) { return allTime ? h.total : h.session; }
inline const strikestats::Agg& Of(const strikestats::Weapon& w, bool allTime) { return allTime ? w.total : w.session; }

// The weapons of the period, the most blows first (then the most misses), at most kRows.
inline std::vector<const strikestats::Weapon*> Rows(const strikestats::Hero& h, bool allTime) {
    std::vector<const strikestats::Weapon*> out;
    for (const strikestats::Weapon& w : h.weapons) if (!Of(w, allTime).Empty()) out.push_back(&w);
    std::stable_sort(out.begin(), out.end(), [&](const strikestats::Weapon* a, const strikestats::Weapon* b) {
        const strikestats::Agg &x = Of(*a, allTime), &y = Of(*b, allTime);
        return x.blows != y.blows ? x.blows > y.blows : x.Misses() > y.Misses();
    });
    if (out.size() > (size_t)kRows) out.resize(kRows);
    return out;
}
// How many weapons of the period the rows leave out.
inline int RowsLeftOut(const strikestats::Hero& h, bool allTime) {
    int n = 0;
    for (const strikestats::Weapon& w : h.weapons) if (!Of(w, allTime).Empty()) ++n;
    return std::max(0, n - kRows);
}

// The line under the hero's name: "Hero 1 of 3 - this game since 19:42" / "... - all time".
inline std::string HeroLine(const View& v, int hero, bool allTime) {
    std::string s = uitext::TrF("vr.stats.hero_of", "Hero {i} of {n}", {{"i", std::to_string(hero + 1)}, {"n", std::to_string((int)v.heroes.size())}});
    if (hero == v.current) s += uitext::Tr("vr.stats.playing", " (playing)");
    if (allTime) return s + uitext::Tr("vr.stats.since_ever", " - all time");
    if (v.started > 0) {
        const time_t t = (time_t)v.started;
        tm lt{};
        if (localtime_s(&lt, &t) == 0) {
            char hm[16];
            std::snprintf(hm, sizeof hm, "%02d:%02d", lt.tm_hour, lt.tm_min);
            return s + uitext::TrF("vr.stats.since_game", " - this game since {time}", {{"time", hm}});
        }
    }
    return s + uitext::Tr("vr.stats.this_game", " - this game");
}

// The line under the page (hset_scene.h StatusText).
inline const char* StatusText(const View& v) {
    if (!v.on) return uitext::Tr("vr.stats.status.off", "Strike statistics are off (D2R VR Authentic Settings > Strikes): nothing new is counted.");
    if (!v.known) return uitext::Tr("vr.stats.status.reading", "Reading the strike statistics...");
    if (v.heroes.empty()) return uitext::Tr("vr.stats.status.empty", "No blows yet: swing at a monster in first person (F4), hit by touch.");
    return uitext::Tr("vr.stats.status.idle", "Average / best. The blows you struck by touch (F4); the game still rolls hit or miss.");
}

}  // namespace stats
}  // namespace hset
