// vr/strike_stats.h - [stats] strike_stats: the player's own strike statistics (Serg 2026-11-06: "я хочу чтобы у игроков была статистика
// сколько ударов они нанесли за сессию и за все время в общем и для каждого оружия скорость и сила удара"). Pure: no game, no clock, no file
// (the wire, vr/strike_stats_wire.cpp, feeds it from the touch and writes the file).
//
// What a blow is. VR first person (F4), hit by touch: a touch the touch code handed to the game as its own attack (vr/melee_touch.cpp, the
// press: ++g_n.pressed) - the game's roll still decides hit or miss, the statistics count the blows the player struck. Its speed is the
// blade's where it touched (m/s; a strike judged by force, [weight] strike_by_force: its arrival's closing speed), its force the weight
// model's measure of the strike (N s: a strike by force's score; a swing's: the weight model's live strike on that monster, when it measures
// one - otherwise the blow has no force and the average is over those that had one).
//
// A swing that touched a monster and landed nothing (MissTracker): the touch's refusals of one stroke (or one strike of the weight model)
// are one missed swing - "too slow" when the first refusal was the speed, "off" when it was the rules (not toward the monster, not the head,
// the shaft or the butt, a swing too small, a strike by force too weak). A stroke that lands a blow on any monster is no miss.
//
// Kept per hero (the name read from the game, local only - never in a log or report) and per weapon (its base item code, the game's own
// name from vr/weapon_names.h); each with this session's numbers and all time's. The file (Serialize / Parse) is plain text, one line a
// record; an unknown line is skipped, so a newer file reads in an older build (what it does not know is lost at the next write).
//
// Version 2 (Serg 2026-11-17: "распределения, а не средние"; the numbers players may share, vr/strike_share.h): per hero and weapon class
// (the touch's kind), this game and all time - the blows by speed bucket (kSpeedEdges), the missed swings by the speed of their first
// refused touch (the same buckets: where a threshold cuts real people), the blows by force bucket (kForceEdges); per hero the fatigue
// curve - blows and speed by the minute since the session's first blow (kCurveMinutes, the last minute takes the rest); per weapon the
// weight model's steel kg when it measured one.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace strikestats {

constexpr int kVersion = 2;
// The buckets' upper edges: speed m/s (a bucket i holds [edge i-1, edge i); the last one everything from the last edge up) and force N s.
constexpr float kSpeedEdges[] = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 10.0f};
constexpr int kSpeedBuckets = static_cast<int>(sizeof kSpeedEdges / sizeof kSpeedEdges[0]) + 1;   // 14
constexpr float kForceEdges[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f};
constexpr int kForceBuckets = static_cast<int>(sizeof kForceEdges / sizeof kForceEdges[0]) + 1;   // 10
constexpr int kCurveMinutes = 120;   // minutes 0 .. 119 since the session's first blow; 119 takes everything later
template <size_t N>
inline int BucketOf(const float (&edges)[N], float v) {
    int i = 0;
    while (i < static_cast<int>(N) && v >= edges[i]) ++i;
    return i;
}
inline int SpeedBucket(float v) { return BucketOf(kSpeedEdges, v); }
inline int ForceBucket(float v) { return BucketOf(kForceEdges, v); }
constexpr size_t kMaxHeroes = 64;     // a file never grows past this many heroes ...
constexpr size_t kMaxWeapons = 160;   // ... or weapons a hero (the oldest-used go first)
constexpr uint32_t kBareHands = 0;    // the code of no weapon: bare fists
// a druid's paws ([shapeshift] claw_hits: a form strikes by the unarmed rules): no item has '*' in its code
constexpr uint32_t kPaws = static_cast<uint32_t>('*') | static_cast<uint32_t>('p') << 8 | static_cast<uint32_t>('a') << 16 | static_cast<uint32_t>('w') << 24;

