// [render] dim_own_effects: the hero's own effects (Bone Armor ...) drawn fainter - the pure part (vr/own_effects.h) and the table made from the game's own tables
// (vr/own_effects_table.h, tools/gen_own_effects.py).
//
//   own_effects_test
//
// Checks: how an overlay's name becomes a key (any case, a path, ".json", the HD files' underscores); the table is sorted and well formed and knows the overlays of Bone Armor, the
// other armors, Energy Shield and the aura rings, and does not know a hit or a skill's cast; the opacity of each group and what it is when the option is off; what is done to one
// effect each frame (nothing before the effect is made, the fade, 0 % = the game's own switch, an effect that cannot fade, giving it all back when the option is unticked or the
// view is not F4, an effect the game's own fade set back to 1); a SOLID effect (Bone Armor's opaque bones, the table's mark): not drawn under 100 %, switched off again when the
// game switches it on; reading an overlay instance, its definition's name and the effect's switch flag out of a fake game memory (and refusing a wrong one); the set of tracked
// effects and when an entity is let go; the hero's keys.

#include "../../vr/own_effects.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_fails = 0;
int g_checks = 0;

#define EXPECT(c)                                              \
    do {                                                       \
        ++g_checks;                                            \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
        }                                                      \
    } while (0)

using namespace owneffects;

// A fake game memory: one flat block at kBase.
constexpr uintptr_t kBase = 0x100000;
struct Mem {
    std::vector<uint8_t> b = std::vector<uint8_t>(0x10000, 0);
    bool rd(uintptr_t at, void* out, size_t n) const {
        if (at < kBase || at + n > kBase + b.size()) return false;
        std::memcpy(out, b.data() + (at - kBase), n);
        return true;
    }
    template <class T> void put(uintptr_t at, T v) { std::memcpy(b.data() + (at - kBase), &v, sizeof v); }
    void str(uintptr_t at, const char* s) { std::memcpy(b.data() + (at - kBase), s, std::strlen(s) + 1); }
};

void Names() {
    char k[96];
    EXPECT(Key("bonearmor_front", k, sizeof k) == 14 && std::strcmp(k, "bonearmorfront") == 0);
    EXPECT(Key("BoneArmor_Front", k, sizeof k) == 14 && std::strcmp(k, "bonearmorfront") == 0);
    EXPECT(Key("data/hd/overlays/necro/bonearmor_front.json", k, sizeof k) == 14 && std::strcmp(k, "bonearmorfront") == 0);
    EXPECT(Key("data\\hd\\overlays\\necro\\BoneArmor_Front.JSON", k, sizeof k) == 14 && std::strcmp(k, "bonearmorfront") == 0);
    EXPECT(Key("cyclonearmor_1front", k, sizeof k) == 18 && std::strcmp(k, "cyclonearmor1front") == 0);
    EXPECT(Key("", k, sizeof k) == 0 && k[0] == 0);
    EXPECT(Key(nullptr, k, sizeof k) == 0);
    EXPECT(Key("a.json", k, 2) == 1 && std::strcmp(k, "a") == 0);   // a small buffer cuts, never overruns
    EXPECT(Key("abcdef", k, 4) == 3 && std::strcmp(k, "abc") == 0);
    EXPECT(Key("x", nullptr, 4) == 0);
}

void Table() {
    // sorted strictly (the binary search needs it), lowercase letters and digits, group 1 or 2
    EXPECT(table::kCount > 100);
    bool sorted = true, clean = true, groups = true;
    for (unsigned i = 0; i < table::kCount; ++i) {
        const char* n = table::kEntries[i].name;
        if (i && std::strcmp(table::kEntries[i - 1].name, n) >= 0) sorted = false;
        if (!*n) clean = false;
        for (const char* p = n; *p; ++p)
            if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))) clean = false;
        if (table::kEntries[i].group != 1 && table::kEntries[i].group != 2) groups = false;
    }
    EXPECT(sorted);
    EXPECT(clean);
    EXPECT(groups);
    // every entry is found by its own name
    bool found = true;
    for (unsigned i = 0; i < table::kCount; ++i)
        if (GroupOfKey(table::kEntries[i].name) == Group::None) found = false;
    EXPECT(found);
    // the player's case: Bone Armor and its casting flash, however the name comes
    EXPECT(GroupOf("bonearmor_front") == Group::Body);
    EXPECT(GroupOf("bonearmor_back") == Group::Body);
    EXPECT(GroupOf("bonearmor_cast") == Group::Body);
    EXPECT(GroupOf("data/hd/overlays/necro/bonearmor_front.json") == Group::Body);
    // the other armors, Energy Shield, Cyclone Armor (the HD spelling too), Blade Shield, Fade
    EXPECT(GroupOf("frozenarmor") == Group::Body);
    EXPECT(GroupOf("shiverarmor") == Group::Body);
    EXPECT(GroupOf("chillarmor") == Group::Body);
    EXPECT(GroupOf("energyshield") == Group::Body);
    EXPECT(GroupOf("cyclonearmor1front") == Group::Body);
    EXPECT(GroupOf("cyclonearmor_1front") == Group::Body);
    EXPECT(GroupOf("bladeshield") == Group::Body);
    EXPECT(GroupOf("fade") == Group::Body);
    // the aura rings at the feet
    EXPECT(GroupOf("aura_might_front") == Group::Aura);
    EXPECT(GroupOf("aura_might_back") == Group::Aura);
    EXPECT(GroupOf("aura_holyfire_front") == Group::Aura);
    EXPECT(GroupOf("meditationfront") == Group::Aura);
    EXPECT(GroupOf("convictionback") == Group::Aura);
    // not a state's overlay: a hit, a skill's cast, an unknown name
    EXPECT(GroupOf("hit_thorns") == Group::None);
    EXPECT(GroupOf("levelup") == Group::None);
    EXPECT(GroupOf("") == Group::None);
    EXPECT(GroupOf(nullptr) == Group::None);
    EXPECT(GroupOf("null") == Group::None);
    // the solid mark (from the HD particle files): Bone Armor's front (the bones), Blade Shield, Energy Shield; not its empty back or its cast flash, not the ice armors (ribbons
    // and billboards, which take the Fade), not an aura ring; only 0 / 1, only on body effects
    EXPECT(SolidOf("bonearmor_front") && SolidOf("data/hd/overlays/necro/bonearmor_front.json") && SolidOf("BoneArmor_Front"));
    EXPECT(SolidOf("bladeshield") && SolidOf("energyshield"));
    EXPECT(!SolidOf("bonearmor_back") && !SolidOf("bonearmor_cast"));
    EXPECT(!SolidOf("frozenarmor") && !SolidOf("shiverarmor") && !SolidOf("chillarmor") && !SolidOf("fade"));
    EXPECT(!SolidOf("aura_might_front") && !SolidOf("hit_thorns") && !SolidOf("") && !SolidOf(nullptr));
    bool marks = true;
    unsigned solids = 0;
    for (unsigned i = 0; i < table::kCount; ++i) {
        if (table::kEntries[i].solid > 1 || (table::kEntries[i].solid && table::kEntries[i].group != 1)) marks = false;
        solids += table::kEntries[i].solid;
    }
    EXPECT(marks);
    EXPECT(solids >= 3 && solids < table::kCount / 4);   // a few, not most
}

