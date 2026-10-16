// skill_triggers.h - [debug] skill_triggers: what CASTS A SKILL, drawn in the headset and described in the log, so it can be tuned by eye.
// Pure part: what to draw, the reasons, the text of the 5 Hz log line. No game calls, no globals (the state is skill_triggers.cpp's).
//
// Apart from [debug] melee_boxes (vr/melee_boxes.h), which is hit by touch's alone: its own toggle, its own ini keys, its own scene, drawn
// through the same effect (D2R_DepthFog.fx Hitboxes, see-through, rimmed) with the capsules it shares: this module takes its part of the
// effect's 32 first (kShare), melee_boxes gets what is left.
//
// What is drawn, for the SHIELD BASH ([gameplay] shield_smite: a hard bash of the left shield through a monster presses the pad slot Smite
// sits in; vr/shield_smite.h decides, shield_smite.cpp feeds this):
//   violet   the shield's capsule - the one the bash tests monsters with (the attachment's centre +-0.20 m along its axis, radius 0.15), on
//            the left arm
//   green    the same capsule while a bash WOULD COUNT: a stroke on, the shield at or above [gameplay] shield_smite_min_speed, the right trigger
//            up, Smite on a pad slot, in a game and F4
//   white ball   where the shield's speed REACHED the bash speed in a bash (at the shield's centre); orange ball: where it fell below it
//   trail        the shield's centre's path as a thin tube: cyan where a bash would not count, green where it would
//   red      the monster a bash pressed Smite on, for half a second
//   yellow   the monsters near the shield (the nearest few; pale where the server's own word, when hit by touch has asked it, is "not a
//            target": not hostile, or a wall between)
// [debug] skill_mark_ms says how long the balls and the trail stay. Five times a second the log gets "skill trigger shield bash:" with the shield's
// speed (and its peak) against the bash speed, the stroke's start distance, the right trigger, the Smite slot and the pad button it would press, the
// hero's mana against Smite's cost, the nearest monster, and - if a bash would not count - why (Why).
//
// OTHER TRIGGERS join by adding a feed like BashFrame (one capsule or a few, the numbers the trigger decides on, its reasons, a line of text) and a
// Trigger value; the colours, the marks and the trail, the budget, the staleness and the module's plumbing are shared. Which exist in this tree and what
// each could show (nothing but the shield bash is implemented):
//   bow draw (vr/bow_draw.h, bow_draw_wire.cpp)       the nock zone (a 0.28 m ball 0.16 m behind the bow hand), the draw line, the pull against its 0.28 m and
//                                                     the +0.12 m, the 0.12 s hold, the cancel at 1.10 m; the slot of the selected skill it presses
//   throw gesture (vr/throw_gesture.h, _wire.cpp)     the throwing hand's path coloured by its speed band (1.4 - 12 m/s), the 0.18 m travel, the grace window,
//                                                     the release's direction as a line; reasons: too short / too slow / too fast / held too long / cancelled
//   weight throw (vr/weight_throw.h)                  the grip release of a heavy weapon that counts as a throw: the hand's speed against its threshold
//   skill wheel cast (vr/wheel_cast.h)                the picked slot, the trigger against its on / off, the left trigger's lead for the second row
//   hit by touch (vr/melee_touch.h)                   already drawn by [debug] melee_boxes (blade, monsters, marks, trail)
//   rune gestures                                     drawn by [openxr] runes_draw as the player makes them; what casts is BodyWalk's binding, not the plugin's
// It is a drawing only: nothing here feeds back into any trigger.
//
// Frames and units are melee_boxes': metres in the render world, packed by meleeboxes::Pack into the effect's world units relative to where the
// hands hang.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "melee_boxes.h"
#include "melee_marks.h"
#include "stroke_contact.h"

