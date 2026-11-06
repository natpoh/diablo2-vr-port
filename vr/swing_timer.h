#pragma once
// swing_timer.h - [hud] swing_timer: the hero's attack as the game times it, and what came of it, shown while he fights (Serg, 2026-11-05: "круг
// появляется только после замаха на врага ... потом тает"; "циферки можно показывать в центре круга"; "если выбрана полоска, то она вверху, а
// циферки над ней появляются и тают"). The plan: docs/notes/swing_timer.md.
//
// What it shows. No ring = go: nothing stands in the way of an attack. An attack the game started fills a ring around the middle of the view (or
// a bar at its top) from the start to the moment the game lets the next attack in, in the colour of the game's attack gate 0x42D840
// (vr/quick_blow.h): more than 5 of the server's 40 ms frames left - red, the gate refuses a new attack (a touch is lost); 2-5 left - amber,
// the gate answers "not yet" and its caller keeps the attack for the animation's end; 1 or none - green, in now. Green stands a moment, then the
// ring melts away. Inside the ring (or rising over the bar) the attack's outcome: the life it took (a crit gold and larger, several monsters of
// one swing summed, "x3"), or the roll's "miss" / "blocked" / "dodged" / "avoided"; a touch while red says "too soon" - no attack, no roll.
//
// Where the times come from. Single player or a game we host: the server's scheduler of a mode's frame events 0x491240 (vr/move_attack_wire.cpp)
// at our hero's attack start - its length in the server's frames, exact; with [gameplay] quick_blow_faster the green comes once its least wait
// passed. Joined to another's game (no server here): our hero's client mode, the attack's length measured from the last one (as
// meleedirectclient::AttackClock), the first one guessed. The outcomes: [debug] hit_outcome's two read-only observers (vr/hit_outcome.h), our
// hero's blows only - on our server, so a joined player sees the timer and "too soon" but no numbers.
//
// [gameplay] swing_pace_joined (its own tick, Serg 2026-11-05: "расчёт на клиенте, регистрация на сервере"): joined to another's game, a touch
// the host's gate would refuse is not sent, and one in its last moments is held and sent when the client's clock turns green (PaceAt). That
// one changes what is sent; the rest only shows. Pure: no game, no Windows, no clock (the caller passes the time); the test is
// cleanroom/tests/swing_timer_test.cpp, the picture vr/swing_timer_paint.cpp, the wiring vr/swing_timer_wire.cpp.

#include <algorithm>
#include <cstdint>

