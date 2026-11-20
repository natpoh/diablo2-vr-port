// vr/strike_share.h - [stats] strike_stats, shared anonymously (Serg 2026-11-17: "кнопку поделиться анонимно, и тогда после нажатия они будут
// видеть не только свою но и других и средние показатели"). Pure: the text D2R VR Settings sends when the player presses Share (and shows him
// first, word for word), and the server's answer read back. The server is bodywalk_site's python-backend (routes/strike_stats.py), which
// parses the same format (services/strike_stats_agg.py).
//
// What is sent: a random install id (made on the PC: no account, no name, no email), the mod's version, the conditions the numbers were
// struck under (the weight model on, strike by force on, standing or seated, the player's height, the touch's least speed and stroke
// start, quick blow) and per weapon class, every hero together: the blows, the missed swings (too slow / off the rules), the sums for the
// average speed and force, the weapons' average steel, the three distributions (blows by speed, missed swings by speed, blows by force),
// and the fatigue curve (all time, by the minute since a session's first blow). NEVER a hero's name, a hero count by name, an item's own
// name or anything of the PC. The server keeps one snapshot per install id: a new share replaces the last one.
//
// The format: plain text, a record a line, "d2rvr-stats 1" first. The answer: "d2rvr-compare 1" first, then "players <n>", a "class" line per
// weapon class (everyone's numbers) and a "you" line per class the player has numbers in - "<word> <kind> key value key value ...".
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "strike_stats.h"

