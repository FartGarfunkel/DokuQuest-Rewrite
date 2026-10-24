// world/light.h — light (sky << 4 | block, 0..15 each) for chunks.
// initialChunkLight runs on the worker that generated/loaded a chunk; LightEngine (world system)
// spreads light across chunk borders and updates it when blocks change.
#pragma once
#include "world/world.h"

namespace dq {
void initialChunkLight(Chunk& c);   // sky light down each column (band-aware), emitters lit
} // namespace dq
