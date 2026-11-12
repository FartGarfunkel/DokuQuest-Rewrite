// render/place_pop.h — a placed block pops (after A Good Place's pop_in placement animation; the sample pack in
// Animations n' Stuff, used with permission): it swells a touch past its size and settles, over about four ticks. The
// real block is already in the world, so the pop only ever grows (never shrinks under it). Off with particles off.
#pragma once
#include "common/common.h"

namespace dq {

constexpr float PLACE_POP_TIME = 0.22f;   // seconds
// the size (1 = the block's own) t seconds after it was placed
float placePopScale(float t);
// a block was placed at pos (client/client_fx.cpp: the "place" sound's position, inside the block)
void notePlacement(vec3 pos);
void registerPlacePopLayer();   // idempotent (render/item_layers.cpp registerItemLayers)

} // namespace dq