namespace skilltriggers {

using meleeboxes::Capsule;
using meleeboxes::Monster;
using meleeboxes::Verdict;

constexpr int kShare = 18;                // of the effect's 32 capsules, the most this module takes (it takes them first; melee_boxes gets the rest)
constexpr int kKindTrigger = 11;          // the effect's HitboxB.w for a trigger's own capsule: violet (green = meleeboxes::kBladeHot while it would count)
constexpr size_t kMonstersKept = 24;      // the scan's monsters the scene holds (the nearest)
constexpr size_t kMaxStrikes = 2;
constexpr int kNearestAlways = 3;         // the nearest monsters drawn whatever the budget
constexpr uint64_t kStruckMs = 500;
constexpr uint64_t kStaleMs = 300;        // nothing is drawn from a feed older than this
constexpr uint64_t kLogMs = 200;          // the log line: 5 Hz
constexpr uint32_t kDefaultMarkMs = 800, kMaxMarkMs = 5000;   // [debug] skill_mark_ms

enum class Trigger : int { ShieldBash = 0, Count };

// Why a trigger would not cast now: any of these. The first group stops the PRESS (the trigger's own gate and detector); kWhyNoMana and
// kWhyNotHostile are the GAME's checks of the cast it would be - the press goes out all the same and the game refuses it.
enum Why : uint32_t {
    kWhyOff = 1u << 0,          // [gameplay] shield_smite is not ticked: nothing is ever pressed (the capsule is drawn all the same)
    kWhyNotPlaying = 1u << 1,   // a menu or panel, the headset without focus, a view other than F4, the left controller not tracked
    kWhySlow = 1u << 2,         // the shield moves slower than the bash speed
    kWhyNoStroke = 1u << 3,     // fast enough, but the movement has not become a stroke (it must move far enough, straight enough, within 0.12 s)
    kWhyTrigger = 1u << 4,      // the right trigger is down (the weapon hand is attacking)
    kWhyNoSlot = 1u << 5,       // Smite is on none of the pad's ten skill slots
    kWhyTooSoon = 1u << 6,      // the last bash was less than shieldsmite::kMinGap ago
    kWhyNoHero = 1u << 7,       // the hero's unit is not known (no monsters can be looked for)
    kWhyNoMonster = 1u << 8,    // no monster near enough to touch
    kWhyNoMana = 1u << 9,       // the game's: less mana than Smite costs
    kWhyNotHostile = 1u << 10,  // the game's: the nearest monster is not an enemy, or a wall is between
};
inline const char* WhyName(uint32_t bit) {
    switch (bit) {
        case kWhyOff: return "[gameplay] shield_smite is off";
        case kWhyNotPlaying: return "not playing (menu, no focus, not F4, hand not tracked)";
        case kWhySlow: return "too slow";
        case kWhyNoStroke: return "no stroke yet";
        case kWhyTrigger: return "right trigger down";
        case kWhyNoSlot: return "no Smite slot";
        case kWhyTooSoon: return "too soon after the last bash";
        case kWhyNoHero: return "hero unit unknown";
        case kWhyNoMonster: return "no monster near";
        case kWhyNoMana: return "no mana";
        case kWhyNotHostile: return "nearest not hostile";
        default: return "?";
    }
}
constexpr size_t kWhyTextMax = 480;   // every reason at once fits
// "too slow, right trigger down" - the reasons in `why`, in bit order; "none" for 0.
inline int WhyText(uint32_t why, char* buf, size_t size) {
    if (!size) return 0;
    buf[0] = '\0';
    if (!why) return std::snprintf(buf, size, "none");
    int w = 0;
    for (uint32_t bit = 1; bit && bit <= kWhyNotHostile; bit <<= 1) {
        if (!(why & bit) || w < 0 || static_cast<size_t>(w) >= size) continue;
        w += std::snprintf(buf + w, size - static_cast<size_t>(w), "%s%s", w ? ", " : "", WhyName(bit));
    }
    return w;
}

// ---- the shield bash's feed, once per hand frame -----------------------------------------------------------------------------------
struct BashIn {
    bool have = false;               // the shield's capsule is known this frame (a shield on the arm, the skeleton posed it)
    stroke::Blade blade;             // metres, in melee_touch's frame: the very capsule shield_smite tests monsters with
    bool inStroke = false;           // the (ungated) observer engine says a stroke is on
    float speed = 0.0f;              // the observer's speed of this frame's step, m/s - the value the detector's gates are on
    float minSpeed = 0.8f;           // the bash speed ([gameplay] shield_smite_min_speed), m/s
    float beginSpeed = 1.2f;         // the speed that may BEGIN a stroke, m/s
    float startCm = 8.0f;            // the distance a movement must cover within 0.12 s to become a stroke ([gameplay] shield_smite_stroke_cm), cm
    bool counting = false;           // the detector's own engine swept something this step: a bash would count right now (gate open)
    float sweepSpeed = 0.0f;         // the speed of the last sweep the detector made, m/s ...
    uint64_t sweepMs = 0;            // ... and when (0 = none yet)
    float rightTrigger = 0.0f;       // 0..1
    int slot = -1;                   // the pad slot Smite sits in, -1 none
    uint16_t padButtons = 0;         // the pad button(s) that slot is (XInput bits) and whether it needs the left trigger - for the line
    bool padLt = false;
    float mana = -1.0f, manaNeed = 0.0f;   // the hero's mana now (< 0 unknown) and what Smite costs
    uint32_t why = 0;                // Why bits
    // The monster nearest the shield (in the scan): its class, the hero's distance to it, the gap between the shield's capsule and its (less the
    // two radii; < 0 is a touch now) and what the server's checks said of it.
    bool haveNear = false;
    uint32_t nearCls = 0;
    float nearHeroM = 0.0f, nearGapM = 0.0f;
    Verdict nearVerdict = Verdict::Unknown;
};
struct BashFrame {
    uint64_t nowMs = 0;
    float upm = 4.0f;                // world units per metre of the frame the capsules are in
    float floorY = 0.0f;
    uint32_t markMs = kDefaultMarkMs;   // the module fills it
    BashIn bash;
    const Monster* monsters = nullptr;  // the scan: the monsters near the hero
    size_t count = 0;
};

// ---- the scene ---------------------------------------------------------------------------------------------------------------------
class Scene {
public:
    void SetBash(const BashFrame& f) {
        at_ = f.nowMs;
        upm_ = f.upm;
        floorY_ = f.floorY;
        markMs_ = std::min(f.markMs, kMaxMarkMs);
        const bool was = bash_.have;
        bash_ = f.bash;
        if (bash_.have && markMs_) {
            if (!was) track_.Clear();
            track_.Feed(f.nowMs, Centre(bash_.blade), bash_.counting);   // the marks and the trail follow the shield's CENTRE
        } else {
            track_.Clear();
        }
        if (bash_.have) peak_.Note(bash_.speed);
        count_ = std::min(f.count, monsters_.size());
        if (count_) std::memcpy(monsters_.data(), f.monsters, count_ * sizeof(Monster));
    }

