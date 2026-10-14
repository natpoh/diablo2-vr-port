// throw_trace.h - [debug] throw_trace: what the game does after a throw is pressed, told line by line in the log. Pure (no game, no clock of its own);
// unit test cleanroom/tests/throw_trace_test.cpp.
//
// A throw by gesture ends in the game's own throw, and what the game does with it was, until this, a guess: the hero went into his throw mode (the log
// said so) and then ... a missile flew, or none did, and it exploded, or not. The tracer watches for a few seconds after every released throw, every
// ~50 ms: the hero's mode, and every missile unit the client has (class, place, the frames it has left to live), and says
//   - when the hero's mode changes (neutral -> throw -> neutral: how long his animation took),
//   - when a missile appears (a potion's class 44 - 49) and where (how far from the hero), with its life,
//   - the potion's flight (every 0.25 s), and the moment it is gone and where it was last seen,
//   - any missile that appears near the spot it vanished at within 1.5 s (the explosion's own: class 29 fire, 51 the potion's burst, 222 - 224 the gas
//     clouds ...), or the plain fact that none did.
// All times are seconds since the release. Nothing here touches the game; vrcam feeds it what meleetouch::MissileScan reads.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace throwtrace {

inline constexpr double kWindowS = 5.0;       // how long a throw is watched
inline constexpr double kEveryS = 0.25;       // a flying potion is told this often
inline constexpr double kEffectWindowS = 1.5; // a missile appearing this soon after a potion vanished, near its spot, is its effect
inline constexpr float kEffectCells = 4.0f;   // ... within this many cells
inline constexpr size_t kMaxTracked = 8;
inline constexpr int kMaxLines = 80;          // at most this many lines for one throw

inline const char* ClassName(uint32_t cls) {
    switch (cls) {
        case 44: return "oilpotion (Fulminating)";
        case 45: return "explosivepotion";
        case 46: return "fulminatingpotion";
        case 47: return "rancidgaspotion";
        case 48: return "chokinggaspotion";
        case 49: return "stranglinggaspotion";
        case 29: return "fireexplode";
        case 51: return "explosivepotionexp";
        case 222: return "rancidgascloud";
        case 223: return "chokinggascloud";
        case 224: return "stranglinggascloud";
        default: return "";
    }
}
inline bool IsPotion(uint32_t cls) { return cls >= 44 && cls <= 49; }

struct Missile {
    uint32_t id = 0, cls = 0, mode = 0, flags = 0;
    float x = 0.0f, z = 0.0f;   // game WORLD units (twice the cell)
    float life = -1.0f;         // frames left to live by the client's count (-1: not read)
};

struct Snapshot {
    double t = 0.0;                    // seconds (any base: GetTickCount64 / 1000)
    bool heroModeKnown = false;
    uint32_t heroMode = 0;
    bool heroKnown = false;
    float hx = 0.0f, hz = 0.0f;        // the hero's place, world units
    const Missile* m = nullptr;
    size_t n = 0;
};

// One line of the log.
struct Line { char text[260]; };

class Tracker {
public:
    bool Active() const { return active_; }
    // A throw was released at `releaseT` (the same base as Snapshot::t). `heroMode` -1 when not known: the mode then starts being told at the first read.
    void Arm(double releaseT) {
        *this = Tracker();
        active_ = true;
        t0_ = releaseT;
    }
    void Stop() { active_ = false; }

