// sim/straw.h — straw (after TerraFirmaCraft): grass plants cut with a knife or a scythe give straw (pit kiln fuel, thatch,
// a better chance with the firestarter). A scythe cuts the grass plants and the crops in the 3x3 round what it hits.
// sim/controller.cpp breakBlock calls strawOnBreak.
#pragma once
#include "sim/level.h"

namespace dq {

class Player;
class PlayerController;
bool isGrassPlant(BlockId id);
// after a block is broken: straw for grass cut with a knife or scythe; a scythe sweeps the 3x3 round it
void strawOnBreak(Level& lv, PlayerController& pc, Player& p, int x, int y, int z, BlockId id);

} // namespace dq
