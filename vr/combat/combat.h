// vr/combat/combat.h - what vrcam.cpp uses of vr/combat/combat_ticks.cpp.
#pragma once

#include <atomic>
#include <cstdint>

namespace vrc {

void* HeroUnitForSound();
size_t MeleeAnimNote(char* out, size_t n);
void PressTraceBegin(uint32_t monster, int hand);
void PressTraceOutcome(bool hit, double ms);

}  // namespace vrc