    // A bash pressed Smite on monster `id`: its capsule flashes red for half a second (copied now, so it outlives the monster's death).
    void StruckBody(uint32_t id, meleeboxes::Kind how, uint64_t nowMs, const stroke::Body& body) {
        Strike s;
        s.id = id;
        s.kind = how;
        s.at = nowMs;
        s.body = body;
        for (Strike& t : strikes_)
            if (t.at && t.id == id) { t = s; return; }
        strikes_[nextStrike_++ % strikes_.size()] = s;
    }

    // The capsules to draw at nowMs, at most `max` (and kShare): the trigger's own first, the nearest monsters, the flash, the trail, the marks on top.
    int Capsules(uint64_t nowMs, Capsule* out, int max) const {
        if (!Fresh(nowMs) || max <= 0) return 0;
        const int cap = std::min(max, kShare);
        int n = 0;
        out[n++] = {bash_.blade.base, bash_.blade.tip, bash_.blade.radius, bash_.counting ? meleeboxes::kBladeHot : static_cast<meleeboxes::Kind>(kKindTrigger)};
        int struck = 0;
        Capsule hit[kMaxStrikes];
        for (const Strike& t : strikes_)
            if (Active(t, nowMs) && struck < static_cast<int>(kMaxStrikes)) hit[struck++] = {t.body.bottom, t.body.top, t.body.radius, t.kind};
        std::array<uint8_t, kMonstersKept> order{};
        size_t m = 0;
        for (size_t i = 0; i < count_; ++i) {
            bool isStruck = false;
            for (const Strike& t : strikes_) isStruck = isStruck || (Active(t, nowMs) && t.id == monsters_[i].id);
            if (!isStruck) order[m++] = static_cast<uint8_t>(i);
        }
        std::sort(order.begin(), order.begin() + m, [&](uint8_t x, uint8_t y) { return monsters_[x].d2 < monsters_[y].d2; });
        Capsule marks[meleeboxes::kMarkSlots];
        int nMarks = 0;
        Capsule trail[meleeboxes::kTrailSegments];
        int nTrail = 0;
        if (markMs_) {
            // what is left after the capsule, the flash and the nearest monsters is the marks' and the trail's: the marks first, then the trail
            const int room = std::max(0, cap - n - struck - static_cast<int>(std::min<size_t>(m, kNearestAlways)));
            meleeboxes::Mark mk[meleeboxes::kMarkSlots];
            const size_t k = track_.Marks(nowMs, markMs_, mk, meleeboxes::kMarkSlots);
            for (size_t j = 0; j < k && nMarks < room; ++j) {
                const stroke::Vec up{0.0f, meleeboxes::kMarkHalfM, 0.0f};
                marks[nMarks++] = {mk[j].at - up, mk[j].at + up, meleeboxes::kMarkRadiusM, mk[j].reached ? meleeboxes::kMarkReached : meleeboxes::kMarkSlowed};
            }
            const int each = std::min(meleeboxes::kTrailSegments, room - nMarks);
            if (each > 0) {
                meleeboxes::TrailSegment seg[meleeboxes::kTrailSegments];
                const size_t t = track_.Trail(nowMs, markMs_, seg, static_cast<size_t>(each));
                for (size_t j = 0; j < t; ++j)
                    trail[nTrail++] = {seg[j].a, seg[j].b, meleeboxes::kTrailRadiusM, seg[j].hot ? meleeboxes::kTrailHot : meleeboxes::kTrailCold};
            }
        }
        const int reserved = struck + nMarks + nTrail;
        for (size_t k = 0; k < m && n < cap - reserved; ++k) {
            const Monster& mo = monsters_[order[k]];
            out[n++] = {mo.body.bottom, mo.body.top, mo.body.radius, mo.verdict == Verdict::Refused ? meleeboxes::kRefused : meleeboxes::kMonster};
        }
        for (int k = 0; k < struck && n < cap; ++k) out[n++] = hit[k];
        for (int k = 0; k < nTrail && n < cap; ++k) out[n++] = trail[k];
        for (int k = 0; k < nMarks && n < cap; ++k) out[n++] = marks[k];   // on top
        return n;
    }