void Numbers() {
    Settings s;
    EXPECT(!s.on);
    EXPECT(OpacityFor(Group::Body, s) == 1.0f && OpacityFor(Group::Aura, s) == 1.0f);   // off: as the game draws them
    s.on = true;
    s.body = 0.3f;
    s.aura = 0.6f;
    EXPECT(OpacityFor(Group::Body, s) == 0.3f);
    EXPECT(OpacityFor(Group::Aura, s) == 0.6f);
    EXPECT(OpacityFor(Group::None, s) == 1.0f);
    EXPECT(ClampPct(-5.0f) == 0.0f && ClampPct(150.0f) == 100.0f && ClampPct(42.0f) == 42.0f);
    EXPECT(ClampPct(NAN) == 100.0f);   // a broken value dims nothing
}

// One effect over a few frames, the way the tick runs it: the effect instance's address, the game's setter that takes the Fade (or refuses it), the visibility switch.
struct Fx {
    State st;
    float fade = 1.0f;          // the effect's Fade as the game would have it
    bool visible = true;
    int sets = 0, shows = 0;    // calls of the Fade setter / of the visibility switch
    uintptr_t effect = 0x5000;
    bool accepts = true;        // the effect has a parameter "Fade"
    bool solid = false;         // the table's mark (Bone Armor)
    bool readable = true;       // the component's visibility wish can be read
    void Frame(Group g, const Settings& s, bool f4, bool made = true, bool ready = true) {
        const uintptr_t e = made ? effect : 0;
        const Plan p = Decide(g, s, f4, e, ready, st, solid, readable ? (visible ? 1 : 0) : -1);
        bool ok = false;
        if (p.visible >= 0) { visible = p.visible != 0; ++shows; }
        if (p.setFade) { ++sets; ok = accepts; if (ok) fade = p.fade; }
        Apply(p, e, ok, &st);
    }
};