// One set of numbers: the blows, their speeds and forces (sums for the average, the best), and the swings that missed.
struct Agg {
    uint64_t blows = 0;
    double speedSum = 0.0;
    uint64_t speedN = 0;
    float speedBest = 0.0f;
    double forceSum = 0.0;
    uint64_t forceN = 0;
    float forceBest = 0.0f;
    uint64_t missSlow = 0, missOff = 0;

    // speed m/s and force N s; a value that is not one (<= 0, NaN, absurd) is not counted (the blow is)
    void Blow(float speed, float force) {
        ++blows;
        if (speed > 0.0f && speed < 100.0f) {
            speedSum += speed;
            ++speedN;
            speedBest = std::max(speedBest, speed);
        }
        if (force > 0.0f && force < 1000.0f) {
            forceSum += force;
            ++forceN;
            forceBest = std::max(forceBest, force);
        }
    }
    void Miss(bool slow) { ++(slow ? missSlow : missOff); }
    float SpeedAvg() const { return speedN ? static_cast<float>(speedSum / static_cast<double>(speedN)) : 0.0f; }
    float ForceAvg() const { return forceN ? static_cast<float>(forceSum / static_cast<double>(forceN)) : 0.0f; }
    uint64_t Misses() const { return missSlow + missOff; }
    bool Empty() const { return blows == 0 && Misses() == 0; }
};

struct Weapon {
    uint32_t code = kBareHands;   // the base item's code (four characters, space padded), kBareHands, kPaws
    uint32_t kind = 0;            // its D2RVR_WeaponType (the touch's kind)
    uint64_t lastUse = 0;         // a counter of the store's notes: the oldest-used weapon goes first when a hero has too many
    float kg = 0.0f;              // the weight model's steel, kg, the last time it measured one (0: never)
    Agg session, total;
};

// The distributions of one weapon class: blows by speed, missed swings by speed, blows by force.
struct Hist {
    uint64_t hit[kSpeedBuckets] = {}, miss[kSpeedBuckets] = {}, force[kForceBuckets] = {};
    bool Empty() const {
        for (uint64_t v : hit) if (v) return false;
        for (uint64_t v : miss) if (v) return false;
        for (uint64_t v : force) if (v) return false;
        return true;
    }
};
struct KindHist {
    uint32_t kind = 0;
    Hist session, total;
};
// One minute of the fatigue curve: the blows in it, and their speeds (sum over those that had one).
struct Minute {
    uint64_t blows = 0, speedN = 0;
    double speedSum = 0.0;
};

struct Hero {
    std::string name;   // "" = a hero whose name was not read
    int cls = -1;       // the game's class (0 Amazon .. 7), -1 not read
    uint64_t lastUse = 0;
    Agg session, total;
    std::vector<Weapon> weapons;
    std::vector<KindHist> kinds;                    // version 2: a weapon class each, in the order first struck
    std::vector<Minute> curveTotal, curveSession;   // version 2: kCurveMinutes each once a blow was counted (empty before)
};

// A code as the store keeps it: the game's padding (0 or a space) always a space; bare hands stay 0.
inline uint32_t NormCode(uint32_t code) {
    if (code == kBareHands) return kBareHands;
    uint32_t out = 0;
    for (int i = 0; i < 4; ++i) {
        uint32_t c = (code >> (8 * i)) & 0xFFu;
        if (c == 0 || c == ' ' || c < 0x21 || c > 0x7E) c = ' ';
        out |= c << (8 * i);
    }
    return out;
}
inline std::string CodeText(uint32_t code) {   // "lsd" (the trailing spaces off), "-" bare hands
    if (code == kBareHands) return "-";
    std::string s;
    for (int i = 0; i < 4; ++i) s += static_cast<char>((code >> (8 * i)) & 0xFFu);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s.empty() ? "-" : s;
}
inline uint32_t CodeOf(const char* s) {
    if (!s || !*s || !strcmp(s, "-")) return kBareHands;
    uint32_t out = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = s[i] ? s[i] : ' ';
        out |= static_cast<uint32_t>(static_cast<uint8_t>(c)) << (8 * i);
        if (!s[i]) {
            for (int j = i + 1; j < 4; ++j) out |= static_cast<uint32_t>(' ') << (8 * j);
            break;
        }
    }
    return NormCode(out);
}