    // The shield bash's line for the log (5 Hz). False (nothing written) when the shield has not been fed or has nothing to show.
    bool DescribeBash(char* buf, size_t size, uint64_t nowMs) {
        if (!size || !Fresh(nowMs)) return false;
        const BashIn& s = bash_;
        const float peak = peak_.Take(s.speed);
        const double ago = s.sweepMs && nowMs >= s.sweepMs ? static_cast<double>(nowMs - s.sweepMs) / 1000.0 : -1.0;
        char why[kWhyTextMax];
        WhyText(s.why, why, sizeof why);
        int w = std::snprintf(buf, size, "skill trigger shield bash: %.2f m/s (peak %.2f; bash speed %.2f, begins at %.2f, start %.0f cm) %s,", s.speed, peak, s.minSpeed, s.beginSpeed,
                              s.startCm, s.counting ? "COUNTS" : s.inStroke ? "below" : "no stroke");
        if (w > 0 && static_cast<size_t>(w) < size) {
            if (ago >= 0.0 && ago < 60.0) w += std::snprintf(buf + w, size - static_cast<size_t>(w), " sweep %.2f m/s %.1f s ago;", s.sweepSpeed, ago);
            else w += std::snprintf(buf + w, size - static_cast<size_t>(w), " no sweep yet;");
        }
        if (w > 0 && static_cast<size_t>(w) < size) {
            char mana[40], pad[40];
            if (s.mana >= 0.0f) std::snprintf(mana, sizeof mana, "%.1f (Smite needs %.1f)", s.mana, s.manaNeed);
            else std::snprintf(mana, sizeof mana, "not read (Smite needs %.1f)", s.manaNeed);
            if (s.slot >= 0) std::snprintf(pad, sizeof pad, "slot %d = pad 0x%04X%s", s.slot, s.padButtons, s.padLt ? " + LT" : "");
            else std::snprintf(pad, sizeof pad, "none");
            w += std::snprintf(buf + w, size - static_cast<size_t>(w), " right trigger %.2f, Smite %s, mana %s;", s.rightTrigger, pad, mana);
        }
        if (w > 0 && static_cast<size_t>(w) < size) {
            if (s.haveNear)
                w += std::snprintf(buf + w, size - static_cast<size_t>(w), " nearest monster: cls %u %s, %.1f m from the hero, gap %+.2f m (< 0 touching);", s.nearCls,
                                   s.nearVerdict == Verdict::Refused ? "refused" : s.nearVerdict == Verdict::Hostile ? "hostile" : "hostility unknown", s.nearHeroM, s.nearGapM);
            else w += std::snprintf(buf + w, size - static_cast<size_t>(w), " no monster in the scan;");
        }
        if (w > 0 && static_cast<size_t>(w) < size)
            w += std::snprintf(buf + w, size - static_cast<size_t>(w), s.why ? " a bash would NOT count: %s" : " a bash would count (%s)", why);
        return w > 0;
    }