namespace swingtimer {

enum class Style : uint8_t { Ring = 0, Bar = 1 };   // [hud] swing_timer_style
enum class Zone : uint8_t { Red = 0, Amber = 1, Green = 2 };
enum class Text : uint8_t { None = 0, Damage, Miss, Blocked, Dodged, Avoided, TooSoon };

constexpr uint32_t kFrameMs = 40;      // the server's frame
constexpr int32_t kAmberFrames = 5;    // the gate: 2-5 frames left - "not yet", kept; more - refused; 1 or none - in
constexpr uint32_t kDefaultClientMs = 600, kMinClientMs = 150, kMaxClientMs = 2000;   // the client's guess until it measured one, and what is plausible
constexpr uint64_t kServerFreshMs = 3000;   // a server note this recent: our server is here and its times win over the client's
constexpr uint64_t kGreenHoldMs = 400, kFadeMs = 300;   // green stands this long, then melts away in this long
constexpr uint64_t kLabelMinMs = 700;  // the ring's number stays at least this long (a blow with no timer: Direct hit's cleave extras)
constexpr uint64_t kRiseMs = 1200, kRiseFadeFromMs = 700;   // the bar's number rises for this long, fading from here on
constexpr uint64_t kTooSoonMs = 600, kTooSoonFadeFromMs = 400;
constexpr uint64_t kMergeMs = 150;     // outcomes this close with no attack of ours around them belong together (one swing through several)
constexpr int kSteps = 48, kAlphaSteps = 8, kRiseSteps = 24;   // the picture changes in these steps only (each step is a new picture to paint)
// The joined client's pace: a touch this close to the client's green is held and sent then (the gate's 5 frames of amber and 100 ms more:
// the client's clock may run behind the host's by half the ping - a touch the host would take a little sooner is late, never lost); sooner -
// dropped, the host refuses it anyway. A held touch not sent by kHoldMaxMs goes.
constexpr uint64_t kHoldMs = 300, kHoldMaxMs = 500;
enum class Pace : uint8_t { Send, Hold, Drop };

// The modes a weapon attack plays in: attack 1 / 2, throw, kick, sequence (Zeal, Fury ...). Not the cast or the skill modes.
inline bool TimedMode(uint32_t mode) { return mode == 7 || mode == 8 || mode == 11 || mode == 12 || mode == 18; }

// The roll's answer (0x44B720, vr/hit_outcome.h's bits).
constexpr uint16_t kRollHit = 0x0001, kRollBlockShield = 0x0010, kRollDodge = 0x0080, kRollAvoid = 0x0100, kRollBlockWeapon = 0x8000;
inline Text RollText(uint16_t flags) {
    if (flags & kRollHit) return Text::None;   // the damage tells it
    if (flags & (kRollBlockShield | kRollBlockWeapon)) return Text::Blocked;
    if (flags & kRollDodge) return Text::Dodged;
    if (flags & kRollAvoid) return Text::Avoided;
    return Text::Miss;   // (0 is also out of reach or not rolled - D2MOO; a label cannot tell them apart)
}
// Of one swing's rolls the one shown: a landed blow over everything, then a block over the dodges over a miss.
inline int Rank(Text t) {
    switch (t) {
    case Text::Damage: return 5;
    case Text::Blocked: return 4;
    case Text::Dodged: return 3;
    case Text::Avoided: return 2;
    case Text::Miss: return 1;
    default: return 0;
    }
}
// The life a blow took (stat 6, 1/256) in whole points, as the game shows life: any loss at least 1.
inline int32_t Whole(int32_t lost256) { return lost256 <= 0 ? 0 : std::max<int32_t>(1, (lost256 + 128) >> 8); }

// One attack's times, from its start.
struct Attack {
    bool valid = false, server = false;
    uint64_t startMs = 0;
    uint32_t amberMs = 0, greenMs = 0;
};
// From the server: `frames` from the start to the animation's end, the gate lets the next attack in at `greenFrame` (frames - 1 by the game's
// own rule; sooner with quick_blow_faster). Amber is the gate's last 5 frames before the end, never after green.
inline Attack FromFrames(uint64_t startMs, int32_t frames, int32_t greenFrame) {
    Attack a;
    if (frames < 1 || frames > 1000) return a;
    const int32_t green = std::clamp(greenFrame, 0, frames - 1);
    const int32_t amber = std::min(green, std::max<int32_t>(0, frames - kAmberFrames));
    a.valid = a.server = true;
    a.startMs = startMs;
    a.greenMs = static_cast<uint32_t>(green) * kFrameMs;
    a.amberMs = static_cast<uint32_t>(amber) * kFrameMs;
    return a;
}
// From the client: the attack takes `lengthMs` (measured on the last one); the same zones counted back from its end.
inline Attack FromClient(uint64_t startMs, uint32_t lengthMs) {
    Attack a;
    a.valid = true;
    a.startMs = startMs;
    a.greenMs = lengthMs > kFrameMs ? lengthMs - kFrameMs : 0;
    const uint32_t lead = static_cast<uint32_t>(kAmberFrames) * kFrameMs;
    a.amberMs = std::min(a.greenMs, lengthMs > lead ? lengthMs - lead : 0u);
    return a;
}
inline Zone ZoneAt(const Attack& a, uint64_t now) {
    if (!a.valid) return Zone::Green;
    const uint64_t t = now > a.startMs ? now - a.startMs : 0;
    return t >= a.greenMs ? Zone::Green : t >= a.amberMs ? Zone::Amber : Zone::Red;
}
// The ring's fill: full exactly when it turns green.
inline float Progress(const Attack& a, uint64_t now) {
    if (!a.valid || a.greenMs == 0) return 1.0f;
    const uint64_t t = now > a.startMs ? now - a.startMs : 0;
    return t >= a.greenMs ? 1.0f : static_cast<float>(t) / static_cast<float>(a.greenMs);
}
inline uint64_t HoldEnd(const Attack& a) { return a.startMs + a.greenMs + kGreenHoldMs; }
// 1 until `holdEnd`, then down to 0 in kFadeMs.
inline float FadeAlpha(uint64_t holdEnd, uint64_t now, uint64_t fadeMs = kFadeMs) {
    if (now <= holdEnd) return 1.0f;
    const uint64_t d = now - holdEnd;
    return d >= fadeMs ? 0.0f : 1.0f - static_cast<float>(d) / static_cast<float>(fadeMs);
}
inline int AlphaStep(float a) {
    if (a <= 0.0f) return 0;
    const int s = static_cast<int>(a * kAlphaSteps + 0.999f);
    return std::clamp(s, 1, kAlphaSteps);
}

// The hero's client mode, sampled: an attack starts when he enters a timed mode, its length is measured when he leaves it.
class ClientClock {
public:
    // true: an attack started at this sample; *lengthMs the length it is expected to take.
    bool Sample(bool known, uint32_t mode, uint64_t now, uint32_t* lengthMs) {
        if (!known) return false;
        const bool in = TimedMode(mode);
        bool started = false;
        if (in && !in_) {
            startAt_ = now;
            started = true;
            if (lengthMs) *lengthMs = ms_;
        }
        if (!in && in_ && startAt_ && now > startAt_) {
            const uint64_t d = now - startAt_;
            if (d >= kMinClientMs && d <= kMaxClientMs) {
                ms_ = static_cast<uint32_t>(d);
                measured_ = true;
            }
        }
        in_ = in;
        return started;
    }
    uint32_t Ms() const { return ms_; }
    bool Measured() const { return measured_; }

private:
    bool in_ = false, measured_ = false;
    uint64_t startAt_ = 0;
    uint32_t ms_ = kDefaultClientMs;
};

// The outcome of one swing.
struct Label {
    Text text = Text::None;
    int32_t value = 0;      // Damage: the life taken, whole points, summed over the swing's monsters
    uint8_t count = 0;      // Damage: blows that landed in the swing
    bool crit = false;
    uint64_t atMs = 0;      // its first outcome
    uint64_t attackStart = 0;   // the attack it belongs to (0: none of ours around it)
};

// What the picture shows - compared to know when to paint a new one, so it moves in steps.
struct Look {
    bool visible = false;
    Style style = Style::Ring;
    bool timer = false;
    int step = 0;           // the fill, 0..kSteps
    Zone zone = Zone::Green;
    int timerAlpha = 0;     // 0..kAlphaSteps
    Text text = Text::None;
    int32_t value = 0;
    uint8_t count = 0;
    bool crit = false;
    int textAlpha = 0;      // 0..kAlphaSteps
    int rise = 0;           // the bar's number, 0..kRiseSteps up
    bool operator==(const Look& o) const {
        return visible == o.visible && style == o.style && timer == o.timer && step == o.step && zone == o.zone && timerAlpha == o.timerAlpha &&
               text == o.text && value == o.value && count == o.count && crit == o.crit && textAlpha == o.textAlpha && rise == o.rise;
    }
    bool operator!=(const Look& o) const { return !(*this == o); }
};

class State {
public:
    // Our server: our hero's attack started now (vr/move_attack_wire.cpp).
    void ServerStart(uint64_t now, int32_t frames, int32_t greenFrame) {
        const Attack a = FromFrames(now, frames, greenFrame);
        if (!a.valid) return;
        attack_ = a;
        serverAt_ = now;
    }
    // The hero's client mode (every ~10 ms): the start where no server of ours told it.
    void ClientMode(bool known, uint32_t mode, uint64_t now) {
        uint32_t len = 0;
        if (!clock_.Sample(known, mode, now, &len)) return;
        if (serverAt_ && now >= serverAt_ && now - serverAt_ < kServerFreshMs) return;   // our server times it
        attack_ = FromClient(now, len);
    }
    // A touch the plugin asked the game to attack for (VR): true when the gate would refuse it now - "too soon".
    bool Touch(uint64_t now) {
        if (!attack_.valid || ZoneAt(attack_, now) != Zone::Red) return false;
        tooSoonAt_ = now;
        return true;
    }
    // The roll of a blow of ours (0x44B720's answer).
    void Roll(uint16_t flags, uint64_t now) {
        const Text t = RollText(flags);
        if (t == Text::None) return;
        Label& l = Current(now);
        if (Rank(t) > Rank(l.text)) l.text = t;
    }
    // A blow of ours that landed: the life it took (1/256), crit or not.
    void Damage(int32_t lost256, bool crit, uint64_t now) {
        Label& l = Current(now);
        if (l.text != Text::Damage) {
            l.text = Text::Damage;
            l.value = 0;
            l.count = 0;
            l.crit = false;
        }
        l.value += Whole(lost256);
        if (l.count < 255) ++l.count;
        l.crit = l.crit || crit;
    }