void Decisions() {
    Settings s;
    s.on = true;
    s.body = 0.3f;
    s.aura = 0.6f;
    {   // the effect is not made yet: nothing, then the Fade once it is
        Fx f;
        f.Frame(Group::Body, s, true, false);
        EXPECT(f.sets == 0 && f.shows == 0 && f.fade == 1.0f);
        f.Frame(Group::Body, s, true, true);
        EXPECT(f.sets == 1 && f.fade == 0.3f && f.visible);
        f.Frame(Group::Body, s, true, true);   // set again every frame: the game sets the Fade itself when the unit's alpha changes, and its queue may come after the tick
        EXPECT(f.sets == 2 && f.fade == 0.3f);
        f.fade = 1.0f;                          // the game's own push of the unit's alpha
        f.Frame(Group::Body, s, true, true);
        EXPECT(f.fade == 0.3f);
    }
    {   // the aura group has its own value
        Fx f;
        f.Frame(Group::Aura, s, true, true);
        EXPECT(f.fade == 0.6f);
    }
    {   // the slider moves
        Fx f;
        f.Frame(Group::Body, s, true, true);
        s.body = 0.5f;
        f.Frame(Group::Body, s, true, true);
        EXPECT(f.fade == 0.5f);
        s.body = 0.3f;
    }
    {   // the option is unticked / the view is not F4: given back, once
        Fx f;
        f.Frame(Group::Body, s, true, true);
        EXPECT(f.fade == 0.3f && f.st.faded);
        Settings off = s;
        off.on = false;
        f.Frame(Group::Body, off, true, true);
        EXPECT(f.fade == 1.0f && !f.st.faded);
        const int w = f.sets;
        f.Frame(Group::Body, off, true, true);
        EXPECT(f.sets == w);   // restored once, then left alone
        f.Frame(Group::Body, s, true, true);   // ticked again: faint again
        EXPECT(f.fade == 0.3f);
        f.Frame(Group::Body, s, false, true);   // F1 / F2 / F3: as the game draws it
        EXPECT(f.fade == 1.0f);
        f.Frame(Group::Body, s, true, true);    // back in F4
        EXPECT(f.fade == 0.3f);
    }
    {   // 0 %: not drawn, by the game's own visibility switch; a fainter value shows it again
        Fx f;
        s.body = 0.0f;
        f.Frame(Group::Body, s, true, true);
        EXPECT(!f.visible && f.st.hidden && f.sets == 0);
        const int w = f.shows;
        f.Frame(Group::Body, s, true, true);
        EXPECT(f.shows == w);
        s.body = 0.4f;
        f.Frame(Group::Body, s, true, true);
        EXPECT(f.visible && !f.st.hidden && f.fade == 0.4f);
        s.body = 0.0f;
        f.Frame(Group::Body, s, true, true);
        EXPECT(!f.visible);
        s.on = false;
        f.Frame(Group::Body, s, true, true);   // unticked while off: drawn again, and its Fade given back
        EXPECT(f.visible && f.fade == 1.0f);
        s.on = true;
        s.body = 0.3f;
    }
    {   // an effect that has no "Fade": asked once, then left alone (not once a frame: the game's setter queues a refused call); 0 % still works
        Fx f;
        f.accepts = false;
        f.Frame(Group::Body, s, true, true);
        EXPECT(f.sets == 1 && f.st.fadeBad && f.fade == 1.0f);
        const Plan p = Decide(Group::Body, s, true, f.effect, true, f.st);
        EXPECT(p.cannotFade && !p.setFade);
        for (int i = 0; i < 5; ++i) f.Frame(Group::Body, s, true, true);
        EXPECT(f.sets == 1);
        s.body = 0.0f;
        f.Frame(Group::Body, s, true, true);
        EXPECT(!f.visible);
        s.body = 0.5f;
        f.Frame(Group::Body, s, true, true);   // shown again, not faded
        EXPECT(f.visible && f.fade == 1.0f && f.sets == 1);
        // the game makes the effect again (a new instance): asked again
        f.effect = 0x6000;
        f.accepts = true;
        f.Frame(Group::Body, s, true, true);
        EXPECT(f.sets == 2 && f.fade == 0.5f && !f.st.fadeBad);
        s.body = 0.3f;
    }
    {   // the game's "Fade" name object was not found: told (cannotFade), nothing set, 0 % still works
        Fx f;
        f.Frame(Group::Body, s, true, true, false);
        EXPECT(f.sets == 0);
        EXPECT(Decide(Group::Body, s, true, f.effect, false, f.st).cannotFade);
    }
    {   // a new effect instance is not "given back": its Fade is the game's own
        Fx f;
        f.Frame(Group::Body, s, true, true);
        EXPECT(f.st.faded);
        Settings off = s;
        off.on = false;
        f.effect = 0x7000;
        f.Frame(Group::Body, off, true, true);
        EXPECT(f.sets == 1);   // nothing set on the new instance this frame
    }
    {   // 100 % and an effect never touched: nothing, ever
        Fx f;
        Settings full = s;
        full.body = 1.0f;
        f.Frame(Group::Body, full, true, true);
        EXPECT(f.sets == 0 && f.shows == 0);
        EXPECT(!Decide(Group::None, s, true, f.effect, true, f.st).setFade);   // not a state's overlay: never touched
    }
    {   // an effect we never touched is never "given back"
        State clean;
        Settings off;
        const Plan p = Decide(Group::Body, off, true, 0x5000, true, clean);
        EXPECT(!p.setFade && p.visible == -1);
    }
}

// A SOLID effect (Bone Armor's bones: an opaque mesh that takes no Fade) - drawn as the game draws it or not at all; the game's own switching it back on.
void Solid() {
    Settings s;
    s.on = true;
    s.body = 0.3f;
    s.aura = 0.6f;
    EXPECT(TargetFor(Group::Body, s, true, false) == 0.3f);
    EXPECT(TargetFor(Group::Body, s, true, true) == 0.0f);    // any fainter: off
    EXPECT(TargetFor(Group::Body, s, false, true) == 1.0f);   // not F4: as the game draws it
    EXPECT(TargetFor(Group::None, s, true, true) == 1.0f);
    Settings full = s;
    full.body = 1.0f;
    EXPECT(TargetFor(Group::Body, full, true, true) == 1.0f);
    full.body = 0.99f;
    EXPECT(TargetFor(Group::Body, full, true, true) == 0.0f);
    {   // the player's case: Bone Armor at 30 % is not drawn - by the switch, its Fade never set
        Fx f;
        f.solid = true;
        f.Frame(Group::Body, s, true, false);   // not made yet: nothing
        EXPECT(f.shows == 0 && f.visible);
        f.Frame(Group::Body, s, true, true);
        EXPECT(!f.visible && f.st.hidden && f.sets == 0 && f.shows == 1);
        for (int i = 0; i < 5; ++i) f.Frame(Group::Body, s, true, true);
        EXPECT(f.shows == 1);   // switched once, not once a frame
        // the game switches it back on (its unit's alpha changed): off again the next frame
        f.visible = true;
        f.Frame(Group::Body, s, true, true);
        EXPECT(!f.visible && f.shows == 2 && f.st.hidden);
        f.Frame(Group::Body, s, true, true);
        EXPECT(f.shows == 2);
        // F1 / F2 / F3: drawn again; back in F4: off again
        f.Frame(Group::Body, s, false, true);
        EXPECT(f.visible && !f.st.hidden && f.sets == 0);
        f.Frame(Group::Body, s, true, true);
        EXPECT(!f.visible);
        // the slider at 50 %: still off (no fainter exists); at 100 %: drawn, nothing set
        s.body = 0.5f;
        f.Frame(Group::Body, s, true, true);
        EXPECT(!f.visible);
        s.body = 1.0f;
        f.Frame(Group::Body, s, true, true);
        EXPECT(f.visible && f.sets == 0);
        s.body = 0.3f;
        // unticked: drawn again, once
        f.Frame(Group::Body, s, true, true);
        Settings off = s;
        off.on = false;
        f.Frame(Group::Body, off, true, true);
        EXPECT(f.visible && !f.st.hidden);
        const int w = f.shows;
        f.Frame(Group::Body, off, true, true);
        EXPECT(f.shows == w);
    }
    {   // the wish cannot be read: switched off once, not again (no flood of calls)
        Fx f;
        f.solid = true;
        f.readable = false;
        for (int i = 0; i < 4; ++i) f.Frame(Group::Body, s, true, true);
        EXPECT(!f.visible && f.shows == 1);
    }
    {   // the same game switching-on of a non-solid effect at 0 %: off again too
        Fx f;
        Settings zero = s;
        zero.body = 0.0f;
        f.Frame(Group::Body, zero, true, true);
        f.visible = true;
        f.Frame(Group::Body, zero, true, true);
        EXPECT(!f.visible && f.shows == 2);
    }
    {   // not solid (Frozen Armor's ribbons): faded as before, never switched
        Fx f;
        f.Frame(Group::Body, s, true, true);
        EXPECT(f.visible && f.fade == 0.3f && f.shows == 0);
    }
}