    // Feeds one snapshot; the lines to say go to out (up to maxOut); returns how many. After kWindowS the trace says its summary and goes quiet.
    size_t Step(const Snapshot& s, Line* out, size_t maxOut) {
        size_t k = 0;
        if (!active_) return 0;
        const double dt = s.t - t0_;
        auto say = [&](const char* fmt, auto... args) {
            if (lines_ >= kMaxLines || k >= maxOut) return;
            ++lines_;
            int w = std::snprintf(out[k].text, sizeof out[k].text, "throw trace: +%.3f s ", dt);
            if (w < 0 || w >= (int)sizeof out[k].text) w = 0;
            std::snprintf(out[k].text + w, sizeof out[k].text - (size_t)w, fmt, args...);
            ++k;
        };
        if (s.heroModeKnown && s.heroMode != mode_) {
            if (haveMode_) {
                say("the hero's mode %u -> %u%s", mode_, s.heroMode, s.heroMode == 11 ? " (throw)" : s.heroMode == 1 ? " (neutral)" : "");
                if (s.heroMode == 11) throwStart_ = dt;
                else if (mode_ == 11 && throwStart_ >= 0.0) say("... his throw animation took %.0f ms", (dt - throwStart_) * 1000.0);
            } else {
                say("the hero's mode is %u", s.heroMode);
            }
            mode_ = s.heroMode;
            haveMode_ = true;
        }
        // 1. the potions: the ones seen before are matched by id, new ones are told
        bool seen[kMaxTracked] = {};
        auto cellsFromHero = [&](const Missile& m, float* dx, float* dz) {
            *dx = s.heroKnown ? (m.x - s.hx) * 0.5f : 0.0f;
            *dz = s.heroKnown ? (m.z - s.hz) * 0.5f : 0.0f;
            return std::sqrt(*dx * *dx + *dz * *dz);
        };
        for (size_t i = 0; i < s.n; ++i) {
            const Missile& m = s.m[i];
            if (!IsPotion(m.cls)) continue;
            int at = -1;
            for (size_t j = 0; j < nTracked_; ++j) if (tr_[j].id == m.id && tr_[j].cls == m.cls) { at = (int)j; break; }
            float dx, dz;
            const float dist = cellsFromHero(m, &dx, &dz);
            if (at < 0) {
                if (nTracked_ >= kMaxTracked) continue;
                Tracked& t = tr_[nTracked_];
                t = Tracked();
                t.id = m.id; t.cls = m.cls; t.born = dt; t.lastTold = dt; t.lastDist = dist; t.lastLife = m.life; t.firstLife = m.life;
                t.x = m.x; t.z = m.z;
                seen[nTracked_] = true;
                ++nTracked_;
                ++potions_;
                say("POTION missile #%u class %u (%s) appeared %.1f cells from the hero (dx %+.1f, dy %+.1f), life %.1f frames%s", m.id, m.cls, ClassName(m.cls), dist, dx, dz,
                    m.life, m.life < 0.0f ? " (life not read)" : "");
            } else {
                seen[at] = true;
                Tracked& t = tr_[at];
                t.x = m.x; t.z = m.z; t.lastDist = dist; t.lastLife = m.life; t.lastFlags = m.flags;
                if (dt - t.lastTold >= kEveryS) {
                    t.lastTold = dt;
                    say("potion #%u at %.1f cells (dx %+.1f, dy %+.1f), life %.1f frames left%s", m.id, dist, dx, dz, m.life, (m.flags >> 21) & 1 ? ", ended-mark set" : "");
                }
            }
        }
        // 2. the potions that are gone
        for (size_t j = 0; j < nTracked_;) {
            if (seen[j]) { ++j; continue; }
            const Tracked& t = tr_[j];
            vanishedAt_ = dt;
            vx_ = t.x; vz_ = t.z;
            say("potion #%u is GONE: it lived %.2f s, last seen %.1f cells from the hero with %.1f frames of life left (it had %.1f)", t.id, dt - t.born, t.lastDist, t.lastLife,
                t.firstLife);
            ++gone_;
            for (size_t q = j; q + 1 < nTracked_; ++q) { tr_[q] = tr_[q + 1]; seen[q] = seen[q + 1]; }
            --nTracked_;
        }
        // 3. any other missile that shows up near a potion (flying, or just vanished): its effect - the explosion's own sprite, a gas cloud ...
        for (size_t i = 0; i < s.n; ++i) {
            const Missile& m = s.m[i];
            if (IsPotion(m.cls) || ClassCounted(m.id, m.cls)) continue;
            MarkCounted(m.id, m.cls);
            float best = 1e9f;
            const char* where = "";
            for (size_t j = 0; j < nTracked_; ++j) {
                const float ex = (m.x - tr_[j].x) * 0.5f, ez = (m.z - tr_[j].z) * 0.5f;
                const float d = std::sqrt(ex * ex + ez * ez);
                if (d < best) { best = d; where = "from the potion still flying"; }
            }
            if (vanishedAt_ >= 0.0 && dt - vanishedAt_ <= kEffectWindowS) {
                const float ex = (m.x - vx_) * 0.5f, ez = (m.z - vz_) * 0.5f;
                const float d = std::sqrt(ex * ex + ez * ez);
                if (d < best) { best = d; where = "from where the potion vanished"; }
            }
            if (best <= kEffectCells) {
                ++effects_;
                say("an effect missile #%u class %u (%s) appeared %.1f cells %s", m.id, m.cls, ClassName(m.cls), best, where);
            }
        }
        if (!summary_ && dt >= kWindowS) {
            summary_ = true;
            active_ = false;
            say("over: %d potion missile(s) seen, %d vanished, %d effect missile(s) near where they vanished%s", potions_, gone_, effects_,
                potions_ == 0 ? " - the game made NO potion missile for this throw (or the client never had it)" : effects_ == 0 && gone_ > 0 ? " - NO explosion effect appeared" : "");
        }
        return k;
    }

private:
    struct Tracked {
        uint32_t id = 0, cls = 0;
        double born = 0.0, lastTold = 0.0;
        float lastDist = 0.0f, lastLife = -1.0f, firstLife = -1.0f, x = 0.0f, z = 0.0f;
        uint32_t lastFlags = 0;
    };
    bool ClassCounted(uint32_t id, uint32_t cls) const {
        for (size_t i = 0; i < nCounted_; ++i) if (counted_[i][0] == id && counted_[i][1] == cls) return true;
        return false;
    }
    void MarkCounted(uint32_t id, uint32_t cls) {
        if (nCounted_ < kCountedMax) { counted_[nCounted_][0] = id; counted_[nCounted_][1] = cls; ++nCounted_; }
    }
    static constexpr size_t kCountedMax = 64;

    bool active_ = false, summary_ = false, haveMode_ = false;
    double t0_ = 0.0, throwStart_ = -1.0, vanishedAt_ = -1.0;
    float vx_ = 0.0f, vz_ = 0.0f;
    uint32_t mode_ = 0;
    int lines_ = 0, potions_ = 0, gone_ = 0, effects_ = 0;
    Tracked tr_[kMaxTracked];
    size_t nTracked_ = 0;
    uint32_t counted_[kCountedMax][2] = {};
    size_t nCounted_ = 0;
};

}  // namespace throwtrace
