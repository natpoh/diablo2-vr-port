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

// Model visibility radius in world units (the game's 150). False if this game
// build does not match.
bool  SetModelRadius(float units);
float ModelRadius();   // -1 when unknown

}   // namespace d2rcam::drawdist
