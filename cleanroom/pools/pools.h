#pragma once
// The game's memory pools (Renderer Pool, Entity System Pool). Public entry
// point is d2rcam::GrowPools; this is the internal half.

#include <cstddef>

namespace d2rcam::pools {

// See d2rcam::GrowPools.
bool Grow(unsigned renderMb, unsigned entityMb, char* line, size_t lineSize);

}   // namespace d2rcam::pools
