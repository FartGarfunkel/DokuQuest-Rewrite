// sim/treasure_maps.h — a treasure map (found in loot) shows an X some way off on dry land the first time it's used, says
// which way and how far, and marks the X on your maps. When the land at the X is first loaded a chest of treasure is buried
// a few blocks under it; dig it up. Each map has its own X (kept on the map and in the world: lv.info.data["treasures"]).
#pragma once
#include "sim/level.h"

namespace dq {

void registerTreasureMaps();

} // namespace dq