    Look Frame(uint64_t now, Style style, bool numbers) const {
        Look k;
        k.style = style;
        if (attack_.valid) {
            const float a = FadeAlpha(HoldEnd(attack_), now);
            if (a > 0.0f) {
                k.timer = true;
                k.step = static_cast<int>(Progress(attack_, now) * kSteps + 0.5f);
                k.zone = ZoneAt(attack_, now);
                k.timerAlpha = AlphaStep(a);
            }
        }
        const bool soon = tooSoonAt_ && now >= tooSoonAt_ && now - tooSoonAt_ < kTooSoonMs && tooSoonAt_ >= label_.atMs;
        if (soon) {
            k.text = Text::TooSoon;
            k.textAlpha = AlphaStep(FadeAlpha(tooSoonAt_ + kTooSoonFadeFromMs, now, kTooSoonMs - kTooSoonFadeFromMs));
            if (style == Style::Bar) k.rise = Rise(tooSoonAt_, now);
        } else if (numbers && label_.text != Text::None && now >= label_.atMs) {
            float a = 0.0f;
            if (style == Style::Ring) {
                uint64_t hold = label_.atMs + kLabelMinMs;
                if (label_.attackStart && attack_.valid && label_.attackStart == attack_.startMs) hold = std::max(hold, HoldEnd(attack_));
                a = FadeAlpha(hold, now);
            } else {
                a = FadeAlpha(label_.atMs + kRiseFadeFromMs, now, kRiseMs - kRiseFadeFromMs);
                k.rise = Rise(label_.atMs, now);
            }
            if (a > 0.0f) {
                k.text = label_.text;
                k.value = label_.value;
                k.count = label_.count;
                k.crit = label_.crit;
                k.textAlpha = AlphaStep(a);
            }
        }
        if (k.textAlpha == 0) {
            k.text = Text::None;
            k.value = 0;
            k.count = 0;
            k.crit = false;
            k.rise = 0;
        }
        k.visible = k.timer || k.textAlpha > 0;
        if (!k.visible) k = Look{};   // (one picture for "nothing", whatever the style)
        return k;
    }