// A hero's name as the game keeps it: 2 to 15 letters, '-' or '_' (what D2R lets a name be). Anything else is not a name read right.
inline bool ValidName(const char* s, size_t max) {
    size_t n = 0;
    for (; n < max && s[n]; ++n) {
        const char c = s[n];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_')) return false;
    }
    return n >= 2 && n <= 15 && n < max;
}

class Store {
public:
    // The hero the next blows are his. A name that is not valid is the unnamed hero.
    void SetHero(const char* name, int cls) {
        const std::string n = name && ValidName(name, 16) ? std::string(name) : std::string();
        Hero& h = HeroNamed(n);
        if (cls >= 0) h.cls = cls;
        cur_ = static_cast<int>(&h - heroes_.data());
    }
    const Hero* Current() const { return cur_ >= 0 && cur_ < static_cast<int>(heroes_.size()) ? &heroes_[cur_] : nullptr; }
    int CurrentIndex() const { return Current() ? cur_ : -1; }

    // A blow: speed m/s, force N s (<= 0: none), `minute` since the session's first blow (-1: not known), the weapon's steel kg (0: not measured).
    void Blow(uint32_t code, uint32_t kind, float speed, float force, int minute = -1, float kg = 0.0f) {
        Hero& h = Cur();
        Weapon& w = WeaponOf(h, code, kind);
        h.session.Blow(speed, force);
        h.total.Blow(speed, force);
        w.session.Blow(speed, force);
        w.total.Blow(speed, force);
        if (kg > 0.0f && kg < 1000.0f) w.kg = kg;
        KindHist& k = KindOf(h, kind);
        const bool sp = speed > 0.0f && speed < 100.0f, fo = force > 0.0f && force < 1000.0f;
        if (sp) { ++k.session.hit[SpeedBucket(speed)]; ++k.total.hit[SpeedBucket(speed)]; }
        if (fo) { ++k.session.force[ForceBucket(force)]; ++k.total.force[ForceBucket(force)]; }
        if (minute >= 0) {
            const int m = std::min(minute, kCurveMinutes - 1);
            if (h.curveTotal.empty()) h.curveTotal.resize(kCurveMinutes);
            if (h.curveSession.empty()) h.curveSession.resize(kCurveMinutes);
            for (Minute* c : {&h.curveTotal[m], &h.curveSession[m]}) {
                ++c->blows;
                if (sp) { ++c->speedN; c->speedSum += speed; }
            }
        }
        dirty_ = true;
    }
    // A missed swing: `speed` the speed of its first refused touch (<= 0: not known - counted, not in a bucket).
    void Miss(uint32_t code, uint32_t kind, bool slow, float speed = -1.0f) {
        Hero& h = Cur();
        Weapon& w = WeaponOf(h, code, kind);
        h.session.Miss(slow);
        h.total.Miss(slow);
        w.session.Miss(slow);
        w.total.Miss(slow);
        if (speed > 0.0f && speed < 100.0f) {
            KindHist& k = KindOf(h, kind);
            ++k.session.miss[SpeedBucket(speed)];
            ++k.total.miss[SpeedBucket(speed)];
        }
        dirty_ = true;
    }

    const std::vector<Hero>& Heroes() const { return heroes_; }
    bool Dirty() const { return dirty_; }
    void Clean() { dirty_ = false; }
    void MarkDirty() { dirty_ = true; }   // (a write that failed: the next one tries again)
    void StartSession() {   // this game's numbers start from nothing (all time's stay)
        for (Hero& h : heroes_) {
            h.session = Agg{};
            for (Weapon& w : h.weapons) w.session = Agg{};
            for (KindHist& k : h.kinds) k.session = Hist{};
            h.curveSession.clear();
        }
    }

