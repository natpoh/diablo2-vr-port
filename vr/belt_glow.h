// belt_glow.h - the belt potions' own look in VR first person (F4), visual only, with the game's own per-entity draw values:
//   [hands] belt_inner_glow - a bottle a hand is at glows from inside, instead of the see-through ball round it;
//   [hands] belt_empty_bottle - the emptied bottle is drawn see-through and darker.
//
// What the potion is drawn with (D2R 3.3.93787, read off the image and the game's own shaders - side notes potion-glow-recon.md, color-variant-recon.md): every
// potion model's materials, the glass too, are "StandardMaterial": the model shader (ps_models_p), with no colour variant - so there is no per-bottle colour
// transform, and a bottle's liquid, painted in its glass, cannot be turned grey for one bottle. Two per-entity values do reach it:
//   - the HighlightComponent (one float per entity, the game's hover highlight): the shader turns the lit colour to HSV and multiplies its value by (1 + h) -
//     h > 0 brighter, -1 < h < 0 darker, hue and saturation kept. Once per simulation tick UnitTranslationLayer::UpdateSelectionHighlight (0x784D60, a job) sets
//     every non-zero value back to 0 and then the hovered unit's; ours are written after it (a tail hook) and again on the registry's own thread (engitem's
//     window), so the draw always finds them;
//   - the TransparentComponent (0x2C bytes; opacity = the smaller of its first two floats): below 1 the draw takes the alpha-blend pipeline and fades colour and
//     alpha by it. Written on the registry's thread every frame.
// Only values the game itself writes there; only where the entity has the component (never made); every access behind an SEH guard; a fault turns it off.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstdint>

namespace beltglow {

void Init(const D2RL::PluginContext* ctx);
// [hands] belt_inner_glow and [hands] belt_glow (h for a lit bottle: 1 = twice as bright).
void Configure(bool on, float value);
// [hands] belt_empty_bottle with [hands] belt_empty_dark (h for an emptied bottle, < 0 darker; 0 = none) and belt_empty_alpha (its opacity, 1 = solid).
void ConfigureEmpty(bool on, float dark, float alpha);
// The glow is usable: on, the hook in, no fault.
bool Wanted();
// [debug] belt_bottle_trace: the read-back line every second instead of every five.
void SetTrace(bool on);
// The update thread: the hook goes in once either option wants a highlight (and stays; off, it only calls the game's function).
void Install();

// The engine unit's thread (vrcam's engitem::BeltFrame, the registry's own window): the entities of this frame and what each gets - a highlight (0: none)
// and an opacity (< 0: left alone). Written at once (and the highlight again after the game's reset each tick); what each carried before is read back for
// the log. At most kMax.
constexpr int kMax = 32;
struct Entry { uint32_t entity; float highlight; float opacity; bool lit; };
void SetEntries(const Entry* entries, int n);

// [debug] knock_props_glow (vr/knock_props.cpp): the level's props in the props world lit with the same highlight, h each (the hook writes them after the game's reset each
// tick; nothing else). Any thread; n 0: none. The hook goes in for it too (Install, once wanted).
constexpr int kMaxProps = 192;
void SetProps(const uint32_t* entities, int n, float h);

}  // namespace beltglow