    // [gameplay] swing_pace_joined: what to do with a touch now. Only on the client's own clock, once it measured an attack (its first guess
    // decides nothing); a server's times mean our server's gate is here and decides itself.
    Pace PaceAt(uint64_t now) const {
        if (!attack_.valid || attack_.server || !clock_.Measured()) return Pace::Send;
        const uint64_t green = attack_.startMs + attack_.greenMs;
        if (now >= green) return Pace::Send;
        return green - now <= kHoldMs ? Pace::Hold : Pace::Drop;
    }

    const Attack& Current() const { return attack_; }
    const Label& LastLabel() const { return label_; }
    void Reset() { *this = State{}; }

private:
    // The label this outcome belongs to: the running attack's, or the last one when it came a moment ago (one swing through several
    // monsters with no attack of ours around it); else a new one.
    Label& Current(uint64_t now) {
        const bool running = attack_.valid && now >= attack_.startMs && now <= HoldEnd(attack_);
        if (running && label_.attackStart == attack_.startMs && label_.text != Text::None) return label_;
        if (!running && label_.text != Text::None && now >= label_.atMs && now - label_.atMs < kMergeMs) return label_;
        label_ = Label{};
        label_.atMs = now;
        label_.attackStart = running ? attack_.startMs : 0;
        return label_;
    }
    static int Rise(uint64_t from, uint64_t now) {
        const uint64_t d = now > from ? now - from : 0;
        return d >= kRiseMs ? kRiseSteps : static_cast<int>(d * kRiseSteps / kRiseMs);
    }

    Attack attack_;
    ClientClock clock_;
    Label label_;
    uint64_t serverAt_ = 0, tooSoonAt_ = 0;
};

}  // namespace swingtimer