// The solid part's own models (Bone Armor's bones: ModelInstances the effect's script makes): the table's model keys, a model resource's path and key (only a vfx mesh),
// the renderer's model list, the resource cache, out of a fake game memory; what is done to one model; the set of models we hid.
// [render] slow_own_effects: the solid effects' playback speed - 1 .. 100 %, F4 only, set again when the game changed it, given back once, a new effect instance not given back.
void Speed() {
    EXPECT(ClampSpeedPct(0.0f) == 1.0f && ClampSpeedPct(-5.0f) == 1.0f && ClampSpeedPct(0.5f) == 1.0f);   // never 0: they must still turn
    EXPECT(ClampSpeedPct(1.0f) == 1.0f && ClampSpeedPct(20.0f) == 20.0f && ClampSpeedPct(250.0f) == 100.0f && ClampSpeedPct(NAN) == 100.0f);
    Settings s;
    s.slow = true;
    s.speed = 0.2f;
    EXPECT(SpeedFor(true, true, s) == 0.2f);
    EXPECT(SpeedFor(true, false, s) == 1.0f);    // F1 - F3: the game's
    EXPECT(SpeedFor(false, true, s) == 1.0f);    // not a solid effect: the game's
    s.slow = false;
    EXPECT(SpeedFor(true, true, s) == 1.0f);
    SpeedState st;
    const uintptr_t fx = 0x5000;
    EXPECT(DecideSpeed(0.2f, 1.0f, 0, st) < 0.0f);              // the effect not made yet: nothing
    float v = DecideSpeed(0.2f, 1.0f, fx, st);
    EXPECT(v == 0.2f);
    ApplySpeed(v, fx, &st);
    EXPECT(st.sped && st.effect == fx);
    EXPECT(DecideSpeed(0.2f, 0.2f, fx, st) < 0.0f);             // steady: not set again
    EXPECT(DecideSpeed(0.2f, 1.0f, fx, st) == 0.2f);            // the game set it back (shown the overlay again): set again
    EXPECT(DecideSpeed(0.2f, 0.0f, fx, st) == 0.2f);            // (or paused it)
    EXPECT(DecideSpeed(0.05f, 0.2f, fx, st) == 0.05f);          // the slider moved
    EXPECT(DecideSpeed(0.2f, NAN, fx, st) == 0.2f);             // not read: set (the setter does nothing when it is equal)
    v = DecideSpeed(1.0f, 0.2f, fx, st);                         // unticked / not F4: given back
    EXPECT(v == 1.0f);
    ApplySpeed(v, fx, &st);
    EXPECT(!st.sped && DecideSpeed(1.0f, 0.2f, fx, st) < 0.0f); // once - then the game's own business
    ApplySpeed(0.2f, fx, &st);
    EXPECT(DecideSpeed(1.0f, 0.2f, 0x6000, st) < 0.0f);         // a new effect instance is not "given back"
    ApplySpeed(-1.0f, 0x6000, &st);
    EXPECT(!st.sped && st.effect == 0x6000);
    SpeedState never;
    EXPECT(DecideSpeed(1.0f, 0.5f, fx, never) < 0.0f);          // never touched: never touched
    // the definition's flag 4 lets the speed change
    Mem m;
    auto rd = [&](uintptr_t a, void* o, size_t n) { return m.rd(a, o, n); };
    const uintptr_t eff = kBase + 0x100, def = kBase + 0x400;
    m.put<uintptr_t>(eff + kFxOffDef, def);
    m.put<uint8_t>(def + kFxDefOffFlags, 0x0C);
    EXPECT(CanChangeSpeed(rd, eff) == 1);
    m.put<uint8_t>(def + kFxDefOffFlags, 0x08);
    EXPECT(CanChangeSpeed(rd, eff) == 0 && CanSwitchOff(rd, eff) == 1);
    EXPECT(CanChangeSpeed(rd, 0) == -1);
}

