// adapter_class_test - vr/adapter_class.h (what kind of graphics card the game's device runs on) and the weak-adapter rule of the Quality
// preset's game options (shared/quality_presets.h GuardedPreset / GameOptionTo, [render] preset_game_options_weak_gpu). Pure, no GPU.
// Run: adapter_class_test.exe - prints each failed check and "N passed, M failed"; exit code = failures.
#include <cstdio>
#include <string_view>

#include "adapter_class.h"
#include "quality_presets.h"

namespace {
int g_pass = 0, g_fail = 0;
void Check(bool ok, const char* what) {
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("FAILED: %s\n", what); }
}

using adapterclass::Class;
using adapterclass::Facts;

Facts Card(bool software, bool archKnown, bool uma, unsigned long long dedicatedMB) {
    Facts f;
    f.known = true;
    f.software = software;
    f.archKnown = archKnown;
    f.uma = uma;
    f.dedicatedBytes = dedicatedMB << 20;
    return f;
}

const qualitypreset::GameOption* Option(const char* name) {
    for (const qualitypreset::GameOption& o : qualitypreset::kGameOptions)
        if (std::string_view(o.name) == name) return &o;
    return nullptr;
}
}  // namespace

int main() {
    // ---- the adapter's kind -------------------------------------------------------------------------------------------------------------------
    Check(adapterclass::Classify(Facts{}) == Class::Unknown, "nothing read: Unknown");
    // the crash batch's card: Intel UHD, 128 MB of its own, UMA
    Check(adapterclass::Classify(Card(false, true, true, 128)) == Class::Integrated, "Intel UHD (UMA, 128 MB): Integrated");
    // the same card when the device's architecture could not be asked: its 128 MB still says weak
    Check(adapterclass::Classify(Card(false, false, false, 128)) == Class::Small, "128 MB, architecture not known: Small");
    // an AMD APU reporting a 2 GB carve-out as its own memory: UMA says what it is
    Check(adapterclass::Classify(Card(false, true, true, 2048)) == Class::Integrated, "APU with a 2 GB carve-out (UMA): Integrated");
    // UMA is only believed when the device answered
    Check(adapterclass::Classify(Card(false, false, true, 8192)) == Class::Discrete, "uma without archKnown is not trusted");
    // WARP / the Basic Render Driver
    Check(adapterclass::Classify(Card(true, true, true, 0)) == Class::Software, "software flag: Software (before UMA)");
    Check(adapterclass::Classify(Card(true, true, false, 8192)) == Class::Software, "software flag wins over memory");
    // discrete cards
    Check(adapterclass::Classify(Card(false, true, false, 1023)) == Class::Small, "1023 MB of its own: Small");
    Check(adapterclass::Classify(Card(false, true, false, 1024)) == Class::Discrete, "1 GiB of its own: Discrete (the bound)");
    Check(adapterclass::Classify(Card(false, true, false, 2048)) == Class::Discrete, "GT 1030 2 GB: Discrete");
    Check(adapterclass::Classify(Card(false, true, false, 24576)) == Class::Discrete, "RTX 4090 24 GB: Discrete");
    // which are weak
    Check(adapterclass::Weak(Class::Integrated) && adapterclass::Weak(Class::Small) && adapterclass::Weak(Class::Software), "weak kinds");
    Check(!adapterclass::Weak(Class::Discrete) && !adapterclass::Weak(Class::Unknown), "Discrete and Unknown are not Weak by themselves");
    for (int c = 0; c <= (int)Class::Software; ++c) Check(adapterclass::Name((Class)c)[0] != 0, "every kind has words");

    // ---- the weak-adapter rule (shared/quality_presets.h) ---------------------------------------------------------------------------------------
    const int potato = 0, low = 1, medium = 2;
    Check(qualitypreset::kWeakCap == low, "the cap is Low");
    Check(qualitypreset::GuardedPreset(medium, true) == low, "Medium guarded is Low");
    Check(qualitypreset::GuardedPreset(low, true) == low && qualitypreset::GuardedPreset(potato, true) == potato, "Low / Potato guarded stay");
    Check(qualitypreset::GuardedPreset(medium, false) == medium, "unguarded Medium stays Medium (as before)");

    const qualitypreset::GameOption* ao = Option("Ambient Occlusion Quality");
    const qualitypreset::GameOption* sh = Option("Shadow Quality");
    const qualitypreset::GameOption* li = Option("Light Quality");
    Check(ao && sh && li, "the three game options are in kGameOptions");
    if (ao && sh && li) {
        // the crash batch's log: Medium put AO 2 -> 3 and shadows 2 -> 3 - guarded, both kept at 2
        Check(qualitypreset::GameOptionTo(*ao, medium, 2, true) == 2, "crash case: AO 2 stays 2 under Medium on a weak card");
        Check(qualitypreset::GameOptionTo(*sh, medium, 2, true) == 2, "crash case: shadows 2 stay 2 under Medium on a weak card");
        // unguarded: exactly the old behaviour
        Check(qualitypreset::GameOptionTo(*ao, medium, 2, false) == 3, "unguarded: AO 2 -> 3 (as before)");
        Check(qualitypreset::GameOptionTo(*sh, medium, 4, false) == 3, "unguarded: shadows 4 -> 3 (as before)");
        // lowering stays allowed: above Low's value, the card is brought down to Low's
        Check(qualitypreset::GameOptionTo(*ao, medium, 4, true) == ao->v[low], "guarded Medium lowers AO 4 to Low's");
        Check(qualitypreset::GameOptionTo(*li, medium, 2, true) == li->v[low], "guarded Medium lowers lights 2 to Low's");
        Check(qualitypreset::GameOptionTo(*ao, potato, 2, true) == ao->v[potato], "guarded Potato still lowers AO to off");
        // not known (-1): Low's value
        Check(qualitypreset::GameOptionTo(*ao, medium, -1, true) == ao->v[low], "value not known: Low's");
        // the rule over every option, preset and value: never above the game's own value, never above Low's, never above the preset's own
        for (const qualitypreset::GameOption& o : qualitypreset::kGameOptions)
            for (int p = 0; p < qualitypreset::kCount; ++p)
                for (int now = 0; now <= 4; ++now) {
                    const int to = qualitypreset::GameOptionTo(o, p, now, true);
                    char what[160];
                    std::snprintf(what, sizeof what, "never raise: %s preset %d now %d -> %d", o.name, p, now, to);
                    Check(to <= now, what);
                    Check(to <= o.v[qualitypreset::kWeakCap] && to <= o.v[p], what);
                    // and guarded never puts more than unguarded
                    Check(to <= qualitypreset::GameOptionTo(o, p, now, false), what);
                    // unguarded is the preset's own value whatever the game has
                    Check(qualitypreset::GameOptionTo(o, p, now, false) == o.v[p], "unguarded is the preset's own value");
                }
    }

    std::printf("adapter_class_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