namespace strikeshare {

constexpr int kFormat = 1;
constexpr size_t kMaxBytes = 32 * 1024;   // the server refuses more

// The weapon classes by the touch's kind (shared/d2r_vr_state.h D2RVR_TYPE_*: 0 unknown, 1 unarmed, 2 bow, 3 crossbow, 4 sword ...).
inline const char* KindWord(uint32_t kind) {
    static const char* const w[] = {"", "unarmed", "bow", "crossbow", "sword", "axe", "mace", "scepter", "wand", "staff", "polearm", "spear", "dagger",
                                    "throwing", "javelin", "claw", "orb", "other"};
    return kind < sizeof w / sizeof w[0] ? w[kind] : "";
}
inline uint32_t KindOfWord(const std::string& s) {
    for (uint32_t k = 1; k < 18; ++k) if (s == KindWord(k)) return k;
    return 0;
}

// The conditions, read from d2r_vr.ini when the share is made (-1 / 0: not known, left out).
struct Conditions {
    int weightOn = -1;      // [weight] the weight model on
    int forceOn = -1;       // [weight] strike_by_force
    int seated = -1;        // [calibration] seated
    float heightM = 0.0f;   // the player's height, m
    float minSpeed = 0.0f;  // [gameplay] melee_min_speed, m/s
    float strokeCm = 0.0f;  // [gameplay] melee_stroke_start_cm
    int quickBlow = -1;     // [gameplay] quick_blow
};

// 32 hexadecimal digits, lowercase: what an install id is (anything else is not sent).
inline bool ValidInstall(const std::string& id) {
    if (id.size() != 32) return false;
    for (char c : id) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
inline std::string InstallFromBytes(const uint8_t (&b)[16]) {
    static const char* const hex = "0123456789abcdef";
    std::string s;
    for (uint8_t v : b) { s += hex[v >> 4]; s += hex[v & 15]; }
    return s;
}

// One weapon class, every hero together (all time).
struct KindTotal {
    uint32_t kind = 0;
    strikestats::Agg agg;
    strikestats::Hist hist;
    double kgSum = 0.0;   // the weapons' steel kg times their blows (those with a measured kg)
    uint64_t kgBlows = 0;
};
inline std::vector<KindTotal> Totals(const strikestats::Store& s) {
    std::vector<KindTotal> out;
    auto of = [&](uint32_t kind) -> KindTotal& {
        for (KindTotal& k : out) if (k.kind == kind) return k;
        out.push_back(KindTotal{});
        out.back().kind = kind;
        return out.back();
    };
    for (const strikestats::Hero& h : s.Heroes()) {
        for (const strikestats::Weapon& w : h.weapons) {
            if (!*KindWord(w.kind) || w.total.Empty()) continue;
            KindTotal& k = of(w.kind);
            const strikestats::Agg& g = w.total;
            k.agg.blows += g.blows;
            k.agg.speedSum += g.speedSum;
            k.agg.speedN += g.speedN;
            k.agg.speedBest = std::max(k.agg.speedBest, g.speedBest);
            k.agg.forceSum += g.forceSum;
            k.agg.forceN += g.forceN;
            k.agg.forceBest = std::max(k.agg.forceBest, g.forceBest);
            k.agg.missSlow += g.missSlow;
            k.agg.missOff += g.missOff;
            if (w.kg > 0.0f) { k.kgSum += (double)w.kg * (double)g.blows; k.kgBlows += g.blows; }
        }
        for (const strikestats::KindHist& kh : h.kinds) {
            if (!*KindWord(kh.kind) || kh.total.Empty()) continue;
            KindTotal& k = of(kh.kind);
            for (int i = 0; i < strikestats::kSpeedBuckets; ++i) { k.hist.hit[i] += kh.total.hit[i]; k.hist.miss[i] += kh.total.miss[i]; }
            for (int i = 0; i < strikestats::kForceBuckets; ++i) k.hist.force[i] += kh.total.force[i];
        }
    }
    return out;
}

// The text that is sent (and shown to the player before, as it is). Empty: nothing to share (no blow, no miss) or the id is not one.
inline std::string Build(const strikestats::Store& s, const std::string& install, const std::string& version, const Conditions& c) {
    if (!ValidInstall(install)) return "";
    const std::vector<KindTotal> kinds = Totals(s);
    bool any = false;
    for (const KindTotal& k : kinds) any = any || !k.agg.Empty();
    if (!any) return "";
    std::string out = "d2rvr-stats " + std::to_string(kFormat) + "\n";
    out += "install " + install + "\n";
    std::string v;
    for (char ch : version) if ((ch >= '0' && ch <= '9') || ch == '.') v += ch;   // (a version is digits and dots: nothing else goes)
    if (!v.empty()) out += "version " + v.substr(0, 20) + "\n";
    char b[256];
    std::string cond = "cond";
    if (c.weightOn >= 0) cond += std::string(" weight=") + (c.weightOn ? "1" : "0");
    if (c.forceOn >= 0) cond += std::string(" force=") + (c.forceOn ? "1" : "0");
    if (c.seated >= 0) cond += std::string(" seated=") + (c.seated ? "1" : "0");
    if (c.heightM > 0.5f && c.heightM < 2.6f) { snprintf(b, sizeof b, " height=%.2f", c.heightM); cond += b; }
    if (c.minSpeed > 0.0f && c.minSpeed < 50.0f) { snprintf(b, sizeof b, " min_speed=%.2f", c.minSpeed); cond += b; }
    if (c.strokeCm > 0.0f && c.strokeCm < 100.0f) { snprintf(b, sizeof b, " stroke_cm=%.1f", c.strokeCm); cond += b; }
    if (c.quickBlow >= 0) cond += std::string(" quick_blow=") + (c.quickBlow ? "1" : "0");
    out += cond + "\n";
    for (const KindTotal& k : kinds) {
        if (k.agg.Empty()) continue;
        const char* w = KindWord(k.kind);
        snprintf(b, sizeof b, "kind %s blows=%llu slow=%llu off=%llu speed_n=%llu speed_sum=%.1f force_n=%llu force_sum=%.1f", w,
                 (unsigned long long)k.agg.blows, (unsigned long long)k.agg.missSlow, (unsigned long long)k.agg.missOff, (unsigned long long)k.agg.speedN,
                 k.agg.speedSum, (unsigned long long)k.agg.forceN, k.agg.forceSum);
        out += b;
        if (k.kgBlows) { snprintf(b, sizeof b, " kg=%.2f", k.kgSum / (double)k.kgBlows); out += b; }
        out += "\n";
        const struct { const char* name; const uint64_t* row; int n; } rows[3] = {
            {"hit", k.hist.hit, strikestats::kSpeedBuckets}, {"miss", k.hist.miss, strikestats::kSpeedBuckets}, {"force", k.hist.force, strikestats::kForceBuckets}};
        for (const auto& r : rows) {
            out += std::string(r.name) + " " + w;
            for (int i = 0; i < r.n; ++i) out += " " + std::to_string(r.row[i]);
            out += "\n";
        }
    }
    // the fatigue curve, every hero together: "curve <minute>:<blows>:<speedN>:<speedSum> ..." (twelve minutes a line)
    std::vector<strikestats::Minute> curve(strikestats::kCurveMinutes);
    bool anyMinute = false;
    for (const strikestats::Hero& h : s.Heroes())
        for (size_t m = 0; m < h.curveTotal.size() && m < curve.size(); ++m) {
            curve[m].blows += h.curveTotal[m].blows;
            curve[m].speedN += h.curveTotal[m].speedN;
            curve[m].speedSum += h.curveTotal[m].speedSum;
            anyMinute = anyMinute || h.curveTotal[m].blows;
        }
    if (anyMinute) {
        std::string line;
        int n = 0;
        for (size_t m = 0; m < curve.size(); ++m) {
            if (!curve[m].blows) continue;
            snprintf(b, sizeof b, " %u:%llu:%llu:%.1f", (unsigned)m, (unsigned long long)curve[m].blows, (unsigned long long)curve[m].speedN, curve[m].speedSum);
            line += b;
            if (++n % 12 == 0) { out += "curve" + line + "\n"; line.clear(); }
        }
        if (!line.empty()) out += "curve" + line + "\n";
    }
    return out.size() <= kMaxBytes ? out : std::string();
}

// The server's answer: everyone's numbers per weapon class, and the player's place among them.
struct ClassRow {
    std::string kind;
    long players = 0;
    long long blows = 0;
    float p10 = 0, p25 = 0, p50 = 0, p75 = 0, p90 = 0;   // the players' average touch speed, m/s
    float forceP50 = 0;                                  // the players' average force, median, N s (0: none measured)
    float landed = -1;                                   // % of the swings that touched which struck (-1 not known)
    bool you = false;
    float yourSpeed = 0, yourForce = 0;
    long yourBlows = 0;
    int speedPct = -1, forcePct = -1, blowsPct = -1;     // the share of players below the player, % (-1: not enough to tell)
};
struct Compare {
    bool ok = false;
    long players = 0;   // installs that shared
    std::vector<ClassRow> rows;
};
inline Compare ParseCompare(const std::string& text) {
    Compare c;
    std::map<std::string, size_t> at;
    size_t pos = 0;
    bool first = true;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string ln = text.substr(pos, end - pos);
        pos = end + 1;
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        std::vector<std::string> t;
        for (size_t i = 0; i < ln.size();) {
            while (i < ln.size() && ln[i] == ' ') ++i;
            size_t j = i;
            while (j < ln.size() && ln[j] != ' ') ++j;
            if (j > i) t.push_back(ln.substr(i, j - i));
            i = j;
        }
        if (t.empty()) continue;
        if (first) {
            first = false;
            if (t.size() < 2 || t[0] != "d2rvr-compare" || atoi(t[1].c_str()) < 1) return Compare{};
            c.ok = true;
            continue;
        }
        if (t[0] == "players" && t.size() >= 2) { c.players = atol(t[1].c_str()); continue; }
        if ((t[0] != "class" && t[0] != "you") || t.size() < 2 || !KindOfWord(t[1])) continue;
        if (!at.count(t[1])) { at[t[1]] = c.rows.size(); c.rows.push_back(ClassRow{}); c.rows.back().kind = t[1]; }
        ClassRow& r = c.rows[at[t[1]]];
        for (size_t i = 2; i + 1 < t.size(); i += 2) {
            const std::string& k = t[i];
            const double v = atof(t[i + 1].c_str());
            if (!std::isfinite(v)) continue;
            if (t[0] == "class") {
                if (k == "players") r.players = (long)v;
                else if (k == "blows") r.blows = (long long)v;
                else if (k == "p10") r.p10 = (float)v;
                else if (k == "p25") r.p25 = (float)v;
                else if (k == "p50") r.p50 = (float)v;
                else if (k == "p75") r.p75 = (float)v;
                else if (k == "p90") r.p90 = (float)v;
                else if (k == "force_p50") r.forceP50 = (float)v;
                else if (k == "landed") r.landed = (float)v;
            } else {
                r.you = true;
                if (k == "speed") r.yourSpeed = (float)v;
                else if (k == "force") r.yourForce = (float)v;
                else if (k == "blows") r.yourBlows = (long)v;
                else if (k == "speed_pct") r.speedPct = (int)v;
                else if (k == "force_pct") r.forcePct = (int)v;
                else if (k == "blows_pct") r.blowsPct = (int)v;
            }
        }
    }
    return c;
}

}  // namespace strikeshare