// [render] lower_own_effects: the solid effects drawn lower - cm to world units, F4 only, the matrix moved along y, given back only while it is still ours.
void Height() {
    EXPECT(ClampHeightCm(-60.0f) == -60.0f && ClampHeightCm(-400.0f) == kHeightMinCm && ClampHeightCm(90.0f) == kHeightMaxCm && ClampHeightCm(NAN) == 0.0f);
    EXPECT(std::fabs(LiftUnits(-60.0f, 3.2f) - (-1.92f)) < 1e-5f);   // 60 cm at 3.2 units a metre
    EXPECT(LiftUnits(-60.0f, 0.0f) == 0.0f && LiftUnits(-60.0f, NAN) == 0.0f);   // the scale not known: nothing moves
    Settings s;
    s.lower = true;
    s.heightCm = -60.0f;
    EXPECT(LiftFor(true, true, s, 3.2f) < 0.0f);
    EXPECT(LiftFor(true, false, s, 3.2f) == 0.0f);    // F1 - F3: the game's place
    EXPECT(LiftFor(false, true, s, 3.2f) == 0.0f);    // not a solid effect
    s.heightCm = 0.0f;
    EXPECT(LiftFor(true, true, s, 3.2f) == 0.0f);
    s.heightCm = -60.0f;
    s.lower = false;
    EXPECT(LiftFor(true, true, s, 3.2f) == 0.0f);
    float m[16] = {0.9f, 0, -0.1f, 0, 0, 1, 0, 0, 0.1f, 0, 0.9f, 0, 11502.0f, 0.03f, 11889.0f, 1.0f};
    EXPECT(LiftMatrix(m, -1.92f) && std::fabs(m[13] - (0.03f - 1.92f)) < 1e-5f && m[12] == 11502.0f && m[14] == 11889.0f && m[0] == 0.9f);
    float bad[16] = {};
    EXPECT(!LiftMatrix(bad, -1.0f) && bad[13] == 0.0f);   // m[15] 0: not a world matrix, untouched
    float nanm[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, NAN, 0, 0, 1};
    EXPECT(!LiftMatrix(nanm, -1.0f) && nanm[13] == 0.0f);
    EXPECT(StillLifted(-1.89f, -1.89f) && !StillLifted(0.03f, -1.89f) && !StillLifted(NAN, -1.89f) && !StillLifted(0.03f, NAN));
    EXPECT(StillLifted(11880.0f, 11880.0005f));                  // (a large height's last bits)
}