    // The file's text. `started` / `updated`: this session's start and the write's time (seconds since 1970), kept for the pages.
    std::string Serialize(int64_t started, int64_t updated) const {
        std::string out;
        out += "; D2R VR Authentic - strike statistics (VR first person, hit by touch). Written by the game's plugin while it runs; delete the file to start over.\n";
        char line[512];
        snprintf(line, sizeof line, "version %d\nsession %lld %lld\n", kVersion, static_cast<long long>(started), static_cast<long long>(updated));
        out += line;
        for (const Hero& h : heroes_) {
            snprintf(line, sizeof line, "hero %s %d %llu\n", h.name.empty() ? "-" : h.name.c_str(), h.cls, static_cast<unsigned long long>(h.lastUse));
            out += line;
            out += AggLine("all", h.total);
            if (!h.session.Empty()) out += AggLine("now", h.session);
            for (const Weapon& w : h.weapons) {
                if (w.kg > 0.0f) snprintf(line, sizeof line, "weapon %s %u %llu %.2f\n", CodeText(w.code).c_str(), w.kind, static_cast<unsigned long long>(w.lastUse), w.kg);
                else snprintf(line, sizeof line, "weapon %s %u %llu\n", CodeText(w.code).c_str(), w.kind, static_cast<unsigned long long>(w.lastUse));
                out += line;
                out += AggLine("all", w.total);
                if (!w.session.Empty()) out += AggLine("now", w.session);
            }
            // version 2: "hist <kind> all|now hit|miss|force n0 n1 ..." (only what has a count), "curve all|now <minute> <blows> <speedN> <speedSum>"
            for (const KindHist& k : h.kinds) {
                out += HistLines(k.kind, "all", k.total);
                out += HistLines(k.kind, "now", k.session);
            }
            out += CurveLines("all", h.curveTotal);
            out += CurveLines("now", h.curveSession);
        }
        return out;
    }

