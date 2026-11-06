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
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace strikestats {

constexpr int kVersion = 1;
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
    Agg session, total;
};

struct Hero {
    std::string name;   // "" = a hero whose name was not read
    int cls = -1;       // the game's class (0 Amazon .. 7), -1 not read
    uint64_t lastUse = 0;
    Agg session, total;
    std::vector<Weapon> weapons;
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

    void Blow(uint32_t code, uint32_t kind, float speed, float force) {
        Hero& h = Cur();
        Weapon& w = WeaponOf(h, code, kind);
        h.session.Blow(speed, force);
        h.total.Blow(speed, force);
        w.session.Blow(speed, force);
        w.total.Blow(speed, force);
        dirty_ = true;
    }
    void Miss(uint32_t code, uint32_t kind, bool slow) {
        Hero& h = Cur();
        Weapon& w = WeaponOf(h, code, kind);
        h.session.Miss(slow);
        h.total.Miss(slow);
        w.session.Miss(slow);
        w.total.Miss(slow);
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
                snprintf(line, sizeof line, "weapon %s %u %llu\n", CodeText(w.code).c_str(), w.kind, static_cast<unsigned long long>(w.lastUse));
                out += line;
                out += AggLine("all", w.total);
                if (!w.session.Empty()) out += AggLine("now", w.session);
            }
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
                clock_ = std::max(clock_, w->lastUse);
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
    static std::vector<std::string> Words(const std::string& ln) {
        std::vector<std::string> out;
        size_t i = 0;
        while (i < ln.size() && out.size() < 16) {
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
    };
    // a refusal; returns true with *out set when an earlier stroke's miss is now settled
    bool Refused(uint64_t key, uint32_t code, uint32_t kind, bool slow, uint64_t nowMs, Miss* out) {
        bool settled = false;
        if (have_ && key_ != key) settled = Take(out);
        if (!have_ && !(landed_ && key_ == key)) {
            have_ = true;
            landed_ = false;
            key_ = key;
            at_ = nowMs;
            m_ = Miss{code, kind, slow};
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