void Models() {
    // [sky] summit_backdrop_hidden: the Summit's ground picture as MtnTop.json / ColossalSummit.json name it, any slash, case, level of detail; nothing else
    EXPECT(IsSummitBackdrop("data/hd/env/model/expansion/mountaintop/expansion_mountaintop_skybox/skybox01.model"));
    EXPECT(IsSummitBackdrop("DATA\\HD\\ENV\\MODEL\\Expansion\\MountainTop\\Expansion_MountainTop_Skybox\\SkyBox01_lod0.model"));
    EXPECT(!IsSummitBackdrop("data/hd/env/model/ui/ui_skybox/act5_sky.model") && !IsSummitBackdrop("data/hd/env/model/expansion/mountaintop/rock01.model") &&
           !IsSummitBackdrop("data/hd/env/model/x/expansion_mountaintop_skybo") && !IsSummitBackdrop("") && !IsSummitBackdrop(nullptr));
    // the table names each solid effect's model file; a model is known by its file's key, only under data/hd/vfx/
    char k[96];
    EXPECT(ModelKey("data/hd/vfx/meshes/lit_mesh/bonearmor/bonearmor.model", k, sizeof k) && std::strcmp(k, "bonearmor") == 0);
    EXPECT(ModelKey("data\\hd\\vfx\\meshes\\FX_Mesh_BladeShield.MODEL", k, sizeof k) && std::strcmp(k, "fxmeshbladeshield") == 0);
    // the loaded file carries its level of detail (read in the running game): bonearmor_lod0.model is Bone Armor's bonearmor
    EXPECT(ModelKey("data/hd/vfx/meshes/lit_mesh/bonearmor/bonearmor_lod0.model", k, sizeof k) && std::strcmp(k, "bonearmor") == 0);
    EXPECT(ModelKey("data/hd/vfx/meshes/x/FX_Mesh_BladeShield_LOD12.model", k, sizeof k) && std::strcmp(k, "fxmeshbladeshield") == 0);
    EXPECT(ModelKey("data/hd/vfx/meshes/x/rock_small03.model", k, sizeof k) && std::strcmp(k, "rocksmall03") == 0);   // digits that are no LOD stay
    EXPECT(ModelKey("data/hd/vfx/meshes/x/lod1.model", k, sizeof k) && std::strcmp(k, "lod1") == 0);                   // ("_lod" needs its underscore)
    EXPECT(ModelKey("data/hd/vfx/meshes/x/a_lod.model", k, sizeof k) && std::strcmp(k, "alod") == 0);                  // (and its digits)

    // whose a new model is: the hero's when it first appears within kClaimMs after his overlay of that effect was tracked, or at the first look while it is live
    EXPECT(ClaimAtFirstSight(10000, 9000, false));
    EXPECT(ClaimAtFirstSight(9000 + kClaimMs, 9000, false));
    EXPECT(!ClaimAtFirstSight(9001 + kClaimMs, 9000, false));    // later: another unit's Bone Armor
    EXPECT(ClaimAtFirstSight(90000, 9000, true));                 // already there when the option first looks
    EXPECT(!ClaimAtFirstSight(10000, 0, true) && !ClaimAtFirstSight(10000, 0, false));   // no overlay of his live: never his
    SeenModels seen;
    EXPECT(seen.Mine(0x5000) == -1);
    EXPECT(seen.Add(0x5000, true, 1) && seen.Add(0x6000, false, 1) && seen.Mine(0x5000) == 1 && seen.Mine(0x6000) == 0);
    seen.Seen(0x5000, 2);
    seen.Prune(2);                                                  // 0x6000 was not in the second walk: gone
    EXPECT(seen.Mine(0x5000) == 1 && seen.Mine(0x6000) == -1 && seen.Count() == 1);
    for (size_t i = 1; i < SeenModels::kCapacity; ++i) EXPECT(seen.Add(0x10000 + i * 16, false, 2));
    EXPECT(!seen.Add(0x90000, true, 2));                            // full: refused (the caller leaves it alone)
    EXPECT(IsVfxModel("data/hd/vfx/meshes/lit_mesh/bonearmor/bonearmor.model") && IsVfxModel("Data\\HD\\VFX\\x.model"));
    EXPECT(!IsVfxModel("data/hd/character/player/necromancer/torso.model") && !IsVfxModel("data/hd/vf") && !IsVfxModel(nullptr));
    const int bone = SolidOfModelKey("bonearmor");
    EXPECT(bone >= 0 && std::strcmp(table::kEntries[bone].name, "bonearmorfront") == 0);
    EXPECT(SolidOfModelKey("") < 0 && SolidOfModelKey(nullptr) < 0 && SolidOfModelKey("nectorso") < 0);
    bool modelsOnSolidOnly = true;
    for (unsigned i = 0; i < table::kCount; ++i)
        if (table::kEntries[i].model[0] && !table::kEntries[i].solid) modelsOnSolidOnly = false;
    EXPECT(modelsOnSolidOnly);

    Mem m;
    auto rd = [&](uintptr_t a, void* o, size_t n) { return m.rd(a, o, n); };
    const uintptr_t renderer = kBase + 0x100, list = kBase + 0x1000, res = kBase + 0x1800, path = kBase + 0x1900, res2 = kBase + 0x1A00, path2 = kBase + 0x1B00;
    // a model resource: its path, its key, whether it is a solid effect's
    m.put<uintptr_t>(res + kResourceOffPath, path);
    m.str(path, "data/hd/vfx/meshes/lit_mesh/bonearmor/bonearmor.model");
    m.put<uintptr_t>(res2 + kResourceOffPath, path2);
    m.str(path2, "data/hd/character/player/necromancer/bonearmor.model");   // the right name, not a vfx mesh: never
    char nm[96];
    EXPECT(ReadModelPath(rd, res, nm, sizeof nm) && SolidOfResource(rd, res, nm, sizeof nm) == bone);
    EXPECT(SolidOfResource(rd, res2, nm, sizeof nm) == -1);
    m.str(path2, "data/hd/vfx/textures/x.texture");
    EXPECT(!ReadModelPath(rd, res2, nm, sizeof nm) && SolidOfResource(rd, res2, nm, sizeof nm) == -1);
    m.str(path2, "data/hd/vfx/meshes/other/other.model");
    EXPECT(SolidOfResource(rd, res2, nm, sizeof nm) == -1);
    EXPECT(SolidOfResource(rd, 0, nm, sizeof nm) == -1 && SolidOfResource(rd, kBase + 0x30000, nm, sizeof nm) == -1);

    // the renderer's list (data, count) - not one when the count is out of range or there is no data
    // (the renderer's offset is far beyond the fake memory: a renderer object placed so that its list fields land inside it)
    const uintptr_t fakeRenderer = kBase + 0x2000 - kRendererOffModels;
    m.put<uintptr_t>(fakeRenderer + kRendererOffModels, list);
    m.put<uint64_t>(fakeRenderer + kRendererOffModels + 8, 3);
    uintptr_t data = 0;
    uint64_t count = 0;
    EXPECT(ReadModelList(rd, fakeRenderer, &data, &count) && data == list && count == 3);
    m.put<uint64_t>(fakeRenderer + kRendererOffModels + 8, kMaxSceneModels + 1);
    EXPECT(!ReadModelList(rd, fakeRenderer, &data, &count));
    m.put<uint64_t>(fakeRenderer + kRendererOffModels + 8, 0);
    EXPECT(ReadModelList(rd, fakeRenderer, &data, &count) && count == 0);
    m.put<uintptr_t>(fakeRenderer + kRendererOffModels, 0);
    m.put<uint64_t>(fakeRenderer + kRendererOffModels + 8, 2);
    EXPECT(!ReadModelList(rd, fakeRenderer, &data, &count));
    EXPECT(!ReadModelList(rd, renderer, &data, &count) && !ReadModelList(rd, 0, &data, &count));

    // what each resource is, looked up once; a resource whose path pointer changed is looked up again
    ResourceKinds kinds;
    int e = 99;
    EXPECT(!kinds.Get(res, path, &e));
    kinds.Put(res, path, bone);
    EXPECT(kinds.Get(res, path, &e) && e == bone);
    EXPECT(!kinds.Get(res, path2, &e));
    kinds.Put(res2, path2, -1);
    EXPECT(kinds.Get(res2, path2, &e) && e == -1 && kinds.Get(res, path, &e) && e == bone);

    // one model: hidden when its effect is off and it is the hero's; given back when it is ours and no longer to be hidden; another unit's never touched
    EXPECT(ModelStep(true, true, false, 1) == 1);
    EXPECT(ModelStep(true, true, true, 1 | kLayersHidden) == 0);    // already hidden
    EXPECT(ModelStep(true, true, true, 1) == 1);                    // the game showed it again
    EXPECT(ModelStep(false, true, true, 1 | kLayersHidden) == -1);  // unticked / F1-F3 / 100 %
    EXPECT(ModelStep(true, false, true, 1 | kLayersHidden) == -1);  // it is no longer at the hero's place
    EXPECT(ModelStep(true, false, false, 1) == 0);                  // another unit's
    EXPECT(ModelStep(false, true, false, 1 | kLayersHidden) == 0);  // hidden by the game, not by us: left
    EXPECT(ModelStep(false, true, false, 1) == 0);

    HiddenModels h;
    const uintptr_t inst0 = kBase + 0x3000;
    EXPECT(h.Add(inst0, 1000) && h.Has(inst0) && h.Count() == 1);
    EXPECT(h.Add(inst0, 2000) && h.Count() == 1);
    h.Forget(2000 + HiddenModels::kForgetMs);
    EXPECT(h.Has(inst0));
    h.Forget(2001 + HiddenModels::kForgetMs);                       // not seen for longer: gone with its effect
    EXPECT(!h.Has(inst0) && h.Count() == 0);
    for (size_t i = 0; i < HiddenModels::kCapacity; ++i) EXPECT(h.Add(kBase + 0x100 * (i + 1), 0));
    EXPECT(!h.Add(kBase + 0x90000, 0));                             // full: refused (the caller then does not hide it)
    h.Remove(kBase + 0x100);
    EXPECT(!h.Has(kBase + 0x100) && h.Add(kBase + 0x90000, 0));
}

