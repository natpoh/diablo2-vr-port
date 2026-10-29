#pragma once
// Render distance: how many rings of D2 rooms around the hero the 3D layer
// (D2Prism LevelTranslationLayer) activates and draws. Public entry points are
// d2rcam::SetRenderRadius / GetRenderRadius; this is the internal half.

#include <cstdint>

namespace D2RL {
struct PluginContext;
}

namespace d2rcam::drawdist {

// Installs the two room hooks; false until both are in (call again later).
bool Install(const D2RL::PluginContext* ctx);
bool Installed();

void SetRadius(int rooms);   // clamped to [1, 8]
int  Radius();

int RoomsAddedLastTick();
int RoomsLoadedTotal();

// Called on the game's thread right after each LevelTranslationLayer::SimulationTick, with the layer.
using LevelTickFn = void (*)(void* layer);
void SetLevelTickCallback(LevelTickFn fn);

// Model visibility radius in world units (the game's 150). False if this game
// build does not match.
bool  SetModelRadius(float units);
float ModelRadius();   // -1 when unknown

// The models re-sorted at once (the game's "Recalculate Every Frame" for half a second) after a
// radius changed; false where that byte was not found. HooksReady: Install has had the context.
bool ResortModels();
bool HooksReady();

// Big models (trees, houses, walls) kept drawn past the model radius by their size, out to this
// many world units (0 = off; [render] big_radius). In unleashed_big_objects.cpp - MPL 2.0, from
// D2R VR Unleashed (bearstonem), shared/unleashed_far_objects.h. Hooks the game's test the first
// time it is on; false until that hook is in (it is tried again at most twice a second).
bool     SetBigRadius(float units);
float    BigRadius();
uint32_t TakeBigKept();   // models kept past the model radius by their size since the last call

}   // namespace d2rcam::drawdist
