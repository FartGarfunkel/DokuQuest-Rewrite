// sim/cavein.h — cave-ins (after TerraFirmaCraft, gentler), in an immersive world: mining rock under a wide span with
// nothing holding it up now and then (1%) shakes loose a little rubble from the roof — one to three blocks falling as
// cobblestone, never a chain, never burying you. A support beam within four blocks makes a span safe.
#pragma once
#include "sim/level.h"

namespace dq {

constexpr float CAVEIN_CHANCE = 0.01f;
constexpr int CAVEIN_SPAN = 24, SUPPORT_REACH = 4;
bool isRock(BlockId id);                                       // raw rock (not bricks, not ores, not wood)
bool caveInRisk(World& w, int x, int y, int z);                // the cell just mined: a wide unsupported span with rock over it
int caveIn(Level& lv, World& w, int x, int y, int z, float roll);   // after mining (x,y,z): blocks that fell (roll >= chance: 0)

} // namespace dq