void Reading() {
    Mem m;
    const uintptr_t inst = kBase + 0x1000, ents = kBase + 0x2000, def = kBase + 0x3000, name = kBase + 0x4000;
    m.put<uintptr_t>(inst + kInstOffEntities, ents);
    m.put<uint64_t>(inst + kInstOffCount, 2);
    m.put<uintptr_t>(inst + kInstOffDef, def);
    m.put<uint32_t>(ents, 0x00300123u);
    m.put<uint32_t>(ents + 4, 0x00500456u);
    m.put<uintptr_t>(def + kDefOffName, name);
    m.str(name, "data/hd/overlays/necro/bonearmor_front.json");
    auto rd = [&](uintptr_t a, void* o, size_t n) { return m.rd(a, o, n); };

    uint32_t out[kMaxEntities] = {};
    uint32_t n = 99;
    EXPECT(ReadEntities(rd, inst, out, kMaxEntities, &n) && n == 2 && out[0] == 0x00300123u && out[1] == 0x00500456u);
    char nm[96];
    EXPECT(ReadName(rd, def, nm, sizeof nm) && GroupOf(nm) == Group::Body);
    EXPECT(std::strcmp(nm, "data/hd/overlays/necro/bonearmor_front.json") == 0);

    // an empty instance is fine; a count that is not one, a null pointer, an id that is not one, an unreadable address are not
    m.put<uint64_t>(inst + kInstOffCount, 0);
    EXPECT(ReadEntities(rd, inst, out, kMaxEntities, &n) && n == 0);
    m.put<uint64_t>(inst + kInstOffCount, 5000);
    EXPECT(!ReadEntities(rd, inst, out, kMaxEntities, &n) && n == 0);
    m.put<uint64_t>(inst + kInstOffCount, 1);
    m.put<uintptr_t>(inst + kInstOffEntities, 0);
    EXPECT(!ReadEntities(rd, inst, out, kMaxEntities, &n));
    m.put<uintptr_t>(inst + kInstOffEntities, ents);
    m.put<uint32_t>(ents, 0xFFFFFFFFu);
    EXPECT(!ReadEntities(rd, inst, out, kMaxEntities, &n));
    m.put<uint32_t>(ents, 0x00F00123u);
    EXPECT(ReadEntities(rd, inst, out, kMaxEntities, &n) && n == 1);
    EXPECT(!ReadEntities(rd, 0, out, kMaxEntities, &n));
    EXPECT(!ReadEntities(rd, kBase + 0x20000, out, kMaxEntities, &n));

    // the effect's switch flag: the effect instance's definition at +0x40, its flags at +0x188, 0x08 = can be switched off
    const uintptr_t fx = kBase + 0x5000, fxDef = kBase + 0x6000;
    m.put<uintptr_t>(fx + kFxOffDef, fxDef);
    m.put<uint8_t>(fxDef + kFxDefOffFlags, 0x0C);
    EXPECT(CanSwitchOff(rd, fx) == 1);
    m.put<uint8_t>(fxDef + kFxDefOffFlags, 0x04);   // the speed flag only
    EXPECT(CanSwitchOff(rd, fx) == 0);
    m.put<uintptr_t>(fx + kFxOffDef, 0);
    EXPECT(CanSwitchOff(rd, fx) == -1);
    EXPECT(CanSwitchOff(rd, 0) == -1 && CanSwitchOff(rd, kBase + 0x30000) == -1);

    // a name that is not text, or not there
    m.put<uint8_t>(name, 0x01);
    EXPECT(!ReadName(rd, def, nm, sizeof nm));
    m.str(name, "bonearmor_front");
    EXPECT(ReadName(rd, def, nm, sizeof nm));
    m.put<uintptr_t>(def + kDefOffName, 0);
    EXPECT(!ReadName(rd, def, nm, sizeof nm));
    m.put<uintptr_t>(def + kDefOffName, kBase + 0x20000);   // outside the block
    EXPECT(!ReadName(rd, def, nm, sizeof nm));
    m.put<uintptr_t>(def + kDefOffName, name);
    char tiny[6];
    EXPECT(!ReadName(rd, def, tiny, sizeof tiny));   // longer than the buffer: refused, not cut into another name
    EXPECT(!ReadName(rd, 0, nm, sizeof nm));

    // the game's "Fade" name: a global that holds a pointer to a string object whose characters (at +0x18) read "Fade"
    const uintptr_t global = kBase + 0x5000, strobj = kBase + 0x5100;
    char fn[8];
    m.put<uintptr_t>(global, strobj);
    m.str(strobj + kNameOffChars, "Fade");
    EXPECT(ReadFadeName(rd, global, fn) && std::strcmp(fn, "Fade") == 0);
    m.str(strobj + kNameOffChars, "Time");
    EXPECT(!ReadFadeName(rd, global, fn) && std::strcmp(fn, "Time") == 0);   // another name: not it
    m.str(strobj + kNameOffChars, "FadeX");
    EXPECT(!ReadFadeName(rd, global, fn));                                    // longer: not it
    m.str(strobj + kNameOffChars, "Fade");
    m.put<uintptr_t>(global, 0);
    EXPECT(!ReadFadeName(rd, global, fn));
    m.put<uintptr_t>(global, kBase + 0x20000);                                // outside the block
    EXPECT(!ReadFadeName(rd, global, fn));
    EXPECT(!ReadFadeName(rd, 0, fn));
}