    // Reads a file's text into an empty store. False: not a file of ours (nothing taken). Session numbers are read too (the pages of
    // D2R VR Settings show the last game's); the plugin calls StartSession after reading.
    bool Parse(const std::string& text, int64_t* started = nullptr, int64_t* updated = nullptr) {
        heroes_.clear();
        cur_ = -1;
        bool ours = false;
        Hero* h = nullptr;
        Weapon* w = nullptr;
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            std::string ln = text.substr(at, end - at);
            at = end + 1;
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            if (ln.empty() || ln[0] == ';') continue;
            const std::vector<std::string> t = Words(ln);
            if (t.empty()) continue;
            const std::string& word = t[0];
            long long v = 0;
            if (word == "version" && t.size() >= 2 && Int(t[1], &v)) {
                ours = v >= 1;
                if (!ours) return false;
            } else if (!ours) {
                return false;   // the first record must say what it is
            } else if (word == "session" && t.size() >= 3) {
                long long x = 0, y = 0;
                if (Int(t[1], &x) && Int(t[2], &y)) {
                    if (started) *started = x;
                    if (updated) *updated = y;
                }
            } else if (word == "hero" && t.size() >= 3 && Int(t[2], &v)) {
                if (heroes_.size() >= kMaxHeroes) { h = nullptr; w = nullptr; continue; }
                const std::string n = t[1] != "-" && ValidName(t[1].c_str(), t[1].size() + 1) ? t[1] : std::string();
                h = &HeroNamed(n);
                h->cls = static_cast<int>(v);
                long long use = 0;
                if (t.size() >= 4 && Int(t[3], &use) && use > 0) h->lastUse = std::max(h->lastUse, static_cast<uint64_t>(use));
                clock_ = std::max(clock_, h->lastUse);
                w = nullptr;
            } else if (word == "weapon" && h && t.size() >= 3 && t[1].size() <= 4 && Int(t[2], &v)) {
                if (h->weapons.size() >= kMaxWeapons) { w = nullptr; continue; }
                w = &FindOrAdd(*h, CodeOf(t[1].c_str()), static_cast<uint32_t>(v));
                long long use = 0;
                if (t.size() >= 4 && Int(t[3], &use) && use > 0) w->lastUse = std::max(w->lastUse, static_cast<uint64_t>(use));
                double kg = 0.0;
                if (t.size() >= 5 && Num(t[4], &kg) && kg < 1000.0) w->kg = static_cast<float>(kg);
                clock_ = std::max(clock_, w->lastUse);
            } else if (word == "hist" && h && t.size() >= 4 && Int(t[1], &v) && v > 0 && v < 1000 && (t[2] == "all" || t[2] == "now")) {
                Hist& hs = t[2] == "all" ? KindOf(*h, static_cast<uint32_t>(v)).total : KindOf(*h, static_cast<uint32_t>(v)).session;
                uint64_t* row = t[3] == "hit" ? hs.hit : t[3] == "miss" ? hs.miss : t[3] == "force" ? hs.force : nullptr;
                const size_t n = t[3] == "force" ? kForceBuckets : kSpeedBuckets;
                if (!row || t.size() != 4 + n) continue;
                uint64_t got[kSpeedBuckets] = {};
                bool ok = true;
                for (size_t i = 0; i < n && ok; ++i) {
                    long long c = 0;
                    ok = Int(t[4 + i], &c) && c >= 0;
                    got[i] = static_cast<uint64_t>(c);
                }
                if (ok) for (size_t i = 0; i < n; ++i) row[i] = got[i];
            } else if (word == "curve" && h && t.size() >= 6 && (t[1] == "all" || t[1] == "now") && Int(t[2], &v) && v >= 0 && v < kCurveMinutes) {
                long long b = 0, n = 0;
                double sum = 0.0;
                if (!Int(t[3], &b) || !Int(t[4], &n) || !Num(t[5], &sum) || b < 0 || n < 0) continue;
                std::vector<Minute>& c = t[1] == "all" ? h->curveTotal : h->curveSession;
                if (c.empty()) c.resize(kCurveMinutes);
                c[static_cast<size_t>(v)] = Minute{static_cast<uint64_t>(b), static_cast<uint64_t>(n), sum};
            } else if ((word == "all" || word == "now") && h) {
                Agg g;
                if (!ParseAgg(t, &g)) continue;
                const bool all = word[0] == 'a';
                if (w) (all ? w->total : w->session) = g;
                else (all ? h->total : h->session) = g;
            }
        }
        dirty_ = false;
        return ours;
    }

