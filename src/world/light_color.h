// world/light_color.h — the colour a light-giving block emits.
// Shared by the light engine (propagation) and the coloured-light volume, so a torch is the same warm
// white in the world as it is in the camera volume.
#pragma once
#include "core/registry.h"

namespace dq {

// 0..1, brightest channel normalised to 1. The usual light is warm white (1, 0.85, 0.62).
vec3 blockLightColour(BlockId id);

// Cubyz-style emission: level 0..15 maps onto 0..255 (15 -> 255) in that colour.
void blockLightRgb(BlockId id, u8 level, u8 out[3]);

} // namespace dq