    bool Have() const { return bash_.have; }
    float Upm() const { return upm_; }

private:
    struct Strike { uint32_t id = 0; meleeboxes::Kind kind = meleeboxes::kStruckKey; uint64_t at = 0; stroke::Body body; };
    static bool Active(const Strike& t, uint64_t nowMs) { return t.at && nowMs >= t.at && nowMs - t.at < kStruckMs; }
    static stroke::Vec Centre(const stroke::Blade& b) { return (b.base + b.tip) * 0.5f; }
    bool Fresh(uint64_t nowMs) const { return bash_.have && at_ && nowMs >= at_ && nowMs - at_ <= kStaleMs; }
    uint64_t at_ = 0;
    float upm_ = 4.0f, floorY_ = 0.0f;
    uint32_t markMs_ = kDefaultMarkMs;
    BashIn bash_;
    meleeboxes::Track track_;
    meleeboxes::PeakTracker peak_;
    std::array<Monster, kMonstersKept> monsters_{};
    size_t count_ = 0;
    std::array<Strike, kMaxStrikes> strikes_{};
    size_t nextStrike_ = 0;
};

// ---- the module (skill_triggers.cpp): one scene behind a lock, off unless asked for -----------------------------------------------------
// Off (the default): UpdateBash and StruckBody return at once, Take gives 0, nothing is kept.
using LogSink = void (*)(const char* line);
void SetLogSink(LogSink sink);
// [debug] skill_triggers, from the ini (every read).
void SetWanted(bool on);
bool Wanted();
// [debug] skill_mark_ms, from the ini (every read): how long the speed marks and the trail stay, ms; 0 = none (clamped to kMaxMarkMs).
void SetMarkMs(uint32_t ms);
// shield_smite's timer, once per hand frame, only while Wanted(): the shield's capsule, numbers and scan; every 200 ms its line goes to the log sink.
void UpdateBash(const BashFrame& f);
// shield_smite, when a bash pressed Smite on monster `id`: its capsule flashes red (given here: the scan is the feeder's).
void StruckBody(uint32_t id, meleeboxes::Kind how, uint64_t nowMs, const stroke::Body& body);
// The render thread, for the effect: this frame's capsules as HitboxA / HitboxB (meleeboxes::Pack), at most maxOut (and kShare); 0 when there is
// nothing fresh to draw.
int Take(uint64_t nowMs, const float eyeWorld[3], float (*a)[4], float (*b)[4], int maxOut = kShare);

}  // namespace skilltriggers