private:
    static std::string AggLine(const char* word, const Agg& g) {
        char line[320];
        snprintf(line, sizeof line, "%s %llu %.3f %llu %.3f %.3f %llu %.3f %llu %llu\n", word, static_cast<unsigned long long>(g.blows), g.speedSum,
                 static_cast<unsigned long long>(g.speedN), g.speedBest, g.forceSum, static_cast<unsigned long long>(g.forceN), g.forceBest,
                 static_cast<unsigned long long>(g.missSlow), static_cast<unsigned long long>(g.missOff));
        return line;
    }
    static std::string HistLines(uint32_t kind, const char* period, const Hist& hs) {
        std::string out;
        const struct { const char* name; const uint64_t* row; int n; } rows[3] = {{"hit", hs.hit, kSpeedBuckets}, {"miss", hs.miss, kSpeedBuckets}, {"force", hs.force, kForceBuckets}};
        for (const auto& r : rows) {
            bool any = false;
            for (int i = 0; i < r.n; ++i) any = any || r.row[i];
            if (!any) continue;
            out += "hist " + std::to_string(kind) + " " + period + " " + r.name;
            for (int i = 0; i < r.n; ++i) out += " " + std::to_string(r.row[i]);
            out += "\n";
        }
        return out;
    }
    static std::string CurveLines(const char* period, const std::vector<Minute>& c) {
        std::string out;
        char line[160];
        for (size_t m = 0; m < c.size(); ++m) {
            if (!c[m].blows) continue;
            snprintf(line, sizeof line, "curve %s %u %llu %llu %.3f\n", period, static_cast<unsigned>(m), static_cast<unsigned long long>(c[m].blows),
                     static_cast<unsigned long long>(c[m].speedN), c[m].speedSum);
            out += line;
        }
        return out;
    }
    static KindHist& KindOf(Hero& h, uint32_t kind) {
        for (KindHist& k : h.kinds) if (k.kind == kind) return k;
        h.kinds.push_back(KindHist{});
        h.kinds.back().kind = kind;
        return h.kinds.back();
    }
    static std::vector<std::string> Words(const std::string& ln) {
        std::vector<std::string> out;
        size_t i = 0;
        while (i < ln.size() && out.size() < 24) {
            while (i < ln.size() && (ln[i] == ' ' || ln[i] == '\t')) ++i;
            size_t j = i;
            while (j < ln.size() && ln[j] != ' ' && ln[j] != '\t') ++j;
            if (j > i) out.push_back(ln.substr(i, j - i));
            i = j;
        }
        return out;
    }
    static bool Int(const std::string& s, long long* v) {
        if (s.empty() || s.size() > 20) return false;
        char* end = nullptr;
        const long long x = strtoll(s.c_str(), &end, 10);
        if (!end || *end) return false;
        *v = x;
        return true;
    }
    static bool Num(const std::string& s, double* v) {
        if (s.empty() || s.size() > 32) return false;
        char* end = nullptr;
        const double x = strtod(s.c_str(), &end);
        if (!end || *end || !(x >= 0.0 && x < 1e15)) return false;   // (NaN fails too)
        *v = x;
        return true;
    }
    // "all|now blows speedSum speedN speedBest forceSum forceN forceBest missSlow missOff" (more words: a newer file's, skipped)
    static bool ParseAgg(const std::vector<std::string>& t, Agg* g) {
        if (t.size() < 10) return false;
        long long blows = 0, sn = 0, fn = 0, ms = 0, mo = 0;
        double ss = 0.0, fs = 0.0, sb = 0.0, fb = 0.0;
        if (!Int(t[1], &blows) || !Num(t[2], &ss) || !Int(t[3], &sn) || !Num(t[4], &sb) || !Num(t[5], &fs) || !Int(t[6], &fn) || !Num(t[7], &fb) ||
            !Int(t[8], &ms) || !Int(t[9], &mo))
            return false;
        if (blows < 0 || sn < 0 || fn < 0 || ms < 0 || mo < 0 || sb > 1e6 || fb > 1e6) return false;
        g->blows = static_cast<uint64_t>(blows);
        g->speedSum = ss;
        g->speedN = static_cast<uint64_t>(sn);
        g->speedBest = static_cast<float>(sb);
        g->forceSum = fs;
        g->forceN = static_cast<uint64_t>(fn);
        g->forceBest = static_cast<float>(fb);
        g->missSlow = static_cast<uint64_t>(ms);
        g->missOff = static_cast<uint64_t>(mo);
        return true;
    }
    Hero& HeroNamed(const std::string& n) {
        for (Hero& h : heroes_) if (h.name == n) return h;
        if (heroes_.size() >= kMaxHeroes) {   // the hero used longest ago makes room (never the current one)
            size_t old = 0;
            for (size_t i = 1; i < heroes_.size(); ++i) if (heroes_[i].lastUse < heroes_[old].lastUse) old = i;
            const std::string keep = cur_ >= 0 ? heroes_[cur_].name : std::string();
            heroes_.erase(heroes_.begin() + static_cast<long>(old));
            cur_ = -1;
            for (size_t i = 0; i < heroes_.size(); ++i) if (heroes_[i].name == keep) cur_ = static_cast<int>(i);
        }
        heroes_.push_back(Hero{});
        heroes_.back().name = n;
        return heroes_.back();
    }
    Hero& Cur() {
        if (cur_ < 0 || cur_ >= static_cast<int>(heroes_.size())) {
            HeroNamed(std::string());
            for (size_t i = 0; i < heroes_.size(); ++i) if (heroes_[i].name.empty()) cur_ = static_cast<int>(i);
        }
        Hero& h = heroes_[cur_];
        h.lastUse = ++clock_;
        return h;
    }
    static Weapon& FindOrAdd(Hero& h, uint32_t code, uint32_t kind) {
        for (Weapon& w : h.weapons) if (w.code == code) {
            if (kind) w.kind = kind;
            return w;
        }
        h.weapons.push_back(Weapon{});
        h.weapons.back().code = code;
        h.weapons.back().kind = kind;
        return h.weapons.back();
    }
    Weapon& WeaponOf(Hero& h, uint32_t code, uint32_t kind) {
        code = NormCode(code);
        bool have = false;
        for (const Weapon& w : h.weapons) have = have || w.code == code;
        if (!have && h.weapons.size() >= kMaxWeapons) {   // the weapon used longest ago makes room
            size_t old = 0;
            for (size_t i = 1; i < h.weapons.size(); ++i) if (h.weapons[i].lastUse < h.weapons[old].lastUse) old = i;
            h.weapons.erase(h.weapons.begin() + static_cast<long>(old));
        }
        Weapon& w = FindOrAdd(h, code, kind);
        w.lastUse = ++clock_;
        return w;
    }

    std::vector<Hero> heroes_;
    int cur_ = -1;
    uint64_t clock_ = 0;
    bool dirty_ = false;
};