void Hero() {
    UnitIds player;   // a player's unit
    player.type = 0; player.id = 77;
    UnitIds monster;
    monster.type = 1; monster.id = 77;   // the same number as the hero's id: a monster's unit is never the hero's
    UnitIds otherPlayer;
    otherPlayer.type = 0; otherPlayer.id = 90;
    const uintptr_t kHero = 0x2FA1AA590, kCopy = 0x2FA1BB000, kOther = 0x2FA1CC000;
    // by pointer: the game's own getter, or the unit the aim function last worked on
    EXPECT(IsHero(kHero, player, kHero, 77, 0, 0));
    EXPECT(IsHero(kHero, player, 0, 0, kHero, 77));
    EXPECT(IsHero(kHero, player, 0, 0, kHero, 0));
    // by id: another copy of the same player
    EXPECT(IsHero(kCopy, player, kHero, 77, 0, 0));
    EXPECT(IsHero(kCopy, player, 0, 0, kHero, 77));
    // not the hero: another player (offline there is none; online his id differs), a monster of the same number, nothing
    EXPECT(!IsHero(kOther, otherPlayer, kHero, 77, kHero, 77));
    EXPECT(!IsHero(kCopy, monster, kHero, 77, kHero, 77));
    EXPECT(!IsHero(kHero, monster, kHero, 77, kHero, 77));   // even at the hero's own address, if it is not a player it is not him
    EXPECT(!IsHero(0, player, kHero, 77, kHero, 77));
    EXPECT(!IsHero(0x100, player, kHero, 77, kHero, 77));
    // the hero is not known at all (the getter fails, the aim function has not run): nobody is the hero - nothing is dimmed by a guess
    EXPECT(!IsHero(kCopy, player, 0, 0, 0, 0));
    UnitIds unread;   // type not read
    EXPECT(!IsHero(kHero, unread, kHero, 77, kHero, 77));
}

void Place() {
    const float hero[3] = {100.0f, 0.0f, 200.0f};
    const float feet[3] = {100.5f, 0.0f, 200.5f};     // an aura ring at his feet
    const float body[3] = {100.0f, 3.5f, 200.0f};     // Bone Armor round his body
    const float merc[3] = {106.0f, 0.0f, 203.0f};     // a mercenary a few units away
    const float far[3] = {400.0f, 0.0f, 400.0f};
    const float high[3] = {100.0f, 20.0f, 200.0f};
    EXPECT(NearHero(hero, feet));
    EXPECT(NearHero(hero, body));
    EXPECT(NearHero(hero, hero));
    EXPECT(!NearHero(hero, merc));
    EXPECT(!NearHero(hero, far));
    EXPECT(!NearHero(hero, high));
    const float bad[3] = {NAN, 0.0f, 0.0f};
    EXPECT(!NearHero(hero, bad));
    EXPECT(!NearHero(bad, feet));
    // the edge: 2.5 units on the ground
    const float edgeIn[3] = {102.4f, 0.0f, 200.0f}, edgeOut[3] = {102.6f, 0.0f, 200.0f};
    EXPECT(NearHero(hero, edgeIn) && !NearHero(hero, edgeOut));
}

void Tracking() {
    Tracker t;
    EXPECT(t.Count() == 0);
    EXPECT(t.Add(0x00300123u, Group::Body, "bonearmor_front", 1000) != nullptr);
    EXPECT(t.Add(0x00300123u, Group::Aura, "x", 2000) != nullptr && t.Count() == 1);   // the same entity again: the one slot, the newer group
    EXPECT(t.Slots()[0].group == Group::Aura);
    EXPECT(t.Add(Tracker::kNone, Group::Body, "a", 0) == nullptr);                       // not an entity id
    EXPECT(t.Add(0x000FFFFFu, Group::Body, "a", 0) == nullptr);
    for (uint32_t i = 1; i < Tracker::kCapacity; ++i) EXPECT(t.Add(0x00100000u + i, Group::Body, "e", 0) != nullptr);
    EXPECT(t.Count() == Tracker::kCapacity);
    EXPECT(t.Add(0x00900000u, Group::Body, "full", 0) == nullptr);                       // no room: none, not a crash
    Tracker::Slot* s = &t.Slots()[0];
    t.Drop(s);
    EXPECT(t.Count() == Tracker::kCapacity - 1);
    t.Clear();
    EXPECT(t.Count() == 0);

    // when an entity is let go
    Tracker::Slot a;
    a.addedAt = 1000;
    EXPECT(!Tracker::GoneNow(a, true, 99999));          // its effect component is there
    EXPECT(!Tracker::GoneNow(a, false, 1000 + 3999));   // not found yet, not for long (the overlay's empty root waits a moment)
    EXPECT(Tracker::GoneNow(a, false, 1000 + 4001));    // never found: let go
    a.seen = true;
    EXPECT(Tracker::GoneNow(a, false, 1001));           // found once and gone now: the overlay ended
    EXPECT(!Tracker::GoneNow(a, true, 1001));

    // long names are cut, not overrun
    Tracker::Slot* q = t.Add(0x00300001u, Group::Body, "a_very_long_overlay_name_that_does_not_fit_in_the_slot_at_all", 0);
    EXPECT(q && std::strlen(q->name) == sizeof q->name - 1);

    HeroKeys k;
    EXPECT(!k.Has(0) && !k.Has(7) && !k.Any());
    EXPECT(k.Add(7) && !k.Add(7) && k.Has(7) && k.Any());
    for (uint64_t i = 8; i < 8 + 15; ++i) EXPECT(k.Add(i) && k.Has(7));   // sixteen slots: 7 and fifteen more
    EXPECT(k.Add(100) && !k.Has(7) && k.Has(100) && k.Has(8));          // the next one pushes the oldest out
    k.Clear();
    EXPECT(!k.Has(11) && !k.Any());
}

}  // namespace

int main() {
    Names();
    Table();
    Numbers();
    Decisions();
    Solid();
    Speed();
    Height();
    Models();
    Reading();
    Hero();
    Place();
    Tracking();
    if (g_fails) {
        std::printf("own_effects_test: %d of %d FAILED\n", g_fails, g_checks);
        return 1;
    }
    std::printf("own_effects_test: names, the table (%u overlays), the numbers, what is done to an effect each frame, reading the game's objects, the Fade name, who is the hero, tracking: %d checks, all passed\n", table::kCount, g_checks);
    return 0;
}