// The swings that touched and landed nothing, one hand's: Refused notes a refusal of a stroke (key: the stroke's id, or a weight-model
// strike's serial with the top bit set), Landed says that stroke landed a blow (no miss then). The miss is counted when the hand's next
// stroke is refused or lands, or when the stroke is older than kMissMs - so a stroke refused on one pass and landed on the next is a blow,
// never both. Flush returns the miss to count (true) once.
class MissTracker {
public:
    static constexpr uint64_t kMissMs = 1500;
    struct Miss {
        uint32_t code = 0, kind = 0;
        bool slow = false;
        float speed = -1.0f;   // the speed of the stroke's first refused touch, m/s (-1: not known)
    };
    // a refusal; returns true with *out set when an earlier stroke's miss is now settled
    bool Refused(uint64_t key, uint32_t code, uint32_t kind, bool slow, uint64_t nowMs, Miss* out, float speed = -1.0f) {
        bool settled = false;
        if (have_ && key_ != key) settled = Take(out);
        if (!have_ && !(landed_ && key_ == key)) {
            have_ = true;
            landed_ = false;
            key_ = key;
            at_ = nowMs;
            m_ = Miss{code, kind, slow, speed};
        }
        return settled;
    }
    bool Landed(uint64_t key, Miss* out) {
        bool settled = false;
        if (have_ && key_ != key) settled = Take(out);
        have_ = false;
        landed_ = true;   // (a refusal of this stroke after its blow is no miss either)
        key_ = key;
        return settled;
    }
    bool Flush(uint64_t nowMs, Miss* out) {
        if (!have_ || nowMs - at_ < kMissMs) return false;
        return Take(out);
    }

private:
    bool Take(Miss* out) {
        have_ = false;
        landed_ = false;
        if (out) *out = m_;
        return true;
    }
    bool have_ = false, landed_ = false;
    uint64_t key_ = 0, at_ = 0;
    Miss m_;
};

}  // namespace strikestats
